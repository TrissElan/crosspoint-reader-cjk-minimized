#include "EpdFontFamily.h"

void EpdFontFamily::getTextDimensions(const char* string, int* w, int* h) const {
  regular->getTextDimensions(string, w, h);
}

const EpdFontData* EpdFontFamily::getData() const { return regular->data; }

const EpdGlyph* EpdFontFamily::getGlyph(const uint32_t cp) const { return regular->getGlyph(cp); }

int8_t EpdFontFamily::getKerning(const uint32_t leftCp, const uint32_t rightCp) const {
  return regular->getKerning(leftCp, rightCp);
}

uint32_t EpdFontFamily::applyLigatures(const uint32_t cp, const char*& text) const {
  return regular->applyLigatures(cp, text);
}
