#include "camera_spring_arm.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

void ExpectNear(double actual, double expected, const char* label) {
  if (std::abs(actual - expected) > 0.001) {
    std::cerr << label << ": expected " << expected << ", got " << actual
              << '\n';
    std::exit(1);
  }
}

void ExpectTicks(uint32_t actual, uint32_t expected, const char* label) {
  if (actual != expected) {
    std::cerr << label << ": expected " << expected << ", got " << actual
              << '\n';
    std::exit(1);
  }
}

}  // namespace

int main() {
  const CameraOrbitStickInput horizontal_axis =
      CameraOrbitAxisLock(-1.0, -0.136, 0.25);
  ExpectNear(horizontal_axis.x, -1.0, "axis lock horizontal primary");
  ExpectNear(horizontal_axis.y, 0.0, "axis lock horizontal leakage");
  const CameraOrbitStickInput vertical_axis =
      CameraOrbitAxisLock(0.10, -0.80, 0.25);
  ExpectNear(vertical_axis.x, 0.0, "axis lock vertical leakage");
  ExpectNear(vertical_axis.y, -0.80, "axis lock vertical primary");
  const CameraOrbitStickInput diagonal =
      CameraOrbitAxisLock(0.75, 0.75, 0.25);
  ExpectNear(diagonal.x, 0.75, "axis lock diagonal x");
  ExpectNear(diagonal.y, 0.75, "axis lock diagonal y");
  const CameraOrbitStickInput transition =
      CameraOrbitAxisLock(1.0, 0.625, 0.25);
  ExpectNear(transition.x, 1.0, "axis lock transition primary");
  ExpectNear(transition.y, 0.5, "axis lock transition secondary");

  const CameraRelativeHeadingTarget camera_behind =
      CameraRelativeHeadingFromOrbit(0.0, 0.0, 1.0, 1024, false);
  if (!camera_behind.valid || camera_behind.heading != 512) {
    std::cerr << "stick-up did not select the course away from the camera\n";
    return 1;
  }
  const CameraRelativeHeadingTarget screen_right =
      CameraRelativeHeadingFromOrbit(0.0, 1.0, 0.0, 1024, false);
  if (!screen_right.valid || screen_right.heading != 256) {
    std::cerr << "stick-right did not select screen-right world heading\n";
    return 1;
  }
  const CameraRelativeHeadingTarget screen_left =
      CameraRelativeHeadingFromOrbit(0.0, -1.0, 0.0, 1024, false);
  if (!screen_left.valid || screen_left.heading != 768) {
    std::cerr << "stick-left did not select screen-left world heading\n";
    return 1;
  }
  const CameraRelativeHeadingTarget stick_down =
      CameraRelativeHeadingFromOrbit(0.0, 0.0, -1.0, 1024, false);
  if (!stick_down.valid || stick_down.heading != 0) {
    std::cerr << "stick-down did not select the course toward the camera\n";
    return 1;
  }
  const CameraRelativeHeadingTarget quarter_orbit_up =
      CameraRelativeHeadingFromOrbit(
          3.14159265358979323846 / 2.0, 0.0, 1.0, 1024, false);
  if (!quarter_orbit_up.valid || quarter_orbit_up.heading != 768) {
    std::cerr << "camera rotation did not rotate the movement basis\n";
    return 1;
  }
  const CameraRelativeHeadingTarget inverted_up =
      CameraRelativeHeadingFromOrbit(0.0, 0.0, 1.0, 1024, true);
  if (!inverted_up.valid || inverted_up.heading != 0) {
    std::cerr << "camera-relative Y inversion was not isolated\n";
    return 1;
  }
  if (CameraRelativeHeadingFromOrbit(
          0.0, 0.0, 0.0, 1024, false).valid) {
    std::cerr << "zero movement stick produced a heading\n";
    return 1;
  }
  bool published_yaw_valid = false;
  const double published_yaw = CameraOrbitYawFromPositions(
      {100, 200, 300}, {1100, 900, 300}, &published_yaw_valid);
  if (!published_yaw_valid) {
    std::cerr << "published camera heading was not usable\n";
    return 1;
  }
  ExpectNear(published_yaw, 3.14159265358979323846 / 2.0,
             "published camera heading");
  CameraOrbitYawFromPositions(
      {100, 200, 300}, {100, 900, 300}, &published_yaw_valid);
  if (published_yaw_valid) {
    std::cerr << "vertical camera arm invented a horizontal heading\n";
    return 1;
  }

  uint32_t direct_clear_ticks = 0;
  for (uint32_t tick = 0; tick < 7u; ++tick) {
    const CameraAvoidanceLatchStep latch = StepCameraAvoidanceLatch(
        true, 900.0, 700.0, 650.0, 288.0, direct_clear_ticks, 8u);
    direct_clear_ticks = latch.direct_clear_ticks;
    if (!latch.retain_previous || latch.release_to_direct) {
      std::cerr << "avoidance shot released before sustained direct clearance\n";
      return 1;
    }
  }
  const CameraAvoidanceLatchStep released = StepCameraAvoidanceLatch(
      true, 900.0, 700.0, 650.0, 288.0, direct_clear_ticks, 8u);
  if (released.retain_previous || !released.release_to_direct ||
      released.direct_clear_ticks != 8u) {
    std::cerr << "avoidance shot did not release after sustained clearance\n";
    return 1;
  }
  const CameraAvoidanceLatchStep blocked_again = StepCameraAvoidanceLatch(
      true, 400.0, 700.0, 650.0, 288.0, 7u, 8u);
  if (!blocked_again.retain_previous ||
      blocked_again.direct_clear_ticks != 0u) {
    std::cerr << "interrupted direct clearance was not reset\n";
    return 1;
  }
  const CameraAvoidanceLatchStep collapsed_side = StepCameraAvoidanceLatch(
      true, 100.0, 287.0, 650.0, 288.0, 0u, 8u);
  if (collapsed_side.retain_previous ||
      collapsed_side.release_to_direct) {
    std::cerr << "collapsed avoidance side remained latched\n";
    return 1;
  }
  const CameraRelativeHeadingStep aligned_steering =
      StepCameraRelativeHeading(0, 0, 1024, 34);
  if (aligned_steering.heading_error != 0 ||
      aligned_steering.heading_delta != 0) {
    std::cerr << "aligned camera-relative heading changed\n";
    return 1;
  }
  const CameraRelativeHeadingStep right_steering =
      StepCameraRelativeHeading(256, 0, 1024, 34);
  if (right_steering.heading_error != 256 ||
      right_steering.heading_delta != 34) {
    std::cerr << "right-angle heading was not bounded\n";
    return 1;
  }
  const CameraRelativeHeadingStep wrapped_steering =
      StepCameraRelativeHeading(1000, 20, 1024, 34);
  if (wrapped_steering.heading_error != -44 ||
      wrapped_steering.heading_delta != -34) {
    std::cerr << "wrapped heading did not choose the shortest turn\n";
    return 1;
  }
  const CameraRelativeHeadingStep near_target =
      StepCameraRelativeHeading(10, 0, 1024, 34);
  if (near_target.heading_error != 10 || near_target.heading_delta != 10) {
    std::cerr << "near heading target overshot\n";
    return 1;
  }
  for (int32_t target = 0; target < 1024; ++target) {
    for (int32_t initial = 0; initial < 1024; initial += 31) {
      int32_t current = initial;
      int32_t previous_absolute_error = 1025;
      bool converged = false;
      for (int tick = 0; tick < 18; ++tick) {
        const CameraRelativeHeadingStep step =
            StepCameraRelativeHeading(target, current, 1024, 34);
        const int32_t absolute_error = std::abs(step.heading_error);
        if (absolute_error > previous_absolute_error) {
          std::cerr << "bounded heading moved away from its target\n";
          return 1;
        }
        previous_absolute_error = absolute_error;
        current = (current + step.heading_delta) % 1024;
        if (current < 0) {
          current += 1024;
        }
        if (current == target) {
          converged = true;
          break;
        }
      }
      if (!converged) {
        std::cerr << "bounded heading did not converge\n";
        return 1;
      }
    }
  }

  CameraChaseStep chase = StepCameraChase(
      {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {1000.0, 0.0, 0.0},
      0.06, 2.0, 9000.0, 50000.0);
  if (!(chase.position[0] > 0.0 && chase.position[0] < 1000.0) ||
      chase.position[1] != 0.0 || chase.position[2] != 0.0 ||
      chase.velocity[0] <= 0.0 || chase.velocity[0] > 9000.0) {
    std::cerr << "camera chase did not make bounded target progress\n";
    return 1;
  }
  {
    CameraChaseStep fixed{{0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}};
    double previous_position = fixed.position[0];
    double previous_error = 1000.0;
    for (int tick = 0; tick < 40; ++tick) {
      fixed = StepCameraChase(
          fixed.position, fixed.velocity, {1000.0, 0.0, 0.0},
          0.06, 2.0, 9000.0, 50000.0);
      const double error = 1000.0 - fixed.position[0];
      if (fixed.position[0] < previous_position ||
          fixed.position[0] > 1000.0 || fixed.velocity[0] < 0.0 ||
          error > previous_error) {
        std::cerr << "fixed chase target did not converge monotonically\n";
        return 1;
      }
      previous_position = fixed.position[0];
      previous_error = error;
    }
    if (std::abs(1000.0 - fixed.position[0]) > 0.01) {
      std::cerr << "fixed chase target did not settle\n";
      return 1;
    }
  }
  {
    CameraChaseStep reversed{{900.0, 0.0, 0.0}, {2400.0, 0.0, 0.0}};
    double previous_error = 1900.0;
    for (int tick = 0; tick < 48; ++tick) {
      reversed = StepCameraChase(
          reversed.position, reversed.velocity, {-1000.0, 0.0, 0.0},
          0.06, 2.0, 9000.0, 50000.0);
      const double error = std::abs(-1000.0 - reversed.position[0]);
      if (error > previous_error || reversed.position[0] < -1000.0 ||
          reversed.velocity[0] > 0.0) {
        std::cerr << "reversed chase target moved away or overshot\n";
        return 1;
      }
      previous_error = error;
    }
  }
  {
    CameraChaseStep moving{{0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}};
    double target = 0.0;
    double previous_position = moving.position[0];
    double maximum_lag = 0.0;
    for (int tick = 0; tick < 80; ++tick) {
      target += 60.0;
      moving = StepCameraChase(
          moving.position, moving.velocity, {target, 0.0, 0.0},
          0.06, 2.0, 9000.0, 50000.0);
      if (moving.position[0] < previous_position || moving.velocity[0] < 0.0 ||
          moving.position[0] > target) {
        std::cerr << "moving chase target produced reverse motion\n";
        return 1;
      }
      maximum_lag = std::max(maximum_lag, target - moving.position[0]);
      previous_position = moving.position[0];
    }
    if (maximum_lag > 300.0 || target - moving.position[0] > 200.0) {
      std::cerr << "moving chase target accumulated unbounded lag\n";
      return 1;
    }
  }
  {
    // The retail fall reaches roughly 400 world units per 60 ms source tick.
    // The chase may lag that moving target, but it must advance smoothly. A
    // former caller-side reset at 900 units snapped it to the target every
    // third tick and produced the measured falling-camera cadence.
    CameraChaseStep falling{{0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}};
    double target = 0.0;
    double previous_position = falling.position[1];
    double previous_step = 0.0;
    for (int tick = 0; tick < 24; ++tick) {
      target -= 400.0;
      falling = StepCameraChase(
          falling.position, falling.velocity, {0.0, target, 0.0},
          0.06, 2.0, 9000.0, 50000.0);
      const double step = falling.position[1] - previous_position;
      if (step >= 0.0 || falling.position[1] < target ||
          (tick >= 5 && std::abs(step - previous_step) > 80.0)) {
        std::cerr << "falling chase target produced a periodic snap\n";
        return 1;
      }
      previous_position = falling.position[1];
      previous_step = step;
    }
  }
  {
    const CameraChaseStep vertical_only = StepCameraChase(
        {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 500.0, 0.0},
        0.06, 2.0, 9000.0, 50000.0);
    const CameraChaseStep combined = StepCameraChase(
        {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {10000.0, 500.0, -10000.0},
        0.06, 2.0, 9000.0, 50000.0);
    if (combined.position[1] != vertical_only.position[1] ||
        combined.velocity[1] != vertical_only.velocity[1]) {
      std::cerr << "horizontal chase consumed the vertical response budget\n";
      return 1;
    }
  }
  const CameraChaseStep chase_invalid = StepCameraChase(
      chase.position, chase.velocity, {1000.0, 0.0, 0.0},
      0.0, 2.0, 9000.0, 50000.0);
  if (chase_invalid.position != chase.position ||
      chase_invalid.velocity != chase.velocity) {
    std::cerr << "invalid chase timing changed state\n";
    return 1;
  }

  {
    const CameraChaseStep pivot = StepCameraPivotChase(
        {0.0, 1200.0, 0.0}, {0.0, 7000.0, 0.0},
        {1000.0, -400.0, 0.0}, 0.06, 2.0, 9000.0, 50000.0);
    if (!(pivot.position[0] > 0.0 && pivot.position[0] < 1000.0) ||
        pivot.position[1] != -400.0 || pivot.velocity[1] != 0.0) {
      std::cerr << "camera pivot did not damp X while following Y directly\n";
      return 1;
    }
  }

  if (!CameraPreHistoryUsesSubmittedFixedPoint(false, true, true)) {
    std::cerr << "validated unchanged submitted fixed point was not used\n";
    return 1;
  }
  if (CameraPreHistoryUsesSubmittedFixedPoint(true, true, true) ||
      CameraPreHistoryUsesSubmittedFixedPoint(false, false, true) ||
      CameraPreHistoryUsesSubmittedFixedPoint(false, true, false)) {
    std::cerr << "submitted fallback bypassed fixed-point rules\n";
    return 1;
  }
  if (!CameraPostNativeMeshContactIsIdempotent(true, 0.0) ||
      !CameraPostNativeMeshContactIsIdempotent(true, 2.0) ||
      CameraPostNativeMeshContactIsIdempotent(true, 2.01) ||
      CameraPostNativeMeshContactIsIdempotent(false, 0.0) ||
      CameraPostNativeMeshContactIsIdempotent(
          true, std::numeric_limits<double>::quiet_NaN())) {
    std::cerr << "post-native boundary idempotence classification failed\n";
    return 1;
  }

  const CameraFloorLimit matched_floor = ResolveCameraFloorLimit(
      {-9103, -1400, 14999}, {-9103, -1800, 14999}, true);
  if (matched_floor.minimum_y != -1640 ||
      !matched_floor.player_root_used) {
    std::cerr << "matched player floor limit was not enforced\n";
    return 1;
  }
  const CameraFloorLimit root_dominates = ResolveCameraFloorLimit(
      {100, 1000, 100}, {100, 950, 100}, true);
  if (root_dominates.minimum_y != 1046 ||
      !root_dominates.player_root_used) {
    std::cerr << "player-root camera clearance was not enforced\n";
    return 1;
  }
  const CameraFloorLimit stale_root = ResolveCameraFloorLimit(
      {0, 1000, 0}, {1000, 950, 0}, true);
  if (stale_root.minimum_y != 760 || stale_root.player_root_used) {
    std::cerr << "stale player snapshot changed the focus floor limit\n";
    return 1;
  }

  if (CameraInitialOverlapBlocks(100.0, 121.0)) {
    std::cerr << "outward initial overlap was incorrectly blocked\n";
    return 1;
  }
  if (CameraInitialOverlapBlocks(100.0, 100.0)) {
    std::cerr << "tangential initial overlap was incorrectly blocked\n";
    return 1;
  }
  if (!CameraInitialOverlapBlocks(100.0, 81.0)) {
    std::cerr << "inward initial overlap was not blocked\n";
    return 1;
  }
  if (CameraMeshExtentsBlockVolume({24.0, 36.0, 420.0}, 192.0)) {
    std::cerr << "thin lever was incorrectly classified as a blocker\n";
    return 1;
  }
  if (!CameraMeshExtentsBlockVolume({24.0, 900.0, 1200.0}, 192.0)) {
    std::cerr << "wall was not classified as a blocker\n";
    return 1;
  }
  if (!CameraMeshExtentsBlockVolume({309.0, 1036.0, 309.0}, 192.0)) {
    std::cerr << "thin closed-door-sized mesh was not a blocker\n";
    return 1;
  }
  if (!CameraMeshExtentsBlockVolume({260.0, 340.0, 420.0}, 192.0)) {
    std::cerr << "solid camera-sized housing was not a blocker\n";
    return 1;
  }
  if (!CameraMeshExtentsBlockVolume({420.0, 520.0, 1200.0}, 192.0)) {
    std::cerr << "large block was not classified as a blocker\n";
    return 1;
  }
  std::array<double, 3> pushed{};
  size_t pushed_axis = 3;
  if (!PushCameraOutOfExpandedBox(
          {0.0, 0.0, 0.0}, {50.0, 0.0, 0.0},
          {100.0, 200.0, 300.0}, 1, 8.0, &pushed, &pushed_axis)) {
    std::cerr << "center overlap was not pushed out\n";
    return 1;
  }
  ExpectNear(pushed[0], 108.0, "reference selects stable overlap side");
  ExpectNear(pushed[1], 0.0, "vertical axis remains unchanged");
  if (pushed_axis != 0) {
    std::cerr << "wrong overlap pushout axis\n";
    return 1;
  }
  if (!PushCameraOutOfExpandedBox(
          {-90.0, 0.0, 0.0}, {50.0, 0.0, 0.0},
          {100.0, 200.0, 300.0}, 1, 8.0, &pushed, &pushed_axis)) {
    std::cerr << "near-face overlap was not pushed out\n";
    return 1;
  }
  ExpectNear(pushed[0], -108.0, "nearest side beats reference side");
  if (PushCameraOutOfExpandedBox(
          {101.0, 0.0, 0.0}, {50.0, 0.0, 0.0},
          {100.0, 200.0, 300.0}, 1, 8.0, &pushed, &pushed_axis)) {
    std::cerr << "clear point was incorrectly pushed\n";
    return 1;
  }
  if (!PushCameraToUsableExpandedBoxFace(
          {0.0, 0.0, 0.0}, {0.0, 0.0, -500.0},
          {100.0, 200.0, 300.0}, 1, 8.0, 120.0,
          &pushed, &pushed_axis)) {
    std::cerr << "contained pivot did not find a usable side face\n";
    return 1;
  }
  ExpectNear(pushed[2], -308.0,
             "contained pivot follows requested camera side");
  if (pushed_axis != 2) {
    std::cerr << "contained pivot selected the wrong usable face\n";
    return 1;
  }
  if (!PushCameraToUsableExpandedBoxFace(
          {0.0, 0.0, 0.0}, {-308.0, 0.0, 0.0},
          {300.0, 200.0, 300.0}, 1, 8.0, 120.0,
          &pushed, &pushed_axis)) {
    std::cerr << "continuous near-pivot contact lost its prior face\n";
    return 1;
  }
  ExpectNear(pushed[0], -308.0,
             "continuous near-pivot contact retains its prior face");
  if (pushed_axis != 0) {
    std::cerr << "continuous near-pivot contact switched face axis\n";
    return 1;
  }
  if (!PushCameraToUsableExpandedBoxRayExit(
          {0.0, 0.0, 0.0}, {500.0, 0.0, 100.0},
          {100.0, 200.0, 300.0}, 1, 8.0, 120.0,
          &pushed, &pushed_axis)) {
    std::cerr << "contained pivot did not follow the requested ray exit\n";
    return 1;
  }

  const CameraExpandedBoxRayExit contained_exit =
      FindContainedExpandedBoxRayExit(
          {0.0, 0.0, 0.0}, {1.0, 0.0, 0.0},
          {100.0, 200.0, 300.0}, 8.0, 1400.0);
  if (!contained_exit.valid || contained_exit.axis != 0u) {
    std::cerr << "contained expanded-box ray did not find its exit\n";
    return 1;
  }
  ExpectNear(contained_exit.distance, 108.0,
             "contained expanded-box ray exit distance");
  const CameraExpandedBoxRayExit tangent_exit =
      FindContainedExpandedBoxRayExit(
          {0.0, 0.0, 0.0}, {0.0, 0.0, 1.0},
          {100.0, 200.0, 300.0}, 8.0, 1400.0);
  if (!tangent_exit.valid || tangent_exit.axis != 2u) {
    std::cerr << "tangent contained ray did not exit through its live face\n";
    return 1;
  }
  const CameraExpandedBoxRayExit short_ray_exit =
      FindContainedExpandedBoxRayExit(
          {0.0, 0.0, 0.0}, {1.0, 0.0, 0.0},
          {100.0, 200.0, 300.0}, 8.0, 80.0);
  if (short_ray_exit.valid) {
    std::cerr << "ray ending inside expanded box was treated as an exit\n";
    return 1;
  }
  const CameraExpandedBoxRayExit outside_exit =
      FindContainedExpandedBoxRayExit(
          {109.0, 0.0, 0.0}, {1.0, 0.0, 0.0},
          {100.0, 200.0, 300.0}, 8.0, 1400.0);
  if (outside_exit.valid) {
    std::cerr << "outside pivot was treated as contained\n";
    return 1;
  }
  ExpectNear(std::hypot(pushed[0], pushed[2]), 120.0,
             "contained ray exit reaches usable distance");
  ExpectNear(pushed[2] / pushed[0], 0.2,
             "contained ray exit preserves orbit direction");
  if (pushed_axis != 0 || pushed[0] <= 108.0) {
    std::cerr << "contained ray exit did not clear its selected face\n";
    return 1;
  }
  if (!PushCameraAlongExpandedBoxSupportingFace(
          {108.0, 0.0, 0.0}, {108.0, 0.0, 250.0},
          {100.0, 200.0, 300.0}, 1, 8.0, 120.0,
          &pushed, &pushed_axis)) {
    std::cerr << "outside pivot did not slide along its supporting face\n";
    return 1;
  }
  ExpectNear(pushed[0], 108.0,
             "supporting-face slide keeps the safe coordinate");
  ExpectNear(pushed[2], 250.0,
             "supporting-face slide follows current orbit tangent");
  if (pushed_axis != 0u) {
    std::cerr << "supporting-face slide lost its support axis\n";
    return 1;
  }
  if (!PushCameraAlongExpandedBoxSupportingFace(
          {108.0, 0.0, 0.0}, {108.0, 0.0, 60.0},
          {100.0, 200.0, 300.0}, 1, 8.0, 120.0,
          &pushed, &pushed_axis)) {
    std::cerr << "near-zero tangent did not build a continuous arc\n";
    return 1;
  }
  ExpectNear(std::hypot(pushed[0] - 108.0, pushed[2]), 120.0,
             "supporting-face zero tangent minimum-radius arc");
  if (PushCameraAlongExpandedBoxSupportingFace(
          {0.0, 0.0, 0.0}, {0.0, 0.0, 250.0},
          {100.0, 200.0, 300.0}, 1, 8.0, 120.0,
          &pushed, &pushed_axis)) {
    std::cerr << "contained pivot was misclassified as a support slide\n";
    return 1;
  }
  if (!PushCameraToUsableExpandedBoxRayExit(
          {0.0, 0.0, 0.0}, {300.0, 0.0, 310.0},
          {100.0, 200.0, 100.0}, 1, 0.0, 120.0,
          &pushed, &pushed_axis, 0, 16.0) ||
      pushed_axis != 0) {
    std::cerr << "near-tie ray exit did not retain its supporting axis\n";
    return 1;
  }
  if (!PushCameraToUsableExpandedBoxRayExit(
          {0.0, 0.0, 0.0}, {300.0, 0.0, 600.0},
          {100.0, 200.0, 100.0}, 1, 0.0, 120.0,
          &pushed, &pushed_axis, 0, 16.0) ||
      pushed_axis != 2) {
    std::cerr << "materially better ray exit did not replace the old axis\n";
    return 1;
  }
  if (!PushCameraOutOfExpandedBox(
          {0.0, 0.0, 5.0}, {50.0, 0.0, 50.0},
          {100.0, 200.0, 100.0}, 1, 0.0,
          &pushed, &pushed_axis, 0, 16.0) ||
      pushed_axis != 0) {
    std::cerr << "near-tie overlap did not retain its supporting axis\n";
    return 1;
  }
  if (!PushCameraOutOfExpandedBox(
          {0.0, 0.0, 80.0}, {50.0, 0.0, 100.0},
          {100.0, 200.0, 100.0}, 1, 0.0,
          &pushed, &pushed_axis, 0, 16.0) ||
      pushed_axis != 2) {
    std::cerr << "materially nearer overlap face did not replace the old axis\n";
    return 1;
  }
  if (PushCameraToUsableExpandedBoxRayExit(
          {101.0, 0.0, 0.0}, {-500.0, 0.0, 100.0},
          {100.0, 200.0, 300.0}, 1, 8.0, 120.0,
          &pushed, &pushed_axis)) {
    std::cerr << "outside pivot was routed back through the expanded box\n";
    return 1;
  }
  if (!PushCameraToUsableExpandedBoxFace(
          {-101.0, 0.0, 0.0}, {-101.0, 0.0, -500.0},
          {100.0, 200.0, 300.0}, 1, 8.0, 120.0,
          &pushed, &pushed_axis)) {
    std::cerr << "near-pivot contact did not slide along the outside face\n";
    return 1;
  }
  ExpectNear(pushed[0], -101.0,
             "outside support coordinate remains fixed");
  ExpectNear(pushed[2], -308.0,
             "near-pivot contact follows requested camera side");
  if (pushed_axis != 2) {
    std::cerr << "near-pivot contact selected its support face\n";
    return 1;
  }

  const std::array<int32_t, 3> stale_native = {-8229, -1646, 17607};
  const std::array<int32_t, 3> translated =
      TranslateCameraTargetWithFocus(
          {-8380, -1400, 16329}, {-8300, -1390, 16400},
          stale_native);
  if (translated != std::array<int32_t, 3>{-8149, -1636, 17678}) {
    std::cerr << "presentation target did not follow camera focus\n";
    return 1;
  }
  const std::array<int32_t, 3> saturated =
      TranslateCameraTargetWithFocus(
          {0, 0, 0},
          {std::numeric_limits<int32_t>::max(), 0, 0},
          {1, 0, 0});
  if (saturated[0] != std::numeric_limits<int32_t>::max()) {
    std::cerr << "presentation target focus translation overflowed\n";
    return 1;
  }

  if (CameraTargetMeetsMinimumDistance(
          {0, 0, 0}, {10, 0, 0}, 120.0) ||
      CameraTargetMeetsMinimumDistance(
          {0, 0, 0}, {119, 0, 0}, 120.0)) {
    std::cerr << "near-pivot presentation target was accepted\n";
    return 1;
  }
  if (!CameraTargetMeetsMinimumDistance(
          {100, 200, 300}, {220, 200, 300}, 120.0)) {
    std::cerr << "minimum usable presentation target was rejected\n";
    return 1;
  }

  constexpr uint64_t kWallBlocker = 1u;
  constexpr uint64_t kMeshBlocker = 2u;
  auto step = StepCameraSpringArm(1400.0, 420.0, 1400.0, true, 0, 0,
                                  0.0, 0u, kWallBlocker);
  ExpectNear(step.radius, 420.0, "immediate contraction");

  step = StepCameraSpringArm(1400.0, 420.0, step.radius, true,
                             step.clear_ticks, step.blocked_release_ticks,
                             step.blocked_candidate_distance,
                             step.blocker_key, kWallBlocker);
  ExpectNear(step.radius, 420.0, "stable contact does not oscillate");

  step = StepCameraSpringArm(1400.0, 1400.0, step.radius, false,
                             step.clear_ticks, step.blocked_release_ticks,
                             step.blocked_candidate_distance,
                             step.blocker_key, 0u);
  ExpectNear(step.radius, 420.0, "one missing snapshot is held");
  ExpectTicks(step.clear_ticks, 1, "first clear tick");

  for (uint32_t clear_tick = 2; clear_tick < 4; ++clear_tick) {
    step = StepCameraSpringArm(1400.0, 1400.0, step.radius, false,
                               step.clear_ticks,
                               step.blocked_release_ticks,
                               step.blocked_candidate_distance,
                               step.blocker_key, 0u);
    ExpectNear(step.radius, 420.0, "unconfirmed clear space is held");
  }
  step = StepCameraSpringArm(1400.0, 1400.0, step.radius, false,
                             step.clear_ticks, step.blocked_release_ticks,
                             step.blocked_candidate_distance,
                             step.blocker_key, 0u);
  ExpectNear(step.radius, 484.0, "confirmed clear-space release");

  step = StepCameraSpringArm(1400.0, 500.0, step.radius, true,
                             step.clear_ticks, step.blocked_release_ticks,
                             step.blocked_candidate_distance,
                             step.blocker_key, kWallBlocker);
  ExpectNear(step.radius, 484.0, "first blocked outward sample is held");
  ExpectTicks(step.blocked_release_ticks, 1,
              "first blocked outward confirmation");

  step = StepCameraSpringArm(1400.0, 540.0, step.radius, true,
                             step.clear_ticks, step.blocked_release_ticks,
                             step.blocked_candidate_distance,
                             step.blocker_key, kWallBlocker);
  ExpectNear(step.radius, 484.0, "second blocked outward sample is held");

  step = StepCameraSpringArm(1400.0, 580.0, step.radius, true,
                             step.clear_ticks, step.blocked_release_ticks,
                             step.blocked_candidate_distance,
                             step.blocker_key, kWallBlocker);
  ExpectNear(step.radius, 548.0, "confirmed moving boundary release");

  step = StepCameraSpringArm(1400.0, 450.0, step.radius, true,
                             step.clear_ticks, step.blocked_release_ticks,
                             step.blocked_candidate_distance,
                             step.blocker_key, kWallBlocker);
  ExpectNear(step.radius, 450.0, "new inward boundary is authoritative");

  step = StepCameraSpringArm(1400.0, 0.0, step.radius, true,
                             step.clear_ticks, step.blocked_release_ticks,
                             step.blocked_candidate_distance,
                             step.blocker_key, kWallBlocker);
  ExpectNear(step.radius, 0.0, "verified pivot overlap is safe");

  step = StepCameraSpringArm(1400.0, 1600.0, 1390.0, false, 3, 0);
  ExpectNear(step.radius, 1400.0, "never extends beyond desired arm");

  step = StepCameraSpringArm(1400.0, 182.0, 246.0, true, 0, 0,
                             0.0, 0u, kWallBlocker);
  ExpectNear(step.radius, 182.0, "alternating boundary contracts");
  step = StepCameraSpringArm(1400.0, 311.0, step.radius, true,
                             step.clear_ticks, step.blocked_release_ticks,
                             step.blocked_candidate_distance,
                             step.blocker_key, kWallBlocker);
  ExpectNear(step.radius, 182.0, "one-frame outward boundary is held");
  step = StepCameraSpringArm(1400.0, 182.0, step.radius, true,
                             step.clear_ticks, step.blocked_release_ticks,
                             step.blocked_candidate_distance,
                             step.blocker_key, kWallBlocker);
  ExpectNear(step.radius, 182.0, "alternating inward boundary stays stable");
  ExpectTicks(step.blocked_release_ticks, 0,
              "alternating boundary resets confirmation");

  step = StepCameraSpringArm(1400.0, 500.0, 420.0, true, 0, 0,
                             420.0, kWallBlocker, kWallBlocker);
  step = StepCameraSpringArm(1400.0, 540.0, step.radius, true,
                             step.clear_ticks, step.blocked_release_ticks,
                             step.blocked_candidate_distance,
                             step.blocker_key, kMeshBlocker);
  ExpectNear(step.radius, 420.0, "changed blocker cannot inherit release");
  ExpectTicks(step.blocked_release_ticks, 1,
              "changed blocker restarts confirmation");
  step = StepCameraSpringArm(1400.0, 510.0, step.radius, true,
                             step.clear_ticks, step.blocked_release_ticks,
                             step.blocked_candidate_distance,
                             step.blocker_key, kMeshBlocker);
  ExpectTicks(step.blocked_release_ticks, 1,
              "regressing clearance restarts confirmation");

  // The fully-owned camera uses a longer evidence window than the frozen
  // hybrid. A boundary that alternates hit/clear every few source ticks must
  // not create a release-then-contract sawtooth.
  step = StepCameraSpringArm(1400.0, 700.0, 1400.0, true, 0, 0,
                             0.0, 0u, kMeshBlocker,
                             10u, 8u, 64.0);
  ExpectNear(step.radius, 700.0, "owned policy contracts immediately");
  for (uint32_t clear_tick = 1; clear_tick < 10; ++clear_tick) {
    step = StepCameraSpringArm(1400.0, 1400.0, step.radius, false,
                               step.clear_ticks,
                               step.blocked_release_ticks,
                               step.blocked_candidate_distance,
                               step.blocker_key, 0u,
                               10u, 8u, 64.0);
    ExpectNear(step.radius, 700.0,
               "owned policy holds intermittent clear evidence");
  }
  step = StepCameraSpringArm(1400.0, 1400.0, step.radius, false,
                             step.clear_ticks,
                             step.blocked_release_ticks,
                             step.blocked_candidate_distance,
                             step.blocker_key, 0u,
                             10u, 8u, 64.0);
  ExpectNear(step.radius, 764.0,
             "owned policy releases after sustained clear evidence");
  step = StepCameraSpringArm(1400.0, 900.0, 700.0, true, 0, 0,
                             700.0, kMeshBlocker, kMeshBlocker,
                             10u, 8u, 64.0);
  for (uint32_t blocked_tick = 1; blocked_tick < 8; ++blocked_tick) {
    ExpectNear(step.radius, 700.0,
               "owned policy holds moving boundary evidence");
    step = StepCameraSpringArm(1400.0, 900.0, step.radius, true,
                               step.clear_ticks,
                               step.blocked_release_ticks,
                               step.blocked_candidate_distance,
                               step.blocker_key, kMeshBlocker,
                               10u, 8u, 64.0);
  }
  ExpectNear(step.radius, 764.0,
             "owned moving boundary releases after confirmation");

  if (!CameraPreNativeEscapeOwnsFinalTarget(
          true, false, true, true, true, true) ||
      CameraPreNativeEscapeOwnsFinalTarget(
          true, false, true, true, false, true) ||
      CameraPreNativeEscapeOwnsFinalTarget(
          true, false, false, true, true, true) ||
      CameraPreNativeEscapeOwnsFinalTarget(
          false, false, true, true, true, true) ||
      CameraPreNativeEscapeOwnsFinalTarget(
          true, false, true, true, true, false)) {
    std::cerr << "pre-native escape ownership classification failed\n";
    return 1;
  }
  if (!CameraConfiguredMeshResultAllowsPreNativeEscape(
          false, false, false, false) ||
      !CameraConfiguredMeshResultAllowsPreNativeEscape(
          true, false, true, true) ||
      !CameraConfiguredMeshResultAllowsPreNativeEscape(
          true, true, false, false) ||
      CameraConfiguredMeshResultAllowsPreNativeEscape(
          true, true, true, false) ||
      CameraConfiguredMeshResultAllowsPreNativeEscape(
          true, false, false, false)) {
    std::cerr << "configured mesh constraint-set classification failed\n";
    return 1;
  }

  std::array<double, 3> pitched_escape{50.0, 0.0, 0.0};
  if (!PreserveCameraEscapePitch(
          {0.0, 0.0, 0.0}, {100.0, 80.0, 0.0}, 1u,
          &pitched_escape)) {
    std::cerr << "escape pitch preservation failed\n";
    return 1;
  }
  ExpectNear(pitched_escape[1], 40.0,
             "escape preserves orbit pitch by horizontal progress");
  pitched_escape = {150.0, 0.0, 0.0};
  if (!PreserveCameraEscapePitch(
          {0.0, 0.0, 0.0}, {100.0, 80.0, 0.0}, 1u,
          &pitched_escape)) {
    std::cerr << "bounded escape pitch preservation failed\n";
    return 1;
  }
  ExpectNear(pitched_escape[1], 80.0,
             "escape pitch cannot exceed requested vertical target");
  if (!CameraPreHistoryMeshVetoMayApply(
          true, true, true, true, true) ||
      CameraPreHistoryMeshVetoMayApply(
          false, true, true, true, true) ||
      CameraPreHistoryMeshVetoMayApply(
          true, false, true, true, true) ||
      CameraPreHistoryMeshVetoMayApply(
          true, true, false, true, true) ||
      CameraPreHistoryMeshVetoMayApply(
          true, true, true, false, true) ||
      CameraPreHistoryMeshVetoMayApply(
          true, true, true, true, false)) {
    std::cerr << "pre-history mesh-veto ownership failed\n";
    return 1;
  }
  if (!CameraModernEndpointMayOwnFinalPosition(
          true, false, false, false, true, true, true, true) ||
      CameraModernEndpointMayOwnFinalPosition(
          false, false, false, false, true, true, true, true) ||
      CameraModernEndpointMayOwnFinalPosition(
          true, true, false, false, true, true, true, true) ||
      CameraModernEndpointMayOwnFinalPosition(
          true, false, true, false, true, true, true, true) ||
      CameraModernEndpointMayOwnFinalPosition(
          true, false, false, true, true, true, true, true) ||
      CameraModernEndpointMayOwnFinalPosition(
          true, false, false, false, false, true, true, true) ||
      CameraModernEndpointMayOwnFinalPosition(
          true, false, false, false, true, false, true, true) ||
      CameraModernEndpointMayOwnFinalPosition(
          true, false, false, false, true, true, false, true) ||
      CameraModernEndpointMayOwnFinalPosition(
          true, false, false, false, true, true, true, false)) {
    std::cerr << "modern endpoint final-ownership classification failed\n";
    return 1;
  }
  if (!CameraPreNativeSceneContactMayOwnFinalPosition(
          true, false, false, false, true, true, true, true) ||
      CameraPreNativeSceneContactMayOwnFinalPosition(
          false, false, false, false, true, true, true, true) ||
      CameraPreNativeSceneContactMayOwnFinalPosition(
          true, true, false, false, true, true, true, true) ||
      CameraPreNativeSceneContactMayOwnFinalPosition(
          true, false, true, false, true, true, true, true) ||
      CameraPreNativeSceneContactMayOwnFinalPosition(
          true, false, false, true, true, true, true, true) ||
      CameraPreNativeSceneContactMayOwnFinalPosition(
          true, false, false, false, false, true, true, true) ||
      CameraPreNativeSceneContactMayOwnFinalPosition(
          true, false, false, false, true, false, true, true) ||
      CameraPreNativeSceneContactMayOwnFinalPosition(
          true, false, false, false, true, true, false, true) ||
      CameraPreNativeSceneContactMayOwnFinalPosition(
          true, false, false, false, true, true, true, false)) {
    std::cerr << "pre-native scene-contact ownership classification failed\n";
    return 1;
  }
  auto pivot_camera = InterpolateCameraPivotRelative(
      {0.0, 0.0, 0.0}, {0.0, 0.0, 1000.0},
      {100.0, 20.0, 0.0}, {100.0, 20.0, 1000.0}, 0.5);
  if (!pivot_camera.valid) {
    std::cerr << "pivot-relative camera interpolation was invalid\n";
    return 1;
  }
  ExpectNear(pivot_camera.focus[0], 50.0,
             "pivot-relative interpolated focus x");
  ExpectNear(pivot_camera.focus[1], 10.0,
             "pivot-relative interpolated focus y");
  ExpectNear(pivot_camera.position[0], 50.0,
             "pivot-relative constant offset x");
  ExpectNear(pivot_camera.position[1], 10.0,
             "pivot-relative constant offset y");
  ExpectNear(pivot_camera.position[2], 1000.0,
             "pivot-relative constant offset z");

  const double wrap_angle = 179.0 * 3.14159265358979323846 / 180.0;
  pivot_camera = InterpolateCameraPivotRelative(
      {0.0, 0.0, 0.0},
      {std::sin(wrap_angle) * 1000.0, 0.0,
       std::cos(wrap_angle) * 1000.0},
      {0.0, 0.0, 0.0},
      {std::sin(-wrap_angle) * 1000.0, 0.0,
       std::cos(-wrap_angle) * 1000.0}, 0.5);
  if (!pivot_camera.valid || pivot_camera.position[2] > -999.0) {
    std::cerr << "pivot-relative yaw did not take shortest wrap path\n";
    return 1;
  }

  std::cout << "camera spring-arm state tests passed\n";
  return 0;
}
