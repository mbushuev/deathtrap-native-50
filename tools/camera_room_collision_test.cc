#include "camera_room_collision.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

using deathtrap_camera::RoomPlane;
using deathtrap_camera::RoomPortal;
using deathtrap_camera::RoomSector;
using deathtrap_camera::RoomVec3;
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

  std::cout << "camera room collision tests passed\n";
  return 0;
}
