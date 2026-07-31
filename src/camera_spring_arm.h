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

struct CameraMeshPresentationLatchClearStep {
  uint32_t clear_ticks = 0;
  bool release = false;
};

struct CameraFloorLimit {
  int32_t minimum_y = 0;
  bool player_root_used = false;
};

inline bool CameraMeshLatchRetainsPreviousTarget(
    bool same_camera, bool owner_changed, bool incoming_usable,
    bool manual_orbit_owned) {
  // Stable contact normally keeps the preceding face across adjacent mesh
  // nodes. During a manual orbit, however, a usable incoming target is the
  // user's current side of the obstruction; retaining the old face makes the
  // latch jump back as soon as input grace expires.
  return same_camera && (owner_changed || !incoming_usable) &&
         !(manual_orbit_owned && incoming_usable);
}

inline bool CameraContinuousMeshContactNeedsCommit(
    bool pre_native_mesh_contact, bool post_native_mesh_contact,
    bool presentation_latch_active, bool submitted_usable,
    bool submitted_endpoint_clear) {
  // Once a qualified mesh contact owns the spring arm, pin the already
  // validated submitted endpoint on a tick where the native history happens
  // to publish a clear intermediate. Otherwise the next history sample can
  // re-enter the same mesh and create a clear/contact two-cycle.
  return pre_native_mesh_contact && !post_native_mesh_contact &&
         presentation_latch_active && submitted_usable &&
         submitted_endpoint_clear;
}

inline CameraFloorLimit ResolveCameraFloorLimit(
    const std::array<int32_t, 3>& focus,
    const std::array<int32_t, 3>& player_root,
    bool player_root_valid) {
  constexpr int64_t kMaximumDropBelowFocus = 240;
  constexpr int64_t kPlayerRootClearance = 96;
  const auto saturate = [](int64_t value) {
    return static_cast<int32_t>(std::clamp<int64_t>(
        value, std::numeric_limits<int32_t>::min(),
        std::numeric_limits<int32_t>::max()));
  };

  CameraFloorLimit result;
  result.minimum_y =
      saturate(static_cast<int64_t>(focus[1]) - kMaximumDropBelowFocus);
  const auto axis_delta = [](int32_t a, int32_t b) {
    return std::abs(static_cast<int64_t>(a) -
                    static_cast<int64_t>(b));
  };
  const bool player_matches_focus =
      player_root_valid &&
      axis_delta(player_root[0], focus[0]) <= 256 &&
      axis_delta(player_root[1], focus[1]) <= 800 &&
      axis_delta(player_root[2], focus[2]) <= 256;
  if (player_matches_focus) {
    result.minimum_y = std::max(
        result.minimum_y,
        saturate(static_cast<int64_t>(player_root[1]) +
                 kPlayerRootClearance));
    result.player_root_used = true;
  }
  return result;
}

inline CameraMeshPresentationLatchClearStep
StepCameraMeshPresentationLatchClear(
    uint32_t previous_clear_ticks,
    bool native_candidate_mesh_blocked) {
  CameraMeshPresentationLatchClearStep result;
  if (native_candidate_mesh_blocked) {
    return result;
  }
  result.clear_ticks =
      std::min(previous_clear_ticks + 1u, 120u);
  result.release = result.clear_ticks >= 2u;
  return result;
}

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

inline bool PushCameraToUsableExpandedBoxFace(
    const std::array<double, 3>& point,
    const std::array<double, 3>& reference,
    const std::array<double, 3>& half_extents,
    size_t excluded_axis, double margin, double minimum_distance,
    std::array<double, 3>* pushed, size_t* pushed_axis = nullptr) {
  if (!pushed || excluded_axis >= point.size() ||
      !std::isfinite(margin) || margin < 0.0 ||
      !std::isfinite(minimum_distance) || minimum_distance <= 0.0) {
    return false;
  }
  for (size_t axis = 0; axis < point.size(); ++axis) {
    if (!std::isfinite(point[axis]) || !std::isfinite(reference[axis]) ||
        !std::isfinite(half_extents[axis]) || half_extents[axis] <= 0.0) {
      return false;
    }
  }

  // If the pivot is just outside one expanded face, keep that coordinate
  // fixed and slide to the other horizontal face. The complete path then
  // remains outside the conservative OBB instead of crossing through it.
  size_t outside_axes = 0;
  size_t sole_outside_axis = point.size();
  for (size_t axis = 0; axis < point.size(); ++axis) {
    if (axis != excluded_axis &&
        std::abs(point[axis]) >= half_extents[axis]) {
      ++outside_axes;
      sole_outside_axis = axis;
    }
  }

  std::array<double, 3> reference_direction{};
  double reference_length_squared = 0.0;
  for (size_t axis = 0; axis < point.size(); ++axis) {
    reference_direction[axis] = reference[axis] - point[axis];
    reference_length_squared +=
        reference_direction[axis] * reference_direction[axis];
  }
  const double reference_length =
      std::sqrt(reference_length_squared);

  bool found = false;
  double best_alignment = -std::numeric_limits<double>::infinity();
  double best_distance = std::numeric_limits<double>::infinity();
  std::array<double, 3> best = point;
  size_t best_axis = point.size();
  constexpr double kComparisonEpsilon = 1.0e-6;
  for (size_t axis = 0; axis < point.size(); ++axis) {
    if (axis == excluded_axis ||
        (outside_axes == 1u && axis == sole_outside_axis)) {
      continue;
    }
    for (double sign : {-1.0, 1.0}) {
      std::array<double, 3> candidate = point;
      candidate[axis] =
          sign * (half_extents[axis] + margin);
      const double delta = candidate[axis] - point[axis];
      const double distance = std::abs(delta);
      if (distance + kComparisonEpsilon < minimum_distance) {
        continue;
      }
      const double alignment =
          reference_length > kComparisonEpsilon
              ? delta * reference_direction[axis] /
                    (distance * reference_length)
              : 0.0;
      if (!found ||
          alignment > best_alignment + kComparisonEpsilon ||
          (std::abs(alignment - best_alignment) <= kComparisonEpsilon &&
           distance < best_distance)) {
        found = true;
        best_alignment = alignment;
        best_distance = distance;
        best = candidate;
        best_axis = axis;
      }
    }
  }
  if (!found) {
    return false;
  }
  *pushed = best;
  if (pushed_axis) {
    *pushed_axis = best_axis;
  }
  return true;
}

inline std::array<int32_t, 3> SelectCameraMeshPresentationTarget(
    bool post_native_mesh_contact,
    const std::array<int32_t, 3>& submitted,
    const std::array<int32_t, 3>& final_published) {
  // A pre-configure mesh hit proves the submitted spring-arm point safe, but
  // the native position-history ring may still publish an older point. Only a
  // positive post-native mesh correction proves final_published safe.
  return post_native_mesh_contact ? final_published : submitted;
}

inline std::array<int32_t, 3> TranslateCameraTargetWithFocus(
    const std::array<int32_t, 3>& previous_focus,
    const std::array<int32_t, 3>& current_focus,
    const std::array<int32_t, 3>& target) {
  std::array<int32_t, 3> translated{};
  for (size_t axis = 0; axis < translated.size(); ++axis) {
    const int64_t value =
        static_cast<int64_t>(target[axis]) +
        static_cast<int64_t>(current_focus[axis]) -
        static_cast<int64_t>(previous_focus[axis]);
    translated[axis] = static_cast<int32_t>(std::clamp<int64_t>(
        value, std::numeric_limits<int32_t>::min(),
        std::numeric_limits<int32_t>::max()));
  }
  return translated;
}

inline std::array<int32_t, 3> StepCameraPresentationFollow(
    const std::array<int32_t, 3>& current,
    const std::array<int32_t, 3>& target,
    double response, double horizontal_maximum_step,
    double vertical_maximum_step) {
  if (!std::isfinite(response) || response <= 0.0 || response > 1.0 ||
      !std::isfinite(horizontal_maximum_step) ||
      horizontal_maximum_step <= 0.0 ||
      !std::isfinite(vertical_maximum_step) ||
      vertical_maximum_step <= 0.0) {
    return current;
  }

  std::array<int32_t, 3> next{};
  for (size_t axis = 0; axis < next.size(); ++axis) {
    const int64_t delta =
        static_cast<int64_t>(target[axis]) - current[axis];
    if (std::abs(delta) <= 1) {
      next[axis] = target[axis];
      continue;
    }
    const double maximum_step =
        axis == 1u ? vertical_maximum_step : horizontal_maximum_step;
    double step = std::clamp(
        static_cast<double>(delta) * response,
        -maximum_step, maximum_step);
    if (std::abs(step) < 1.0) {
      step = delta < 0 ? -1.0 : 1.0;
    }
    const int64_t value =
        static_cast<int64_t>(current[axis]) +
        static_cast<int64_t>(std::llround(step));
    next[axis] = static_cast<int32_t>(std::clamp<int64_t>(
        value, std::numeric_limits<int32_t>::min(),
        std::numeric_limits<int32_t>::max()));
  }
  return next;
}

inline bool CameraPresentationFollowInputIdle(
    uint64_t now_ms, uint64_t last_input_ms, uint64_t grace_ms) {
  if (!last_input_ms || !grace_ms || now_ms < last_input_ms) {
    return false;
  }
  return now_ms - last_input_ms >= grace_ms;
}

inline bool CameraTargetMeetsMinimumDistance(
    const std::array<int32_t, 3>& focus,
    const std::array<int32_t, 3>& target,
    double minimum_distance) {
  if (!std::isfinite(minimum_distance) || minimum_distance <= 0.0) {
    return false;
  }
  const double dx = static_cast<double>(target[0]) - focus[0];
  const double dy = static_cast<double>(target[1]) - focus[1];
  const double dz = static_cast<double>(target[2]) - focus[2];
  const double distance = std::hypot(std::hypot(dx, dz), dy);
  return std::isfinite(distance) &&
         distance + 0.5 >= minimum_distance;
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
