#include <cmath>
#include <cstdlib>
#include <iostream>

#include "immersive_first_person.h"

namespace {

bool Near(double a, double b, double epsilon = 1.0e-9) {
  return std::abs(a - b) <= epsilon;
}

void Require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "immersive first-person test failed: " << message << '\n';
    std::exit(1);
  }
}

}  // namespace

int main() {
  constexpr double kPi = 3.14159265358979323846;
  const std::array<int32_t, 3> root = {100, 200, 300};

  Require(MatchesImmersiveHeadJoint(1, 0, 3, 76, 5),
          "female head with one braid child must resolve");
  Require(MatchesImmersiveHeadJoint(0, 0, 3, 90, 5),
          "measured male leaf head must resolve without a braid child");
  // Pose coordinates are intentionally absent from the predicate, so the
  // measured attack transition from -1/100/21 to -1/99/13 cannot alter
  // structural head identity.
  Require(!MatchesImmersiveHeadJoint(2, 0, 3, 76, 5),
          "branching hand or equipment node must not resolve as a head");
  Require(CountsTowardImmersiveJointTopology(0xE58u, 0xE58u),
          "female braid child must count toward head topology");
  Require(!CountsTowardImmersiveJointTopology(0xE58u, 0xE00u),
          "embedded arrow attachment must not count as a head child");
  Require(!MatchesImmersiveHeadJoint(0, 1, 3, 76, 5),
          "render-bearing parent must not resolve as a neck anchor");
  Require(!MatchesImmersiveHeadJoint(0, 0, 2, 90, 5),
          "non-branching torso ancestry must not resolve as a head");
  Require(!MatchesImmersiveHeadJoint(0, 0, 3, 44, 5) &&
              !MatchesImmersiveHeadJoint(0, 0, 3, 116, 5),
          "out-of-envelope bounds must not resolve as a head");
  Require(ImmersivePhysicalMouseVerticalSign(false) == -1.0,
          "default immersive physical mouse Y must reverse at look-at");
  Require(ImmersivePhysicalMouseVerticalSign(true) == 1.0,
          "InvertY must reverse the immersive physical mouse default");
  Require(Near(ImmersiveOrbitYawFromPlayerHeading(512, 1024), 0.0),
          "negative-Z body course must seed a zero radial yaw");
  Require(Near(ImmersiveOrbitYawFromPlayerHeading(0, 1024), -kPi),
          "positive-Z body course must seed the opposite radial yaw");
  Require(Near(ImmersiveOrbitYawFromPlayerHeading(521, 1024),
               9.0 * 2.0 * kPi / 1024.0),
          "live Q10 heading must preserve its fine angular offset");
  Require(std::isnan(ImmersiveOrbitYawFromPlayerHeading(0, 0)),
          "invalid heading scale must fail closed");
  Require(Near(ThirdPersonOrbitYawFromImmersiveYaw(0.0), kPi),
          "head-forward zero must place third person a half-turn behind");
  Require(Near(ThirdPersonOrbitYawFromImmersiveYaw(-kPi), 0.0),
          "opposite head direction must wrap to zero orbit yaw");
  Require(std::isnan(ThirdPersonOrbitYawFromImmersiveYaw(
              std::numeric_limits<double>::quiet_NaN())),
          "invalid immersive yaw must fail closed");

  Require(!ImmersiveLocomotionRequiresVectorRouting(0.0, 1.0) &&
              !ImmersiveLocomotionRequiresVectorRouting(0.10, 1.0) &&
              !ImmersiveLocomotionRequiresVectorRouting(-0.10, -1.0),
          "straight longitudinal movement and axial stick noise must stay native");
  Require(ImmersiveLocomotionRequiresVectorRouting(0.21, 1.0) &&
              ImmersiveLocomotionRequiresVectorRouting(1.0, 1.0) &&
              ImmersiveLocomotionRequiresVectorRouting(1.0, 0.0),
          "deliberate diagonal and lateral movement must retain vector routing");
  Require(!ImmersiveLocomotionRequiresVectorRouting(
              std::numeric_limits<double>::quiet_NaN(), 1.0),
          "invalid immersive movement input must fail closed");

  const ImmersiveLocomotionPlan forward_motion =
      BuildImmersiveLocomotionPlan(512, false, 0.75, 1024);
  Require(forward_motion.active && forward_motion.motion_heading == 512 &&
              forward_motion.transaction_heading == 512 &&
              forward_motion.native_axis_milli == 750,
          "forward motion must align root and collision courses");
  const ImmersiveLocomotionPlan backward_motion =
      BuildImmersiveLocomotionPlan(0, true, 1.0, 1024);
  Require(backward_motion.active && backward_motion.motion_heading == 0 &&
              backward_motion.transaction_heading == 512 &&
              backward_motion.native_axis_milli == -1000,
          "backward collision course must oppose the requested world course");
  const ImmersiveLocomotionPlan pure_lateral =
      BuildImmersiveLocomotionPlan(256, false, 1.0, 1024);
  Require(pure_lateral.active && pure_lateral.motion_heading == 256 &&
              pure_lateral.transaction_heading == 256 &&
              pure_lateral.native_axis_milli == 1000,
          "pure lateral input must use full-speed forward root motion");
  Require(!BuildImmersiveLocomotionPlan(0, false, 0.0, 1024).active &&
              !BuildImmersiveLocomotionPlan(0, false, 1.0, 0).active,
          "zero motion or invalid heading units must fail closed");

  const ImmersiveRootMotionInput root_left =
      BuildImmersiveRootMotionInput(0, 10, 16384, 0, 0, 16384,
                                    768, 1024);
  Require(root_left.active && root_left.local_x == -10 &&
              root_left.local_z == 0,
          "identity root basis must redirect forward motion to world left");
  const ImmersiveRootMotionInput root_forward =
      BuildImmersiveRootMotionInput(0, 10, 16384, 0, 0, 16384,
                                    512, 1024);
  Require(root_forward.active && root_forward.local_x == 0 &&
              root_forward.local_z == -10,
          "identity root basis must redirect motion toward negative Z");
  const ImmersiveRootMotionInput rotated_basis =
      BuildImmersiveRootMotionInput(0, 10, 0, -16384, 16384, 0,
                                    512, 1024);
  Require(rotated_basis.active && rotated_basis.local_x == 10 &&
              rotated_basis.local_z == 0,
          "root solve must invert the cached actor basis");
  Require(!BuildImmersiveRootMotionInput(
               0, 10, 0, 0, 0, 0, 512, 1024).active,
          "degenerate root basis must fail closed");

  const ImmersiveFirstPersonPose forward =
      BuildImmersiveFirstPersonPose(root, 0.0, 0.0, 60, 120);
  Require(forward.valid, "forward pose must be valid");
  Require(forward.eye == std::array<int32_t, 3>{100, 260, 180},
          "yaw zero eye must be 120 units toward negative Z");
  Require(Near(forward.forward[0], 0.0) &&
              Near(forward.forward[1], 0.0) &&
              Near(forward.forward[2], -1.0),
          "yaw zero look vector must face negative Z");
  const std::array<double, 3> engine_look_at =
      ImmersiveFirstPersonLookAtVector(forward.forward);
  Require(Near(engine_look_at[0], 0.0) &&
              Near(engine_look_at[1], 0.0) &&
              Near(engine_look_at[2], 1.0),
          "Dungeon look-at input must reverse the visible view vector");

  const std::array<int32_t, 3> moved_anchor = {137, 211, 209};
  const ImmersiveFirstPersonPose moved =
      BuildImmersiveFirstPersonPose(moved_anchor, 0.0, 0.0, 60, 120);
  Require(moved.eye[0] - forward.eye[0] == 37 &&
              moved.eye[1] - forward.eye[1] == 11 &&
              moved.eye[2] - forward.eye[2] == -91,
          "eye must preserve the complete player-focus translation");

  const ImmersiveFirstPersonPose right =
      BuildImmersiveFirstPersonPose(root, -kPi / 2.0, 0.0, 60, 120);
  Require(right.eye == std::array<int32_t, 3>{220, 260, 300},
          "negative quarter-turn must place the eye toward positive X");
  Require(Near(right.forward[0], 1.0) && Near(right.forward[2], 0.0),
          "negative quarter-turn must look toward positive X");

  const ImmersiveFirstPersonPose up =
      BuildImmersiveFirstPersonPose(root, 0.0, -kPi / 6.0, 60, 120);
  Require(Near(up.forward[1], 0.5),
          "negative pitch must look upward");
  Require(up.eye == forward.eye,
          "pitch must not pull the eye back into the body");

  const ImmersiveFirstPersonPose invalid =
      BuildImmersiveFirstPersonPose(root, 0.0, 0.0, -1, 120);
  Require(!invalid.valid, "negative eye height must fail closed");

  const ImmersiveFirstPersonPose mounted =
      BuildImmersiveHeadMountedPose(root, 0.0, 0.0, 10, 55);
  Require(mounted.valid, "head-mounted pose must be valid");
  Require(mounted.eye == std::array<int32_t, 3>{100, 210, 355},
          "head-mounted eye must advance along Dungeon's published view");
  Require(mounted.forward == forward.forward,
          "head mounting must not change user-owned look rotation");

  const ImmersiveFirstPersonPose mounted_right =
      BuildImmersiveHeadMountedPose(root, -kPi / 2.0, 0.0, 10, 55);
  Require(mounted_right.eye == std::array<int32_t, 3>{45, 210, 300},
          "head-mounted eye must track the published horizontal heading");

  const ImmersiveFirstPersonPose mounted_up_limit =
      BuildImmersiveHeadMountedPose(root, 0.0, -75.0 * kPi / 180.0, 10, 55);
  Require(mounted_up_limit.valid &&
              mounted_up_limit.eye ==
                  std::array<int32_t, 3>{100, 210, 355},
          "head mount must remain valid at the upward pitch limit");
  const ImmersiveFirstPersonPose mounted_down_limit =
      BuildImmersiveHeadMountedPose(root, 0.0, 75.0 * kPi / 180.0, 10, 55);
  Require(mounted_down_limit.valid &&
              mounted_down_limit.eye ==
                  std::array<int32_t, 3>{100, 210, 355},
          "head mount must remain valid at the downward pitch limit");

  std::cout << "immersive first-person pose tests passed\n";
  return 0;
}
