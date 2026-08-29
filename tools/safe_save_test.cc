#include <cstdlib>
#include <iostream>

#include "safe_save.h"

namespace {

void Require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "safe_save_test: " << message << '\n';
    std::exit(1);
  }
}

deathtrap_save::SafeSaveObservation StandingObservation(int32_t y = 1000) {
  deathtrap_save::SafeSaveObservation observation;
  observation.scene = 0x1000;
  observation.player = 0x2000;
  observation.vertical_position = y;
  observation.gameplay = true;
  observation.alive = true;
  observation.ground_state = true;
  observation.position_valid = true;
  return observation;
}

}  // namespace

int main() {
  using deathtrap_save::SafeSaveReason;
  using deathtrap_save::SafeSaveTracker;

  SafeSaveTracker tracker;
  auto observation = StandingObservation();
  Require(!tracker.Observe(observation).eligible,
          "the first grounded sample must not be enough");
  Require(!tracker.Observe(observation).eligible,
          "the second grounded sample must still stabilize");
  Require(tracker.Observe(observation).eligible,
          "three stable grounded samples must allow saving");

  observation.ground_state = false;
  const auto airborne = tracker.Observe(observation);
  Require(!airborne.eligible &&
              airborne.reason == SafeSaveReason::kNotGroundState,
          "an airborne movement state must revoke saving immediately");

  observation = StandingObservation();
  tracker.Observe(observation);
  tracker.Observe(observation);
  observation.vertical_position += SafeSaveTracker::kMaximumVerticalDelta + 1;
  const auto moving = tracker.Observe(observation);
  Require(!moving.eligible && moving.reason == SafeSaveReason::kVerticalMotion,
          "large vertical motion must restart stabilization");

  observation = StandingObservation();
  tracker.Observe(observation);
  tracker.Observe(observation);
  tracker.Observe(observation);
  observation.scene = 0x3000;
  const auto changed = tracker.Observe(observation);
  Require(!changed.eligible && changed.reason == SafeSaveReason::kSceneChanged,
          "a level or scene change must invalidate the old safe state");

  observation = StandingObservation();
  observation.scripted_view = true;
  const auto scripted = tracker.Observe(observation);
  Require(!scripted.eligible &&
              scripted.reason == SafeSaveReason::kScriptedView,
          "scripted presentation must reject saving");

  observation = StandingObservation();
  observation.alive = false;
  const auto dead = tracker.Observe(observation);
  Require(!dead.eligible && dead.reason == SafeSaveReason::kDead,
          "a dead player must reject saving");

  std::cout << "safe_save_test: ok\n";
  return 0;
}
