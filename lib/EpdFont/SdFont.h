#pragma once

#include <HalStorage.h>

#include <cstdint>
#include <cstring>
#include <string>

#include "EpdFontData.h"
#include "SdFontFormat.h"

/**
 * Arena-based bitmap cache for glyph data loaded from SD card.
 * Uses a single pre-allocated memory block (arena) to avoid heap fragmentation.
 * When the arena is full, it flushes entirely and refills on demand.
 */
class GlyphBitmapCache {
 public:
  static constexpr size_t TABLE_SIZE = 512;  // Open-addressing hash table slots

  struct Entry {
    uint32_t key;     // 0 = empty slot
    uint16_t offset;  // Byte offset into arena
    uint16_t size;    // Bitmap size in bytes
  };

 private:
  uint8_t* arena;
  size_t arenaSize;
  size_t writePos;
  size_t entryCount;
  Entry table[TABLE_SIZE];

  size_t findSlot(uint32_t key) const;
  bool ensureArena();

 public:
  explicit GlyphBitmapCache(size_t maxSize = 32768);
  ~GlyphBitmapCache();

  // Returns cached bitmap or nullptr if not cached
  const uint8_t* get(uint32_t key) const;

  // Stores bitmap in cache (copies data), returns pointer to cached data
  const uint8_t* put(uint32_t key, const uint8_t* data, uint32_t size);

  // Reserves space in arena and returns writable pointer (for direct SD read)
  // Returns nullptr if size exceeds arena capacity. Flushes if needed.
  uint8_t* reserve(uint32_t key, uint32_t size);

  // Confirms a previous reserve() succeeded (bitmap data was written)
  void commitReserve(uint32_t key, uint32_t size);

  void clear();
  void releaseArena();
  size_t getUsedSize() const { return writePos; }
  size_t getMaxSize() const { return arenaSize; }
};

/**
 * SD Card font data structure.
 * Mimics EpdFontData interface but loads data on-demand from SD card.
 */
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

class SdFontData {
 private:
  std::string filePath;
  bool loaded;

  // Memory-mapped data (for embed_binfiles / Flash-resident fonts)
  const uint8_t* memData;
  size_t memSize;

  // Font metadata (loaded once, kept in RAM)
  EpdFontHeader header;
  const EpdFontInterval* intervals;  // Points to Flash or dynamically allocated
  bool ownsIntervals;                // true if intervals was allocated with new[]
  // Note: glyphs are NOT preloaded - loaded on-demand to save memory

  // Glyph metadata cache (per-font, small LRU cache)
  mutable GlyphMetadataCache glyphCache;

  // Bitmap cache (shared across all SdFontData instances)
  static GlyphBitmapCache* sharedCache;
  static int cacheRefCount;
  static int nextCacheId;

  // Per-instance cache ID to prevent cache collisions between different fonts
  int cacheId;

  // Encode font identity into cache key so different fonts don't collide
  uint32_t makeCacheKey(uint32_t codepoint) const {
    return (static_cast<uint32_t>(cacheId & 0x7FF) << 21) | (codepoint & 0x1FFFFF);
  }

  // File handle for reading (opened on demand, only used when memData==nullptr)
  mutable HalFile fontFile;

  // Binary search for glyph index
  int findGlyphIndex(uint32_t codepoint) const;

  // Load a single glyph by index (from SD or memory)
  bool loadGlyphFromSD(int glyphIndex, EpdGlyph* outGlyph) const;

  // Ensure font file is open (only used when memData==nullptr)
  bool ensureFileOpen() const;

 public:
  explicit SdFontData(const char* path);
  // Constructor for memory-resident (Flash-embedded) font data
  SdFontData(const uint8_t* data, size_t size);
  ~SdFontData();

  // Disable copy to prevent resource issues
  SdFontData(const SdFontData&) = delete;
  SdFontData& operator=(const SdFontData&) = delete;

  // Move constructor and assignment
  SdFontData(SdFontData&& other) noexcept;
  SdFontData& operator=(SdFontData&& other) noexcept;

  // Load font header and metadata from SD card
  bool load();
  bool isLoaded() const { return loaded; }

  // EpdFontData-compatible getters
  uint8_t getAdvanceY() const { return header.advanceY; }
  int8_t getAscender() const { return header.ascender; }
  int8_t getDescender() const { return header.descender; }
  bool is2Bit() const { return header.is2Bit != 0; }
  uint32_t getIntervalCount() const { return header.intervalCount; }
  uint32_t getGlyphCount() const { return header.glyphCount; }

  // Get glyph by codepoint (loads bitmap on demand)
  const EpdGlyph* getGlyph(uint32_t codepoint) const;

  // Get bitmap for a glyph (loads from SD if not cached)
  const uint8_t* getGlyphBitmap(uint32_t codepoint) const;

  // Static cache management
  static void setCacheSize(size_t maxBytes);
  static void clearCache();
  static void releaseCache();
  static size_t getCacheUsedSize();
};

/**
 * SD Card font class - similar interface to EpdFont but loads from SD card.
 */
class SdFont {
 private:
  SdFontData* data;
  bool ownsData;

 public:
  explicit SdFont(SdFontData* fontData, bool takeOwnership = false);
  explicit SdFont(const char* filePath);
  // Constructor for Flash-embedded font data
  SdFont(const uint8_t* data, size_t size);
  ~SdFont();

  // Disable copy
  SdFont(const SdFont&) = delete;
  SdFont& operator=(const SdFont&) = delete;

  // Move semantics
  SdFont(SdFont&& other) noexcept;
  SdFont& operator=(SdFont&& other) noexcept;

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

  SdFontData* getData() const { return data; }
};
