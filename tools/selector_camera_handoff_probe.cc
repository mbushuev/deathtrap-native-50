// Regression test for selector presentation-camera handoff.
// Uses the production interpolation and scheduler helpers, without the game/GPU.
#include "camera_spring_arm.h"
#include "selector_time_dilation.h"
#include <iostream>

int main() {
  using namespace deathtrap::selector_time;
  static_assert(PollPresentationLook(true, true, false, true, false),
                "Head view must reach presentation input");
  static_assert(PollPresentationLook(true, true, true, false, false));
  static_assert(!PollPresentationLook(false, true, true, false, false));
  static_assert(!PollPresentationLook(true, true, false, false, true));
  static_assert(WritePresentationMatrix(false, true, true),
                "Held-angle camera still needs its radial matrix written");
  static_assert(!WritePresentationMatrix(false, true, false));
  static_assert(!WritePresentationMatrix(false, false, true));
  static_assert(WritePresentationMatrix(true, false, false));
  static_assert(UseContinuousOrbitBasis(true, false, false),
                "Moving selector camera must not drop basis when delta is zero");
  static_assert(!UseContinuousOrbitBasis(false, false, false));
  static_assert(UseResponsiveRecovery(true, false),
                "Mouse release must not stop selector recovery");
  static_assert(!UseResponsiveRecovery(false, false));
  // Clear space remains clear when mouse input stops after recovery starts.
  // Keep the two-sample safety confirmation, then reach the full distance
  // without requiring any additional mouse event.
  CameraSpringArmStep recovery{};
  recovery.radius = 400.0;
  for (int tick = 0; tick < 15; ++tick) {
    const bool responsive = UseResponsiveRecovery(true, tick < 2);
    const double before = recovery.radius;
    recovery = StepCameraSpringArm(1400.0, 1400.0, recovery.radius, false,
        recovery.clear_ticks, recovery.blocked_release_ticks,
        recovery.blocked_candidate_distance, recovery.blocker_key, 0,
        responsive ? 2u : 10u, responsive ? 2u : 8u,
        responsive ? 96.0 : 64.0);
    if (tick > 0 && before < 1400.0 && recovery.radius <= before) return 7;
  }
  if (std::abs(recovery.radius - 1400.0) > 0.001) return 8;
  const auto wall = StepCameraSpringArm(1400.0, 300.0, recovery.radius, true,
      recovery.clear_ticks, recovery.blocked_release_ticks);
  if (wall.radius > 300.0) return 9;
  constexpr double pi = 3.14159265358979323846;
  const auto position = [pi](double degrees) {
    const double yaw = degrees * pi / 180.0;
    return std::array<double, 3>{1000.0 * std::sin(yaw), 0.0,
                                 1000.0 * std::cos(yaw)};
  };
  const std::array<double, 3> focus{};
  const auto plan = deathtrap::selector_time::MakePlan(true, true, true,
                                                     16u, 3u, 25u);
  // Last source snapshot starts at yaw=0. Mouse adds 30 degrees during the
  // presentation interval, so the last visible exact frame is yaw=30.
  const double last_visible_yaw = 30.0;
  // The source callback materializes the already visible yaw. Selector mode
  // must hold that current accepted camera instead of interpolating from the
  // older zero-degree source pose and replaying the arc.
  const bool interpolate_camera =
      deathtrap::selector_time::InterpolateCameraAcrossSourceTicks(plan.active);
  const bool route_presentation_basis =
      deathtrap::selector_time::RoutePresentationCameraBasis(plan.active,
                                                              true);
  const auto first = InterpolateCameraPivotRelative(
      focus, position(0), focus, position(30),
      interpolate_camera ? 1.0 / plan.presentation_passes : 1.0);
  const double first_visible_yaw = first.yaw * 180.0 / pi;
  const double jump = first_visible_yaw - last_visible_yaw;
  std::cout << "source_period_ms=" << plan.source_period_ms
            << " passes=" << plan.presentation_passes
            << " last_visible_yaw=" << last_visible_yaw
            << " next_first_yaw=" << first_visible_yaw
            << " backwards_jump=" << jump << '\n';
  if (!first.valid || interpolate_camera || !route_presentation_basis ||
      deathtrap::selector_time::RoutePresentationCameraBasis(false, true) ||
      deathtrap::selector_time::RoutePresentationCameraBasis(true, false) ||
      plan.presentation_passes != 12u ||
      std::abs(first_visible_yaw - 30.0) > 0.00001 ||
      std::abs(jump) > 0.00001) return 1;
  std::cout << "Selector presentation handoff remains continuous.\n";
  // A source-distance change is spread over all presentation phases without
  // rewinding the already displayed 30-degree control angle.
  for (int step = 0; step <= 12; ++step) {
    const double phase = step / 12.0;
    const auto p = InterpolateCameraRadiusAtCurrentAngle(
        400.0, focus, position(30), phase);
    const double radius = std::hypot(std::hypot(p[0], p[2]), p[1]);
    const double yaw = std::atan2(p[0], p[2]) * 180.0 / pi;
    if (std::abs(radius - (400.0 + 600.0 * phase)) > 0.00001 ||
        std::abs(yaw - 30.0) > 0.00001) return 2;
    const auto inward = InterpolateCameraRadiusAtCurrentAngle(
        1600.0, focus, position(30), phase);
    if (std::abs(std::hypot(inward[0], inward[2]) -
                 (1600.0 - 600.0 * phase)) > 0.00001) return 3;
  }
  std::cout << "Selector radius progresses independently of control angle.\n";
  // All movement directions progress every subframe, including source-tick
  // handoff. No angular input is accepted or modified by this helper.
  for (const auto delta : {std::array<double, 3>{120, 0, 0},
                          std::array<double, 3>{-120, 0, 0},
                          std::array<double, 3>{0, 0, 120},
                          std::array<double, 3>{0, 0, -120}}) {
    for (int step = 0; step <= 12; ++step) {
      const auto eye = InterpolateHeadCameraTranslation(focus, delta, step / 12.0);
      for (size_t axis = 0; axis < 3u; ++axis) {
        if (std::abs(eye[axis] - delta[axis] * step / 12.0) > 0.00001)
          return 4;
      }
    }
    const auto next = InterpolateHeadCameraTranslation(
        delta, {2 * delta[0], 2 * delta[1], 2 * delta[2]}, 0.0);
    if (next != delta) return 5;
  }
  std::cout << "Head translation progresses in all four movement directions.\n";
  // A moving third-person pivot must share the character's phase, while the
  // already displayed camera angle stays current (not replayed backwards).
  for (int step = 0; step <= 12; ++step) {
    const double phase = step / 12.0;
    const std::array<double, 3> end_focus{120, 0, -60};
    const auto moving = InterpolateHeadCameraTranslation(focus, end_focus, phase);
    auto end_eye = position(30);
    for (size_t axis = 0; axis < 3; ++axis) end_eye[axis] += end_focus[axis];
    auto eye = InterpolateCameraRadiusAtCurrentAngle(400, end_focus, end_eye, phase);
    for (size_t axis = 0; axis < 3; ++axis) eye[axis] += moving[axis] - end_focus[axis];
    if (std::abs(std::atan2(eye[0] - moving[0], eye[2] - moving[2]) *
                 180.0 / pi - 30.0) > 0.00001 ||
        std::abs(moving[0] - step * 10.0) > 0.00001) return 6;
  }
  std::cout << "Third-person follow pivot progresses without angle rewind.\n";
}
