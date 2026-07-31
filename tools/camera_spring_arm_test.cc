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
  if (!CameraMeshExtentsBlockVolume({260.0, 340.0, 420.0}, 192.0)) {
    std::cerr << "solid housing was not classified as a blocker\n";
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

  const std::array<int32_t, 3> submitted = {-8198, -1540, 16942};
  const std::array<int32_t, 3> stale_native = {-8229, -1646, 17607};
  if (SelectCameraMeshPresentationTarget(
          false, submitted, stale_native) != submitted) {
    std::cerr << "pre-native mesh contact retained stale publication\n";
    return 1;
  }
  if (SelectCameraMeshPresentationTarget(
          true, submitted, stale_native) != stale_native) {
    std::cerr << "post-native mesh correction was not retained\n";
    return 1;
  }
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

  const std::array<int32_t, 3> follow_step =
      StepCameraPresentationFollow(
          {0, 0, 0}, {1000, 1000, -1000}, 0.35, 128.0, 48.0);
  if (follow_step != std::array<int32_t, 3>{128, 48, -128}) {
    std::cerr << "presentation follow did not respect axis speed limits\n";
    return 1;
  }
  const std::array<int32_t, 3> follow_response =
      StepCameraPresentationFollow(
          {100, 100, 100}, {200, 120, 50}, 0.35, 128.0, 48.0);
  if (follow_response != std::array<int32_t, 3>{135, 107, 82}) {
    std::cerr << "presentation follow response was not proportional\n";
    return 1;
  }
  const std::array<int32_t, 3> follow_settle =
      StepCameraPresentationFollow(
          {100, 100, 100}, {101, 99, 100}, 0.35, 128.0, 48.0);
  if (follow_settle != std::array<int32_t, 3>{101, 99, 100}) {
    std::cerr << "presentation follow did not settle exactly\n";
    return 1;
  }
  const std::array<int32_t, 3> invalid_follow =
      StepCameraPresentationFollow(
          {100, 100, 100}, {200, 200, 200}, 0.0, 128.0, 48.0);
  if (invalid_follow != std::array<int32_t, 3>{100, 100, 100}) {
    std::cerr << "invalid presentation follow parameters changed position\n";
    return 1;
  }
  if (CameraPresentationFollowInputIdle(1239, 1000, 240)) {
    std::cerr << "presentation follow resumed inside manual-input grace\n";
    return 1;
  }
  if (!CameraPresentationFollowInputIdle(1240, 1000, 240)) {
    std::cerr << "presentation follow did not resume after manual-input grace\n";
    return 1;
  }
  if (CameraPresentationFollowInputIdle(999, 1000, 240) ||
      CameraPresentationFollowInputIdle(1240, 0, 240)) {
    std::cerr << "presentation follow accepted invalid input timing\n";
    return 1;
  }
  if (!CameraMeshLatchRetainsPreviousTarget(
          true, true, true, false) ||
      CameraMeshLatchRetainsPreviousTarget(
          true, true, true, true) ||
      !CameraMeshLatchRetainsPreviousTarget(
          true, false, false, true)) {
    std::cerr << "manual orbit did not own a usable latch retarget\n";
    return 1;
  }
  if (!CameraContinuousMeshContactNeedsCommit(
          true, false, true, true, true) ||
      CameraContinuousMeshContactNeedsCommit(
          true, true, true, true, true) ||
      CameraContinuousMeshContactNeedsCommit(
          true, false, false, true, true) ||
      CameraContinuousMeshContactNeedsCommit(
          true, false, true, true, false)) {
    std::cerr << "continuous mesh-contact commit ownership was invalid\n";
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

  auto latch_clear =
      StepCameraMeshPresentationLatchClear(0, false);
  ExpectTicks(latch_clear.clear_ticks, 1,
              "first complete latch-clear sample");
  if (latch_clear.release) {
    std::cerr << "presentation latch released after one clear sample\n";
    return 1;
  }
  latch_clear = StepCameraMeshPresentationLatchClear(
      latch_clear.clear_ticks, true);
  ExpectTicks(latch_clear.clear_ticks, 0,
              "pending native mesh collision resets latch clear evidence");
  if (latch_clear.release) {
    std::cerr << "presentation latch released into pending native collision\n";
    return 1;
  }
  latch_clear = StepCameraMeshPresentationLatchClear(
      latch_clear.clear_ticks, false);
  latch_clear = StepCameraMeshPresentationLatchClear(
      latch_clear.clear_ticks, false);
  if (!latch_clear.release) {
    std::cerr << "presentation latch did not release after two fully clear samples\n";
    return 1;
  }

  auto step = StepCameraSpringArm(1400.0, 420.0, 1400.0, true, 0, 0);
  ExpectNear(step.radius, 420.0, "immediate contraction");

  step = StepCameraSpringArm(1400.0, 420.0, step.radius, true,
                             step.clear_ticks, step.blocked_release_ticks);
  ExpectNear(step.radius, 420.0, "stable contact does not oscillate");

  step = StepCameraSpringArm(1400.0, 1400.0, step.radius, false,
                             step.clear_ticks, step.blocked_release_ticks);
  ExpectNear(step.radius, 420.0, "one missing snapshot is held");
  ExpectTicks(step.clear_ticks, 1, "first clear tick");

  step = StepCameraSpringArm(1400.0, 1400.0, step.radius, false,
                             step.clear_ticks, step.blocked_release_ticks);
  ExpectNear(step.radius, 484.0, "bounded clear-space release");

  step = StepCameraSpringArm(1400.0, 500.0, step.radius, true,
                             step.clear_ticks, step.blocked_release_ticks);
  ExpectNear(step.radius, 484.0, "first blocked outward sample is held");
  ExpectTicks(step.blocked_release_ticks, 1,
              "first blocked outward confirmation");

  step = StepCameraSpringArm(1400.0, 540.0, step.radius, true,
                             step.clear_ticks, step.blocked_release_ticks);
  ExpectNear(step.radius, 484.0, "second blocked outward sample is held");

  step = StepCameraSpringArm(1400.0, 580.0, step.radius, true,
                             step.clear_ticks, step.blocked_release_ticks);
  ExpectNear(step.radius, 548.0, "confirmed moving boundary release");

  step = StepCameraSpringArm(1400.0, 450.0, step.radius, true,
                             step.clear_ticks, step.blocked_release_ticks);
  ExpectNear(step.radius, 450.0, "new inward boundary is authoritative");

  step = StepCameraSpringArm(1400.0, 0.0, step.radius, true,
                             step.clear_ticks, step.blocked_release_ticks);
  ExpectNear(step.radius, 0.0, "verified pivot overlap is safe");

  step = StepCameraSpringArm(1400.0, 1600.0, 1390.0, false, 1, 0);
  ExpectNear(step.radius, 1400.0, "never extends beyond desired arm");

  step = StepCameraSpringArm(1400.0, 182.0, 246.0, true, 0, 0);
  ExpectNear(step.radius, 182.0, "alternating boundary contracts");
  step = StepCameraSpringArm(1400.0, 311.0, step.radius, true,
                             step.clear_ticks, step.blocked_release_ticks);
  ExpectNear(step.radius, 182.0, "one-frame outward boundary is held");
  step = StepCameraSpringArm(1400.0, 182.0, step.radius, true,
                             step.clear_ticks, step.blocked_release_ticks);
  ExpectNear(step.radius, 182.0, "alternating inward boundary stays stable");
  ExpectTicks(step.blocked_release_ticks, 0,
              "alternating boundary resets confirmation");

  std::cout << "camera spring-arm state tests passed\n";
  return 0;
}
