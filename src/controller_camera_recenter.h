#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace deathtrap::camera {

struct RecenterTarget {
  double yaw = 0.0;
  double pitch = 0.0;
  bool valid = false;
};

// Dungeon's third-person orbit endpoint uses +sin(yaw)/+cos(yaw), whereas
// its head-view yaw has an additional half turn. Use the actor course
// directly: this is also the inverse of the verified third-person W/stick-up
// heading conversion. World Y grows upward. The retail pivot is at torso
// height; place the camera just above the head, not above the starting rig.
inline RecenterTarget BehindActorRecenterTarget(
    int32_t heading, int32_t units_per_turn, double radius,
    double head_rise_above_focus = 240.0) {
  RecenterTarget target;
  if (units_per_turn <= 0 || !std::isfinite(radius) || radius <= 0.0 ||
      !std::isfinite(head_rise_above_focus)) {
    return target;
  }
  heading %= units_per_turn;
  if (heading < 0) heading += units_per_turn;
  constexpr double kPi = 3.14159265358979323846;
  constexpr double kHeadClearance = 96.0;
  target.yaw = std::remainder(
      static_cast<double>(heading) * 2.0 * kPi / units_per_turn, 2.0 * kPi);
  target.pitch = std::asin(std::clamp(
      (std::max(0.0, head_rise_above_focus) + kHeadClearance) / radius,
      0.0, 0.99));
  target.valid = true;
  return target;
}

struct RecenterContext {
  bool gameplay = false;
  bool foreground = false;
  bool modern_third_person = false;
  bool orbit_engaged = false;
  bool selector = false;
  bool scripted = false;
  bool first_person = false;
  bool manual_look = false;

  bool Eligible() const {
    return gameplay && foreground && modern_third_person && orbit_engaged &&
        !selector && !scripted && !first_person && !manual_look;
  }
};

// Only orbit angles are changed. The ordinary spring arm, collision checks,
// source/presentation interpolation and actor heading remain their own owners.
class Recenter {
 public:
  bool Begin(const RecenterContext& context, double yaw, double pitch,
             double target_yaw, double target_pitch, double duration_seconds) {
    Cancel();
    if (!context.Eligible() || !std::isfinite(yaw) ||
        !std::isfinite(pitch) || !std::isfinite(target_yaw) ||
        !std::isfinite(target_pitch) || !std::isfinite(duration_seconds) ||
        duration_seconds <= 0.0) {
      return false;
    }
    start_yaw_ = yaw;
    start_pitch_ = pitch;
    yaw_delta_ = std::remainder(target_yaw - yaw, 2.0 * kPi);
    pitch_delta_ = target_pitch - pitch;
    duration_ = duration_seconds;
    active_ = std::abs(yaw_delta_) > 1.0e-8 ||
              std::abs(pitch_delta_) > 1.0e-8;
    return active_;
  }

  bool Advance(const RecenterContext& context, double elapsed_seconds,
               double* yaw, double* pitch) {
    if (!context.Eligible()) {
      Cancel();
      return false;
    }
    if (!active_ || !yaw || !pitch || !std::isfinite(elapsed_seconds) ||
        elapsed_seconds <= 0.0) {
      return false;
    }
    elapsed_ = std::min(duration_, elapsed_ + elapsed_seconds);
    const double t = elapsed_ / duration_;
    const double eased = t * t * (3.0 - 2.0 * t);
    *yaw = std::remainder(start_yaw_ + yaw_delta_ * eased, 2.0 * kPi);
    *pitch = start_pitch_ + pitch_delta_ * eased;
    if (elapsed_ >= duration_) active_ = false;
    return true;
  }

  void Cancel() { active_ = false; elapsed_ = 0.0; }
  bool Active() const { return active_; }

 private:
  static constexpr double kPi = 3.14159265358979323846;
  bool active_ = false;
  double start_yaw_ = 0.0;
  double start_pitch_ = 0.0;
  double yaw_delta_ = 0.0;
  double pitch_delta_ = 0.0;
  double elapsed_ = 0.0;
  double duration_ = 0.42;
};

}  // namespace deathtrap::camera
