#pragma once

#include <cstddef>
#include <cstdint>

#include "game_text.h"

namespace deathtrap::settings {

enum class ControlRow : uint8_t { kMovement, kInvertX, kInvertY, kKeepBodies, kHeroineModel, kBodyWounds, kCount };

struct Controls {
  bool keyboard_modern = true;
  bool controller_modern = true;
  bool invert_x = false;
  bool invert_y = false;
  bool keep_bodies = false;
  bool e3_model = false;
  bool body_wounds = true;
};

constexpr int kControlLeft = 104;
constexpr int kControlRight = 500;
constexpr int kControlTop = 170;
constexpr int kControlRowHeight = 24;
// The legacy Hardware page still owns only its three original added controls.
constexpr int kControlRows = static_cast<int>(ControlRow::kKeepBodies);

constexpr int ControlRowAt(int x, int y) {
  return x < kControlLeft || x > kControlRight || y < kControlTop ||
          y >= kControlTop + kControlRowHeight * kControlRows
      ? -1 : (y - kControlTop) / kControlRowHeight;
}

constexpr Controls ToggleControl(Controls value, ControlRow row) {
  switch (row) {
    case ControlRow::kMovement: {
      // Preserve independently configured keyboard/controller values until
      // the user changes this shared menu option. Mixed -> modern -> tank.
      const bool modern = !(value.keyboard_modern && value.controller_modern);
      value.keyboard_modern = value.controller_modern = modern;
      break;
    }
    case ControlRow::kInvertX: value.invert_x = !value.invert_x; break;
    case ControlRow::kInvertY: value.invert_y = !value.invert_y; break;
    case ControlRow::kKeepBodies: value.keep_bodies = !value.keep_bodies; break;
    case ControlRow::kHeroineModel: value.e3_model = !value.e3_model; break;
    case ControlRow::kBodyWounds: value.body_wounds = !value.body_wounds; break;
    case ControlRow::kCount: break;
  }
  return value;
}

constexpr Controls SetControl(Controls value, ControlRow row, bool enabled) {
  switch (row) {
    case ControlRow::kMovement:
      value.keyboard_modern = value.controller_modern = enabled; break;
    case ControlRow::kInvertX: value.invert_x = enabled; break;
    case ControlRow::kInvertY: value.invert_y = enabled; break;
    case ControlRow::kKeepBodies: value.keep_bodies = enabled; break;
    case ControlRow::kHeroineModel: value.e3_model = enabled; break;
    case ControlRow::kBodyWounds: value.body_wounds = enabled; break;
    case ControlRow::kCount: break;
  }
  return value;
}

inline const char* ControlLabel(text::Language language, ControlRow row) {
  constexpr const char* labels[4][6] = {
      {"Movement", "Invert camera X", "Invert camera Y", "Keep bodies", "Heroine model", "Show wounds"},
      {"D\x82placements", "Inverser cam\x82ra X", "Inverser cam\x82ra Y", "Garder les corps", "Mod\x8Ale h\x82ro\x8Bne", "Afficher blessures"},
      {"Bewegung", "Kamera X umkehren", "Kamera Y umkehren", "Leichen behalten", "Heldinnenmodell", "Wunden anzeigen"},
      {"Movimento", "Inverti telecamera X", "Inverti telecamera Y", "Conserva corpi", "Modello eroina", "Mostra ferite"},
  };
  const auto index = static_cast<size_t>(row);
  const auto lang = static_cast<size_t>(language);
  return index < static_cast<size_t>(ControlRow::kCount) ? labels[lang < 4u ? lang : 0u][index] : "";
}

inline const char* ControlValue(text::Language language, ControlRow row,
                                 Controls value) {
  // CP850, matching the original menu fonts. No OS keyboard-layout dependency.
  constexpr const char* values[4][5] = {
      {"Modern", "Tank", "Custom", "Yes", "No"},
      {"Moderne", "Classique", "Personnalis\x82", "Oui", "Non"},
      {"Modern", "Klassisch", "Benutzerdef.", "Ja", "Nein"},
      {"Moderno", "Classico", "Personalizzato", "S\x8D", "No"},
  };
  size_t index = 0;
  if (row == ControlRow::kHeroineModel) return value.e3_model ? "E3 1997" : "Original";
  switch (row) {
    case ControlRow::kMovement:
      index = value.keyboard_modern != value.controller_modern ? 2u
          : value.keyboard_modern ? 0u : 1u;
      break;
    case ControlRow::kInvertX: index = value.invert_x ? 3u : 4u; break;
    case ControlRow::kInvertY: index = value.invert_y ? 3u : 4u; break;
    case ControlRow::kKeepBodies: index = value.keep_bodies ? 3u : 4u; break;
    case ControlRow::kBodyWounds: index = value.body_wounds ? 3u : 4u; break;
    default: return "";
  }
  const auto lang = static_cast<size_t>(language);
  return values[lang < 4u ? lang : 0u][index];
}

inline const char* ControlSaveError(text::Language language) {
  switch (language) {
    case text::Language::kFrench: return "Erreur";
    case text::Language::kGerman: return "Speicherfehler";
    case text::Language::kItalian: return "Errore";
    default: return "Save failed";
  }
}

}  // namespace deathtrap::settings
