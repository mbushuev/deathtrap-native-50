#pragma once

#include <array>
#include <cmath>
#include <cstdint>

struct ImmersiveFirstPersonPose {
  std::array<int32_t, 3> eye{};
  std::array<double, 3> forward{};
  bool valid = false;
};

// Both playable characters share the central chest/neck/head structure, but
// only Red Lotus has a braid descending from the head node. Their measured
// head-local translations are also distinct: Red Lotus is 0/71/34 and
// Chaindog is -1/100/21. Consequently the head is allowed to be either a leaf
// or to have the one verified braid child, and the geometry envelope covers
// both observed profiles. The remaining ancestry checks keep hands, weapons
// and decorative meshes out of the candidate set.
inline bool MatchesImmersiveHeadJoint(
    uint32_t child_count, uintptr_t parent_resource_handle,
    uint32_t grandparent_child_count, const std::array<int32_t, 3>& local,
    int32_t bounds_radius, uint32_t depth) {
  return child_count <= 1u && parent_resource_handle == 0u &&
      grandparent_child_count >= 3u && std::abs(local[0]) <= 8 &&
      local[1] >= 55 && local[1] <= 105 && local[2] >= 15 &&
      local[2] <= 55 && bounds_radius >= 45 && bounds_radius <= 115 &&
      depth >= 4u && depth <= 7u;
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
