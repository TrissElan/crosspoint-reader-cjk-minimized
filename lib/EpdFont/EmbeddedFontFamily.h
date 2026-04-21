#pragma once

#include "EpdFontFamily.h"
#include "EmbeddedFont.h"

/**
 * Embedded font family - similar interface to EpdFontFamily but uses EmbeddedFont.
 */
class EmbeddedFontFamily {
 private:
  EmbeddedFont* regular;

 public:
  explicit EmbeddedFontFamily(EmbeddedFont* regular) : regular(regular) {}

  ~EmbeddedFontFamily() = default;

  EmbeddedFontFamily(const EmbeddedFontFamily&) = delete;
  EmbeddedFontFamily& operator=(const EmbeddedFontFamily&) = delete;

  bool load();
  bool isLoaded() const;

  // EpdFontFamily-compatible interface
  void getTextDimensions(const char* string, int* w, int* h) const;
  bool hasPrintableChars(const char* string) const;

  const EpdGlyph* getGlyph(uint32_t cp) const;
  const uint8_t* getGlyphBitmap(uint32_t cp) const;

  uint8_t getAdvanceY() const;
  int8_t getAscender() const;
  int8_t getDescender() const;
  bool is2Bit() const;
};
