#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "game_text.h"

namespace deathtrap::selector_text {

constexpr int32_t kEmptyItem = -1;
constexpr int32_t kChalkItem = -2;
// The real gameplay-message font uses Dungeon's gold/yellow palette ramp
// with its original dark outline. Menu font 0x1486 uses palette index 1,
// which is black during gameplay, despite its different menu appearance.
constexpr uint32_t kNameFontResource = 0x1112u;

constexpr int32_t ItemForSlot(uint32_t category, uint32_t slot) {
  if (slot >= 8) return kEmptyItem;
  switch (category) {
    case 1: return static_cast<int32_t>(slot);
    case 2: return slot < 6 ? static_cast<int32_t>(slot) + 8 :
                   (slot == 7 ? kChalkItem : kEmptyItem);
    case 3: return static_cast<int32_t>(slot) + 14;
    case 4: return static_cast<int32_t>(slot) + 22;
    default: return kEmptyItem;
  }
}

inline const char* ChalkName(text::Language language) {
  switch (language) {
    case text::Language::kFrench: return "Craie";
    case text::Language::kGerman: return "Kreide";
    case text::Language::kItalian: return "Gesso";
    default: return "Chalk";
  }
}

// OEM/CP850 is single-byte: clipping at a byte boundary preserves accents.
// Prefer word boundaries, split very long words safely, and never let a
// second line overrun the ring's central gap.
template <typename Measure>
std::array<std::string, 2> WrapName(std::string name, int max_width,
                                     const Measure& measure) {
  std::array<std::string, 2> lines{};
  if (max_width <= 0 || name.size() > 127) return lines;
  const auto trim = [](std::string value) {
    const auto first = value.find_first_not_of(' ');
    if (first == std::string::npos) return std::string{};
    return value.substr(first, value.find_last_not_of(' ') - first + 1);
  };
  name = trim(name);
  for (size_t row = 0; row < lines.size() && !name.empty(); ++row) {
    if (measure(name.c_str()) <= max_width) {
      lines[row] = name;
      break;
    }
    const std::string suffix = row == 1 ? "..." : "";
    size_t fit = 0;
    for (size_t length = 1; length <= name.size(); ++length) {
      if (measure((name.substr(0, length) + suffix).c_str()) > max_width) break;
      fit = length;
    }
    if (fit == 0) break;
    if (row == 1) {
      lines[row] = trim(name.substr(0, fit)) + suffix;
      break;
    }
    const size_t space = name.find_last_of(' ', fit);
    if (space != std::string::npos && space > 0) fit = space;
    lines[row] = trim(name.substr(0, fit));
    name = trim(name.substr(fit));
  }
  return lines;
}

}  // namespace deathtrap::selector_text
