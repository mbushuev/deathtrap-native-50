#include <cstdlib>
#include <iostream>

#include "input_command_bindings.h"

namespace {

using deathtrap::input::Command;
using deathtrap::input::CommandBit;
using deathtrap::input::ResolveRetailKeyboardPlan;
using deathtrap::input::ResolveRootMenuEscapeAction;

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
                   !empty.escape && !empty.up && !empty.down && !empty.left &&
                   !empty.right && !empty.enter && !empty.menu_space,
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
      CommandBit(Command::kMenu) |
      CommandBit(Command::kMenuUp) |
      CommandBit(Command::kMenuDown) |
      CommandBit(Command::kMenuLeft) |
      CommandBit(Command::kMenuRight) |
      CommandBit(Command::kMenuConfirm) |
      CommandBit(Command::kMenuSpace);
  const auto all = ResolveRetailKeyboardPlan(complete);
  ok &= Expect(all.w && all.s && all.a && all.d && all.left_shift &&
                   all.space && all.e && all.q && all.j && all.k && all.tab &&
                   all.escape && all.up && all.down && all.left && all.right &&
                   all.enter && all.menu_space,
               "each fixed XInput command must reach its stable retail key");

  const auto isolated = ResolveRetailKeyboardPlan(
      CommandBit(Command::kOperate) | CommandBit(Command::kMenu));
  ok &= Expect(isolated.e && isolated.escape,
               "operate and menu commands must remain independent");
  ok &= Expect(!isolated.w && !isolated.space && !isolated.q &&
                   !isolated.tab && !isolated.up && !isolated.enter &&
                   !isolated.menu_space,
               "isolated commands must not leak to unrelated keys");

  const auto frontend = ResolveRetailKeyboardPlan(
      CommandBit(Command::kMenuUp) | CommandBit(Command::kMenuConfirm) |
      CommandBit(Command::kMenuSpace) | CommandBit(Command::kMenu));
  ok &= Expect(frontend.up && frontend.enter && frontend.menu_space &&
                   frontend.escape,
               "frontend controller commands must reach fixed retail keys");
  ok &= Expect(!frontend.w && !frontend.e && !frontend.tab,
               "frontend commands must not leak into gameplay actions");

  ok &= Expect(ResolveRootMenuEscapeAction(6u, true, true) == 5u,
               "root Escape must return to an active game");
  ok &= Expect(ResolveRootMenuEscapeAction(6u, true, false) == 0u,
               "root Escape must do nothing without an active game");
  ok &= Expect(ResolveRootMenuEscapeAction(6u, false, true) == 6u,
               "a mouse click on Quit must remain Quit");
  ok &= Expect(ResolveRootMenuEscapeAction(4u, true, true) == 4u,
               "non-Quit root actions must remain untouched");

  if (!ok) {
    return EXIT_FAILURE;
  }
  std::cout << "input command binding tests passed\n";
  return EXIT_SUCCESS;
}
