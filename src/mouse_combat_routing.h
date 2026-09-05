#pragma once

#include <cstdint>

namespace deathtrap::input {

enum class MouseCombatAttack : uint8_t {
  kNone = 0,
  kForward = 1,
  kLeft = 2,
  kRight = 3,
  kBack = 4,
  kRanged = 5,
};

struct MouseCombatKeyboardPlan {
  MouseCombatAttack attack = MouseCombatAttack::kNone;
  bool modifier_down = false;
  bool submit_immersive_movement = false;
  bool rewrite_immersive_movement = false;
  bool key_w = false;
  bool key_a = false;
  bool key_s = false;
  bool key_d = false;
};

struct ControllerCombatDirection {
  bool forward = false;
  bool backward = false;
  bool left = false;
  bool right = false;
};

struct ControllerJoystickMovementPlan {
  bool override_active = false;
  double x = 0.0;
  double y = 0.0;
};

constexpr double Absolute(double value) {
  return value < 0.0 ? -value : value;
}

// Classify the stick into one unambiguous dominant-axis sector.
constexpr ControllerCombatDirection ResolveControllerCombatDirection(
    double x, double y, double threshold) {
  const double abs_x = Absolute(x);
  const double abs_y = Absolute(y);
  if (abs_x <= threshold && abs_y <= threshold) {
    return {};
  }
  if (abs_x > abs_y) {
    return {false, false, x < 0.0, x > 0.0};
  }
  return {y >= 0.0, y < 0.0, false, false};
}

// The native hook must stay active with centered axes while RT owns the stick.
// Disabling the override would expose the same physical controller through the
// game's original DirectInput poll and reintroduce locomotion during combat.
constexpr ControllerJoystickMovementPlan ResolveControllerJoystickMovement(
    bool native_hook_available, bool selector_captures_controls,
    bool combat_owns_stick, bool side_step, double x, double y) {
  if (!native_hook_available || selector_captures_controls) {
    return {};
  }
  if (combat_owns_stick) {
    return {true, 0.0, 0.0};
  }
  return {true, side_step ? 0.0 : x, y};
}

// Modern mouse combat is translated into the original F+direction grammar at
// the DirectInput keyboard boundary. Directionless/forward clicks select the
// established overhead attack; backward selects the retail A+D back attack.
// No live action-table state is mutated.
constexpr MouseCombatKeyboardPlan ResolveMouseCombatKeyboardPlan(
    bool left_button, bool gameplay_accepts_combat,
    bool immersive_vector_locomotion, bool forward, bool backward, bool left,
    bool right, bool ranged_weapon_selected = false) {
  if (left_button && gameplay_accepts_combat) {
    // ACTION_ATTACK_RANGED is plain F. The directional chords below are the
    // retail melee grammar; sending their W/A/D components while a ranged
    // weapon is active also moves Lara as the projectile is launched.
    if (ranged_weapon_selected) {
      return {MouseCombatAttack::kRanged, true, false, false, false, false,
              false, false};
    }
    if (backward || (left && right)) {
      return {MouseCombatAttack::kBack, true, false, false, false, true,
              false, true};
    }
    if (left) {
      return {MouseCombatAttack::kLeft, true, false, false, false, true,
              false, false};
    }
    if (right) {
      return {MouseCombatAttack::kRight, true, false, false, false, false,
              false, true};
    }
    return {MouseCombatAttack::kForward, true, false, false, true, false,
            false, false};
  }

  const int lateral = static_cast<int>(right) - static_cast<int>(left);
  const int longitudinal =
      static_cast<int>(forward) - static_cast<int>(backward);
  const bool pure_lateral = lateral != 0 && longitudinal == 0;
  const bool vector_movement =
      immersive_vector_locomotion && lateral != 0;
  return {MouseCombatAttack::kNone,
          false,
          vector_movement,
          vector_movement,
          vector_movement && (forward || pure_lateral),
          false,
          vector_movement && backward,
          false};
}

}  // namespace deathtrap::input
