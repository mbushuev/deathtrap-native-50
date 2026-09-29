#pragma once

#include <cstdint>

namespace deathtrap::input {

struct KeyboardCameraRelativeContext {
  bool feature_enabled = false;
  bool modern_third_person = false;
  bool gameplay_active = false;
  bool camera_input_owned = false;
  bool dispatcher_available = false;
  bool ground_state = false;
  bool heading_reference_valid = false;
  bool scripted_camera_active = false;
  bool combat_modifier = false;
  // Run is deliberately compatible with the synthetic native-forward driver.
  bool run_modifier = false;
  bool jump_climb_modifier = false;
  bool walk_step_modifier = false;
  bool explicit_sidestep = false;
  bool selector_command = false;
  bool camera_transition = false;
  bool pause_command = false;
};

struct KeyboardCameraRelativePlan {
  bool active = false;
  bool rewrite_wasd = false;
  bool native_forward = false;
  int32_t lateral = 0;
  int32_t longitudinal = 0;
};

constexpr bool KeyboardCameraRelativeContextEligible(
    const KeyboardCameraRelativeContext& context) {
  return context.feature_enabled && context.modern_third_person &&
      context.gameplay_active && context.camera_input_owned &&
      context.dispatcher_available && context.ground_state &&
      context.heading_reference_valid && !context.scripted_camera_active &&
      !context.combat_modifier &&
      !context.jump_climb_modifier && !context.walk_step_modifier &&
      !context.explicit_sidestep && !context.selector_command &&
      !context.camera_transition && !context.pause_command;
}

// Camera-relative keyboard locomotion deliberately keeps the retail forward
// action as its sole root-motion driver. The common player-state dispatcher
// rotates the canonical actor/collision heading toward this screen-space
// vector before the retail movement state runs. Any chord with separate retail
// semantics fails closed to the original W/S/A/D state.
constexpr KeyboardCameraRelativePlan ResolveKeyboardCameraRelativePlan(
    const KeyboardCameraRelativeContext& context, bool forward, bool backward,
    bool left, bool right) {
  const int32_t lateral = static_cast<int32_t>(right) -
      static_cast<int32_t>(left);
  const int32_t longitudinal = static_cast<int32_t>(forward) -
      static_cast<int32_t>(backward);
  if (!KeyboardCameraRelativeContextEligible(context) ||
      (lateral == 0 && longitudinal == 0)) {
    return {};
  }
  return {true, true, true, lateral, longitudinal};
}

}  // namespace deathtrap::input
