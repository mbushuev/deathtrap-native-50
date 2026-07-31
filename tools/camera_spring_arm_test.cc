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
  if (CameraMeshBoundsBlockVolume(180.0, 192.0)) {
    std::cerr << "compact lever housing was incorrectly classified as a wall\n";
    return 1;
  }
  if (!CameraMeshBoundsBlockVolume(310.0, 192.0) ||
      !CameraMeshBoundsBlockVolume(1136.0, 192.0)) {
    std::cerr << "large scene blocker was incorrectly ignored\n";
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
