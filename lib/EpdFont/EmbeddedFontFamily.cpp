#include "EmbeddedFontFamily.h"

#include <HardwareSerial.h>

bool EmbeddedFontFamily::load() {
  if (regular && !regular->load()) {
    Serial.printf("[%lu] [EmbeddedFontFamily] Failed to load regular font\n", millis());
    return false;
  }
  return true;
}

bool EmbeddedFontFamily::isLoaded() const { return regular && regular->isLoaded(); }

void EmbeddedFontFamily::getTextDimensions(const char* string, int* w, int* h) const {
  if (regular) {
    regular->getTextDimensions(string, w, h);
  } else {
    *w = 0;
    *h = 0;
  }
}

bool EmbeddedFontFamily::hasPrintableChars(const char* string) const {
  return regular ? regular->hasPrintableChars(string) : false;
}

const EpdGlyph* EmbeddedFontFamily::getGlyph(uint32_t cp) const {
  return regular ? regular->getGlyph(cp) : nullptr;
}

const uint8_t* EmbeddedFontFamily::getGlyphBitmap(uint32_t cp) const {
  return regular ? regular->getGlyphBitmap(cp) : nullptr;
}

uint8_t EmbeddedFontFamily::getAdvanceY() const { return regular ? regular->getAdvanceY() : 0; }

int8_t EmbeddedFontFamily::getAscender() const { return regular ? regular->getAscender() : 0; }

int8_t EmbeddedFontFamily::getDescender() const { return regular ? regular->getDescender() : 0; }

bool EmbeddedFontFamily::is2Bit() const { return regular ? regular->is2Bit() : false; }
