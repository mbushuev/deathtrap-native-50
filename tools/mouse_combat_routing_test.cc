#include "mouse_combat_routing.h"

#include <cstdlib>
#include <iostream>

namespace {

void Require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "mouse combat routing failure: " << message << '\n';
    std::exit(1);
  }
}

}  // namespace

int main() {
  using deathtrap::input::MouseCombatAttack;
  using deathtrap::input::ResolveControllerCombatDirection;
  using deathtrap::input::ResolveControllerJoystickMovement;
  using deathtrap::input::ResolveMouseCombatKeyboardPlan;

  const auto centered =
      ResolveControllerCombatDirection(0.1, -0.1, 0.25);
  Require(!centered.forward && !centered.backward && !centered.left &&
              !centered.right,
          "centered controller stick must retain the default attack");

  const auto diagonal_right =
      ResolveControllerCombatDirection(0.8, 0.6, 0.25);
  Require(diagonal_right.right && !diagonal_right.forward,
          "a right-dominant diagonal must resolve to one right sector");

  const auto diagonal_forward =
      ResolveControllerCombatDirection(-0.5, 0.9, 0.25);
  Require(diagonal_forward.forward && !diagonal_forward.left,
          "a forward-dominant diagonal must resolve to one forward sector");

  const auto diagonal_back =
      ResolveControllerCombatDirection(0.5, -0.9, 0.25);
  Require(diagonal_back.backward && !diagonal_back.right,
          "a backward-dominant diagonal must resolve to one back sector");

  const auto equal_axes =
      ResolveControllerCombatDirection(-0.7, -0.7, 0.25);
  Require(equal_axes.backward && !equal_axes.left,
          "an exact diagonal must deterministically prefer its vertical sector");

  const auto combat_joystick = ResolveControllerJoystickMovement(
      true, false, true, false, 0.8, -0.6);
  Require(combat_joystick.override_active && combat_joystick.x == 0.0 &&
              combat_joystick.y == 0.0,
          "combat must keep the native joystick override active and centered");

  const auto movement_joystick = ResolveControllerJoystickMovement(
      true, false, false, false, 0.8, -0.6);
  Require(movement_joystick.override_active &&
              movement_joystick.x == 0.8 && movement_joystick.y == -0.6,
          "ordinary movement must retain both joystick axes");

  const auto sidestep_joystick = ResolveControllerJoystickMovement(
      true, false, false, true, 0.8, -0.6);
  Require(sidestep_joystick.override_active &&
              sidestep_joystick.x == 0.0 && sidestep_joystick.y == -0.6,
          "side-step mode must reserve the native horizontal axis");

  const auto selector_joystick = ResolveControllerJoystickMovement(
      true, true, false, false, 0.8, -0.6);
  Require(!selector_joystick.override_active,
          "selector ownership must release the gameplay joystick override");

  const auto simple_attack = ResolveMouseCombatKeyboardPlan(
      true, true, false, false, false, false, false);
  Require(simple_attack.attack == MouseCombatAttack::kForward,
          "plain left click must select the normal forward attack");
  Require(simple_attack.modifier_down && simple_attack.key_w,
          "plain left click must emit the verified F+W grammar");

  const auto ranged_attack = ResolveMouseCombatKeyboardPlan(
      true, true, false, true, false, true, false, true);
  Require(ranged_attack.attack == MouseCombatAttack::kRanged &&
              ranged_attack.modifier_down && !ranged_attack.key_w &&
              !ranged_attack.key_a && !ranged_attack.key_s &&
              !ranged_attack.key_d,
          "ranged fire must emit plain F without a movement direction");

  const auto explicit_forward = ResolveMouseCombatKeyboardPlan(
      true, true, false, true, false, false, false);
  Require(explicit_forward.attack == MouseCombatAttack::kForward &&
              explicit_forward.key_w,
          "left click plus W must retain the normal forward attack");

  const auto left_attack = ResolveMouseCombatKeyboardPlan(
      true, true, false, false, false, true, false);
  Require(left_attack.attack == MouseCombatAttack::kLeft &&
              left_attack.modifier_down && left_attack.key_a &&
              !left_attack.key_w && !left_attack.key_d,
          "left click plus A must select the left attack");

  const auto right_attack = ResolveMouseCombatKeyboardPlan(
      true, true, false, false, false, false, true);
  Require(right_attack.attack == MouseCombatAttack::kRight &&
              right_attack.modifier_down && right_attack.key_d &&
              !right_attack.key_w && !right_attack.key_a,
          "left click plus D must select the right attack");

  const auto back_attack = ResolveMouseCombatKeyboardPlan(
      true, true, false, false, true, false, false);
  Require(back_attack.attack == MouseCombatAttack::kBack &&
              back_attack.modifier_down && back_attack.key_a &&
              back_attack.key_d && !back_attack.key_s,
          "left click plus S must select the original A+D turning attack");

  const auto original_back_chord = ResolveMouseCombatKeyboardPlan(
      true, true, false, false, false, true, true);
  Require(original_back_chord.attack == MouseCombatAttack::kBack &&
              original_back_chord.key_a && original_back_chord.key_d,
          "left click plus A+D must remain a turning attack");

  const auto immersive_strafe = ResolveMouseCombatKeyboardPlan(
      false, true, true, false, false, true, false);
  Require(immersive_strafe.submit_immersive_movement,
          "ordinary immersive strafe must retain vector locomotion");
  Require(immersive_strafe.rewrite_immersive_movement,
          "ordinary immersive strafe must consume the retail A/D state");
  Require(immersive_strafe.key_w,
          "pure immersive strafe needs the native root-motion driver");

  const auto immersive_forward = ResolveMouseCombatKeyboardPlan(
      false, true, true, true, false, false, false);
  Require(!immersive_forward.submit_immersive_movement &&
              !immersive_forward.rewrite_immersive_movement,
          "pure immersive forward movement must retain the exact native W path");

  const auto immersive_backward = ResolveMouseCombatKeyboardPlan(
      false, true, true, false, true, false, false);
  Require(!immersive_backward.submit_immersive_movement &&
              !immersive_backward.rewrite_immersive_movement,
          "pure immersive backward movement must retain the exact native S path");

  const auto immersive_diagonal = ResolveMouseCombatKeyboardPlan(
      false, true, true, true, false, false, true);
  Require(immersive_diagonal.submit_immersive_movement &&
              immersive_diagonal.rewrite_immersive_movement &&
              immersive_diagonal.key_w,
          "immersive diagonals must retain vector locomotion");

  const auto immersive_attack = ResolveMouseCombatKeyboardPlan(
      true, true, true, false, false, true, false);
  Require(!immersive_attack.submit_immersive_movement,
          "combat must suspend the competing immersive movement driver");
  Require(immersive_attack.attack == MouseCombatAttack::kLeft &&
              immersive_attack.key_a,
          "immersive combat must preserve the selected attack direction");

  const auto frontend_click = ResolveMouseCombatKeyboardPlan(
      true, false, false, false, false, false, false);
  Require(frontend_click.attack == MouseCombatAttack::kNone &&
              !frontend_click.modifier_down &&
              !frontend_click.rewrite_immersive_movement,
          "frontend clicks must never enter the gameplay combat router");

  const auto ordinary_third_person = ResolveMouseCombatKeyboardPlan(
      false, true, false, false, false, true, false);
  Require(ordinary_third_person.attack == MouseCombatAttack::kNone &&
              !ordinary_third_person.submit_immersive_movement &&
              !ordinary_third_person.rewrite_immersive_movement,
          "ordinary third-person movement must remain untouched");

  std::cout << "mouse combat routing tests passed\n";
  return 0;
}
