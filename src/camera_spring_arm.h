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
  double blocked_candidate_distance = 0.0;
  uint64_t blocker_key = 0;
};

inline bool CameraPreHistoryMeshVetoMayApply(
    bool modern_configure_scope_active, bool controller_matches,
    bool position_history_ring_matches, bool scene_mesh_contact,
    bool replacement_valid) {
  // The native ring-adder is shared by five unrelated camera histories.
  // Pre-publication replacement is legal only for the position ring belonging
  // to the controller in the one explicit modern-camera configure call.
  return modern_configure_scope_active && controller_matches &&
         position_history_ring_matches && scene_mesh_contact &&
         replacement_valid;
}

inline bool CameraPreHistoryUsesSubmittedFixedPoint(
    bool candidate_replacement_valid, bool submitted_validation_valid,
    bool submitted_unchanged) {
  // Treat the modern configure call as a transaction. If retail's speculative
  // position-ring candidate cannot be corrected, rollback is legal only when
  // repeat validation returns the originally submitted endpoint byte-for-byte.
  // Accepting a merely usable submitted-side correction would introduce a
  // second collision owner and recreate the old four-tick loop.
  return !candidate_replacement_valid && submitted_validation_valid &&
         submitted_unchanged;
}

inline bool CameraPostNativeMeshContactIsIdempotent(
    bool mesh_contact, double correction_distance) {
  // The scene query works in floating point but the native controller stores
  // integer positions. A correction within the validator's two-unit equality
  // tolerance cannot materially improve camera safety. Re-committing that
  // same boundary would only rewind the retail position ring and reset the
  // spring-arm release counters on every source tick.
  return mesh_contact && std::isfinite(correction_distance) &&
         correction_distance <= 2.0;
}

inline bool CameraModernEndpointMayOwnFinalPosition(
    bool configured, bool mesh_contact, bool exact_already_committed,
    bool collision_solver_exhausted, bool full_radius_request,
    bool strict_native_query_available, bool strict_native_volume_clear,
    bool scene_endpoint_clear) {
  // The modern endpoint may bypass the legacy fixed-camera positional
  // resolver only on a completely unconstrained source tick. In particular,
  // the ordinary 0x30910 predicate is insufficient here because it accepts
  // the camera volume when any one of its seven room traces succeeds. Exact
  // positional ownership requires the complete focus and endpoint footprint
  // plus the scene endpoint check; every ambiguous or colliding case remains
  // native-owned.
  return configured && !mesh_contact && !exact_already_committed &&
         !collision_solver_exhausted && full_radius_request &&
         strict_native_query_available && strict_native_volume_clear &&
         scene_endpoint_clear;
}

inline bool CameraPreNativeSceneContactMayOwnFinalPosition(
    bool desired_scene_contact, bool configured_scene_contact,
    bool exact_already_committed, bool collision_solver_exhausted,
    bool minimum_distance_valid, bool strict_native_query_available,
    bool strict_native_volume_clear, bool scene_endpoint_clear) {
  // A scene-mesh spring-arm contraction is already the modern collision
  // solution. The legacy fixed-camera resolver must not reshape that safe
  // endpoint and create a second radius/height owner. Publication is still
  // fail-closed: no conflicting post-native contact, prior exact owner or
  // ambiguous room/endpoint result may be bypassed.
  return desired_scene_contact && !configured_scene_contact &&
         !exact_already_committed && !collision_solver_exhausted &&
         minimum_distance_valid && strict_native_query_available &&
         strict_native_volume_clear && scene_endpoint_clear;
}

struct CameraExpandedBoxRayExit {
  bool valid = false;
  double distance = 0.0;
  size_t axis = std::numeric_limits<size_t>::max();
};

struct CameraPivotRelativeInterpolation {
  std::array<double, 3> focus{};
  std::array<double, 3> position{};
  double yaw = 0.0;
  double pitch = 0.0;
  double radius = 0.0;
  bool valid = false;
};

struct CameraFloorLimit {
  int32_t minimum_y = 0;
  bool player_root_used = false;
};

struct CameraChaseStep {
  std::array<double, 3> position{};
  std::array<double, 3> velocity{};
};

struct CameraRelativeHeadingTarget {
  int32_t heading = 0;
  bool valid = false;
};

struct CameraRelativeHeadingStep {
  int32_t heading_error = 0;
  int32_t heading_delta = 0;
};

struct CameraAvoidanceLatchStep {
  uint32_t direct_clear_ticks = 0;
  bool retain_previous = false;
  bool release_to_direct = false;
};

struct CameraNearPivotModeStep {
  uint32_t direct_clear_ticks = 0;
  bool active = false;
  bool changed = false;
};

struct CameraOrbitStickInput {
  double x = 0.0;
  double y = 0.0;
};

inline CameraOrbitStickInput CameraOrbitModeInput(
    double x, double y, bool custom_head_view, bool invert_x,
    bool invert_y, double third_person_speed_scale) {
  CameraOrbitStickInput result;
  if (!std::isfinite(x) || !std::isfinite(y) ||
      !std::isfinite(third_person_speed_scale) ||
      third_person_speed_scale <= 0.0) {
    return result;
  }

  // The accepted immersive head view already has the expected XInput
  // convention. Dungeon's trailing mode-3 orbit consumes pitch with the
  // opposite sign at its endpoint boundary, even though horizontal orbit has
  // the same handedness. Keep that distinction local to controller input;
  // physical mouse input has its own verified publication convention.
  const double speed = custom_head_view ? 1.0 : third_person_speed_scale;
  const double horizontal_sign = invert_x ? -1.0 : 1.0;
  const double configured_vertical_sign = invert_y ? -1.0 : 1.0;
  const double vertical_sign = custom_head_view
      ? configured_vertical_sign
      : -configured_vertical_sign;
  result.x = x * horizontal_sign * speed;
  result.y = y * vertical_sign * speed;
  return result;
}

inline CameraOrbitStickInput CameraOrbitAxisLock(
    double x, double y, double lock_ratio) {
  CameraOrbitStickInput result;
  if (!std::isfinite(x) || !std::isfinite(y) ||
      !std::isfinite(lock_ratio)) {
    return result;
  }
  lock_ratio = std::clamp(lock_ratio, 0.0, 0.95);
  result = {x, y};
  if (lock_ratio <= 0.0) {
    return result;
  }

  const double absolute_x = std::abs(x);
  const double absolute_y = std::abs(y);
  // XInput sticks rarely sit on a mathematically exact axis. A large primary
  // deflection can therefore accumulate unintended pitch/yaw for as long as
  // the player holds the stick. Suppress the smaller component inside a
  // proportional axial cone, then restore it continuously toward a true
  // diagonal. The dominant component and full diagonals are unchanged.
  const auto reshape_secondary = [lock_ratio](double secondary,
                                               double primary_absolute) {
    const double secondary_absolute = std::abs(secondary);
    const double threshold = lock_ratio * primary_absolute;
    if (secondary_absolute <= threshold) {
      return 0.0;
    }
    const double denominator = primary_absolute - threshold;
    if (denominator <= 0.000001) {
      return secondary;
    }
    const double restored = primary_absolute *
        (secondary_absolute - threshold) / denominator;
    return std::copysign(restored, secondary);
  };
  if (absolute_x > absolute_y) {
    result.y = reshape_secondary(y, absolute_x);
  } else if (absolute_y > absolute_x) {
    result.x = reshape_secondary(x, absolute_y);
  }
  return result;
}

inline CameraRelativeHeadingTarget CameraRelativeHeadingFromOrbit(
    double camera_yaw, double stick_x, double stick_y,
    int32_t heading_units_per_turn, bool invert_y) {
  CameraRelativeHeadingTarget result;
  if (heading_units_per_turn <= 0 || !std::isfinite(camera_yaw) ||
      !std::isfinite(stick_x) || !std::isfinite(stick_y)) {
    return result;
  }
  const double forward_x = -std::sin(camera_yaw);
  const double forward_z = -std::cos(camera_yaw);
  const double right_x = -forward_z;
  const double right_z = forward_x;
  const double movement_y = invert_y ? -stick_y : stick_y;
  const double desired_x = forward_x * movement_y + right_x * stick_x;
  const double desired_z = forward_z * movement_y + right_z * stick_x;
  if (std::hypot(desired_x, desired_z) <= 0.000001) {
    return result;
  }
  int32_t heading = static_cast<int32_t>(std::lround(
      std::atan2(desired_x, desired_z) *
      static_cast<double>(heading_units_per_turn) /
      (2.0 * 3.14159265358979323846)));
  heading %= heading_units_per_turn;
  if (heading < 0) {
    heading += heading_units_per_turn;
  }
  result.heading = heading;
  result.valid = true;
  return result;
}

inline CameraAvoidanceLatchStep StepCameraAvoidanceLatch(
    bool previous_avoidance_valid, double direct_safe_distance,
    double previous_safe_distance, double useful_distance,
    double minimum_retained_distance, uint32_t direct_clear_ticks,
    uint32_t release_ticks) {
  CameraAvoidanceLatchStep result;
  if (!previous_avoidance_valid || release_ticks == 0u ||
      !std::isfinite(direct_safe_distance) ||
      !std::isfinite(previous_safe_distance) ||
      !std::isfinite(useful_distance) || useful_distance < 0.0 ||
      !std::isfinite(minimum_retained_distance) ||
      minimum_retained_distance < 0.0 ||
      previous_safe_distance < minimum_retained_distance) {
    return result;
  }

  if (direct_safe_distance < useful_distance) {
    result.retain_previous = true;
    return result;
  }
  result.direct_clear_ticks = std::min(
      release_ticks, direct_clear_ticks +
          static_cast<uint32_t>(direct_clear_ticks < release_ticks));
  if (result.direct_clear_ticks < release_ticks) {
    result.retain_previous = true;
  } else {
    result.release_to_direct = true;
  }
  return result;
}

inline CameraNearPivotModeStep StepCameraNearPivotMode(
    bool previously_active, double direct_safe_distance,
    uint32_t previous_direct_clear_ticks, double enter_distance,
    double exit_distance, uint32_t exit_clear_ticks) {
  CameraNearPivotModeStep result;
  if (!std::isfinite(direct_safe_distance) ||
      !std::isfinite(enter_distance) || enter_distance < 0.0 ||
      !std::isfinite(exit_distance) || exit_distance < enter_distance ||
      exit_clear_ticks == 0u) {
    result.active = previously_active;
    return result;
  }
  if (!previously_active) {
    result.active = direct_safe_distance < enter_distance;
    result.changed = result.active;
    return result;
  }
  result.active = true;
  if (direct_safe_distance >= exit_distance) {
    result.direct_clear_ticks = std::min(
        exit_clear_ticks, previous_direct_clear_ticks +
            static_cast<uint32_t>(
                previous_direct_clear_ticks < exit_clear_ticks));
    if (result.direct_clear_ticks >= exit_clear_ticks) {
      result.active = false;
      result.changed = true;
    }
  }
  return result;
}

inline CameraRelativeHeadingStep StepCameraRelativeHeading(
    int32_t target_heading, int32_t current_heading,
    int32_t heading_units_per_turn, int32_t maximum_step) {
  CameraRelativeHeadingStep result;
  if (heading_units_per_turn <= 0 || maximum_step <= 0) {
    return result;
  }
  const auto normalize = [heading_units_per_turn](int32_t value) {
    int32_t normalized = value % heading_units_per_turn;
    if (normalized < 0) {
      normalized += heading_units_per_turn;
    }
    return normalized;
  };
  int32_t error = normalize(target_heading) - normalize(current_heading);
  const int32_t half_turn = heading_units_per_turn / 2;
  if (error > half_turn) {
    error -= heading_units_per_turn;
  } else if (error < -half_turn) {
    error += heading_units_per_turn;
  }
  result.heading_error = error;
  result.heading_delta = std::clamp(error, -maximum_step, maximum_step);
  return result;
}

inline CameraChaseStep StepCameraChase(
    const std::array<double, 3>& current,
    const std::array<double, 3>& velocity,
    const std::array<double, 3>& target,
    double delta_seconds, double response_hz,
    double maximum_speed, double maximum_acceleration) {
  CameraChaseStep result{current, velocity};
  if (!std::isfinite(delta_seconds) || delta_seconds <= 0.0 ||
      delta_seconds > 0.25 || !std::isfinite(response_hz) ||
      response_hz <= 0.0 || !std::isfinite(maximum_speed) ||
      maximum_speed <= 0.0 || !std::isfinite(maximum_acceleration) ||
      maximum_acceleration <= 0.0) {
    return result;
  }

  // Smooth the followed pivot, never the collision result. Backward Euler is
  // used for the critically damped system because the native camera advances
  // only once every 60 ms. The previous semi-implicit Euler step has a
  // negative discrete pole at the configured 2 Hz response and alternates
  // around even a fixed target. This implicit form is unconditionally stable
  // and keeps each axis independent, so a horizontal turn cannot consume the
  // vertical acceleration or velocity budget.
  const double omega = 2.0 * 3.14159265358979323846 * response_hz;
  const double omega_squared = omega * omega;
  const double denominator =
      1.0 + 2.0 * omega * delta_seconds +
      omega_squared * delta_seconds * delta_seconds;
  const double maximum_velocity_delta =
      maximum_acceleration * delta_seconds;
  if (!std::isfinite(denominator) || denominator <= 0.0 ||
      !std::isfinite(maximum_velocity_delta)) {
    return result;
  }

  for (size_t axis = 0; axis < 3; ++axis) {
    if (!std::isfinite(current[axis]) || !std::isfinite(velocity[axis]) ||
        !std::isfinite(target[axis])) {
      return CameraChaseStep{target, {0.0, 0.0, 0.0}};
    }

    const double error = target[axis] - current[axis];
    double next_velocity =
        (velocity[axis] +
         omega_squared * delta_seconds * error) /
        denominator;
    next_velocity = std::clamp(
        next_velocity, velocity[axis] - maximum_velocity_delta,
        velocity[axis] + maximum_velocity_delta);
    next_velocity =
        std::clamp(next_velocity, -maximum_speed, maximum_speed);

    // A sampled target may reverse abruptly. Carrying the preceding velocity
    // through the new target direction makes the visual pivot move away for a
    // tick and later snap back. Stop at the reversal boundary; the next stable
    // step accelerates toward the new target.
    if (error == 0.0 || next_velocity * error <= 0.0) {
      result.position[axis] = target[axis];
      result.velocity[axis] = 0.0;
      continue;
    }

    const double displacement = next_velocity * delta_seconds;
    if (std::abs(displacement) >= std::abs(error)) {
      result.position[axis] = target[axis];
      result.velocity[axis] = 0.0;
    } else {
      result.position[axis] = current[axis] + displacement;
      result.velocity[axis] = next_velocity;
    }
  }
  return result;
}

inline CameraChaseStep StepCameraPivotChase(
    const std::array<double, 3>& current,
    const std::array<double, 3>& velocity,
    const std::array<double, 3>& target,
    double delta_seconds, double response_hz,
    double maximum_speed, double maximum_acceleration) {
  CameraChaseStep result = StepCameraChase(
      current, velocity, target, delta_seconds, response_hz,
      maximum_speed, maximum_acceleration);
  if (!std::isfinite(target[1])) {
    return result;
  }

  // Horizontal damping gives the follow camera weight. Vertical damping is
  // unsafe during fast falls: the orbit pivot trails the real focus, inflates
  // the spring arm and eventually forces a reverse native-camera correction.
  // Exact source ticks follow Y directly; x2/x3 presentation still
  // interpolates between consecutive exact pivots.
  result.position[1] = target[1];
  result.velocity[1] = 0.0;
  return result;
}

inline bool CameraPreNativeEscapeOwnsFinalTarget(
    bool pre_native_mesh_contact, bool overlap_pushout,
    bool near_pivot_escape, bool submitted_usable,
    bool submitted_native_clear, bool post_native_result_consistent) {
  // A contained-pivot OBB exit is already rebuilt from the current focus,
  // requested ray and current object transform. Once its native room-volume
  // validation succeeds, retail history must not replace it with an older
  // radial point and create an escape/radial A/B cycle. A post-native sweep
  // of that same submitted point remains newer evidence and can explicitly
  // disprove the escape before it becomes authoritative.
  return pre_native_mesh_contact &&
         (overlap_pushout || near_pivot_escape) && submitted_usable &&
         submitted_native_clear && post_native_result_consistent;
}

inline bool CameraConfiguredMeshResultAllowsPreNativeEscape(
    bool configured_mesh_contact, bool idempotent_contact,
    bool configured_correction_applied,
    bool accepted_target_matches_escape) {
  // The scoped pre-history hook and the post-configure sweep both inspect
  // geometry generated by the current configure call. If either one selects
  // a different target, the exceptional pre-native escape is not safe with
  // respect to the complete current constraint set. A delayed retail
  // publication is not permission to overwrite that positive contact.
  return !configured_mesh_contact || accepted_target_matches_escape ||
         (idempotent_contact && !configured_correction_applied);
}

inline bool PreserveCameraEscapePitch(
    const std::array<double, 3>& point,
    const std::array<double, 3>& requested, size_t vertical_axis,
    std::array<double, 3>* pushed) {
  if (!pushed || vertical_axis >= point.size()) {
    return false;
  }
  double requested_horizontal_squared = 0.0;
  double pushed_horizontal_squared = 0.0;
  for (size_t axis = 0; axis < point.size(); ++axis) {
    if (!std::isfinite(point[axis]) || !std::isfinite(requested[axis]) ||
        !std::isfinite((*pushed)[axis])) {
      return false;
    }
    if (axis == vertical_axis) {
      continue;
    }
    const double requested_delta = requested[axis] - point[axis];
    const double pushed_delta = (*pushed)[axis] - point[axis];
    requested_horizontal_squared += requested_delta * requested_delta;
    pushed_horizontal_squared += pushed_delta * pushed_delta;
  }
  constexpr double kHorizontalEpsilon = 1.0e-9;
  if (requested_horizontal_squared <= kHorizontalEpsilon) {
    (*pushed)[vertical_axis] = point[vertical_axis];
    return true;
  }
  const double progress = std::clamp(
      std::sqrt(pushed_horizontal_squared / requested_horizontal_squared),
      0.0, 1.0);
  if (!std::isfinite(progress)) {
    return false;
  }
  // The OBB solver chooses only a horizontal supporting face. Reapply the
  // current orbit's vertical progress at the same horizontal progress so a
  // collision cannot flatten a pitched orbit to focus height in one tick.
  (*pushed)[vertical_axis] =
      point[vertical_axis] +
      (requested[vertical_axis] - point[vertical_axis]) * progress;
  return std::isfinite((*pushed)[vertical_axis]);
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
    const std::array<double, 3>& extents,
    double minimum_two_axis_span) {
  if (!std::isfinite(minimum_two_axis_span) ||
      minimum_two_axis_span <= 0.0) {
    return false;
  }
  std::array<double, 3> sorted = extents;
  for (double extent : sorted) {
    if (!std::isfinite(extent) || extent < 0.0) {
      return false;
    }
  }
  std::sort(sorted.begin(), sorted.end());
  // A thin lever, flag or narrow housing may be long on one intrinsic mesh
  // axis, but rotating it must not turn its world AABB into a camera wall.
  // The caller supplies a conservative transverse-size threshold; a blocker
  // must meet it on at least two intrinsic axes.
  return sorted[1] >= minimum_two_axis_span;
}

inline bool PushCameraOutOfExpandedBox(
    const std::array<double, 3>& point,
    const std::array<double, 3>& reference,
    const std::array<double, 3>& half_extents,
    size_t excluded_axis, double margin,
    std::array<double, 3>* pushed, size_t* pushed_axis = nullptr,
    size_t preferred_axis = std::numeric_limits<size_t>::max(),
    double preferred_axis_hysteresis = 0.0) {
  if (!pushed || excluded_axis >= point.size() ||
      !std::isfinite(margin) || margin < 0.0 ||
      !std::isfinite(preferred_axis_hysteresis) ||
      preferred_axis_hysteresis < 0.0) {
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

  // A moving OBB or a near-corner ray can make two horizontal faces differ
  // by only a few world units. Picking the numerical minimum every source
  // tick then alternates the camera between faces. Retain the preceding axis
  // only while it is still valid and no alternative improves the escape by
  // more than the explicit world-space hysteresis. The point itself is always
  // rebuilt from the current pivot and current bounds; no absolute camera
  // position is retained.
  if (preferred_axis < point.size() && preferred_axis != excluded_axis) {
    const double preferred_face =
        half_extents[preferred_axis] - std::abs(point[preferred_axis]);
    if (preferred_face >= 0.0 &&
        preferred_face <= nearest_face + preferred_axis_hysteresis) {
      nearest_axis = preferred_axis;
    }
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

inline bool PushCameraAlongExpandedBoxSupportingFace(
    const std::array<double, 3>& point,
    const std::array<double, 3>& requested,
    const std::array<double, 3>& half_extents,
    size_t excluded_axis, double margin, double minimum_distance,
    std::array<double, 3>* pushed, size_t* pushed_axis = nullptr) {
  if (!pushed || excluded_axis >= point.size() ||
      !std::isfinite(margin) || margin < 0.0 ||
      !std::isfinite(minimum_distance) || minimum_distance <= 0.0) {
    return false;
  }

  size_t outside_count = 0u;
  size_t support_axis = point.size();
  size_t tangent_axis = point.size();
  for (size_t axis = 0; axis < point.size(); ++axis) {
    if (!std::isfinite(point[axis]) || !std::isfinite(requested[axis]) ||
        !std::isfinite(half_extents[axis]) || half_extents[axis] <= 0.0) {
      return false;
    }
    if (axis == excluded_axis) {
      continue;
    }
    if (std::abs(point[axis]) >= half_extents[axis]) {
      ++outside_count;
      support_axis = axis;
    } else {
      tangent_axis = axis;
    }
  }
  if (outside_count != 1u || support_axis >= point.size() ||
      tangent_axis >= point.size()) {
    return false;
  }

  // Keep the complete route on the already safe side of the expanded box,
  // but derive tangential progress from the current orbit request.  The old
  // fallback changed only one discrete face coordinate and returned the same
  // world point while yaw continued to move.  Near zero tangent, move farther
  // out along a continuous minimum-radius arc instead of selecting another
  // face.  No previous camera endpoint participates in this construction.
  constexpr double kDirectionEpsilon = 1.0e-6;
  const double support_sign = point[support_axis] < 0.0 ? -1.0 : 1.0;
  const double tangent_delta =
      requested[tangent_axis] - point[tangent_axis];
  const double face_delta = std::max(
      0.0, half_extents[support_axis] + margin -
               std::abs(point[support_axis]));
  const double minimum_support_delta = std::sqrt(std::max(
      0.0, minimum_distance * minimum_distance -
               tangent_delta * tangent_delta));
  const double support_delta = std::max(face_delta, minimum_support_delta);
  if (!std::isfinite(tangent_delta) || !std::isfinite(support_delta) ||
      (std::abs(tangent_delta) <= kDirectionEpsilon &&
       support_delta <= kDirectionEpsilon)) {
    return false;
  }

  *pushed = point;
  (*pushed)[support_axis] += support_sign * support_delta;
  (*pushed)[tangent_axis] = requested[tangent_axis];
  if (pushed_axis) {
    *pushed_axis = support_axis;
  }
  return true;
}

inline bool PushCameraToUsableExpandedBoxRayExit(
    const std::array<double, 3>& point,
    const std::array<double, 3>& requested,
    const std::array<double, 3>& half_extents,
    size_t excluded_axis, double margin, double minimum_distance,
    std::array<double, 3>* pushed, size_t* pushed_axis = nullptr,
    size_t preferred_axis = std::numeric_limits<size_t>::max(),
    double preferred_axis_hysteresis = 0.0) {
  if (!pushed || excluded_axis >= point.size() ||
      !std::isfinite(margin) || margin < 0.0 ||
      !std::isfinite(minimum_distance) || minimum_distance <= 0.0 ||
      !std::isfinite(preferred_axis_hysteresis) ||
      preferred_axis_hysteresis < 0.0) {
    return false;
  }

  std::array<double, 3> direction{};
  double horizontal_length_squared = 0.0;
  for (size_t axis = 0; axis < point.size(); ++axis) {
    if (!std::isfinite(point[axis]) || !std::isfinite(requested[axis]) ||
        !std::isfinite(half_extents[axis]) || half_extents[axis] <= 0.0) {
      return false;
    }
    direction[axis] = requested[axis] - point[axis];
    if (axis == excluded_axis) {
      continue;
    }
    // This operation is only for a pivot contained by the horizontal
    // expanded OBB. A pivot already outside needs the existing supporting-
    // face slide so its route cannot cross back through the box.
    if (std::abs(point[axis]) >= half_extents[axis]) {
      return false;
    }
    horizontal_length_squared += direction[axis] * direction[axis];
  }
  const double horizontal_length = std::sqrt(horizontal_length_squared);
  constexpr double kDirectionEpsilon = 1.0e-6;
  if (!std::isfinite(horizontal_length) ||
      horizontal_length <= kDirectionEpsilon) {
    return false;
  }

  std::array<double, 3> exit_scales{};
  exit_scales.fill(std::numeric_limits<double>::infinity());
  double exit_scale = std::numeric_limits<double>::infinity();
  size_t exit_axis = point.size();
  for (size_t axis = 0; axis < point.size(); ++axis) {
    if (axis == excluded_axis ||
        std::abs(direction[axis]) <= kDirectionEpsilon) {
      continue;
    }
    const double face =
        direction[axis] < 0.0
            ? -(half_extents[axis] + margin)
            : half_extents[axis] + margin;
    const double scale = (face - point[axis]) / direction[axis];
    exit_scales[axis] = scale;
    if (scale > 0.0 && scale < exit_scale) {
      exit_scale = scale;
      exit_axis = axis;
    }
  }
  if (exit_axis >= point.size() || !std::isfinite(exit_scale)) {
    return false;
  }


  if (preferred_axis < point.size() && preferred_axis != excluded_axis) {
    const double preferred_scale = exit_scales[preferred_axis];
    const double extra_distance =
        (preferred_scale - exit_scale) * horizontal_length;
    // A scale above one would retain a face beyond the requested endpoint,
    // which feels like a stuck camera. A negative scale means the ray now
    // points away from the old face. Both cases release immediately.
    if (preferred_scale > 0.0 && preferred_scale <= 1.0 &&
        std::isfinite(extra_distance) &&
        extra_distance <= preferred_axis_hysteresis) {
      exit_scale = preferred_scale;
      exit_axis = preferred_axis;
    }
  }

  // Continue outward on the same requested ray when the box exit alone is
  // too close to the player. Unlike choosing a face-normal point, this maps
  // orbit angle continuously around the OBB perimeter and cannot become a
  // fixed contact anchor.
  const double usable_scale = std::max(
      exit_scale, minimum_distance / horizontal_length);
  *pushed = point;
  for (size_t axis = 0; axis < point.size(); ++axis) {
    if (axis != excluded_axis) {
      (*pushed)[axis] = point[axis] + direction[axis] * usable_scale;
    }
  }
  if (pushed_axis) {
    *pushed_axis = exit_axis;
  }
  return true;
}

inline CameraExpandedBoxRayExit FindContainedExpandedBoxRayExit(
    const std::array<double, 3>& point,
    const std::array<double, 3>& direction,
    const std::array<double, 3>& half_extents,
    double margin, double maximum_distance) {
  CameraExpandedBoxRayExit result;
  if (!std::isfinite(margin) || margin < 0.0 ||
      !std::isfinite(maximum_distance) || maximum_distance <= 0.0) {
    return result;
  }

  constexpr double kDirectionEpsilon = 1.0e-9;
  constexpr double kBoundaryTolerance = 1.0;
  double exit_distance = std::numeric_limits<double>::infinity();
  size_t exit_axis = point.size();
  for (size_t axis = 0; axis < point.size(); ++axis) {
    if (!std::isfinite(point[axis]) || !std::isfinite(direction[axis]) ||
        !std::isfinite(half_extents[axis]) || half_extents[axis] <= 0.0) {
      return result;
    }
    if (std::abs(point[axis]) >
        half_extents[axis] + kBoundaryTolerance) {
      return result;
    }
    if (std::abs(direction[axis]) <= kDirectionEpsilon) {
      continue;
    }
    const double face = direction[axis] < 0.0
                            ? -(half_extents[axis] + margin)
                            : half_extents[axis] + margin;
    const double candidate = (face - point[axis]) / direction[axis];
    if (candidate > 0.0 && candidate < exit_distance) {
      exit_distance = candidate;
      exit_axis = axis;
    }
  }
  if (exit_axis >= point.size() || !std::isfinite(exit_distance) ||
      exit_distance > maximum_distance + kBoundaryTolerance) {
    return result;
  }
  result.valid = true;
  result.distance = std::min(exit_distance, maximum_distance);
  result.axis = exit_axis;
  return result;
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
    uint32_t previous_blocked_release_ticks,
    double previous_blocked_candidate_distance = 0.0,
    uint64_t previous_blocker_key = 0,
    uint64_t current_blocker_key = 0,
    uint32_t clear_ticks_before_release = 4u,
    uint32_t blocked_ticks_before_release = 3u,
    double release_step = 64.0,
    bool require_monotonic_blocked_candidate = true) {
  CameraSpringArmStep result;
  if (!std::isfinite(desired_distance) || desired_distance <= 0.0) {
    return result;
  }
  const double safe = std::clamp(
      hard_safe_distance, 0.0, desired_distance);
  const double previous = std::clamp(
      previous_radius, 0.0, desired_distance);
  constexpr double kBlockedCandidateRegressionTolerance = 2.0;
  clear_ticks_before_release = std::max(clear_ticks_before_release, 1u);
  blocked_ticks_before_release = std::max(blocked_ticks_before_release, 1u);
  if (!std::isfinite(release_step) || release_step <= 0.0) {
    release_step = 64.0;
  }

  if (safe + 0.5 < previous) {
    // Pull-in is hard and immediate. No submitted camera point may cross the
    // current complete-ray query.
    result.radius = safe;
    result.clear_ticks = 0;
    result.blocked_release_ticks = 0;
    result.blocked_candidate_distance = safe;
    result.blocker_key = obstruction_present ? current_blocker_key : 0;
  } else if (obstruction_present) {
    // A boolean native volume boundary can alternate between adjacent portal
    // or prop samples even while the camera and input are unchanged. Do not
    // follow a one-frame outward sample: require a short run of consistently
    // available space first. A genuinely retracting wall/block still releases
    // at the bounded rate after confirmation.
    const bool same_blocker = current_blocker_key != 0 &&
                              current_blocker_key == previous_blocker_key;
    const bool outward_space = safe > previous + 0.5;
    const bool candidate_monotonic =
        !require_monotonic_blocked_candidate ||
        previous_blocked_release_ticks == 0u ||
        safe + kBlockedCandidateRegressionTolerance >=
            previous_blocked_candidate_distance;
    if (outward_space) {
      result.blocked_release_ticks =
          same_blocker && candidate_monotonic
              ? std::min(previous_blocked_release_ticks + 1u, 120u)
              : 1u;
    }
    result.radius = previous;
    if (result.blocked_release_ticks >= blocked_ticks_before_release) {
      result.radius = std::min(safe, previous + release_step);
    }
    result.clear_ticks = 0;
    result.blocked_candidate_distance = safe;
    result.blocker_key = current_blocker_key;
  } else {
    // A missing scene snapshot or a flickering native portal must not count as
    // proof that the previous contact has gone away. Four complete clear
    // source ticks are required before bounded recovery begins. The previous
    // blocker identity remains pending until the arm is fully restored.
    result.clear_ticks = std::min(previous_clear_ticks + 1u, 120u);
    result.blocked_release_ticks = 0;
    result.radius = previous;
    if (result.clear_ticks >= clear_ticks_before_release) {
      result.radius = std::min(desired_distance, previous + release_step);
    }
    result.blocked_candidate_distance =
        previous_blocked_candidate_distance;
    result.blocker_key = previous_blocker_key;
  }
  result.radius = std::clamp(result.radius, 0.0, safe);
  if (!obstruction_present &&
      result.radius + 0.5 >= desired_distance) {
    result.blocked_candidate_distance = desired_distance;
    result.blocker_key = 0;
  }
  return result;
}

inline CameraPivotRelativeInterpolation InterpolateCameraPivotRelative(
    const std::array<double, 3>& previous_focus,
    const std::array<double, 3>& previous_position,
    const std::array<double, 3>& current_focus,
    const std::array<double, 3>& current_position, double phase) {
  CameraPivotRelativeInterpolation result;
  if (!std::isfinite(phase)) {
    return result;
  }
  phase = std::clamp(phase, 0.0, 1.0);
  std::array<double, 3> previous_offset{};
  std::array<double, 3> current_offset{};
  for (size_t axis = 0; axis < 3u; ++axis) {
    if (!std::isfinite(previous_focus[axis]) ||
        !std::isfinite(previous_position[axis]) ||
        !std::isfinite(current_focus[axis]) ||
        !std::isfinite(current_position[axis])) {
      return result;
    }
    result.focus[axis] = previous_focus[axis] +
                         (current_focus[axis] - previous_focus[axis]) * phase;
    previous_offset[axis] = previous_position[axis] - previous_focus[axis];
    current_offset[axis] = current_position[axis] - current_focus[axis];
  }
  const double previous_horizontal =
      std::hypot(previous_offset[0], previous_offset[2]);
  const double current_horizontal =
      std::hypot(current_offset[0], current_offset[2]);
  const double previous_radius =
      std::hypot(previous_horizontal, previous_offset[1]);
  const double current_radius =
      std::hypot(current_horizontal, current_offset[1]);
  if (!std::isfinite(previous_radius) || !std::isfinite(current_radius) ||
      previous_radius <= 0.0 || current_radius <= 0.0) {
    return result;
  }
  constexpr double kPi = 3.14159265358979323846;
  constexpr double kTwoPi = 2.0 * kPi;
  const double previous_yaw =
      std::atan2(previous_offset[0], previous_offset[2]);
  const double current_yaw =
      std::atan2(current_offset[0], current_offset[2]);
  const double yaw_delta = std::remainder(
      current_yaw - previous_yaw, kTwoPi);
  const double previous_pitch =
      std::atan2(previous_offset[1], previous_horizontal);
  const double current_pitch =
      std::atan2(current_offset[1], current_horizontal);
  result.yaw = previous_yaw + yaw_delta * phase;
  result.pitch = previous_pitch +
                 (current_pitch - previous_pitch) * phase;
  result.radius = previous_radius +
                  (current_radius - previous_radius) * phase;
  const double horizontal = std::cos(result.pitch) * result.radius;
  result.position = {
      result.focus[0] + std::sin(result.yaw) * horizontal,
      result.focus[1] + std::sin(result.pitch) * result.radius,
      result.focus[2] + std::cos(result.yaw) * horizontal};
  result.valid = std::isfinite(result.position[0]) &&
                 std::isfinite(result.position[1]) &&
                 std::isfinite(result.position[2]);
  return result;
}
