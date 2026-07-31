#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

struct CameraSpringArmStep {
  double radius = 0.0;
  uint32_t clear_ticks = 0;
  uint32_t blocked_release_ticks = 0;
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

inline bool CameraMeshExtentsBlockVolume(
    const std::array<double, 3>& extents, double camera_diameter) {
  if (!std::isfinite(camera_diameter) || camera_diameter <= 0.0) {
    return false;
  }
  std::array<double, 3> sorted = extents;
  for (double extent : sorted) {
    if (!std::isfinite(extent) || extent < 0.0) {
      return false;
    }
  }
  std::sort(sorted.begin(), sorted.end());
  // A thin lever may be long on one intrinsic mesh axis, but rotating it must
  // not turn its world AABB into a camera wall. Require a blocker to span the
  // complete camera diameter on at least two intrinsic axes.
  return sorted[1] >= camera_diameter;
}

inline bool CameraMeshBoundsBlockVolume(double bounds_radius,
                                        double camera_diameter) {
  if (!std::isfinite(bounds_radius) || !std::isfinite(camera_diameter) ||
      bounds_radius < 0.0 || camera_diameter <= 0.0) {
    return false;
  }
  // Props smaller than one complete camera diameter in bounding-sphere radius
  // cannot form a wall-like occluder around the spring arm. Their enlarged
  // swept sphere otherwise fills the entire player-to-prop gap and traps the
  // camera against lever handles and compact housings.
  return bounds_radius >= camera_diameter;
}

inline CameraSpringArmStep StepCameraSpringArm(
    double desired_distance, double hard_safe_distance,
    double previous_radius, bool obstruction_present,
    uint32_t previous_clear_ticks,
    uint32_t previous_blocked_release_ticks) {
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
  constexpr uint32_t kBlockedTicksBeforeRelease = 3u;

  if (safe + 0.5 < previous) {
    // Pull-in is hard and immediate. No submitted camera point may cross the
    // current complete-ray query.
    result.radius = safe;
    result.clear_ticks = 0;
    result.blocked_release_ticks = 0;
  } else if (obstruction_present) {
    // A boolean native volume boundary can alternate between adjacent portal
    // or prop samples even while the camera and input are unchanged. Do not
    // follow a one-frame outward sample: require a short run of consistently
    // available space first. A genuinely retracting wall/block still releases
    // at the bounded rate after confirmation.
    if (safe > previous + 0.5) {
      result.blocked_release_ticks = std::min(
          previous_blocked_release_ticks + 1u, 120u);
    }
    result.radius = previous;
    if (result.blocked_release_ticks >= kBlockedTicksBeforeRelease) {
      result.radius = std::min(safe, previous + kReleaseStep);
    }
    result.clear_ticks = 0;
  } else {
    // Tolerate one missing scene snapshot. The second complete clear query
    // begins a bounded spring return toward the desired endpoint.
    result.clear_ticks = std::min(previous_clear_ticks + 1u, 120u);
    result.blocked_release_ticks = 0;
    result.radius = previous;
    if (result.clear_ticks >= kClearTicksBeforeRelease) {
      result.radius = std::min(desired_distance, previous + kReleaseStep);
    }
  }
  result.radius = std::clamp(result.radius, 0.0, safe);
  return result;
}
