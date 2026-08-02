#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

struct ImmersiveFirstPersonPose {
  std::array<int32_t, 3> eye{};
  std::array<double, 3> forward{};
  bool valid = false;
};

struct ImmersiveLocomotionPlan {
  int32_t motion_heading = 0;
  int32_t transaction_heading = 0;
  int32_t native_axis_milli = 0;
  bool active = false;
};

struct ImmersiveRootMotionInput {
  int32_t local_x = 0;
  int32_t local_z = 0;
  bool active = false;
};

// Convert Dungeon's actor-visible course to the radial yaw shared by the
// third-person orbit.  The camera sits behind the actor, so radial yaw is one
// half-turn opposite the visible heading.
inline double ImmersiveOrbitYawFromPlayerHeading(
    int32_t player_heading, int32_t heading_units_per_turn) {
  if (heading_units_per_turn <= 0) {
    return std::numeric_limits<double>::quiet_NaN();
  }
  int32_t heading = player_heading % heading_units_per_turn;
  if (heading < 0) {
    heading += heading_units_per_turn;
  }
  constexpr double kPi = 3.14159265358979323846;
  const double body_yaw = static_cast<double>(heading) * 2.0 * kPi /
      static_cast<double>(heading_units_per_turn);
  return std::remainder(body_yaw - kPi, 2.0 * kPi);
}

// Head view and third-person orbit share one stored angle but consume it from
// opposite sides of the focus. Move the third-person camera behind the final
// head-view direction when leaving immersive mode.
inline double ThirdPersonOrbitYawFromImmersiveYaw(double immersive_yaw) {
  constexpr double kPi = 3.14159265358979323846;
  return std::isfinite(immersive_yaw)
             ? std::remainder(immersive_yaw + kPi, 2.0 * kPi)
             : std::numeric_limits<double>::quiet_NaN();
}

// Dungeon's J/K side-step states exclude forward/back locomotion and therefore
// cannot express diagonals. Immersive view instead keeps the retail W/S
// animation and root-motion transaction and records the complete requested
// world course for the root-motion input boundary below.
inline ImmersiveLocomotionPlan BuildImmersiveLocomotionPlan(
    int32_t desired_motion_heading, bool native_backward, double magnitude,
    int32_t heading_units_per_turn) {
  ImmersiveLocomotionPlan plan;
  if (heading_units_per_turn <= 0 ||
      !std::isfinite(magnitude) ||
      magnitude <= 0.000001 || magnitude > 1.000001) {
    return plan;
  }
  int32_t heading = desired_motion_heading % heading_units_per_turn;
  if (heading < 0) {
    heading += heading_units_per_turn;
  }
  plan.motion_heading = heading;
  // The native W/S collision transaction consumes actor course separately
  // from the later cached-basis animation-root writer. S travels opposite the
  // actor course, while motion_heading always remains the requested world
  // direction used by the root adapter.
  plan.transaction_heading = native_backward
      ? (heading + heading_units_per_turn / 2) % heading_units_per_turn
      : heading;
  plan.native_axis_milli = static_cast<int32_t>(std::lround(
      (native_backward ? -magnitude : magnitude) * 1000.0));
  plan.active = plan.native_axis_milli != 0;
  return plan;
}

// The animation root writer at Dungeon.dll+0x32430 does not consult the Q10
// heading. It transforms local root motion through the cached horizontal
// matrix columns at node +0xD0/+0xD8 and +0xE8/+0xF0. Convert the existing
// local delta to world space, keep its exact horizontal magnitude, choose the
// requested world course, then solve the same 2x2 matrix back to local input.
// The original writer remains responsible for applying the result.
inline ImmersiveRootMotionInput BuildImmersiveRootMotionInput(
    int32_t local_x, int32_t local_z, int32_t matrix_xx,
    int32_t matrix_zx, int32_t matrix_xz, int32_t matrix_zz,
    int32_t desired_motion_heading, int32_t heading_units_per_turn,
    double matrix_scale = 16384.0) {
  ImmersiveRootMotionInput result;
  if (heading_units_per_turn <= 0 || !std::isfinite(matrix_scale) ||
      matrix_scale <= 0.0) {
    return result;
  }
  const double a = static_cast<double>(matrix_xx) / matrix_scale;
  const double b = static_cast<double>(matrix_xz) / matrix_scale;
  const double c = static_cast<double>(matrix_zx) / matrix_scale;
  const double d = static_cast<double>(matrix_zz) / matrix_scale;
  const double original_world_x = a * local_x + b * local_z;
  const double original_world_z = c * local_x + d * local_z;
  const double world_magnitude =
      std::hypot(original_world_x, original_world_z);
  const double determinant = a * d - b * c;
  if (!std::isfinite(world_magnitude) || world_magnitude <= 0.000001 ||
      !std::isfinite(determinant) || std::abs(determinant) <= 0.000001) {
    return result;
  }
  int32_t heading = desired_motion_heading % heading_units_per_turn;
  if (heading < 0) {
    heading += heading_units_per_turn;
  }
  const double radians = static_cast<double>(heading) *
      2.0 * 3.14159265358979323846 /
      static_cast<double>(heading_units_per_turn);
  const double desired_world_x = std::sin(radians) * world_magnitude;
  const double desired_world_z = std::cos(radians) * world_magnitude;
  const double solved_x =
      (desired_world_x * d - b * desired_world_z) / determinant;
  const double solved_z =
      (a * desired_world_z - desired_world_x * c) / determinant;
  if (!std::isfinite(solved_x) || !std::isfinite(solved_z) ||
      solved_x < static_cast<double>(std::numeric_limits<int32_t>::min()) ||
      solved_x > static_cast<double>(std::numeric_limits<int32_t>::max()) ||
      solved_z < static_cast<double>(std::numeric_limits<int32_t>::min()) ||
      solved_z > static_cast<double>(std::numeric_limits<int32_t>::max())) {
    return result;
  }
  result.local_x = static_cast<int32_t>(std::lround(solved_x));
  result.local_z = static_cast<int32_t>(std::lround(solved_z));
  result.active = result.local_x != local_x || result.local_z != local_z;
  return result;
}

// Both playable characters share the central chest/neck/head structure, but
// only Red Lotus has a braid descending from the head node. Local head
// translation is deliberately absent from this contract: live action frames
// prove it changes with animation. Stable bounds and ancestry distinguish the
// head from hands, weapons and decorative meshes for both characters.
inline bool MatchesImmersiveHeadJoint(
    uint32_t child_count, uintptr_t parent_resource_handle,
    uint32_t grandparent_child_count, int32_t bounds_radius, uint32_t depth) {
  return child_count <= 1u && parent_resource_handle == 0u &&
      grandparent_child_count >= 3u && bounds_radius >= 45 &&
      bounds_radius <= 115 && depth >= 4u && depth <= 7u;
}

// Embedded projectiles and other temporary attachments become ordinary scene
// children of the struck body joint.  They do not belong to the animated
// skeleton: unlike a real joint child, their scene-node flags differ from the
// parent.  Exclude them before applying the stable head-topology predicate so
// an arrow lodged in the head cannot turn first person into third person.
inline bool CountsTowardImmersiveJointTopology(uint32_t parent_flags,
                                                uint32_t child_flags) {
  return parent_flags == child_flags;
}

// Physical DirectInput mouse Y needs the opposite sign at Dungeon's
// head-mounted look-at boundary. InvertY deliberately reverses that default.
inline double ImmersivePhysicalMouseVerticalSign(bool invert_y) {
  return invert_y ? 1.0 : -1.0;
}

// Dungeon's camera angle builder consumes the opposite of the visible view
// direction. Keep that engine convention at the publication boundary instead
// of rotating the persistent orbit/body course.
inline std::array<double, 3> ImmersiveFirstPersonLookAtVector(
    const std::array<double, 3>& visible_forward) {
  return {-visible_forward[0], -visible_forward[1], -visible_forward[2]};
}

inline ImmersiveFirstPersonPose BuildImmersiveFirstPersonPose(
    const std::array<int32_t, 3>& player_root, double yaw, double pitch,
    int32_t height, int32_t forward_offset) {
  ImmersiveFirstPersonPose pose;
  if (!std::isfinite(yaw) || !std::isfinite(pitch) || height < 0 ||
      forward_offset < 0) {
    return pose;
  }
  const double horizontal = std::cos(pitch);
  const double forward_x = -std::sin(yaw);
  const double forward_z = -std::cos(yaw);
  pose.eye = {
      player_root[0] + static_cast<int32_t>(std::lround(
          forward_x * static_cast<double>(forward_offset))),
      player_root[1] + height,
      player_root[2] + static_cast<int32_t>(std::lround(
          forward_z * static_cast<double>(forward_offset)))};
  pose.forward = {forward_x * horizontal, -std::sin(pitch),
                  forward_z * horizontal};
  pose.valid = std::isfinite(pose.forward[0]) &&
      std::isfinite(pose.forward[1]) && std::isfinite(pose.forward[2]);
  return pose;
}

// A visible-body head mount uses the animated head centre as its pivot. The
// engine-facing look vector is the direction actually published to Dungeon's
// look-at builder, so advancing the eye along it puts the near plane in front
// of the face instead of behind the skull. Rotation remains user-owned: only
// the skeletal translation is inherited, avoiding animation-driven head bob
// and roll.
inline ImmersiveFirstPersonPose BuildImmersiveHeadMountedPose(
    const std::array<int32_t, 3>& head_center, double yaw, double pitch,
    int32_t upward_offset, int32_t forward_offset) {
  ImmersiveFirstPersonPose pose = BuildImmersiveFirstPersonPose(
      head_center, yaw, pitch, 0, 0);
  if (!pose.valid || upward_offset < 0 || forward_offset < 0) {
    pose.valid = false;
    return pose;
  }
  const std::array<double, 3> camera_forward =
      ImmersiveFirstPersonLookAtVector(pose.forward);
  const double horizontal_length =
      std::hypot(camera_forward[0], camera_forward[2]);
  // Configured pitch is bounded to +/-75 degrees, so its horizontal
  // projection remains non-zero. Reject only a genuinely degenerate vector;
  // the former 0.5 threshold incorrectly disabled the head view beyond
  // roughly +/-60 degrees.
  if (!std::isfinite(horizontal_length) || horizontal_length < 1.0e-6) {
    pose.valid = false;
    return pose;
  }
  pose.eye = {
      head_center[0] + static_cast<int32_t>(std::lround(
          camera_forward[0] / horizontal_length * forward_offset)),
      head_center[1] + upward_offset,
      head_center[2] + static_cast<int32_t>(std::lround(
          camera_forward[2] / horizontal_length * forward_offset))};
  return pose;
}
