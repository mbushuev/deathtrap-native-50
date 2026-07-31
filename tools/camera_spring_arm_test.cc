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

  auto step = StepCameraSpringArm(1400.0, 420.0, 1400.0, true, 0);
  ExpectNear(step.radius, 420.0, "immediate contraction");

  step = StepCameraSpringArm(1400.0, 420.0, step.radius, true,
                             step.clear_ticks);
  ExpectNear(step.radius, 420.0, "stable contact does not oscillate");

  step = StepCameraSpringArm(1400.0, 1400.0, step.radius, false,
                             step.clear_ticks);
  ExpectNear(step.radius, 420.0, "one missing snapshot is held");
  ExpectTicks(step.clear_ticks, 1, "first clear tick");

  step = StepCameraSpringArm(1400.0, 1400.0, step.radius, false,
                             step.clear_ticks);
  ExpectNear(step.radius, 484.0, "bounded clear-space release");

  step = StepCameraSpringArm(1400.0, 500.0, step.radius, true,
                             step.clear_ticks);
  ExpectNear(step.radius, 500.0, "moving boundary release");

  step = StepCameraSpringArm(1400.0, 450.0, step.radius, true,
                             step.clear_ticks);
  ExpectNear(step.radius, 450.0, "new inward boundary is authoritative");

  step = StepCameraSpringArm(1400.0, 0.0, step.radius, true,
                             step.clear_ticks);
  ExpectNear(step.radius, 0.0, "verified pivot overlap is safe");

  step = StepCameraSpringArm(1400.0, 1600.0, 1390.0, false, 1);
  ExpectNear(step.radius, 1400.0, "never extends beyond desired arm");

  std::cout << "camera spring-arm state tests passed\n";
  return 0;
}
