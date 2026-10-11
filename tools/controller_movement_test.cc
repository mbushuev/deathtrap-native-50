#include "controller_movement.h"

#include <cstdlib>
#include <iostream>
#include <limits>

namespace {

using deathtrap::input::ControllerMovementContext;
using deathtrap::input::ControllerMovementPlan;
using deathtrap::input::ControllerMovementThresholds;
using deathtrap::input::ResolveControllerMovementPlan;

void Require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "controller movement failure: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

bool NoCommands(const ControllerMovementPlan& plan) {
  return !plan.key_forward && !plan.key_backward && !plan.key_left &&
      !plan.key_right && !plan.sidestep_left && !plan.sidestep_right &&
      !plan.step_forward && !plan.step_backward;
}

bool Neutral(const ControllerMovementPlan& plan) {
  return plan.joystick_x == 0.0 && plan.joystick_y == 0.0 &&
      !plan.heading_active && !plan.walking && !plan.run && NoCommands(plan);
}

}  // namespace

int main() {
  const ControllerMovementThresholds thresholds;
  ControllerMovementContext normal;
  normal.native_joystick_available = true;
  normal.camera_relative_available = true;
  const auto resolve = [&](const ControllerMovementContext& context,
                           double x, double y, bool was_running = false,
                           bool was_walking = false) {
    return ResolveControllerMovementPlan(
        context, thresholds, x, y, std::hypot(x, y), was_running, was_walking);
  };

  Require(Neutral(resolve(normal, 0.0, 0.0, true)),
          "centering the stick must release movement and latched run");
  Require(Neutral(resolve(normal, 0.0, 0.10)),
          "the exact turn threshold must remain neutral");
  const auto walk_boundary = resolve(normal, 0.0, thresholds.walk);
  Require(walk_boundary.heading_active && !walk_boundary.walking &&
              walk_boundary.joystick_y == 0.0,
          "the exact walk threshold must not produce a forward step");

  // Every azimuth, including backwards and diagonals, gets the same radial
  // zones. Turn-only must retain an authoritative, fully centered override.
  for (int angle = 0; angle < 360; ++angle) {
    const double radians = angle * 3.14159265358979323846 / 180.0;
    const auto turn = resolve(normal, std::sin(radians) * 0.25,
                              std::cos(radians) * 0.25, true);
    Require(turn.joystick_override && turn.heading_active &&
                !turn.walking && !turn.run && turn.joystick_x == 0.0 &&
                turn.joystick_y == 0.0 && NoCommands(turn),
            "turn-only zone must never expose native translation");
    const auto extended_turn = resolve(normal, std::sin(radians) * 0.40,
                                       std::cos(radians) * 0.40);
    Require(extended_turn.heading_active && !extended_turn.walking &&
                extended_turn.joystick_y == 0.0,
            "the former 35-percent walk zone must now remain turn-only");
    const auto walk = resolve(normal, std::sin(radians) * 0.60,
                              std::cos(radians) * 0.60, true);
    Require(walk.heading_active && walk.walking && !walk.run &&
                walk.joystick_x == 0.0 && walk.joystick_y > 0.59,
            "wide walking zone must work identically in every direction");
    const auto high_walk = resolve(normal, std::sin(radians) * 0.84,
                                   std::cos(radians) * 0.84, true);
    Require(high_walk.walking && !high_walk.run,
            "the former 80-percent run zone must now walk even after a run");
    const auto near_full = resolve(normal, std::sin(radians) * 0.91,
                                   std::cos(radians) * 0.91);
    Require(near_full.walking && near_full.run,
            "deliberate near-full deflection must still run at every angle");
    const auto run = resolve(normal, std::sin(radians), std::cos(radians));
    Require(run.walking && run.run && run.joystick_y > 0.99,
            "full tilt must run on cardinals and diagonals");
  }
  Require(!resolve(normal, 0.0, 0.89).run,
          "run must not start before its greater deflection threshold");
  Require(resolve(normal, 0.0, thresholds.run).run,
          "the exact run entry threshold must start running");
  Require(resolve(normal, 0.0, 0.88, true).run,
          "small run hysteresis must avoid threshold chatter");
  Require(!resolve(normal, 0.0, thresholds.run_release, true).run,
          "the exact run release threshold must restore walking");
  Require(resolve(normal, 0.0, 0.425, false, true).walking &&
              !resolve(normal, 0.0, 0.425, false, false).walking,
          "walk hysteresis must prevent chatter around the entry threshold");
  Require(!resolve(normal, 0.0, thresholds.walk_release, false, true).walking,
          "the exact walk release threshold must stop translation");
  Require(Neutral(resolve(normal, 0.0, 0.0, true, true)),
          "centering must release both run and walk hysteresis immediately");

  using deathtrap::input::ControllerHeadingTurnScale;
  using deathtrap::input::SmoothControllerMovementMagnitude;
  double previous_turn_scale = 0.0;
  for (int index = 0; index <= 1000; ++index) {
    const double scale = ControllerHeadingTurnScale(index / 1000.0, thresholds);
    Require(scale >= previous_turn_scale && scale <= 1.0 &&
                scale - previous_turn_scale < 0.01,
            "turn strength must rise continuously, without a threshold kick");
    previous_turn_scale = scale;
  }
  double filtered = 0.0;
  for (int poll = 0; poll < 20; ++poll) {
    const double next = SmoothControllerMovementMagnitude(filtered, 1.0, 0.02, 100);
    Require(next > filtered && next < 1.0 && next - filtered < 0.19,
            "a sudden full tilt must ramp up rather than jump straight to run");
    filtered = next;
  }
  Require(filtered > thresholds.run &&
              SmoothControllerMovementMagnitude(filtered, 0.0, 0.02, 100) == 0.0,
          "ramp must reach running promptly and release without coasting");
  Require(SmoothControllerMovementMagnitude(1.0, 0.25, 0.02, 100) == 0.25,
          "reducing the stick must never retain a delayed walking/run input");
  Require(SmoothControllerMovementMagnitude(0.0, 0.9, 0.02, 0) == 0.9,
          "zero response time must retain configurable direct input");
  double split = 0.0;
  for (int index = 0; index < 5; ++index) {
    split = SmoothControllerMovementMagnitude(split, 1.0, 0.02, 100);
  }
  Require(std::abs(split - SmoothControllerMovementMagnitude(0.0, 1.0, 0.10, 100))
              < 1.0e-12,
          "ramp response must be independent of polling frequency");

  ControllerMovementContext cautious = normal;
  cautious.cautious_modifier = true;
  Require(!resolve(cautious, 0.0, 0.14, true).step_forward,
          "LB must not step inside its action threshold");
  const auto forward = resolve(cautious, 0.0, 1.0, true);
  Require(forward.step_forward && !forward.step_backward &&
              !forward.sidestep_left && !forward.sidestep_right &&
              !forward.run && forward.heading_active && forward.walking &&
              forward.joystick_x == 0.0 && forward.joystick_y == 0.0,
          "LB full-forward must issue only the original cautious step");
  for (int angle = 0; angle < 360; ++angle) {
    const double radians = angle * 3.14159265358979323846 / 180.0;
    const auto step = resolve(cautious, std::sin(radians), std::cos(radians), true);
    const int count = static_cast<int>(step.step_forward) +
        static_cast<int>(step.step_backward) +
        static_cast<int>(step.sidestep_left) +
        static_cast<int>(step.sidestep_right);
    Require(count == 1 && step.step_forward && step.heading_active &&
                !step.run && step.walking &&
                step.joystick_x == 0.0 && step.joystick_y == 0.0,
            "LB must use the full ordinary heading at every angle, not sectors");
  }
  const auto release_lb = resolve(normal, 0.0, 0.60, forward.run);
  Require(release_lb.walking && !release_lb.run &&
              !release_lb.step_forward && !release_lb.step_backward,
          "releasing LB must release Ctrl and not revive an old run latch");

  ControllerMovementContext strafe = normal;
  strafe.cautious_strafe_modifier = true;
  Require(Neutral(resolve(strafe, 0.0, 0.14, true)),
          "RB must retain its cautious action threshold");
  for (int angle = 0; angle < 360; ++angle) {
    const double radians = angle * 3.14159265358979323846 / 180.0;
    const double x = std::sin(radians);
    const double y = std::cos(radians);
    const auto step = resolve(strafe, x, y, true);
    const bool lateral = std::abs(x) > std::abs(y);
    const int count = static_cast<int>(step.sidestep_left) +
        static_cast<int>(step.sidestep_right) +
        static_cast<int>(step.step_forward) +
        static_cast<int>(step.step_backward);
    Require(count == 1 && !step.heading_active && !step.run &&
                step.sidestep_left == (lateral && x < 0.0) &&
                step.sidestep_right == (lateral && x > 0.0) &&
                step.step_forward == (!lateral && y > 0.0) &&
                step.step_backward == (!lateral && y < 0.0) &&
                step.joystick_override && step.joystick_x == 0.0 &&
                step.joystick_y == 0.0,
            "RB must choose original J/K or Ctrl+W/S without mixing or turning");
  }
  strafe.cautious_modifier = true;
  Require(resolve(strafe, 0.8, 0.6).sidestep_right &&
              !resolve(strafe, 0.8, 0.6).heading_active,
          "RB must win over LB when both shoulders are held");
  Require(resolve(strafe, -0.8, 0.6).sidestep_left &&
              resolve(strafe, 0.0, -1.0).step_backward &&
              !resolve(strafe, 0.6, 0.8).heading_active,
          "RB must retain native cautious actions, never LB heading");
  for (const bool native : {false, true}) {
    for (const bool head : {false, true}) {
      strafe.native_joystick_available = native;
      strafe.immersive_first_person = head;
      Require(resolve(strafe, 1.0, 0.0).sidestep_right &&
                  resolve(strafe, -1.0, 0.0).sidestep_left &&
                  !resolve(strafe, 1.0, 0.0).step_forward,
              "RB side input must keep real strafe in every view/hook fallback");
    }
  }

  for (const bool captured : {false, true}) {
    for (const bool combat : {false, true}) {
      if (!captured && !combat) continue;
      ControllerMovementContext owned = cautious;
      owned.selector_captures_controls = captured;
      owned.combat_owns_stick = combat;
      const auto blocked = resolve(owned, 0.6, 0.8, true);
      Require(blocked.joystick_override && Neutral(blocked),
              "attacks and selectors must suppress LB, run and physical axes");
      owned.cautious_strafe_modifier = true;
      Require(Neutral(resolve(owned, 0.6, 0.8, true)),
              "attacks and selectors must also suppress RB native steps");
    }
  }

  ControllerMovementContext retail_view = normal;
  retail_view.camera_relative_available = false;
  retail_view.first_person = true;
  Require(Neutral(resolve(retail_view, 0.25, 0.0)),
          "small first-person deflections must not turn or sidestep");
  const auto retail_strafe = resolve(retail_view, 0.6, 0.0);
  Require(retail_strafe.sidestep_right && retail_strafe.joystick_x == 0.0 &&
              retail_strafe.joystick_y == 0.0,
          "retail first person must retain its explicit lateral action");
  ControllerMovementContext immersive = normal;
  immersive.first_person = true;
  immersive.immersive_first_person = true;
  const auto immersive_strafe = resolve(immersive, 1.0, 0.0);
  Require(immersive_strafe.run && immersive_strafe.joystick_y == 1.0 &&
              !immersive_strafe.sidestep_right,
          "immersive full-speed lateral motion must keep its native root driver");
  Require(resolve(immersive, 0.0, -0.6).joystick_y < 0.0,
          "immersive backward movement must preserve its negative driver");
  immersive.cautious_modifier = true;
  Require(resolve(immersive, 0.0, 1.0, true).step_forward,
          "LB cautious steps must also work in immersive first person");
  Require(resolve(immersive, 0.6, -0.8, true).step_backward &&
              resolve(immersive, 1.0, 0.0, true).step_forward &&
              resolve(immersive, 0.6, -0.8, true).heading_active,
          "immersive LB diagonals must keep the native backward root driver");
  Require(resolve(immersive, -1.0, 0.0, true).walking &&
              !resolve(immersive, -1.0, 0.0, true).sidestep_left &&
              !resolve(immersive, -1.0, 0.0, true).run,
          "immersive LB strafe must expose its complete vector without J/K");

  ControllerMovementContext fallback;
  const auto fallback_turn = resolve(fallback, 0.25, 0.0);
  Require(fallback_turn.key_right && !fallback_turn.key_forward &&
              !fallback_turn.joystick_override,
          "unavailable native hooks must retain the keyboard turn fallback");
  Require(!resolve(fallback, 0.0, 0.25).key_forward &&
              resolve(fallback, 0.0, 1.0).key_forward,
          "keyboard fallback forward motion must obey the walking gate");
  fallback.cautious_modifier = true;
  Require(resolve(fallback, 0.0, -1.0).step_backward,
          "cautious commands must not depend on native joystick hooks");
  Require(resolve(fallback, -0.8, 0.6).sidestep_left &&
              resolve(fallback, 0.7, 0.7).step_forward,
          "missing heading hooks must retain the verified retail step fallback");
  Require(Neutral(ResolveControllerMovementPlan(
              normal, thresholds, std::numeric_limits<double>::quiet_NaN(),
              0.0, 1.0, true)),
          "invalid samples must fail neutral instead of retaining motion");

  std::cout << "controller movement zones, LB/RB steps and input ownership tests passed\n";
}
