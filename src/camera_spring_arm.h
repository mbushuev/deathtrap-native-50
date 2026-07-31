#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <limits>

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

inline bool PushCameraOutOfExpandedBox(
    const std::array<double, 3>& point,
    const std::array<double, 3>& reference,
    const std::array<double, 3>& half_extents,
    size_t excluded_axis, double margin,
    std::array<double, 3>* pushed, size_t* pushed_axis = nullptr) {
  if (!pushed || excluded_axis >= point.size() ||
      !std::isfinite(margin) || margin < 0.0) {
    return false;
  }
  for (size_t axis = 0; axis < point.size(); ++axis) {
    if (!std::isfinite(point[axis]) || !std::isfinite(reference[axis]) ||
        !std::isfinite(half_extents[axis]) || half_extents[axis] <= 0.0 ||
        std::abs(point[axis]) >= half_extents[axis]) {
      return false;
    }
  }

  size_t nearest_axis = point.size();
  double nearest_face = std::numeric_limits<double>::infinity();
  for (size_t axis = 0; axis < point.size(); ++axis) {
    if (axis == excluded_axis) {
      continue;
    }
    const double face_distance =
        half_extents[axis] - std::abs(point[axis]);
    if (face_distance < nearest_face) {
      nearest_face = face_distance;
      nearest_axis = axis;
    }
  }
  if (nearest_axis >= point.size()) {
    return false;
  }

  double sign = point[nearest_axis] < 0.0 ? -1.0 : 1.0;
  constexpr double kSideEpsilon = 1.0e-6;
  if (std::abs(point[nearest_axis]) <= kSideEpsilon &&
      std::abs(reference[nearest_axis]) > kSideEpsilon) {
    sign = reference[nearest_axis] < 0.0 ? -1.0 : 1.0;
  }
  *pushed = point;
  (*pushed)[nearest_axis] =
      sign * (half_extents[nearest_axis] + margin);
  if (pushed_axis) {
    *pushed_axis = nearest_axis;
  }
  return true;
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
