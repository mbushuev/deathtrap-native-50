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
  const std::array<CameraConvexPlane, 6> convex_box{{
      {{{100.0, 0.0, 0.0}}, {{1.0, 0.0, 0.0}}},
      {{{-100.0, 0.0, 0.0}}, {{-1.0, 0.0, 0.0}}},
      {{{0.0, 100.0, 0.0}}, {{0.0, 1.0, 0.0}}},
      {{{0.0, -100.0, 0.0}}, {{0.0, -1.0, 0.0}}},
      {{{0.0, 0.0, 100.0}}, {{0.0, 0.0, 1.0}}},
      {{{0.0, 0.0, -100.0}}, {{0.0, 0.0, -1.0}}},
  }};
  const CameraConvexSweep convex_entry =
      SweepCameraSphereAgainstConvexVolume(
          {-300.0, 0.0, 0.0}, {2.0, 0.0, 0.0}, 10.0, 500.0,
          convex_box.data(), convex_box.size());
  ExpectTicks(convex_entry.valid ? 1u : 0u, 1u,
              "convex entry valid");
  ExpectTicks(convex_entry.hit ? 1u : 0u, 1u,
              "convex entry hit");
  ExpectTicks(convex_entry.initial_overlap ? 1u : 0u, 0u,
              "convex entry outside");
  ExpectNear(convex_entry.distance, 190.0,
             "convex sphere expanded entry");
  ExpectNear(convex_entry.exit_distance, 410.0,
             "convex sphere expanded exit");

  const CameraConvexSweep convex_miss =
      SweepCameraSphereAgainstConvexVolume(
          {-300.0, 150.0, 0.0}, {1.0, 0.0, 0.0}, 10.0, 500.0,
          convex_box.data(), convex_box.size());
  ExpectTicks(convex_miss.valid ? 1u : 0u, 1u,
              "convex miss valid");
  ExpectTicks(convex_miss.hit ? 1u : 0u, 0u,
              "convex parallel miss");

  const CameraConvexSweep convex_tangent =
      SweepCameraSphereAgainstConvexVolume(
          {-300.0, 110.0, 0.0}, {1.0, 0.0, 0.0}, 10.0, 500.0,
          convex_box.data(), convex_box.size());
  ExpectTicks(convex_tangent.hit ? 1u : 0u, 1u,
              "convex expanded tangent hit");
  ExpectNear(convex_tangent.distance, 190.0,
             "convex expanded tangent distance");

  const CameraConvexSweep convex_inside =
      SweepCameraSphereAgainstConvexVolume(
          {0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, 10.0, 500.0,
          convex_box.data(), convex_box.size());
  ExpectTicks(convex_inside.hit ? 1u : 0u, 1u,
              "convex initial overlap reports contact");
  ExpectTicks(convex_inside.initial_overlap ? 1u : 0u, 1u,
              "convex initial overlap classified");
  ExpectNear(convex_inside.distance, 0.0,
             "convex initial overlap contact distance");
  ExpectNear(convex_inside.exit_distance, 110.0,
             "convex initial overlap exit distance");

  const CameraConvexSweep convex_inside_exit =
      SweepCameraSphereAgainstConvexVolume(
          {0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, 10.0, 500.0,
          convex_box.data(), convex_box.size(), true);
  ExpectTicks(convex_inside_exit.valid ? 1u : 0u, 1u,
              "convex allowed exit valid");
  ExpectTicks(convex_inside_exit.hit ? 1u : 0u, 0u,
              "convex allowed exit clear");
  ExpectTicks(convex_inside_exit.initial_overlap ? 1u : 0u, 1u,
              "convex allowed exit classified");
  ExpectNear(convex_inside_exit.exit_distance, 110.0,
             "convex allowed exit distance");

  const CameraConvexSweep convex_inside_short_arm =
      SweepCameraSphereAgainstConvexVolume(
          {0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, 10.0, 50.0,
          convex_box.data(), convex_box.size(), true);
  ExpectTicks(convex_inside_short_arm.hit ? 1u : 0u, 1u,
              "convex endpoint inside volume remains blocked");
  ExpectTicks(convex_inside_short_arm.initial_overlap ? 1u : 0u, 1u,
              "convex short arm overlap classified");
  ExpectNear(convex_inside_short_arm.exit_distance, 110.0,
             "convex short arm preserves real exit distance");

  const CameraConvexSweep convex_repeat =
      SweepCameraSphereAgainstConvexVolume(
          {-300.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, 10.0, 500.0,
          convex_box.data(), convex_box.size());
  ExpectNear(convex_repeat.distance, convex_entry.distance,
             "convex accepted boundary idempotent");
  ExpectNear(convex_repeat.exit_distance, convex_entry.exit_distance,
             "convex exit idempotent");

  const CameraConvexSweep convex_point =
      SweepCameraSphereAgainstConvexVolume(
          {-300.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, 0.0, 500.0,
          convex_box.data(), convex_box.size());
  ExpectNear(convex_point.distance, 200.0,
             "convex zero-radius entry");

  const CameraCapsuleClearance capsule_side =
      MeasureCameraCapsuleClearance(
          {5.0, 5.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 10.0, 0.0}, 2.0);
  ExpectTicks(capsule_side.valid ? 1u : 0u, 1u,
              "capsule side valid");
  ExpectTicks(capsule_side.inside ? 1u : 0u, 0u,
              "capsule side outside");
  ExpectNear(capsule_side.segment_parameter, 0.5,
             "capsule side segment parameter");
  ExpectNear(capsule_side.centerline_distance, 5.0,
             "capsule side centerline distance");
  ExpectNear(capsule_side.clearance, 3.0, "capsule side clearance");

  const CameraCapsuleClearance capsule_cap =
      MeasureCameraCapsuleClearance(
          {0.0, 13.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 10.0, 0.0}, 4.0);
  ExpectTicks(capsule_cap.inside ? 1u : 0u, 1u,
              "capsule cap inside");
  ExpectNear(capsule_cap.segment_parameter, 1.0,
             "capsule cap segment parameter");
  ExpectNear(capsule_cap.clearance, -1.0, "capsule cap clearance");

  const CameraCapsuleClearance capsule_degenerate =
      MeasureCameraCapsuleClearance(
          {1.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, 2.0);
  ExpectTicks(capsule_degenerate.valid ? 1u : 0u, 0u,
              "capsule degenerate invalid");

  CameraCharacterFadeStep character_fade = StepCameraCharacterFade(
      false, true, 20.0, 0u, 0.0, 64.0, 3u);
  ExpectTicks(character_fade.active ? 1u : 0u, 0u,
              "character fade outside remains opaque");
  character_fade = StepCameraCharacterFade(
      character_fade.active, true, -1.0, character_fade.clear_ticks,
      0.0, 64.0, 3u);
  ExpectTicks(character_fade.active ? 1u : 0u, 1u,
              "character fade enters capsule");
  ExpectTicks(character_fade.changed ? 1u : 0u, 1u,
              "character fade enter transition");
  character_fade = StepCameraCharacterFade(
      character_fade.active, true, 32.0, character_fade.clear_ticks,
      0.0, 64.0, 3u);
  ExpectTicks(character_fade.active ? 1u : 0u, 1u,
              "character fade hysteresis retains near boundary");
  for (uint32_t tick = 0; tick < 2u; ++tick) {
    character_fade = StepCameraCharacterFade(
        character_fade.active, true, 80.0, character_fade.clear_ticks,
        0.0, 64.0, 3u);
  }
  ExpectTicks(character_fade.active ? 1u : 0u, 1u,
              "character fade waits for sustained clearance");
  character_fade = StepCameraCharacterFade(
      character_fade.active, true, 80.0, character_fade.clear_ticks,
      0.0, 64.0, 3u);
  ExpectTicks(character_fade.active ? 1u : 0u, 0u,
              "character fade releases after sustained clearance");
  character_fade = StepCameraCharacterFade(
      true, false, 0.0, 2u, 0.0, 64.0, 3u);
  ExpectTicks(character_fade.active ? 1u : 0u, 0u,
              "character fade invalid snapshot fails open");

  const CameraOrbitStickInput third_person_orbit =
      CameraOrbitModeInput(0.5, 0.25, false, false, false, 1.4);
  ExpectNear(third_person_orbit.x, 0.7,
             "third-person horizontal direction and speed");
  ExpectNear(third_person_orbit.y, -0.35,
             "third-person vertical convention and speed");
  const CameraOrbitStickInput immersive_orbit =
      CameraOrbitModeInput(0.5, 0.25, true, false, false, 1.4);
  ExpectNear(immersive_orbit.x, 0.5,
             "immersive horizontal convention unchanged");
  ExpectNear(immersive_orbit.y, 0.25,
             "immersive vertical convention unchanged");
  const CameraOrbitStickInput inverted_third_person_orbit =
      CameraOrbitModeInput(0.5, 0.25, false, true, true, 1.4);
  ExpectNear(inverted_third_person_orbit.x, -0.7,
             "third-person horizontal inversion");
  ExpectNear(inverted_third_person_orbit.y, 0.35,
             "third-person vertical inversion");

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
  CameraNearPivotModeStep near_pivot = StepCameraNearPivotMode(
      false, 119.0, 0u, 120.0, 288.0, 4u);
  if (!near_pivot.active || !near_pivot.changed) {
    std::cerr << "collapsed direct arm did not enter near-pivot view\n";
    return 1;
  }
  for (uint32_t clear_tick = 1; clear_tick < 4u; ++clear_tick) {
    near_pivot = StepCameraNearPivotMode(
        near_pivot.active, 400.0, near_pivot.direct_clear_ticks,
        120.0, 288.0, 4u);
    if (!near_pivot.active || near_pivot.changed ||
        near_pivot.direct_clear_ticks != clear_tick) {
      std::cerr << "near-pivot view exited without sustained direct room\n";
      return 1;
    }
  }
  near_pivot = StepCameraNearPivotMode(
      near_pivot.active, 400.0, near_pivot.direct_clear_ticks,
      120.0, 288.0, 4u);
  if (near_pivot.active || !near_pivot.changed ||
      near_pivot.direct_clear_ticks != 4u) {
    std::cerr << "near-pivot view did not exit after sustained direct room\n";
    return 1;
  }
  near_pivot = StepCameraNearPivotMode(
      true, 287.0, 3u, 120.0, 288.0, 4u);
  if (!near_pivot.active || near_pivot.changed ||
      near_pivot.direct_clear_ticks != 0u) {
    std::cerr << "near-pivot direct-clear evidence survived a relapse\n";
    return 1;
  }
  CameraNearPivotModeStep rotating_near_pivot =
      StepCameraNearPivotMode(
          true, 500.0, 0u, 120.0, 288.0, 2u);
  if (!rotating_near_pivot.active ||
      rotating_near_pivot.direct_clear_ticks != 1u) {
    std::cerr << "rotating near-pivot exit lost first clear ray\n";
    return 1;
  }
  rotating_near_pivot = StepCameraNearPivotMode(
      rotating_near_pivot.active, 500.0,
      rotating_near_pivot.direct_clear_ticks,
      120.0, 288.0, 2u);
  if (rotating_near_pivot.active || !rotating_near_pivot.changed) {
    std::cerr << "rotating near-pivot view remained latched\n";
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
  if (!CameraMeshExtentsAreSoftObstacle(
          {309.0, 1036.0, 309.0}, 192.0, 384.0) ||
      CameraMeshExtentsAreSoftObstacle(
          {24.0, 900.0, 1200.0}, 192.0, 384.0) ||
      CameraMeshExtentsAreSoftObstacle(
          {420.0, 520.0, 1200.0}, 192.0, 384.0)) {
    std::cerr << "soft obstacle geometry classification failed\n";
    return 1;
  }
  if (!CameraMeshExtentsAreThinSheet(
          {21.0, 1071.0, 676.0}, 48.0, 384.0) ||
      CameraMeshExtentsAreThinSheet(
          {208.0, 526.0, 208.0}, 48.0, 384.0) ||
      CameraMeshExtentsAreThinSheet(
          {420.0, 520.0, 1200.0}, 48.0, 384.0)) {
    std::cerr << "thin render-sheet classification failed\n";
    return 1;
  }

  auto soft_gate = StepCameraSoftObstacleGate(
      0u, 0u, 0u, 42u, 100u, 3u);
  ExpectTicks(soft_gate.accepted ? 1u : 0u, 0u,
              "first soft contact remains provisional");
  ExpectTicks(soft_gate.consecutive_ticks, 1u,
              "first soft contact evidence");
  soft_gate = StepCameraSoftObstacleGate(
      soft_gate.blocker_key, soft_gate.last_source_tick,
      soft_gate.consecutive_ticks, 42u, 100u, 3u);
  ExpectTicks(soft_gate.consecutive_ticks, 1u,
              "same-tick revalidation is not temporal evidence");
  soft_gate = StepCameraSoftObstacleGate(
      soft_gate.blocker_key, soft_gate.last_source_tick,
      soft_gate.consecutive_ticks, 42u, 101u, 3u);
  ExpectTicks(soft_gate.accepted ? 1u : 0u, 0u,
              "second soft contact remains provisional");
  soft_gate = StepCameraSoftObstacleGate(
      soft_gate.blocker_key, soft_gate.last_source_tick,
      soft_gate.consecutive_ticks, 42u, 102u, 3u);
  ExpectTicks(soft_gate.accepted ? 1u : 0u, 1u,
              "persistent soft contact becomes authoritative");
  soft_gate = StepCameraSoftObstacleGate(
      soft_gate.blocker_key, soft_gate.last_source_tick,
      soft_gate.consecutive_ticks, 77u, 103u, 3u);
  ExpectTicks(soft_gate.accepted ? 1u : 0u, 0u,
              "changed soft blocker restarts evidence");
  ExpectTicks(soft_gate.consecutive_ticks, 1u,
              "changed soft blocker evidence reset");
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

  // Once a blocked boundary has supplied enough evidence to release, retain
  // a small radial cushion. Advancing exactly onto the moving safe sample
  // makes the next quantized sample contract the arm again and creates a
  // rapid release/contract loop in tight spaces.
  step = StepCameraSpringArm(
      1400.0, 760.0, 700.0, true, 0, 0, 700.0,
      kMeshBlocker, kMeshBlocker, 10u, 1u, 64.0, false,
      std::numeric_limits<double>::quiet_NaN(), 0.0, 48.0);
  ExpectNear(step.radius, 712.0,
             "blocked recovery retains boundary cushion");
  step = StepCameraSpringArm(
      1400.0, 745.0, step.radius, true, step.clear_ticks,
      step.blocked_release_ticks, step.blocked_candidate_distance,
      step.blocker_key, kMeshBlocker, 10u, 1u, 64.0, false,
      std::numeric_limits<double>::quiet_NaN(), 0.0, 48.0);
  ExpectNear(step.radius, 712.0,
             "boundary cushion prevents immediate reverse step");
  step = StepCameraSpringArm(
      1400.0, 820.0, step.radius, true, step.clear_ticks,
      step.blocked_release_ticks, step.blocked_candidate_distance,
      step.blocker_key, kMeshBlocker, 10u, 1u, 64.0, false,
      std::numeric_limits<double>::quiet_NaN(), 0.0, 48.0);
  ExpectNear(step.radius, 772.0,
             "blocked recovery stops before buffered boundary");

  step = StepCameraSpringArm(
      1400.0, 1400.0, 900.0, false, 12u, 0u, 900.0, 0u, 0u,
      10u, 8u, 64.0, false,
      std::numeric_limits<double>::quiet_NaN(), 0.0, 48.0, true);
  ExpectNear(step.radius, 900.0,
             "active orbit holds clear-sector radius");
  step = StepCameraSpringArm(
      1400.0, 1200.0, step.radius, true, step.clear_ticks,
      step.blocked_release_ticks, step.blocked_candidate_distance,
      step.blocker_key, kWallBlocker, 10u, 8u, 64.0, false,
      std::numeric_limits<double>::quiet_NaN(), 0.0, 48.0, true);
  ExpectNear(step.radius, 900.0,
             "active orbit holds outward blocked sample");
  step = StepCameraSpringArm(
      1400.0, 700.0, step.radius, true, step.clear_ticks,
      step.blocked_release_ticks, step.blocked_candidate_distance,
      step.blocker_key, kWallBlocker, 10u, 8u, 64.0, false,
      std::numeric_limits<double>::quiet_NaN(), 0.0, 48.0, true);
  ExpectNear(step.radius, 700.0,
             "active orbit preserves immediate inward safety");
  for (uint32_t clear_tick = 0; clear_tick < 10u; ++clear_tick) {
    step = StepCameraSpringArm(
        1400.0, 1400.0, step.radius, false, step.clear_ticks,
        step.blocked_release_ticks, step.blocked_candidate_distance,
        step.blocker_key, 0u, 10u, 8u, 64.0, false,
        std::numeric_limits<double>::quiet_NaN(), 0.0, 48.0, false);
  }
  ExpectNear(step.radius, 764.0,
             "orbit radius recovers after input release");

  // Near-pivot recovery must outlast the repeating 22--24 tick wall-contact
  // interval observed while orbiting inside a tight corner. Otherwise the
  // arm grows by several 64-unit steps and the same face immediately snaps it
  // back to the close radius on every revolution.
  step = StepCameraSpringArm(
      1400.0, 1400.0, 90.0, false, 0, 0, 90.0, kWallBlocker, 0u,
      30u, 30u, 16.0, false);
  for (uint32_t clear_tick = 1; clear_tick < 24u; ++clear_tick) {
    ExpectNear(step.radius, 90.0,
               "near-pivot recovery holds between repeated contacts");
    step = StepCameraSpringArm(
        1400.0, 1400.0, step.radius, false, step.clear_ticks,
        step.blocked_release_ticks, step.blocked_candidate_distance,
        step.blocker_key, 0u, 30u, 30u, 16.0, false);
  }
  ExpectNear(step.radius, 90.0,
             "near-pivot recovery outlasts wall orbit period");
  step = StepCameraSpringArm(
      1400.0, 90.0, step.radius, true, step.clear_ticks,
      step.blocked_release_ticks, step.blocked_candidate_distance,
      step.blocker_key, kWallBlocker, 30u, 30u, 16.0, false);
  ExpectNear(step.radius, 90.0,
             "repeated near-pivot contact remains idempotent");
  for (uint32_t clear_tick = 0; clear_tick < 30u; ++clear_tick) {
    step = StepCameraSpringArm(
        1400.0, 1400.0, step.radius, false, step.clear_ticks,
        step.blocked_release_ticks, step.blocked_candidate_distance,
        step.blocker_key, 0u, 30u, 30u, 16.0, false);
  }
  ExpectNear(step.radius, 106.0,
             "near-pivot recovery resumes after stable clearance");

  // Explicit orbit input is a request to search for a different current ray.
  // A close camera must not require the stationary-wall 30-tick hold on every
  // angle and remain trapped inside the actor for a complete rotation.
  step = StepCameraSpringArm(
      1400.0, 1400.0, 33.0, false, 0u, 0u, 33.0, 0u, 0u,
      2u, 2u, 96.0, false);
  ExpectNear(step.radius, 33.0,
             "orbit close recovery confirms first clear ray");
  step = StepCameraSpringArm(
      1400.0, 1400.0, step.radius, false, step.clear_ticks,
      step.blocked_release_ticks, step.blocked_candidate_distance,
      step.blocker_key, 0u, 2u, 2u, 96.0, false);
  ExpectNear(step.radius, 129.0,
             "orbit close recovery exits actor after confirmation");
  step = StepCameraSpringArm(
      1400.0, 34.0, step.radius, true, step.clear_ticks,
      step.blocked_release_ticks, step.blocked_candidate_distance,
      step.blocker_key, kWallBlocker, 2u, 2u, 96.0, false);
  ExpectNear(step.radius, 34.0,
             "orbit close recovery keeps immediate wall contraction");

  step = StepCameraSpringArm(
      1400.0, 1400.0, 1400.0, false, 0, 0, 1400.0, 0u, 0u,
      10u, 8u, 64.0, false, 200.0, 256.0);
  ExpectNear(step.radius, 1144.0,
             "predictive doorway contraction starts before contact");
  step = StepCameraSpringArm(
      1400.0, 1400.0, step.radius, false, step.clear_ticks,
      step.blocked_release_ticks, step.blocked_candidate_distance,
      step.blocker_key, 0u, 10u, 8u, 64.0, false, 200.0, 256.0);
  ExpectNear(step.radius, 888.0,
             "predictive doorway contraction remains rate limited");
  step = StepCameraSpringArm(
      1400.0, 600.0, step.radius, true, step.clear_ticks,
      step.blocked_release_ticks, step.blocked_candidate_distance,
      step.blocker_key, kWallBlocker, 10u, 8u, 64.0, false,
      200.0, 256.0);
  ExpectNear(step.radius, 600.0,
             "current hard boundary overrides insufficient prediction");
  step = StepCameraSpringArm(
      1400.0, 1400.0, 1400.0, false, 0, 0, 1400.0, 0u, 0u,
      10u, 8u, 64.0, false);
  ExpectNear(step.radius, 1400.0,
             "missing doorway prediction preserves clear arm");

  // A direct room boundary can move non-monotonically as the player crosses a
  // convex corner while still leaving a large margin beyond the contracted
  // arm. Owned direct-path recovery must count that sustained margin instead
  // of remaining pinned forever to the historical minimum.
  step = StepCameraSpringArm(1400.0, 1200.0, 300.0, true, 0, 0,
                             300.0, kWallBlocker, kWallBlocker,
                             10u, 8u, 64.0, false);
  const std::array<double, 7> nonmonotonic_clearance = {
      1350.0, 1275.0, 1420.0, 1210.0, 1390.0, 1240.0, 1500.0};
  for (size_t sample = 0; sample < nonmonotonic_clearance.size(); ++sample) {
    ExpectNear(step.radius, 300.0,
               "owned direct margin waits for sustained evidence");
    step = StepCameraSpringArm(
        1400.0, nonmonotonic_clearance[sample], step.radius, true,
        step.clear_ticks, step.blocked_release_ticks,
        step.blocked_candidate_distance, step.blocker_key, kWallBlocker,
        10u, 8u, 64.0, false);
  }
  ExpectNear(step.radius, 364.0,
             "owned direct margin releases despite boundary regression");
  step = StepCameraSpringArm(
      1400.0, 364.0, step.radius, true, step.clear_ticks,
      step.blocked_release_ticks, step.blocked_candidate_distance,
      step.blocker_key, kWallBlocker, 10u, 8u, 64.0, false);
  ExpectTicks(step.blocked_release_ticks, 0,
              "lost direct margin resets release evidence");

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

  constexpr double kPredictionDegrees =
      3.14159265358979323846 / 180.0;
  const CameraOrbitAngularPrediction angular_prediction =
      PredictCameraOrbitAngularMotion(
          {0.0, 0.0, 0.0}, {0.0, 0.0, 1000.0},
          {0.0, 0.0, 0.0},
          {std::sin(5.0 * kPredictionDegrees) * 1000.0, 0.0,
           std::cos(5.0 * kPredictionDegrees) * 1000.0},
          4.0, 18.0 * kPredictionDegrees);
  if (!angular_prediction.valid) {
    std::cerr << "angular camera prediction was invalid\n";
    return 1;
  }
  ExpectNear(
      std::atan2(angular_prediction.position[0],
                 angular_prediction.position[2]) /
          kPredictionDegrees,
      23.0, "angular camera prediction cap");
  ExpectNear(angular_prediction.angular_distance / kPredictionDegrees,
             18.0, "angular camera prediction distance");

  const CameraOrbitAngularPrediction stationary_prediction =
      PredictCameraOrbitAngularMotion(
          {0.0, 0.0, 0.0}, {0.0, 0.0, 1000.0},
          {10.0, 0.0, 0.0}, {10.0, 0.0, 1000.0},
          4.0, 18.0 * kPredictionDegrees);
  if (stationary_prediction.valid) {
    std::cerr << "translation-only motion became angular prediction\n";
    return 1;
  }

  const CameraOrbitAngularPrediction control_prediction =
      PredictCameraOrbitControlMotion(
          {10.0, 0.0, 0.0},
          {10.0 + std::sin(5.0 * kPredictionDegrees) * 1000.0, 0.0,
           std::cos(5.0 * kPredictionDegrees) * 1000.0},
          5.0 * kPredictionDegrees, 0.0, 4.0,
          18.0 * kPredictionDegrees);
  if (!control_prediction.valid) {
    std::cerr << "control-angle camera prediction was invalid\n";
    return 1;
  }
  ExpectNear(
      std::atan2(control_prediction.position[0] - 10.0,
                 control_prediction.position[2]) /
          kPredictionDegrees,
      23.0, "control-angle camera prediction cap");
  ExpectNear(control_prediction.angular_distance / kPredictionDegrees,
             18.0, "control-angle prediction distance");
  const CameraOrbitAngularPrediction no_control_prediction =
      PredictCameraOrbitControlMotion(
          {0.0, 0.0, 0.0}, {0.0, 0.0, 1000.0},
          0.0, 0.0, 4.0, 18.0 * kPredictionDegrees);
  if (no_control_prediction.valid) {
    std::cerr << "stationary controls became angular prediction\n";
    return 1;
  }
  if (!CameraPredictionMayContractWithoutNearPivot(
          false, 288.0, 288.0) ||
      CameraPredictionMayContractWithoutNearPivot(
          false, 287.0, 288.0) ||
      CameraPredictionMayContractWithoutNearPivot(
          true, 900.0, 288.0)) {
    std::cerr << "near-pivot prediction policy failed\n";
    return 1;
  }
  if (CameraAngularPredictionMayContract(
          false, false, 900.0, 288.0) ||
      !CameraAngularPredictionMayContract(
          true, false, 900.0, 288.0) ||
      CameraAngularPredictionMayContract(
          true, true, 900.0, 288.0) ||
      CameraAngularPredictionMayContract(
          true, false, 287.0, 288.0)) {
    std::cerr << "clear-ray angular prediction policy failed\n";
    return 1;
  }

  const CameraContinuousLookAtBasis rear_look_at =
      BuildCameraContinuousLookAtBasis(
          {0.0, 0.0, 10.0}, {0.0, 0.0, 0.0});
  if (!rear_look_at.valid) {
    std::cerr << "rear look-at basis was invalid\n";
    return 1;
  }
  ExpectNear(rear_look_at.rows[0][0], -1.0,
             "rear look-at right X");
  ExpectNear(rear_look_at.rows[1][1], 1.0,
             "rear look-at up Y");
  ExpectNear(rear_look_at.rows[2][2], -1.0,
             "rear look-at forward Z");
  const CameraContinuousLookAtBasis vertical_look_at =
      BuildCameraContinuousLookAtBasis(
          {0.0, -10.0, 0.0}, {0.0, 0.0, 0.0});
  const CameraContinuousLookAtBasis degenerate_look_at =
      BuildCameraContinuousLookAtBasis(
          {1.0, 2.0, 3.0}, {1.0, 2.0, 3.0});
  if (!vertical_look_at.valid || degenerate_look_at.valid) {
    std::cerr << "look-at singularity handling failed\n";
    return 1;
  }

  CameraRadiusOscillationStep oscillation;
  const std::array<double, 4> oscillating_deltas = {
      64.0, -40.0, 52.0, -36.0};
  for (size_t index = 0; index < oscillating_deltas.size(); ++index) {
    oscillation = StepCameraRadiusOscillationDetector(
        oscillation.previous_meaningful_delta,
        oscillation.window_start_tick, oscillation.reversal_count,
        static_cast<uint64_t>(index + 1u), oscillating_deltas[index]);
  }
  if (!oscillation.detected) {
    std::cerr << "rapid radius reversal was not detected\n";
    return 1;
  }
  oscillation = StepCameraRadiusOscillationDetector(
      -36.0, 4u, 2u, 30u, 48.0);
  if (oscillation.detected || oscillation.reversal_count != 1u ||
      oscillation.window_start_tick != 30u) {
    std::cerr << "expired radius reversal window was not reset\n";
    return 1;
  }

  std::cout << "camera spring-arm state tests passed\n";
  return 0;
}
