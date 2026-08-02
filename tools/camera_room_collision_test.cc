#include "camera_room_collision.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

using deathtrap_camera::RoomPlane;
using deathtrap_camera::RoomOrbitCandidateOffset;
using deathtrap_camera::RoomPortal;
using deathtrap_camera::RoomSector;
using deathtrap_camera::RoomVec3;
using deathtrap_camera::SelectRoomOrbitPlan;
using deathtrap_camera::ShouldUseRoomOrbitSafetyCut;
using deathtrap_camera::SweepSphereThroughRooms;

void ExpectNear(double actual, double expected, const char* label) {
  if (std::abs(actual - expected) > 0.001) {
    std::cerr << label << ": expected " << expected << ", got " << actual
              << '\n';
    std::exit(1);
  }
}

RoomSector Box(double minimum_x, double maximum_x, double minimum_y,
               double maximum_y, double minimum_z, double maximum_z,
               uint64_t key) {
  RoomSector sector;
  sector.key = key;
  sector.solid_planes = {
      {{minimum_x, 0.0, 0.0}, {1.0, 0.0, 0.0}},
      {{maximum_x, 0.0, 0.0}, {-1.0, 0.0, 0.0}},
      {{0.0, minimum_y, 0.0}, {0.0, 1.0, 0.0}},
      {{0.0, maximum_y, 0.0}, {0.0, -1.0, 0.0}},
      {{0.0, 0.0, minimum_z}, {0.0, 0.0, 1.0}},
      {{0.0, 0.0, maximum_z}, {0.0, 0.0, -1.0}},
  };
  return sector;
}

}  // namespace

int main() {
  {
    const std::vector<RoomSector> sectors = {Box(-10, 10, -10, 10, -10, 10, 1)};
    const auto clear = SweepSphereThroughRooms(
        sectors, 0, {0, 0, 0}, {5, 0, 0}, 1.0, 0.0);
    if (!clear.valid || clear.blocked || clear.sector != 0) {
      std::cerr << "clear in-sector sweep failed\n";
      return 1;
    }
    ExpectNear(clear.position.x, 5.0, "clear endpoint");

    const auto wall = SweepSphereThroughRooms(
        sectors, 0, {0, 0, 0}, {20, 0, 0}, 1.0, 0.0);
    if (!wall.valid || !wall.blocked) {
      std::cerr << "solid wall did not block\n";
      return 1;
    }
    ExpectNear(wall.position.x, 9.0, "sphere wall boundary");

    const auto repeated = SweepSphereThroughRooms(
        sectors, 0, {0, 0, 0}, wall.position, 1.0, 0.0);
    if (!repeated.valid || repeated.blocked) {
      std::cerr << "accepted boundary was not idempotent\n";
      return 1;
    }
    ExpectNear(repeated.position.x, 9.0, "idempotent boundary");
  }

  {
    std::vector<RoomSector> sectors = {
        Box(-10, 0, -10, 10, -10, 10, 10),
        Box(0, 10, -10, 10, -10, 10, 11),
    };
    // The shared X plane is a portal, not a solid face.
    sectors[0].solid_planes.erase(sectors[0].solid_planes.begin() + 1);
    sectors[1].solid_planes.erase(sectors[1].solid_planes.begin());
    sectors[0].portals.push_back(
        {{{0, 0, 0}, {-1, 0, 0}}, 1, true, 100});
    sectors[1].portals.push_back(
        {{{0, 0, 0}, {1, 0, 0}}, 0, true, 101});

    const auto through = SweepSphereThroughRooms(
        sectors, 0, {-5, 0, 0}, {5, 0, 0}, 1.0, 0.0);
    if (!through.valid || through.blocked || through.sector != 1 ||
        through.portal_transitions != 1) {
      std::cerr << "open portal traversal failed\n";
      return 1;
    }
    ExpectNear(through.position.x, 5.0, "portal endpoint");

    sectors[0].portals[0].open = false;
    const auto closed = SweepSphereThroughRooms(
        sectors, 0, {-5, 0, 0}, {5, 0, 0}, 1.0, 0.0);
    if (!closed.valid || !closed.blocked) {
      std::cerr << "closed portal did not become solid\n";
      return 1;
    }
    ExpectNear(closed.position.x, -1.0, "closed portal boundary");
  }

  {
    const std::vector<RoomSector> sectors = {Box(-10, 10, -10, 10, -10, 10, 5)};
    const auto escaping = SweepSphereThroughRooms(
        sectors, 0, {9.5, 0, 0}, {0, 0, 0}, 1.0, 0.0);
    if (!escaping.valid || escaping.blocked || !escaping.started_overlapping) {
      std::cerr << "outward initial-overlap escape failed\n";
      return 1;
    }
    ExpectNear(escaping.position.x, 0.0, "overlap escape endpoint");

    const auto deeper = SweepSphereThroughRooms(
        sectors, 0, {9.5, 0, 0}, {12, 0, 0}, 1.0, 0.0);
    if (!deeper.valid || !deeper.blocked || !deeper.started_overlapping) {
      std::cerr << "deeper initial overlap was not blocked\n";
      return 1;
    }
    ExpectNear(deeper.position.x, 9.5, "overlap pin");
  }

  {
    const std::vector<RoomSector> sectors = {
        Box(-2, 2, -10, 10, -10, 10, 20)};
    constexpr double kHalfPi = 1.5707963267948966;
    const std::vector<RoomOrbitCandidateOffset> offsets = {
        {0.0, 0.0}, {kHalfPi, 0.0}, {-kHalfPi, 0.0}};
    const auto plan = SelectRoomOrbitPlan(
        sectors, 0, {0, 0, 0}, {8, 0, 0}, 0.5, 5.0, offsets,
        std::numeric_limits<size_t>::max(), 0.0, 0.0);
    if (!plan.valid || !plan.avoidance_required ||
        plan.selected_index != 1u) {
      std::cerr << "near-pivot orbit escape was not selected\n";
      return 1;
    }
    ExpectNear(plan.candidates[0].safe_distance, 1.5,
               "direct near-pivot distance");
    ExpectNear(plan.candidates[1].safe_distance, 8.0,
               "escaped orbit distance");

    const auto retained = SelectRoomOrbitPlan(
        sectors, 0, {0, 0, 0}, {8, 0, 0}, 0.5, 5.0, offsets, 2u, 1.0,
        0.0);
    if (!retained.valid || !retained.retained_previous ||
        retained.selected_index != 2u) {
      std::cerr << "safe angular-side hysteresis was not retained\n";
      return 1;
    }

    const std::vector<RoomSector> open_sector = {
        Box(-10, 10, -10, 10, -10, 10, 21)};
    const auto direct = SelectRoomOrbitPlan(
        open_sector, 0, {0, 0, 0}, {0, 0, 8}, 0.5, 5.0, offsets, 2u,
        1.0, 0.0);
    if (!direct.valid || direct.avoidance_required ||
        direct.selected_index != 0u || direct.candidates.size() != 1u) {
      std::cerr << "useful direct shot did not release avoidance\n";
      return 1;
    }
  }

  {
    // A focus inside two camera-radius margins has no escape in the requested
    // direction or either 90-degree side. The opposite orbit direction is the
    // only useful shot and must remain discoverable instead of collapsing the
    // camera into the player.
    const std::vector<RoomSector> corner = {
        Box(0, 10, -10, 10, 0, 10, 30)};
    constexpr double kHalfPi = 1.5707963267948966;
    constexpr double kPi = 3.1415926535897932;
    const std::vector<RoomOrbitCandidateOffset> offsets = {
        {0.0, 0.0}, {kHalfPi, 0.0}, {-kHalfPi, 0.0}, {kPi, 0.0}};
    const auto plan = SelectRoomOrbitPlan(
        corner, 0, {0.4, 0, 0.4}, {-5, 0, -5}, 0.5, 5.0, offsets,
        std::numeric_limits<size_t>::max(), 0.5, 0.0);
    if (!plan.valid || plan.selected_index != 3u ||
        plan.candidates[3].safe_distance < 5.0) {
      std::cerr << "full-orbit corner escape was not selected\n";
      return 1;
    }
  }

  if (!ShouldUseRoomOrbitSafetyCut(95.0, 650.0, 288.0) ||
      ShouldUseRoomOrbitSafetyCut(300.0, 650.0, 288.0) ||
      ShouldUseRoomOrbitSafetyCut(95.0, 287.0, 288.0)) {
    std::cerr << "near-pivot safety-cut policy failed\n";
    return 1;
  }

  std::cout << "camera room collision tests passed\n";
  return 0;
}
