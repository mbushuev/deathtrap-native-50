#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

struct CameraSpringArmStep {
  double radius = 0.0;
  uint32_t clear_ticks = 0;
};

inline bool CameraInitialOverlapBlocks(double start_distance_squared,
                                       double probe_distance_squared) {
  if (!std::isfinite(start_distance_squared) ||
      !std::isfinite(probe_distance_squared)) {
    return true;
  }
  // Distance to a convex triangle is convex along a straight ray. If a short
  // forward probe is no closer than the start, the ray is depenetrating or
  // sliding tangentially and this triangle cannot become a new blocker later
  // on that ray. A small scale-aware tolerance prevents fixed-point noise
  // from trapping the camera against a surface it is leaving.
  const double tolerance =
      std::max(1.0e-4, start_distance_squared * 1.0e-6);
  return probe_distance_squared + tolerance < start_distance_squared;
}

inline CameraSpringArmStep StepCameraSpringArm(
    double desired_distance, double hard_safe_distance,
    double previous_radius, bool obstruction_present,
    uint32_t previous_clear_ticks) {
  CameraSpringArmStep result;
  if (!std::isfinite(desired_distance) || desired_distance <= 0.0) {
    return result;
  }
  const double safe = std::clamp(
      hard_safe_distance, 0.0, desired_distance);
  const double previous = std::clamp(
      previous_radius, 0.0, desired_distance);
  constexpr double kReleaseStep = 64.0;
  constexpr uint32_t kClearTicksBeforeRelease = 2u;

  if (safe + 0.5 < previous) {
    // Pull-in is hard and immediate. No submitted camera point may cross the
    // current complete-ray query.
    result.radius = safe;
    result.clear_ticks = 0;
  } else if (obstruction_present) {
    // A continuously observed boundary may move outward, but it cannot be
    // crossed and it cannot cause an extend/retract cycle in one source tick.
    result.radius = std::min(safe, previous + kReleaseStep);
    result.clear_ticks = 0;
  } else {
    // Tolerate one missing scene snapshot. The second complete clear query
    // begins a bounded spring return toward the desired endpoint.
    result.clear_ticks = std::min(previous_clear_ticks + 1u, 120u);
    result.radius = previous;
    if (result.clear_ticks >= kClearTicksBeforeRelease) {
      result.radius = std::min(desired_distance, previous + kReleaseStep);
    }
  }
  result.radius = std::clamp(result.radius, 0.0, safe);
  return result;
}
