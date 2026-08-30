#include <cstdlib>
#include <iostream>

#include "input_command_bindings.h"

namespace {

using deathtrap::input::Command;
using deathtrap::input::CommandBit;
using deathtrap::input::ResolveRetailKeyboardPlan;

bool Expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    return false;
  }
  return true;
}

}  // namespace

int main() {
  bool ok = true;
  const auto empty = ResolveRetailKeyboardPlan(0);
  ok &= Expect(!empty.w && !empty.s && !empty.a && !empty.d &&
                   !empty.left_shift && !empty.space && !empty.e &&
                   !empty.q && !empty.j && !empty.k && !empty.tab &&
                   !empty.escape,
               "empty command mask must not press a retail key");

  const uint32_t complete =
      CommandBit(Command::kMoveForward) |
      CommandBit(Command::kMoveBackward) |
      CommandBit(Command::kMoveLeft) |
      CommandBit(Command::kMoveRight) |
      CommandBit(Command::kRun) |
      CommandBit(Command::kJumpClimb) |
      CommandBit(Command::kOperate) |
      CommandBit(Command::kCastSpell) |
      CommandBit(Command::kLeftSidestep) |
      CommandBit(Command::kRightSidestep) |
      CommandBit(Command::kRetailFirstPerson) |
      CommandBit(Command::kMenu);
  const auto all = ResolveRetailKeyboardPlan(complete);
  ok &= Expect(all.w && all.s && all.a && all.d && all.left_shift &&
                   all.space && all.e && all.q && all.j && all.k && all.tab &&
                   all.escape,
               "each fixed XInput command must reach its stable retail key");

  const auto isolated = ResolveRetailKeyboardPlan(
      CommandBit(Command::kOperate) | CommandBit(Command::kMenu));
  ok &= Expect(isolated.e && isolated.escape,
               "operate and menu commands must remain independent");
  ok &= Expect(!isolated.w && !isolated.space && !isolated.q &&
                   !isolated.tab,
               "isolated commands must not leak to unrelated keys");

  if (!ok) {
    return EXIT_FAILURE;
  }
  std::cout << "input command binding tests passed\n";
  return EXIT_SUCCESS;
}
