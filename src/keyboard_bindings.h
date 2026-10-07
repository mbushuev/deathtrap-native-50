#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace deathtrap::input {

enum class KeyboardAction : size_t {
  kMoveForward,
  kMoveBackward,
  kMoveLeft,
  kMoveRight,
  kRun,
  kWalkStep,
  kJumpClimb,
  kAttack,
  kCastSpell,
  kRetailFirstPerson,
  kOperate,
  kSidestepLeft,
  kSidestepRight,
  kMeleeSelector,
  kRangedSelector,
  kMagicSelector,
  kItemSelector,
  kImmersiveView,
  kNativeRate,
  kPause,
  kCount,
};

constexpr size_t kKeyboardActionCount =
    static_cast<size_t>(KeyboardAction::kCount);

struct KeyboardActionDescriptor {
  const char* id;
  const char* label;
  uint16_t default_retail_code;
  uint8_t stable_scan;
};

// Retail letter codes are A..Z at 0x80..0x99 rather than DirectInput scans.
// The remaining supported keyboard values use their DirectInput scan code.
constexpr std::array<uint8_t, 26> kLetterScans = {
    0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17,
    0x24, 0x25, 0x26, 0x32, 0x31, 0x18, 0x19, 0x10, 0x13,
    0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C};

constexpr uint8_t RetailCodeToDirectInputScan(uint16_t code) {
  if (code >= 0x80u && code <= 0x99u) {
    return kLetterScans[code - 0x80u];
  }
  if ((code > 0u && code < 0x80u) ||
      (code >= 0xC7u && code <= 0xD3u)) {
    return static_cast<uint8_t>(code);
  }
  return 0u;
}

constexpr bool IsBindableKeyboardRetailCode(uint16_t code) {
  return RetailCodeToDirectInputScan(code) != 0u;
}

constexpr std::array<KeyboardActionDescriptor, kKeyboardActionCount>
    kKeyboardActions = {{
        {"move_forward", "MOVE FORWARD", 0x96u, 0x11u},
        {"move_backward", "MOVE BACK", 0x92u, 0x1Fu},
        {"move_left", "MOVE LEFT", 0x80u, 0x1Eu},
        {"move_right", "MOVE RIGHT", 0x83u, 0x20u},
        {"run", "RUN", 0x2Au, 0x2Au},
        {"walk_step", "WALK / STEP", 0x1Du, 0x1Du},
        {"jump_climb", "JUMP / CLIMB", 0x39u, 0x39u},
        {"attack", "ATTACK", 0x85u, 0x21u},
        {"cast_spell", "CAST SPELL", 0x90u, 0x10u},
        {"retail_first_person", "RETAIL 1ST PERSON", 0x0Fu, 0x0Fu},
        {"operate", "OPERATE", 0x84u, 0x12u},
        {"sidestep_left", "SIDESTEP LEFT", 0x89u, 0x24u},
        {"sidestep_right", "SIDESTEP RIGHT", 0x8Au, 0x25u},
        {"melee_selector", "MELEE SELECTOR", 0x3Bu, 0x3Bu},
        {"ranged_selector", "RANGED SELECTOR", 0x3Cu, 0x3Cu},
        {"magic_selector", "MAGIC SELECTOR", 0x3Du, 0x3Du},
        {"item_selector", "ITEM SELECTOR", 0x3Eu, 0x3Eu},
        {"immersive_view", "IMMERSIVE VIEW", 0x44u, 0x44u},
        {"native_rate", "NATIVE RATE", 0x57u, 0x57u},
        {"pause", "PAUSE", 0x8Fu, 0x19u},
    }};

constexpr std::array<uint16_t, kKeyboardActionCount>
DefaultKeyboardBindings() {
  std::array<uint16_t, kKeyboardActionCount> result{};
  for (size_t index = 0; index < result.size(); ++index) {
    result[index] = kKeyboardActions[index].default_retail_code;
  }
  return result;
}

using KeyboardBindingArray =
    std::array<uint16_t, kKeyboardActionCount>;
using KeyboardBindingScanArray =
    std::array<uint8_t, kKeyboardActionCount>;
using KeyboardBindingModeArray =
    std::array<uint8_t, kKeyboardActionCount>;
using DirectInputKeyboardState = std::array<uint8_t, 256>;

constexpr KeyboardBindingModeArray DefaultKeyboardBindingModes() {
  // The retail input and capture routines both describe physical DirectInput
  // positions. Keyboard Setup translates only their labels for the active
  // layout (WASD -> ZQSD on French AZERTY), without translating input twice.
  return {};
}

constexpr uint8_t CapturedKeyboardBindingMode(uint16_t) {
  // Dungeon's capture routine polls its fixed DirectInput-position table and
  // returns that position's retail code. This is physical even when the label
  // painted for the active Windows layout is Z/Q/S/D or another arrangement.
  return 0u;
}

constexpr uint8_t ResolveKeyboardBindingSourceScan(
    uint16_t binding, uint8_t layout_aware, uint8_t layout_scan) {
  return layout_aware != 0u ? layout_scan
                            : RetailCodeToDirectInputScan(binding);
}

constexpr KeyboardBindingScanArray ResolveQwertyKeyboardBindingScans(
    const KeyboardBindingArray& bindings) {
  KeyboardBindingScanArray scans{};
  for (size_t action = 0; action < scans.size(); ++action) {
    scans[action] = RetailCodeToDirectInputScan(bindings[action]);
  }
  return scans;
}

inline KeyboardBindingScanArray ResolveKeyboardBindingSourceScans(
    const KeyboardBindingArray& bindings,
    const KeyboardBindingModeArray& layout_aware,
    const KeyboardBindingScanArray& layout_scans) {
  KeyboardBindingScanArray scans{};
  for (size_t action = 0; action < scans.size(); ++action) {
    scans[action] = ResolveKeyboardBindingSourceScan(
        bindings[action], layout_aware[action], layout_scans[action]);
  }
  return scans;
}

inline std::array<bool, kKeyboardActionCount> ResolveKeyboardActionsFromScans(
    const KeyboardBindingScanArray& scans,
    const DirectInputKeyboardState& physical) {
  std::array<bool, kKeyboardActionCount> active{};
  for (size_t action = 0; action < active.size(); ++action) {
    const uint8_t scan = scans[action];
    active[action] = scan != 0u && (physical[scan] & 0x80u) != 0u;
  }
  return active;
}

inline void ApplyKeyboardBindingsFromScans(
    const KeyboardBindingScanArray& scans,
    DirectInputKeyboardState* keyboard) {
  if (!keyboard) {
    return;
  }
  const DirectInputKeyboardState physical = *keyboard;
  const auto active = ResolveKeyboardActionsFromScans(scans, physical);
  // Classify the complete physical state before clearing any stable target.
  // This makes arbitrary swaps (for example W<->S) deterministic.
  for (const auto& descriptor : kKeyboardActions) {
    (*keyboard)[descriptor.stable_scan] &= static_cast<uint8_t>(~0x80u);
  }
  for (size_t action = 0; action < active.size(); ++action) {
    if (active[action]) {
      (*keyboard)[kKeyboardActions[action].stable_scan] |= 0x80u;
    }
  }
}

inline void TranslateCanonicalKeyboardTargets(
    const KeyboardBindingScanArray& target_scans,
    DirectInputKeyboardState* keyboard) {
  if (!keyboard) {
    return;
  }
  std::array<bool, kKeyboardActionCount> active{};
  for (size_t action = 0; action < active.size(); ++action) {
    active[action] =
        ((*keyboard)[kKeyboardActions[action].stable_scan] & 0x80u) != 0u;
  }
  for (size_t action = 0; action < active.size(); ++action) {
    (*keyboard)[kKeyboardActions[action].stable_scan] &=
        static_cast<uint8_t>(~0x80u);
    const uint8_t target_scan = target_scans[action];
    if (target_scan != 0u) {
      (*keyboard)[target_scan] &= static_cast<uint8_t>(~0x80u);
    }
  }
  for (size_t action = 0; action < active.size(); ++action) {
    const uint8_t target_scan = target_scans[action];
    if (active[action] && target_scan != 0u) {
      (*keyboard)[target_scan] |= 0x80u;
    }
  }
}

inline std::array<bool, kKeyboardActionCount> ResolveKeyboardActions(
    const KeyboardBindingArray& bindings,
    const DirectInputKeyboardState& physical) {
  return ResolveKeyboardActionsFromScans(
      ResolveQwertyKeyboardBindingScans(bindings), physical);
}

inline void ApplyKeyboardBindings(const KeyboardBindingArray& bindings,
                                  DirectInputKeyboardState* keyboard) {
  ApplyKeyboardBindingsFromScans(
      ResolveQwertyKeyboardBindingScans(bindings), keyboard);
}

}  // namespace deathtrap::input
