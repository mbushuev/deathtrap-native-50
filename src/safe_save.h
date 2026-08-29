#pragma once

#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <limits>

namespace deathtrap_save {

enum class SafeSaveReason {
  kEligible,
  kNoGameplay,
  kNoPlayer,
  kDead,
  kScriptedView,
  kNotGroundState,
  kSceneChanged,
  kVerticalMotion,
  kStabilizing,
};

struct SafeSaveObservation {
  uintptr_t scene = 0;
  uintptr_t player = 0;
  int32_t vertical_position = 0;
  bool gameplay = false;
  bool alive = false;
  bool scripted_view = false;
  bool ground_state = false;
  bool position_valid = false;
};

struct SafeSaveStatus {
  bool eligible = false;
  SafeSaveReason reason = SafeSaveReason::kNoGameplay;
  uint32_t stable_ticks = 0;
};

class SafeSaveTracker {
 public:
  static constexpr uint32_t kRequiredStableTicks = 3;
  static constexpr int32_t kMaximumVerticalDelta = 12;

  SafeSaveStatus Observe(const SafeSaveObservation& observation) {
    if (!observation.gameplay) {
      return Reset(SafeSaveReason::kNoGameplay);
    }
    if (!observation.scene || !observation.player ||
        !observation.position_valid) {
      return Reset(SafeSaveReason::kNoPlayer);
    }
    if (!observation.alive) {
      return Reset(SafeSaveReason::kDead);
    }
    if (observation.scripted_view) {
      return Reset(SafeSaveReason::kScriptedView);
    }
    if (!observation.ground_state) {
      return Reset(SafeSaveReason::kNotGroundState);
    }
    const bool same_owner = initialized_ &&
                            observation.scene == scene_ &&
                            observation.player == player_;
    if (!same_owner) {
      scene_ = observation.scene;
      player_ = observation.player;
      vertical_position_ = observation.vertical_position;
      initialized_ = true;
      stable_ticks_ = 1;
      status_ = {false, SafeSaveReason::kSceneChanged, stable_ticks_};
      return status_;
    }

    const int64_t vertical_delta = std::llabs(
        static_cast<int64_t>(observation.vertical_position) -
        vertical_position_);
    vertical_position_ = observation.vertical_position;
    if (vertical_delta > kMaximumVerticalDelta) {
      stable_ticks_ = 1;
      status_ = {false, SafeSaveReason::kVerticalMotion, stable_ticks_};
      return status_;
    }

    stable_ticks_ = std::min(
        kRequiredStableTicks,
        stable_ticks_ == std::numeric_limits<uint32_t>::max()
            ? stable_ticks_
            : stable_ticks_ + 1u);
    const bool eligible = stable_ticks_ >= kRequiredStableTicks;
    status_ = {eligible,
               eligible ? SafeSaveReason::kEligible
                        : SafeSaveReason::kStabilizing,
               stable_ticks_};
    return status_;
  }

  SafeSaveStatus status() const { return status_; }

  SafeSaveStatus Reset(SafeSaveReason reason) {
    initialized_ = false;
    scene_ = 0;
    player_ = 0;
    vertical_position_ = 0;
    stable_ticks_ = 0;
    status_ = {false, reason, 0};
    return status_;
  }

 private:
  uintptr_t scene_ = 0;
  uintptr_t player_ = 0;
  int32_t vertical_position_ = 0;
  uint32_t stable_ticks_ = 0;
  bool initialized_ = false;
  SafeSaveStatus status_{};
};

inline const char* SafeSaveReasonName(SafeSaveReason reason) {
  switch (reason) {
    case SafeSaveReason::kEligible:
      return "eligible";
    case SafeSaveReason::kNoGameplay:
      return "no_gameplay";
    case SafeSaveReason::kNoPlayer:
      return "no_player";
    case SafeSaveReason::kDead:
      return "dead";
    case SafeSaveReason::kScriptedView:
      return "scripted_view";
    case SafeSaveReason::kNotGroundState:
      return "not_ground_state";
    case SafeSaveReason::kSceneChanged:
      return "scene_changed";
    case SafeSaveReason::kVerticalMotion:
      return "vertical_motion";
    case SafeSaveReason::kStabilizing:
      return "stabilizing";
  }
  return "unknown";
}

}  // namespace deathtrap_save
