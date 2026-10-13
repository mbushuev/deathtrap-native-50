#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "keyboard_bindings.h"

namespace deathtrap::text {

enum class Language { kEnglish, kFrench, kGerman, kItalian };

// Strings are OEM/CP850 bytes, like the original localization files and FNT
// glyphs, not UTF-8 or the user's Windows keyboard/system code page.
inline const char* AddedActionLabel(Language language, size_t action) {
  constexpr size_t first =
      static_cast<size_t>(input::KeyboardAction::kSidestepLeft);
  constexpr std::array<const char*, 9> french = {
      "PAS \xB7 GAUCHE", "PAS \xB7 DROITE", "ARMES DE CONTACT",
      "ARMES \xB7 DISTANCE", "SORTS", "OBJETS", "VUE IMMERSIVE",
      "FPS NATIFS / 50", "PAUSE"};
  constexpr std::array<const char*, 9> german = {
      "SCHRITT LINKS", "SCHRITT RECHTS", "NAHKAMPFWAFFEN",
      "FERNKAMPFWAFFEN", "ZAUBER", "GEGENST\x8E" "NDE",
      "IMMERSIVE SICHT", "ORIGINAL / 50 FPS", "PAUSE"};
  constexpr std::array<const char*, 9> italian = {
      "PASSO A SINISTRA", "PASSO A DESTRA", "ARMI DA MISCHIA",
      "ARMI A DISTANZA", "INCANTESIMI", "OGGETTI", "VISTA IMMERSIVA",
      "FPS NATIVI / 50", "PAUSA"};
  if (action >= input::kKeyboardActionCount) {
    return "";
  }
  if (action < first || language == Language::kEnglish) {
    return input::kKeyboardActions[action].label;
  }
  switch (language) {
    case Language::kFrench: return french[action - first];
    case Language::kGerman: return german[action - first];
    case Language::kItalian: return italian[action - first];
    default: return input::kKeyboardActions[action].label;
  }
}

inline const char* HardwareCaption(Language language, const char* original) {
  if (!original || std::strcmp(original, "Invert First Person") != 0) {
    return original;
  }
  switch (language) {
    case Language::kFrench: return "Inverser 1re pers.";
    case Language::kGerman: return "1. Person umkehren";
    case Language::kItalian: return "Inverti prima pers.";
    default: return original;
  }
}

struct RetailFontHeader {
  uint32_t signature;
  uint32_t glyph_count;
};

struct RetailFontGlyph {
  uint8_t width;
  uint8_t height;
  int8_t offset_x;
  int8_t offset_y;
  int8_t advance_x;
  int8_t advance_y;
  uint16_t pixels;
};

static_assert(sizeof(RetailFontHeader) == 8);
static_assert(sizeof(RetailFontGlyph) == 8);
constexpr uint32_t kRetailFontSignature = 0x46475441u;  // "ATGF"
constexpr size_t kFirstFontCharacter = 32;
constexpr size_t kMaximumFontGlyphs = 256 - kFirstFontCharacter;

// Respect the actual FNT count (including smaller credits fonts), the signed
// retail dimension fields and the unchanged 256x256 texture with 32px rows.
// Never extend the loop into bitmap bytes or beyond the native atlas buffer.
inline size_t AtlasGlyphCount(const RetailFontHeader& header,
                             const RetailFontGlyph* glyphs,
                             size_t available) {
  if (header.signature != kRetailFontSignature || !glyphs ||
      header.glyph_count == 0 ||
      header.glyph_count > kMaximumFontGlyphs ||
      header.glyph_count > available) {
    return 0;
  }
  size_t x = 0;
  size_t y = 0;
  for (size_t index = 0; index < header.glyph_count; ++index) {
    const auto& glyph = glyphs[index];
    if (glyph.width > 127 || glyph.height > 32) {
      return 0;
    }
    if (x + glyph.width > 256) {
      x = 0;
      y += 32;
    }
    if (y + glyph.height > 256) {
      return index;
    }
    x += glyph.width;
  }
  return header.glyph_count;
}

}  // namespace deathtrap::text
