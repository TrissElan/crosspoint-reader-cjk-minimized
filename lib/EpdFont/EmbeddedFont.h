#pragma once

#include <cstdint>
#include <cstring>

#include "EpdFontData.h"
#include "EmbeddedFontFormat.h"

/**
 * Simple fixed-size cache for glyph metadata (EpdGlyph) loaded on-demand.
 * Uses a simple circular buffer to avoid STL container overhead on ESP32.
 */
class GlyphMetadataCache {
 public:
  static constexpr size_t MAX_ENTRIES = 128;  // Balanced for Korean text while conserving memory

  struct CacheEntry {
    uint32_t codepoint;
    EpdGlyph glyph;
    bool valid;
  };

 private:
  CacheEntry entries[MAX_ENTRIES];
  size_t nextSlot;

 public:
  GlyphMetadataCache() : nextSlot(0) {
    for (size_t i = 0; i < MAX_ENTRIES; i++) {
      entries[i].valid = false;
    }
  }

  const EpdGlyph* get(uint32_t codepoint);
  const EpdGlyph* put(uint32_t codepoint, const EpdGlyph& glyph);
  void clear();
};

class EmbeddedFontData {
 private:
  bool loaded;

  // Memory-mapped data (for Flash-resident fonts)
  const uint8_t* memData;
  size_t memSize;

  // Font metadata (loaded once, kept in RAM)
  EpdFontHeader header;
  const EpdFontInterval* intervals;  // Points directly to Flash (zero-copy)

  // Glyph metadata cache (per-font, small circular buffer)
  mutable GlyphMetadataCache glyphCache;

  // Binary search for glyph index
  int findGlyphIndex(uint32_t codepoint) const;

  // Load a single glyph by index (from Flash memory)
  bool loadGlyph(int glyphIndex, EpdGlyph* outGlyph) const;

 public:
  // Constructor for memory-resident (Flash-embedded) font data
  EmbeddedFontData(const uint8_t* data, size_t size);
  ~EmbeddedFontData() = default;

  // Disable copy to prevent resource issues
  EmbeddedFontData(const EmbeddedFontData&) = delete;
  EmbeddedFontData& operator=(const EmbeddedFontData&) = delete;

  // Load font header and metadata
  bool load();
  bool isLoaded() const { return loaded; }

  // EpdFontData-compatible getters
  uint8_t getAdvanceY() const { return header.advanceY; }
  int8_t getAscender() const { return header.ascender; }
  int8_t getDescender() const { return header.descender; }
  bool is2Bit() const { return header.is2Bit != 0; }
  uint32_t getIntervalCount() const { return header.intervalCount; }
  uint32_t getGlyphCount() const { return header.glyphCount; }

  // Get glyph by codepoint (loads metadata on demand)
  const EpdGlyph* getGlyph(uint32_t codepoint) const;

  // Get bitmap for a glyph (direct Flash pointer, zero-copy)
  const uint8_t* getGlyphBitmap(uint32_t codepoint) const;
};

/**
 * Embedded font class - similar interface to EpdFont but loads from embedded Flash memory.
 */
class EmbeddedFont {
 private:
  EmbeddedFontData* data;
  bool ownsData;

 public:
  explicit EmbeddedFont(EmbeddedFontData* fontData, bool takeOwnership = false);
  // Constructor for Flash-embedded font data
  EmbeddedFont(const uint8_t* data, size_t size);
  ~EmbeddedFont();

  // Disable copy
  EmbeddedFont(const EmbeddedFont&) = delete;
  EmbeddedFont& operator=(const EmbeddedFont&) = delete;

  bool load();
  bool isLoaded() const { return data && data->isLoaded(); }

  // EpdFont-compatible interface
  void getTextDimensions(const char* string, int* w, int* h) const;
  bool hasPrintableChars(const char* string) const;
  const EpdGlyph* getGlyph(uint32_t cp) const;
  const uint8_t* getGlyphBitmap(uint32_t cp) const;

  // Metadata accessors
  uint8_t getAdvanceY() const { return data ? data->getAdvanceY() : 0; }
  int8_t getAscender() const { return data ? data->getAscender() : 0; }
  int8_t getDescender() const { return data ? data->getDescender() : 0; }
  bool is2Bit() const { return data ? data->is2Bit() : false; }

  EmbeddedFontData* getData() const { return data; }
};
