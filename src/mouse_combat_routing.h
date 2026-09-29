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

struct MouseAttackFacingStep {
  int32_t heading_delta = 0;
  bool ready = false;
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

// A physical press may request a one-shot camera-facing body turn before the
// retail combat chord is consumed. Held samples must not keep steering an
// already-running attack, and controller attacks retain their own direction
// selection path.
constexpr bool ShouldQueueMouseAttackCameraFacing(
    bool physical_left_button_pressed, bool gameplay_accepts_combat,
    bool camera_relative_available) {
  return physical_left_button_pressed && gameplay_accepts_combat &&
      camera_relative_available;
}

// Advance a latched mouse attack toward its fixed camera heading without
// exposing synthetic A/D to the retail movement grammar. The bounded canonical
// delta produces a visible multi-frame turn; the final small remainder lands
// exactly on target before the attack is released.
constexpr MouseAttackFacingStep ResolveMouseAttackFacingStep(
    int32_t heading_error, int32_t maximum_step,
    int32_t ready_tolerance) {
  const int32_t step = maximum_step < 1 ? 1 : maximum_step;
  const int32_t tolerance = ready_tolerance < 0 ? 0 : ready_tolerance;
  const int32_t absolute_error =
      heading_error < 0 ? -heading_error : heading_error;
  if (absolute_error <= tolerance) {
    return {heading_error, true};
  }
  if (heading_error < -step) {
    return {-step, false};
  }
  if (heading_error > step) {
    return {step, false};
  }
  return {heading_error, true};
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
