#include "camera_spring_arm.h"
#include "keyboard_camera_relative.h"

#include <cstdlib>
#include <iostream>

namespace {

void Require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "keyboard camera-relative failure: " << message << '\n';
    std::exit(1);
  }
}

deathtrap::input::KeyboardCameraRelativeContext EligibleContext() {
  deathtrap::input::KeyboardCameraRelativeContext context;
  context.feature_enabled = true;
  context.modern_third_person = true;
  context.gameplay_active = true;
  context.camera_input_owned = true;
  context.dispatcher_available = true;
  context.ground_state = true;
  context.heading_reference_valid = true;
  return context;
}

}  // namespace

int main() {
  using deathtrap::input::ResolveKeyboardCameraRelativePlan;

  const auto forward = ResolveKeyboardCameraRelativePlan(
      EligibleContext(), true, false, false, false);
  Require(forward.active && forward.rewrite_wasd && forward.native_forward &&
              forward.lateral == 0 && forward.longitudinal == 1,
          "W must request camera-forward motion through native W root motion");

  const auto backward = ResolveKeyboardCameraRelativePlan(
      EligibleContext(), false, true, false, false);
  Require(backward.active && backward.native_forward &&
              backward.lateral == 0 && backward.longitudinal == -1,
          "S must turn toward camera-back while retaining forward root motion");

  const auto left = ResolveKeyboardCameraRelativePlan(
      EligibleContext(), false, false, true, false);
  const auto right = ResolveKeyboardCameraRelativePlan(
      EligibleContext(), false, false, false, true);
  Require(left.active && left.lateral == -1 && left.longitudinal == 0,
          "A must request camera-left motion");
  Require(right.active && right.lateral == 1 && right.longitudinal == 0,
          "D must request camera-right motion");

  const auto diagonal = ResolveKeyboardCameraRelativePlan(
      EligibleContext(), true, false, false, true);
  Require(diagonal.active && diagonal.lateral == 1 &&
              diagonal.longitudinal == 1,
          "W+D must retain both camera-space components");

  auto running_context = EligibleContext();
  running_context.run_modifier = true;
  const auto running = ResolveKeyboardCameraRelativePlan(
      running_context, false, false, true, false);
  Require(running.active && running.native_forward && running.lateral == -1,
          "Shift must preserve camera-relative movement and native running");

  const auto cancelled = ResolveKeyboardCameraRelativePlan(
      EligibleContext(), true, true, false, false);
  Require(!cancelled.active && !cancelled.rewrite_wasd,
          "opposed directions must pass through without a synthetic driver");

  auto legacy = EligibleContext();
  legacy.feature_enabled = false;
  Require(!ResolveKeyboardCameraRelativePlan(
               legacy, true, false, false, false).rewrite_wasd,
          "the opt-out setting must preserve legacy W/A/S/D exactly");

  auto require_passthrough = [](deathtrap::input::KeyboardCameraRelativeContext
                                    context,
                                const char* message) {
    const auto plan = ResolveKeyboardCameraRelativePlan(
        context, false, false, true, false);
    Require(!plan.active && !plan.rewrite_wasd, message);
  };

  auto context = EligibleContext();
  context.combat_modifier = true;
  require_passthrough(context, "attack chords must retain directional grammar");
  context = EligibleContext();
  context.jump_climb_modifier = true;
  require_passthrough(context, "directional jumps must retain legacy keys");
  context = EligibleContext();
  context.walk_step_modifier = true;
  require_passthrough(context, "walk/step chords must retain legacy keys");
  context = EligibleContext();
  context.explicit_sidestep = true;
  require_passthrough(context, "explicit sidestep must retain legacy keys");
  context = EligibleContext();
  context.selector_command = true;
  require_passthrough(context, "selector input must not be rewritten");
  context = EligibleContext();
  context.camera_transition = true;
  require_passthrough(context, "camera transitions must not retain ownership");
  context = EligibleContext();
  context.pause_command = true;
  require_passthrough(context, "pause input must not be rewritten");
  context = EligibleContext();
  context.scripted_camera_active = true;
  require_passthrough(context, "scripted cameras must own their transition");
  context = EligibleContext();
  context.modern_third_person = false;
  require_passthrough(context, "first-person and retail camera modes must pass through");
  context = EligibleContext();
  context.gameplay_active = false;
  require_passthrough(context, "menus must preserve legacy input routing");
  context = EligibleContext();
  context.camera_input_owned = false;
  require_passthrough(context, "lost camera ownership must stop keyboard steering");
  context = EligibleContext();
  context.dispatcher_available = false;
  require_passthrough(context, "a missing native dispatcher hook must fail closed");
  context = EligibleContext();
  context.ground_state = false;
  require_passthrough(context, "airborne and non-ground states must pass through");
  context = EligibleContext();
  context.heading_reference_valid = false;
  require_passthrough(context, "a missing camera heading must fail closed");

  const auto heading_forward = CameraRelativeHeadingFromOrbit(
      0.0, static_cast<double>(forward.lateral),
      static_cast<double>(forward.longitudinal), 1024, true);
  const auto heading_backward = CameraRelativeHeadingFromOrbit(
      0.0, static_cast<double>(backward.lateral),
      static_cast<double>(backward.longitudinal), 1024, true);
  const auto heading_left = CameraRelativeHeadingFromOrbit(
      0.0, static_cast<double>(left.lateral),
      static_cast<double>(left.longitudinal), 1024, false);
  const auto heading_right = CameraRelativeHeadingFromOrbit(
      0.0, static_cast<double>(right.lateral),
      static_cast<double>(right.longitudinal), 1024, false);
  Require(heading_forward.valid && heading_forward.heading == 0,
          "W must use the verified inverted longitudinal camera basis");
  Require(heading_backward.valid && heading_backward.heading == 512,
          "S must oppose W in the verified runtime camera basis");
  Require(heading_left.valid && heading_left.heading == 768,
          "A heading must point screen-left");
  Require(heading_right.valid && heading_right.heading == 256,
          "D heading must point screen-right");

  const auto rotated_forward = CameraRelativeHeadingFromOrbit(
      3.14159265358979323846 / 2.0,
      static_cast<double>(forward.lateral),
      static_cast<double>(forward.longitudinal), 1024, true);
  Require(rotated_forward.valid && rotated_forward.heading == 256,
          "mouse-orbit yaw must rotate keyboard-forward heading");

  std::cout << "keyboard camera-relative tests passed\n";
  return 0;
}
