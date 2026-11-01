#pragma once

#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <cstdio>

#include "game_text.h"

namespace deathtrap::settings {

enum class ControlRow : uint8_t { kMovement, kInvertX, kInvertY, kKeepBodies, kHeroineModel, kBodyWounds,
  kSelectorSlowMotion, kSelectorSpeed, kInterfaceScale, kCount };

struct Controls {
  bool keyboard_modern = true;
  bool controller_modern = true;
  bool invert_x = false;
  bool invert_y = false;
  bool keep_bodies = false;
  bool e3_model = false;
  bool body_wounds = true;
  bool selector_slow_motion = true;
  int selector_speed = 25;
  int interface_scale = 100;
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
    case ControlRow::kSelectorSlowMotion: value.selector_slow_motion = !value.selector_slow_motion; break;
    case ControlRow::kSelectorSpeed: case ControlRow::kInterfaceScale: break;
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
    case ControlRow::kSelectorSlowMotion: value.selector_slow_motion = enabled; break;
    case ControlRow::kSelectorSpeed: case ControlRow::kInterfaceScale: break;
    case ControlRow::kCount: break;
  }
  return value;
}

// Menu contract: -1 = click/Enter (cycle), 0 = left, 1 = right.
inline Controls ChangeControl(Controls value, ControlRow row, int requested) {
  if (row == ControlRow::kSelectorSpeed || row == ControlRow::kInterfaceScale) {
    int& percent = row == ControlRow::kSelectorSpeed ? value.selector_speed : value.interface_scale;
    const int low = row == ControlRow::kSelectorSpeed ? 10 : 75;
    const int high = row == ControlRow::kSelectorSpeed ? 75 : 150;
    percent = requested < 0 && percent >= high ? low :
        std::clamp(percent + (requested == 0 ? -5 : 5), low, high);
    return value;
  }
  return requested < 0 ? ToggleControl(value,row) : SetControl(value,row,requested != 0);
}

inline const char* ControlLabel(text::Language language, ControlRow row) {
  constexpr const char* labels[4][9] = {
      {"Movement", "Invert camera X", "Invert camera Y", "Keep bodies", "Heroine model", "Show wounds", "Selector slow motion", "Selection world speed", "Interface scale"},
      {"D\x82placements", "Inverser cam\x82ra X", "Inverser cam\x82ra Y", "Garder les corps", "Mod\x8Ale h\x82ro\x8Bne", "Afficher blessures", "Ralenti de s\x82lection", "Vitesse du monde", "Taille interface"},
      {"Bewegung", "Kamera X umkehren", "Kamera Y umkehren", "Leichen behalten", "Heldinnenmodell", "Wunden anzeigen", "Auswahl-Zeitlupe", "Spieltempo bei Auswahl", "Interface-Gr\x94sse"},
      {"Movimento", "Inverti telecamera X", "Inverti telecamera Y", "Conserva corpi", "Modello eroina", "Mostra ferite", "Rallenta selezione", "Velocit\x85 del mondo", "Scala interfaccia"},
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
  if (row == ControlRow::kSelectorSpeed || row == ControlRow::kInterfaceScale) {
    static thread_local char percent[16];
    std::snprintf(percent,sizeof(percent),"%d%%",row == ControlRow::kSelectorSpeed ? value.selector_speed : value.interface_scale);
    return percent;
  }
  switch (row) {
    case ControlRow::kMovement:
      index = value.keyboard_modern != value.controller_modern ? 2u
          : value.keyboard_modern ? 0u : 1u;
      break;
    case ControlRow::kInvertX: index = value.invert_x ? 3u : 4u; break;
    case ControlRow::kInvertY: index = value.invert_y ? 3u : 4u; break;
    case ControlRow::kKeepBodies: index = value.keep_bodies ? 3u : 4u; break;
    case ControlRow::kBodyWounds: index = value.body_wounds ? 3u : 4u; break;
    case ControlRow::kSelectorSlowMotion: index = value.selector_slow_motion ? 3u : 4u; break;
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
