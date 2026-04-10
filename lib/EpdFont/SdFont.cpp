#include "SdFont.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <HardwareSerial.h>
#include <Utf8.h>

#include <algorithm>
#include <cstring>
#include <new>

// ============================================================================
// GlyphBitmapCache Implementation (Arena-based, zero per-glyph heap allocation)
// ============================================================================

GlyphBitmapCache::GlyphBitmapCache(size_t maxSize) : arena(nullptr), arenaSize(maxSize), writePos(0), entryCount(0) {
  memset(table, 0, sizeof(table));
}

GlyphBitmapCache::~GlyphBitmapCache() { free(arena); }

bool GlyphBitmapCache::ensureArena() {
  if (arena) return true;
  arena = static_cast<uint8_t*>(malloc(arenaSize));
  if (!arena) {
    Serial.printf("[%lu] [SdFont] Failed to allocate %u byte arena\n", millis(), static_cast<unsigned>(arenaSize));
    return false;
  }
  return true;
}

// Knuth multiplicative hash
size_t GlyphBitmapCache::findSlot(uint32_t key) const {
  size_t idx = (key * 2654435769u) >> (32 - 9);  // 9 bits for TABLE_SIZE=512
  for (size_t i = 0; i < TABLE_SIZE; i++) {
    size_t slot = (idx + i) & (TABLE_SIZE - 1);
    if (table[slot].key == 0 || table[slot].key == key) {
      return slot;
    }
  }
  return TABLE_SIZE;  // Table full (should not happen with 512 slots / ~250 glyphs per page)
}

const uint8_t* GlyphBitmapCache::get(uint32_t key) const {
  if (!arena || key == 0) return nullptr;
  size_t slot = findSlot(key);
  if (slot >= TABLE_SIZE || table[slot].key != key) return nullptr;
  return arena + table[slot].offset;
}

const uint8_t* GlyphBitmapCache::put(uint32_t key, const uint8_t* data, uint32_t size) {
  uint8_t* dest = reserve(key, size);
  if (!dest) return nullptr;
  memcpy(dest, data, size);
  commitReserve(key, size);
  return dest;
}

uint8_t* GlyphBitmapCache::reserve(uint32_t key, uint32_t size) {
  if (key == 0 || size == 0 || size > arenaSize) return nullptr;
  if (!ensureArena()) return nullptr;

  // Check if already cached
  size_t slot = findSlot(key);
  if (slot < TABLE_SIZE && table[slot].key == key) {
    return arena + table[slot].offset;  // Already exists
  }

  // Flush if not enough space or table too full
  if (writePos + size > arenaSize || entryCount >= TABLE_SIZE * 3 / 4) {
    clear();
    slot = findSlot(key);
  }

  if (slot >= TABLE_SIZE) return nullptr;

  return arena + writePos;
}

void GlyphBitmapCache::commitReserve(uint32_t key, uint32_t size) {
  if (!arena || key == 0) return;
  size_t slot = findSlot(key);
  if (slot >= TABLE_SIZE) return;
  if (table[slot].key == key) return;  // Already committed

  table[slot].key = key;
  table[slot].offset = static_cast<uint16_t>(writePos);
  table[slot].size = static_cast<uint16_t>(size);
  writePos += size;
  entryCount++;
}

void GlyphBitmapCache::clear() {
  memset(table, 0, sizeof(table));
  writePos = 0;
  entryCount = 0;
}

void GlyphBitmapCache::releaseArena() {
  clear();
  free(arena);
  arena = nullptr;
}

// ============================================================================
// GlyphMetadataCache Implementation (simple fixed-size circular buffer)
// ============================================================================

const EpdGlyph* GlyphMetadataCache::get(uint32_t codepoint) {
  // Linear search through cache (simple but effective for small cache)
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
// SdFontData Implementation
// ============================================================================

// Static members
GlyphBitmapCache* SdFontData::sharedCache = nullptr;
int SdFontData::cacheRefCount = 0;
int SdFontData::nextCacheId = 0;

SdFontData::SdFontData(const char* path) : filePath(path), loaded(false), memData(nullptr), memSize(0), intervals(nullptr), ownsIntervals(false), cacheId(nextCacheId++) {
  memset(&header, 0, sizeof(header));

  // Initialize shared cache on first SdFontData creation
  // Use larger cache (64KB) to improve performance with Korean fonts
  if (sharedCache == nullptr) {
    sharedCache = new GlyphBitmapCache(32768);  // 32KB cache (conserve memory for XTC)
  }
  cacheRefCount++;
}

SdFontData::SdFontData(const uint8_t* data, size_t size)
    : filePath("<flash>"), loaded(false), memData(data), memSize(size), intervals(nullptr), ownsIntervals(false), cacheId(nextCacheId++) {
  memset(&header, 0, sizeof(header));

  if (sharedCache == nullptr) {
    sharedCache = new GlyphBitmapCache(32768);
  }
  cacheRefCount++;
}

SdFontData::~SdFontData() {
  if (fontFile) {
    fontFile.close();
  }

  if (ownsIntervals) {
    delete[] intervals;
  }

  // Cleanup shared cache when last SdFontData is destroyed
  cacheRefCount--;
  if (cacheRefCount == 0 && sharedCache != nullptr) {
    delete sharedCache;
    sharedCache = nullptr;
  }
}

SdFontData::SdFontData(SdFontData&& other) noexcept
    : filePath(std::move(other.filePath)), loaded(other.loaded), memData(other.memData), memSize(other.memSize),
      header(other.header), intervals(other.intervals), ownsIntervals(other.ownsIntervals), cacheId(other.cacheId) {
  other.intervals = nullptr;
  other.ownsIntervals = false;
  other.loaded = false;
  other.memData = nullptr;
  other.memSize = 0;
  cacheRefCount++;  // New instance references the cache
}

SdFontData& SdFontData::operator=(SdFontData&& other) noexcept {
  if (this != &other) {
    // Clean up current resources
    if (fontFile) {
      fontFile.close();
    }
    if (ownsIntervals) {
      delete[] intervals;
    }

    // Move from other
    filePath = std::move(other.filePath);
    loaded = other.loaded;
    memData = other.memData;
    memSize = other.memSize;
    header = other.header;
    intervals = other.intervals;
    ownsIntervals = other.ownsIntervals;
    cacheId = other.cacheId;

    other.intervals = nullptr;
    other.ownsIntervals = false;
    other.loaded = false;
    other.memData = nullptr;
    other.memSize = 0;
  }
  return *this;
}

// Maximum reasonable values for validation
// CJK fonts (Korean + Chinese + Japanese) can have 120K+ glyphs
// Glyphs are loaded on-demand from SD, so high count doesn't affect memory
static constexpr uint32_t MAX_INTERVAL_COUNT = 10000;
static constexpr uint32_t MAX_GLYPH_COUNT = 150000;
static constexpr size_t MIN_FREE_HEAP_AFTER_LOAD = 16384;  // 16KB minimum heap after loading

bool SdFontData::load() {
  if (loaded) {
    return true;
  }

  // ── Memory-resident (Flash-embedded) path ──────────────────────────────
  if (memData != nullptr) {
    if (memSize < sizeof(EpdFontHeader)) {
      Serial.printf("[%lu] [SdFont] Flash data too small\n", millis());
      return false;
    }
    memcpy(&header, memData, sizeof(EpdFontHeader));

    if (header.magic != EPDFONT_MAGIC) {
      Serial.printf("[%lu] [SdFont] Flash: invalid magic 0x%08X\n", millis(), header.magic);
      return false;
    }
    if (header.version != EPDFONT_VERSION) {
      Serial.printf("[%lu] [SdFont] Flash: bad version %u\n", millis(), header.version);
      return false;
    }
    if (header.intervalCount > MAX_INTERVAL_COUNT || header.glyphCount > MAX_GLYPH_COUNT) {
      Serial.printf("[%lu] [SdFont] Flash: header values out of range\n", millis());
      return false;
    }

    // Point directly to Flash data — no RAM allocation needed
    intervals = reinterpret_cast<const EpdFontInterval*>(memData + header.intervalsOffset);
    ownsIntervals = false;

    size_t intervalsMemory = header.intervalCount * sizeof(EpdFontInterval);
    loaded = true;
    Serial.printf("[%lu] [SdFont] Loaded from Flash (zero-copy): advanceY=%u intervals=%uKB glyphs=%u\n", millis(),
                  header.advanceY, intervalsMemory / 1024, header.glyphCount);
    return true;
  }

  // ── SD-card path ───────────────────────────────────────────────────────
  size_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < MIN_FREE_HEAP_AFTER_LOAD) {
    Serial.printf("[%lu] [SdFont] Insufficient heap: %u bytes (need %u)\n", millis(), freeHeap,
                  MIN_FREE_HEAP_AFTER_LOAD);
    return false;
  }

  // Open font file
  if (!Storage.openFileForRead("SdFont", filePath.c_str(), fontFile)) {
    Serial.printf("[%lu] [SdFont] Failed to open font file: %s\n", millis(), filePath.c_str());
    return false;
  }

  // Read and validate header
  if (fontFile.read(&header, sizeof(EpdFontHeader)) != sizeof(EpdFontHeader)) {
    Serial.printf("[%lu] [SdFont] Failed to read header from: %s\n", millis(), filePath.c_str());
    fontFile.close();
    return false;
  }

  // Validate magic number
  if (header.magic != EPDFONT_MAGIC) {
    Serial.printf("[%lu] [SdFont] Invalid magic: 0x%08X (expected 0x%08X)\n", millis(), header.magic, EPDFONT_MAGIC);
    fontFile.close();
    return false;
  }

  // Validate version
  if (header.version != EPDFONT_VERSION) {
    Serial.printf("[%lu] [SdFont] Bad version: %u (expected %u)\n", millis(), header.version, EPDFONT_VERSION);
    fontFile.close();
    return false;
  }

  // Validate header values to prevent memory issues
  if (header.intervalCount > MAX_INTERVAL_COUNT) {
    Serial.printf("[%lu] [SdFont] Too many intervals: %u (max %u)\n", millis(), header.intervalCount,
                  MAX_INTERVAL_COUNT);
    fontFile.close();
    return false;
  }

  if (header.glyphCount > MAX_GLYPH_COUNT) {
    Serial.printf("[%lu] [SdFont] Too many glyphs: %u (max %u)\n", millis(), header.glyphCount, MAX_GLYPH_COUNT);
    fontFile.close();
    return false;
  }

  // Calculate required memory - only intervals are loaded into RAM
  // Glyphs are loaded on-demand from SD card to save memory
  size_t intervalsMemory = header.intervalCount * sizeof(EpdFontInterval);

  if (intervalsMemory > freeHeap - MIN_FREE_HEAP_AFTER_LOAD) {
    Serial.printf("[%lu] [SdFont] Not enough memory for intervals: need %u, have %u\n", millis(), intervalsMemory,
                  freeHeap);
    fontFile.close();
    return false;
  }

  Serial.printf("[%lu] [SdFont] Loading %s: %u intervals, %u glyphs (on-demand)\n", millis(), filePath.c_str(),
                header.intervalCount, header.glyphCount);

  // Allocate intervals array (SD fonts must copy to RAM)
  auto* mutableIntervals = new (std::nothrow) EpdFontInterval[header.intervalCount];
  ownsIntervals = true;
  if (mutableIntervals == nullptr) {
    Serial.printf("[%lu] [SdFont] Failed to allocate intervals (%u bytes)\n", millis(), intervalsMemory);
    fontFile.close();
    return false;
  }

  // Read intervals - data should be contiguous after header, but verify offset
  // Expected offset for intervals is 32 (right after header)
  if (header.intervalsOffset != sizeof(EpdFontHeader)) {
    // Need to seek - file layout is non-standard
    if (!fontFile.seekSet(header.intervalsOffset)) {
      Serial.printf("[%lu] [SdFont] Failed to seek to intervals at %u\n", millis(), header.intervalsOffset);
      fontFile.close();
      delete[] mutableIntervals;
      return false;
    }
  }
  // Otherwise, we're already positioned right after header - read directly

  if (fontFile.read(mutableIntervals, intervalsMemory) != static_cast<int>(intervalsMemory)) {
    Serial.printf("[%lu] [SdFont] Failed to read intervals\n", millis());
    fontFile.close();
    delete[] mutableIntervals;
    return false;
  }

  intervals = mutableIntervals;

  // Close the file after loading intervals - we'll reopen when reading glyphs/bitmaps
  fontFile.close();

  loaded = true;
  Serial.printf("[%lu] [SdFont] Loaded: %s (advanceY=%u, intervals=%uKB)\n", millis(), filePath.c_str(),
                header.advanceY, intervalsMemory / 1024);

  return true;
}

bool SdFontData::ensureFileOpen() const {
  if (memData != nullptr) {
    return true;  // No file needed for memory-resident fonts
  }
  if (fontFile) {
    return true;
  }
  return Storage.openFileForRead("SdFont", filePath.c_str(), fontFile);
}

bool SdFontData::loadGlyphFromSD(int glyphIndex, EpdGlyph* outGlyph) const {
  if (!loaded || glyphIndex < 0 || glyphIndex >= static_cast<int>(header.glyphCount)) {
    return false;
  }

  uint32_t glyphFileOffset = header.glyphsOffset + (glyphIndex * sizeof(EpdFontGlyph));

  EpdFontGlyph fileGlyph;
  if (memData != nullptr) {
    if (glyphFileOffset + sizeof(EpdFontGlyph) > memSize) {
      return false;
    }
    memcpy(&fileGlyph, memData + glyphFileOffset, sizeof(EpdFontGlyph));
  } else {
    // Keep file open for better performance
    if (!ensureFileOpen()) {
      return false;
    }
    if (!fontFile.seekSet(glyphFileOffset)) {
      return false;
    }
    if (fontFile.read(&fileGlyph, sizeof(EpdFontGlyph)) != sizeof(EpdFontGlyph)) {
      return false;
    }
  }

  // Convert from file format to runtime format
  outGlyph->width = fileGlyph.width;
  outGlyph->height = fileGlyph.height;
  outGlyph->advanceX = fileGlyph.advanceX;
  outGlyph->left = fileGlyph.left;
  outGlyph->top = fileGlyph.top;
  outGlyph->dataLength = static_cast<uint16_t>(fileGlyph.dataLength);
  outGlyph->dataOffset = fileGlyph.dataOffset;

  return true;
}

int SdFontData::findGlyphIndex(uint32_t codepoint) const {
  if (!loaded || intervals == nullptr) {
    return -1;
  }

  // Binary search for the interval containing this codepoint
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
      // Found: codepoint is within this interval
      return static_cast<int>(interval->offset + (codepoint - interval->first));
    }
  }

  return -1;  // Not found
}

const EpdGlyph* SdFontData::getGlyph(uint32_t codepoint) const {
  if (!loaded) {
    return nullptr;
  }

  // Check cache first
  const EpdGlyph* cached = glyphCache.get(codepoint);
  if (cached != nullptr) {
    return cached;
  }

  // Find glyph index using binary search on intervals
  int index = findGlyphIndex(codepoint);
  if (index < 0 || index >= static_cast<int>(header.glyphCount)) {
    return nullptr;
  }

  // Load glyph from SD card
  EpdGlyph glyph;
  if (!loadGlyphFromSD(index, &glyph)) {
    return nullptr;
  }

  // Store in cache and return pointer to cached copy
  return glyphCache.put(codepoint, glyph);
}

const uint8_t* SdFontData::getGlyphBitmap(uint32_t codepoint) const {
  if (!loaded || sharedCache == nullptr) {
    return nullptr;
  }

  // Check cache first (use composite key to avoid collisions between fonts)
  const uint32_t cacheKey = makeCacheKey(codepoint);
  const uint8_t* cached = sharedCache->get(cacheKey);
  if (cached != nullptr) {
    return cached;
  }

  // Find glyph index
  int glyphIndex = findGlyphIndex(codepoint);
  if (glyphIndex < 0 || glyphIndex >= static_cast<int>(header.glyphCount)) {
    return nullptr;
  }

  // ── Memory-resident path ───────────────────────────────────────────────
  if (memData != nullptr) {
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
    return sharedCache->put(cacheKey, memData + bitmapPos, fileGlyph.dataLength);
  }

  // ── SD-card path ───────────────────────────────────────────────────────
  // Ensure file is open (keeps file handle open for performance)
  if (!ensureFileOpen()) {
    return nullptr;
  }

  // Read glyph metadata first (we need dataLength and dataOffset)
  uint32_t glyphFileOffset = header.glyphsOffset + (glyphIndex * sizeof(EpdFontGlyph));
  if (!fontFile.seekSet(glyphFileOffset)) {
    return nullptr;
  }

  EpdFontGlyph fileGlyph;
  if (fontFile.read(&fileGlyph, sizeof(EpdFontGlyph)) != sizeof(EpdFontGlyph)) {
    return nullptr;
  }

  if (fileGlyph.dataLength == 0) {
    return nullptr;
  }

  // Seek to bitmap data
  if (!fontFile.seekSet(header.bitmapOffset + fileGlyph.dataOffset)) {
    return nullptr;
  }

  // Reserve space in arena and read directly into it (no temp buffer malloc)
  uint8_t* dest = sharedCache->reserve(cacheKey, fileGlyph.dataLength);
  if (!dest) {
    return nullptr;
  }

  if (fontFile.read(dest, fileGlyph.dataLength) != static_cast<int>(fileGlyph.dataLength)) {
    return nullptr;
  }

  // File stays open for next glyph read (performance optimization)

  // Commit the reservation
  sharedCache->commitReserve(cacheKey, fileGlyph.dataLength);
  return dest;
}

void SdFontData::setCacheSize(size_t maxBytes) {
  if (sharedCache != nullptr) {
    delete sharedCache;
  }
  sharedCache = new GlyphBitmapCache(maxBytes);
}

void SdFontData::clearCache() {
  if (sharedCache != nullptr) {
    sharedCache->releaseArena();
  }
}

void SdFontData::releaseCache() {
  if (sharedCache != nullptr) {
    sharedCache->releaseArena();
  }
}

size_t SdFontData::getCacheUsedSize() {
  if (sharedCache != nullptr) {
    return sharedCache->getUsedSize();
  }
  return 0;
}

// ============================================================================
// SdFont Implementation
// ============================================================================

SdFont::SdFont(SdFontData* fontData, bool takeOwnership) : data(fontData), ownsData(takeOwnership) {}

SdFont::SdFont(const char* filePath) : data(new SdFontData(filePath)), ownsData(true) {}

SdFont::SdFont(const uint8_t* memPtr, size_t memSz) : data(new SdFontData(memPtr, memSz)), ownsData(true) {}

SdFont::~SdFont() {
  if (ownsData) {
    delete data;
  }
}

SdFont::SdFont(SdFont&& other) noexcept : data(other.data), ownsData(other.ownsData) {
  other.data = nullptr;
  other.ownsData = false;
}

SdFont& SdFont::operator=(SdFont&& other) noexcept {
  if (this != &other) {
    if (ownsData) {
      delete data;
    }
    data = other.data;
    ownsData = other.ownsData;
    other.data = nullptr;
    other.ownsData = false;
  }
  return *this;
}

bool SdFont::load() {
  if (data == nullptr) {
    return false;
  }
  return data->load();
}

void SdFont::getTextDimensions(const char* string, int* w, int* h) const {
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

bool SdFont::hasPrintableChars(const char* string) const {
  int w = 0, h = 0;
  getTextDimensions(string, &w, &h);
  return w > 0 || h > 0;
}

const EpdGlyph* SdFont::getGlyph(uint32_t cp) const {
  if (data == nullptr) {
    return nullptr;
  }
  return data->getGlyph(cp);
}

const uint8_t* SdFont::getGlyphBitmap(uint32_t cp) const {
  if (data == nullptr) {
    return nullptr;
  }
  return data->getGlyphBitmap(cp);
}
