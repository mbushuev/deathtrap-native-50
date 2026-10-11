#pragma once

#include <algorithm>
#include <cmath>

namespace deathtrap::input {

struct ControllerMovementThresholds {
  // Fractions of the radial stick range remaining after its deadzone.
  double turn = 0.10;
  double walk = 0.45;
  double run = 0.90;
  double run_release = 0.85;
  double action = 0.14;
  double walk_release = 0.40;
};

// Only the magnitude is filtered: averaging X/Y would lag direction changes
// or lose the diagonal course. Release is immediate, not a coasting tail.
inline double SmoothControllerMovementMagnitude(
    double previous, double requested, double elapsed_seconds,
    double response_ms) {
  if (!std::isfinite(requested) || requested <= 0.0) return 0.0;
  requested = std::clamp(requested, 0.0, 1.0);
  if (!std::isfinite(previous)) previous = 0.0;
  previous = std::clamp(previous, 0.0, 1.0);
  if (!std::isfinite(response_ms) || response_ms <= 0.0) return requested;
  if (!std::isfinite(elapsed_seconds) || elapsed_seconds <= 0.0) {
    return std::min(previous, requested);
  }
  // Deceleration has no delayed movement/run latch. Ease only the rise.
  if (requested <= previous) return requested;
  const double alpha = -std::expm1(
      -std::min(elapsed_seconds, 0.25) * 1000.0 / response_ms);
  return std::clamp(previous + (requested - previous) * alpha,
                    previous, requested);
}

inline double ControllerHeadingTurnScale(
    double magnitude, const ControllerMovementThresholds& thresholds) {
  if (!std::isfinite(magnitude) || thresholds.walk <= thresholds.turn) {
    return 0.0;
  }
  const double amount = std::clamp(
      (magnitude - thresholds.turn) / (thresholds.walk - thresholds.turn),
      0.0, 1.0);
  return amount * amount * (3.0 - 2.0 * amount);
}

struct ControllerMovementContext {
  bool native_joystick_available = false;
  bool camera_relative_available = false;
  bool first_person = false;
  bool immersive_first_person = false;
  bool selector_captures_controls = false;
  bool combat_owns_stick = false;
  bool cautious_modifier = false;
  bool cautious_strafe_modifier = false;
};

struct ControllerMovementPlan {
  bool joystick_override = false;
  double joystick_x = 0.0;
  double joystick_y = 0.0;
  bool heading_active = false;
  bool walking = false;
  bool run = false;
  bool key_forward = false;
  bool key_backward = false;
  bool key_left = false;
  bool key_right = false;
  bool sidestep_left = false;
  bool sidestep_right = false;
  bool step_forward = false;
  bool step_backward = false;
};

// Heading intent and translation deliberately have separate gates. Keep a
// centered native override even in the turn-only zone: falling through to the
// physical DirectInput controller would reintroduce accidental forward steps.
inline ControllerMovementPlan ResolveControllerMovementPlan(
    const ControllerMovementContext& context,
    const ControllerMovementThresholds& thresholds, double x, double y,
    double magnitude, bool was_running, bool was_walking = false) {
  ControllerMovementPlan plan;
  plan.joystick_override = context.native_joystick_available;
  if (context.selector_captures_controls || context.combat_owns_stick ||
      !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(magnitude)) {
    return plan;
  }
  magnitude = std::clamp(magnitude, 0.0, 1.0);
  const double abs_x = std::abs(x);
  const double abs_y = std::abs(y);
  const bool camera_relative = context.native_joystick_available &&
      context.camera_relative_available;

  // RB wins over LB and invokes the original J/K side-step states, not a
  // forward step with redirected root motion. Keep all heading/axes neutral.
  if (context.cautious_modifier || context.cautious_strafe_modifier) {
    if (camera_relative && !context.cautious_strafe_modifier) {
      // Third person keeps exactly the ordinary camera-relative stick course,
      // without quadrant snapping. The native cautious forward step follows
      // the bounded heading writer. Immersive view instead keeps its existing
      // full-vector root transaction and eye-facing body orientation.
      plan.heading_active = context.immersive_first_person ||
          magnitude > thresholds.turn;
      plan.walking = magnitude > thresholds.action;
      if (plan.walking) {
        plan.step_backward = context.immersive_first_person &&
            y < -thresholds.action;
        plan.step_forward = !plan.step_backward;
      }
      return plan;
    }
    if (magnitude <= thresholds.action) {
      return plan;
    }
    // RB uses the original four native cautious actions in every camera mode.
    // Their states are mutually exclusive: choose the dominant stick axis,
    // never mix J/K with Ctrl+W/S. LB uses this only as its retail fallback.
    if (abs_x > abs_y) {
      plan.sidestep_left = x < 0.0;
      plan.sidestep_right = x > 0.0;
    } else {
      plan.step_forward = y > 0.0;
      plan.step_backward = y < 0.0;
    }
    // Fallback uses original Ctrl+W/S or J/K, never joystick locomotion or
    // Shift. A fresh ordinary run threshold is required after release.
    return plan;
  }

  plan.walking = magnitude > thresholds.walk ||
      (was_walking && magnitude > thresholds.walk_release);
  const bool turn_requested = magnitude > thresholds.turn;
  plan.heading_active = camera_relative &&
      (context.immersive_first_person || turn_requested);
  if (camera_relative) {
    if (plan.walking) {
      // The verified immersive root-motion path supplies the full vector;
      // third person turns the body and drives its ordinary forward action.
      const bool backward = context.immersive_first_person &&
          y < -thresholds.action;
      plan.joystick_y = backward ? -magnitude : magnitude;
    }
  } else {
    const bool sidestep = context.first_person;
    plan.joystick_x = !sidestep && turn_requested ? x : 0.0;
    plan.joystick_y = plan.walking ? y : 0.0;
    plan.sidestep_left = sidestep && plan.walking && x < -thresholds.turn;
    plan.sidestep_right = sidestep && plan.walking && x > thresholds.turn;
    if (!context.native_joystick_available) {
      // Preserve the keyboard-backed tank fallback if native hooks cannot be
      // installed; a small horizontal tilt still turns without forward input.
      plan.key_forward = plan.walking && y > thresholds.turn;
      plan.key_backward = plan.walking && y < -thresholds.turn;
      plan.key_left = !sidestep && turn_requested && x < -thresholds.turn;
      plan.key_right = !sidestep && turn_requested && x > thresholds.turn;
    }
  }
  const double run_magnitude = camera_relative ||
          context.immersive_first_person
      ? magnitude : abs_y;
  plan.run = plan.walking &&
      (run_magnitude >= thresholds.run ||
       (was_running && run_magnitude > thresholds.run_release));
  return plan;
}

}  // namespace deathtrap::input
