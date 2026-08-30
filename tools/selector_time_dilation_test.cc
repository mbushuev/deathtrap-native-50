#include <iostream>

#include "selector_time_dilation.h"

namespace {

bool Expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

}  // namespace

int main() {
  using deathtrap::selector_time::MakePlan;

  const auto quake_style = MakePlan(true, true, true, 16u, 3u, 25u);
  if (!Expect(quake_style.active, "open gameplay selector did not dilate") ||
      !Expect(quake_style.scheduler_rate == 4u,
              "25 percent of 16 Hz must be 4 Hz") ||
      !Expect(quake_style.presentation_passes == 12u,
              "quarter-speed x3 presentation must use twelve passes") ||
      !Expect(quake_style.source_period_ms == 250u,
              "Dungeon's 4 Hz scheduler period must be 250 ms")) {
    return 1;
  }

  const auto keyboard_fallback = MakePlan(true, true, true, 16u, 0u, 25u);
  if (!Expect(keyboard_fallback.presentation_passes == 12u,
              "slow selector must remain smooth when F11 interpolation is off")) {
    return 1;
  }

  for (const auto& inactive : {
           MakePlan(false, true, true, 16u, 3u, 25u),
           MakePlan(true, false, true, 16u, 3u, 25u),
           MakePlan(true, true, false, 16u, 3u, 25u),
           MakePlan(true, true, true, 16u, 3u, 100u),
       }) {
    if (!Expect(!inactive.active, "inactive state changed scheduler rate") ||
        !Expect(inactive.scheduler_rate == 16u,
                "inactive state did not preserve requested rate")) {
      return 1;
    }
  }

  const auto bounded = MakePlan(true, true, true, 60u, 3u, 1u);
  if (!Expect(bounded.scheduler_rate == 1u,
              "minimum scheduler rate was not bounded") ||
      !Expect(bounded.presentation_passes == 24u,
              "presentation pass cap was not applied")) {
    return 1;
  }

  deathtrap::selector_time::KeyboardLatch latch{};
  using deathtrap::selector_time::UpdateKeyboardLatch;
  if (!Expect(!UpdateKeyboardLatch(&latch, 1u, 0x01u, false).apply,
              "opening key must not immediately close its selector") ||
      !Expect(!UpdateKeyboardLatch(&latch, 1u, 0x00u, false).apply,
              "opening-key release should only arm the latch") ||
      !Expect(!UpdateKeyboardLatch(&latch, 1u, 0x01u, false).apply,
              "close command must wait for key release")) {
    return 1;
  }
  const auto close = UpdateKeyboardLatch(&latch, 1u, 0x00u, false);
  if (!Expect(close.apply && close.mode == 0u,
              "a short second press did not close the selector")) {
    return 1;
  }

  UpdateKeyboardLatch(&latch, 1u, 0x00u, false);
  UpdateKeyboardLatch(&latch, 1u, 0x04u, false);
  const auto switch_mode = UpdateKeyboardLatch(&latch, 1u, 0x00u, false);
  if (!Expect(switch_mode.apply && switch_mode.mode == 3u,
              "selector key did not switch directly to its category")) {
    return 1;
  }

  UpdateKeyboardLatch(&latch, 3u, 0x00u, true);
  if (!Expect(latch.observed_mode == 0u && !latch.armed,
              "controller ownership did not clear keyboard latch state")) {
    return 1;
  }

  std::cout << "selector time dilation tests passed\n";
  return 0;
}
