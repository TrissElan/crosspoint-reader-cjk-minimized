#include "EmbeddedFont.h"

#include <Arduino.h>
#include <HardwareSerial.h>
#include <Utf8.h>

#include <algorithm>
#include <cstring>

// ============================================================================
// GlyphMetadataCache Implementation (simple fixed-size circular buffer)
// ============================================================================

const EpdGlyph* GlyphMetadataCache::get(uint32_t codepoint) {
  for (size_t i = 0; i < MAX_ENTRIES; i++) {
    if (entries[i].valid && entries[i].codepoint == codepoint) {
      return &entries[i].glyph;
    }
  }
  return nullptr;
}

const EpdGlyph* GlyphMetadataCache::put(uint32_t codepoint, const EpdGlyph& glyph) {
  // Check if already cached
  for (size_t i = 0; i < MAX_ENTRIES; i++) {
    if (entries[i].valid && entries[i].codepoint == codepoint) {
      return &entries[i].glyph;
    }
  }

  // Add to next slot (circular overwrite)
  entries[nextSlot].codepoint = codepoint;
  entries[nextSlot].glyph = glyph;
  entries[nextSlot].valid = true;

  const EpdGlyph* result = &entries[nextSlot].glyph;
  nextSlot = (nextSlot + 1) % MAX_ENTRIES;
  return result;
}

void GlyphMetadataCache::clear() {
  for (size_t i = 0; i < MAX_ENTRIES; i++) {
    entries[i].valid = false;
  }
  nextSlot = 0;
}

// ============================================================================
// EmbeddedFontData Implementation (Flash-only, zero-copy)
// ============================================================================

// Maximum reasonable values for validation
static constexpr uint32_t MAX_INTERVAL_COUNT = 10000;
static constexpr uint32_t MAX_GLYPH_COUNT = 150000;

EmbeddedFontData::EmbeddedFontData(const uint8_t* data, size_t size)
    : loaded(false), memData(data), memSize(size), intervals(nullptr) {
  memset(&header, 0, sizeof(header));
}

bool EmbeddedFontData::load() {
  if (loaded) {
    return true;
  }

  if (memData == nullptr || memSize < sizeof(EpdFontHeader)) {
    Serial.printf("[%lu] [EmbeddedFont] Flash data null or too small\n", millis());
    return false;
  }

  memcpy(&header, memData, sizeof(EpdFontHeader));

  if (header.magic != EPDFONT_MAGIC) {
    Serial.printf("[%lu] [EmbeddedFont] Invalid magic 0x%08X\n", millis(), header.magic);
    return false;
  }
  if (header.version != EPDFONT_VERSION) {
    Serial.printf("[%lu] [EmbeddedFont] Bad version %u\n", millis(), header.version);
    return false;
  }
  if (header.intervalCount > MAX_INTERVAL_COUNT || header.glyphCount > MAX_GLYPH_COUNT) {
    Serial.printf("[%lu] [EmbeddedFont] Header values out of range\n", millis());
    return false;
  }

  // Point directly to Flash data — no RAM allocation needed
  intervals = reinterpret_cast<const EpdFontInterval*>(memData + header.intervalsOffset);

  loaded = true;
  Serial.printf("[%lu] [EmbeddedFont] Loaded from Flash (zero-copy): advanceY=%u glyphs=%u\n", millis(),
                header.advanceY, header.glyphCount);
  return true;
}

bool EmbeddedFontData::loadGlyph(int glyphIndex, EpdGlyph* outGlyph) const {
  if (!loaded || glyphIndex < 0 || glyphIndex >= static_cast<int>(header.glyphCount)) {
    return false;
  }

  uint32_t glyphFileOffset = header.glyphsOffset + (glyphIndex * sizeof(EpdFontGlyph));
  if (glyphFileOffset + sizeof(EpdFontGlyph) > memSize) {
    return false;
  }

  EpdFontGlyph fileGlyph;
  memcpy(&fileGlyph, memData + glyphFileOffset, sizeof(EpdFontGlyph));

  outGlyph->width = fileGlyph.width;
  outGlyph->height = fileGlyph.height;
  outGlyph->advanceX = fileGlyph.advanceX;
  outGlyph->left = fileGlyph.left;
  outGlyph->top = fileGlyph.top;
  outGlyph->dataLength = static_cast<uint16_t>(fileGlyph.dataLength);
  outGlyph->dataOffset = fileGlyph.dataOffset;

  return true;
}

int EmbeddedFontData::findGlyphIndex(uint32_t codepoint) const {
  if (!loaded || intervals == nullptr) {
    return -1;
  }

  int left = 0;
  int right = static_cast<int>(header.intervalCount) - 1;

  while (left <= right) {
    int mid = left + (right - left) / 2;
    const EpdFontInterval* interval = &intervals[mid];

    if (codepoint < interval->first) {
      right = mid - 1;
    } else if (codepoint > interval->last) {
      left = mid + 1;
    } else {
      return static_cast<int>(interval->offset + (codepoint - interval->first));
    }
  }

  return -1;
}

const EpdGlyph* EmbeddedFontData::getGlyph(uint32_t codepoint) const {
  if (!loaded) {
    return nullptr;
  }

  const EpdGlyph* cached = glyphCache.get(codepoint);
  if (cached != nullptr) {
    return cached;
  }

  int index = findGlyphIndex(codepoint);
  if (index < 0 || index >= static_cast<int>(header.glyphCount)) {
    return nullptr;
  }

  EpdGlyph glyph;
  if (!loadGlyph(index, &glyph)) {
    return nullptr;
  }

  return glyphCache.put(codepoint, glyph);
}

const uint8_t* EmbeddedFontData::getGlyphBitmap(uint32_t codepoint) const {
  if (!loaded) {
    return nullptr;
  }

  int glyphIndex = findGlyphIndex(codepoint);
  if (glyphIndex < 0 || glyphIndex >= static_cast<int>(header.glyphCount)) {
    return nullptr;
  }

  uint32_t glyphFileOffset = header.glyphsOffset + (glyphIndex * sizeof(EpdFontGlyph));
  if (glyphFileOffset + sizeof(EpdFontGlyph) > memSize) {
    return nullptr;
  }

  EpdFontGlyph fileGlyph;
  memcpy(&fileGlyph, memData + glyphFileOffset, sizeof(EpdFontGlyph));

  if (fileGlyph.dataLength == 0) {
    return nullptr;
  }

  uint32_t bitmapPos = header.bitmapOffset + fileGlyph.dataOffset;
  if (bitmapPos + fileGlyph.dataLength > memSize) {
    return nullptr;
  }

  // Return direct pointer to Flash — zero-copy
  return memData + bitmapPos;
}

// ============================================================================
// EmbeddedFont Implementation
// ============================================================================

EmbeddedFont::EmbeddedFont(EmbeddedFontData* fontData, bool takeOwnership) : data(fontData), ownsData(takeOwnership) {}

EmbeddedFont::EmbeddedFont(const uint8_t* memPtr, size_t memSz) : data(new EmbeddedFontData(memPtr, memSz)), ownsData(true) {}

EmbeddedFont::~EmbeddedFont() {
  if (ownsData) {
    delete data;
  }
}

bool EmbeddedFont::load() {
  if (data == nullptr) {
    return false;
  }
  return data->load();
}

void EmbeddedFont::getTextDimensions(const char* string, int* w, int* h) const {
  *w = 0;
  *h = 0;

  if (data == nullptr || !data->isLoaded() || string == nullptr || *string == '\0') {
    return;
  }

  int minX = 0, minY = 0, maxX = 0, maxY = 0;
  int cursorX = 0;
  const int cursorY = 0;

  uint32_t cp;
  while ((cp = utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&string)))) {
    const EpdGlyph* glyph = data->getGlyph(cp);
    if (!glyph) {
      glyph = data->getGlyph('?');
    }
    if (!glyph) {
      continue;
    }

    minX = std::min(minX, cursorX + glyph->left);
    maxX = std::max(maxX, cursorX + glyph->left + glyph->width);
    minY = std::min(minY, cursorY + glyph->top - glyph->height);
    maxY = std::max(maxY, cursorY + glyph->top);
    cursorX += glyph->advanceX;
  }

  *w = maxX - minX;
  *h = maxY - minY;
}

bool EmbeddedFont::hasPrintableChars(const char* string) const {
  int w = 0, h = 0;
  getTextDimensions(string, &w, &h);
  return w > 0 || h > 0;
}

const EpdGlyph* EmbeddedFont::getGlyph(uint32_t cp) const {
  if (data == nullptr) {
    return nullptr;
  }
  return data->getGlyph(cp);
}

const uint8_t* EmbeddedFont::getGlyphBitmap(uint32_t cp) const {
  if (data == nullptr) {
    return nullptr;
  }
  return data->getGlyphBitmap(cp);
}
