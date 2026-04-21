#pragma once
#include "EpdFont.h"

class EpdFontFamily {
 public:
  enum Style : uint8_t { REGULAR = 0, UNDERLINE = 1 };

  explicit EpdFontFamily(const EpdFont* regular) : regular(regular) {}
  ~EpdFontFamily() = default;
  void getTextDimensions(const char* string, int* w, int* h) const;
  const EpdFontData* getData() const;
  const EpdGlyph* getGlyph(uint32_t cp) const;
  int8_t getKerning(uint32_t leftCp, uint32_t rightCp) const;
  uint32_t applyLigatures(uint32_t cp, const char*& text) const;

 private:
  const EpdFont* regular;
};

using EpdFontStyle = EpdFontFamily::Style;

constexpr auto REGULAR = EpdFontFamily::REGULAR;
constexpr auto UNDERLINE = EpdFontFamily::UNDERLINE;
