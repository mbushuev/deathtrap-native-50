#pragma once
#include <cstdint>
#include "game_text.h"

namespace deathtrap::recovery {
// Only events observed in this run can establish a checkpoint. Disk slot dates
// never decide which save to offer. A native death retry preserves the run;
// a fresh level entry (including the same level) starts another one.
struct Checkpoint {
  uintptr_t context = 0;
  uint8_t level = 0;
  int slot = -1;
  void Observe(uintptr_t owner, uint8_t current_level) {
    if (context != owner || level != current_level) {
      context = owner; level = current_level; slot = -1;
    }
  }
  void Prepare(uintptr_t owner, uint8_t current_level, bool death_retry) {
    Observe(owner, current_level);
    if (!death_retry) slot = -1;
  }
  void Completed(uintptr_t owner, uint8_t current_level, int saved_slot, bool success) {
    Observe(owner, current_level);
    if (success && saved_slot >= 0 && saved_slot < 10) slot = saved_slot;
  }
  void Destroy(uintptr_t owner) { if (context == owner) *this = {}; }
};
inline int Slot(const char* name) {
  return name && name[0] == 's' && name[1] >= '0' && name[1] <= '9' && !name[2]
      ? name[1] - '0' : -1;
}
inline const char* Title(text::Language language) {
  constexpr const char* titles[]{"Game over", "Fin de partie", "Spiel vorbei", "Game over"};
  return titles[static_cast<int>(language)];
}
inline const char* Choice(text::Language language, int row) {
  constexpr const char* choices[][2] = {
    {"Load last save", "Restart level"},
    {"Charger la sauvegarde", "Recommencer le niveau"},
    {"Spielstand laden", "Level neu starten"},
    {"Carica il salvataggio", "Ricomincia il livello"}};
  return choices[static_cast<int>(language)][row == 1 ? 1 : 0];
}
inline const char* Description(text::Language language) {
  constexpr const char* descriptions[]{
    "Continue from your last save in this level, or start the level again.",
    "Reprendre la derni\x8are sauvegarde de ce niveau, ou recommencer le niveau.",
    "Letzten Spielstand dieses Levels laden oder das Level neu beginnen.",
    "Riprendi dall'ultimo salvataggio di questo livello, o ricomincia il livello."};
  return descriptions[static_cast<int>(language)];
}
inline const char* Help(text::Language language) {
  constexpr const char* help[]{"Click / Enter / A: select", "Clic / Entr\x82" "e / A : choisir",
    "Klick / Enter / A: w\x84hlen", "Clic / Invio / A: scegli"};
  return help[static_cast<int>(language)];
}
constexpr int kChoiceTop = 163, kChoiceStep = 68;
struct Navigation {
  int selected = 0;
  bool mouse_held = true, key_held = true, up_held = true, down_held = true;
  uint16_t previous_x = UINT16_MAX, previous_y = UINT16_MAX;
  int Poll(uint16_t x, uint16_t y, bool mouse, bool up, bool down, bool confirm) {
    int hovered = -1;
    for (int row = 0; row < 2; ++row)
      if (x >= 127 && x < 458 && y >= kChoiceTop + row*kChoiceStep - 5 &&
          y < kChoiceTop + row*kChoiceStep + 40) hovered = row;
    if (hovered >= 0 && (x != previous_x || y != previous_y)) selected = hovered;
    if ((up && !up_held) || (down && !down_held)) selected ^= 1;
    const int result = (confirm && !key_held) ? selected :
        (mouse && !mouse_held && hovered >= 0) ? hovered : -1;
    mouse_held = mouse; key_held = confirm; up_held = up; down_held = down;
    previous_x = x; previous_y = y;
    return result;
  }
};
}  // namespace deathtrap::recovery
