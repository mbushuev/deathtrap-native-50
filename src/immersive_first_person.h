#pragma once

#include <array>
#include <cmath>
#include <cstdint>

struct ImmersiveFirstPersonPose {
  std::array<int32_t, 3> eye{};
  std::array<double, 3> forward{};
  bool valid = false;
};

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
