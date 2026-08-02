#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace deathtrap_camera {

struct RoomVec3 {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

inline RoomVec3 operator+(const RoomVec3& a, const RoomVec3& b) {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}

inline RoomVec3 operator-(const RoomVec3& a, const RoomVec3& b) {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}

inline RoomVec3 operator*(const RoomVec3& value, double scale) {
  return {value.x * scale, value.y * scale, value.z * scale};
}

inline double Dot(const RoomVec3& a, const RoomVec3& b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline double Length(const RoomVec3& value) {
  return std::sqrt(Dot(value, value));
}

struct RoomPlane {
  RoomVec3 point{};
  RoomVec3 inward_normal{};
};

inline double SignedDistance(const RoomPlane& plane,
                             const RoomVec3& point) {
  return Dot(point - plane.point, plane.inward_normal);
}

struct RoomPortal {
  RoomPlane plane{};
  size_t neighbor = std::numeric_limits<size_t>::max();
  bool open = true;
  uint64_t key = 0;
};

struct RoomSector {
  // Normals point into the sector. A sphere is contained when the signed
  // distance from every solid plane is at least its radius.
  std::vector<RoomPlane> solid_planes;
  std::vector<RoomPortal> portals;
  uint64_t key = 0;
};

struct RoomSweepResult {
  RoomVec3 position{};
  size_t sector = std::numeric_limits<size_t>::max();
  double fraction = 0.0;
  bool valid = false;
  bool blocked = false;
  bool started_overlapping = false;
  uint64_t blocker_key = 0;
  size_t portal_transitions = 0;
};

namespace detail {

constexpr double kRoomSweepEpsilon = 1.0e-7;

inline bool SphereFitsSector(const RoomSector& sector, const RoomVec3& point,
                             double radius) {
  for (const RoomPlane& plane : sector.solid_planes) {
    if (SignedDistance(plane, point) + kRoomSweepEpsilon < radius) {
      return false;
    }
  }
  return true;
}

}  // namespace detail

inline RoomSweepResult SweepSphereThroughRooms(
    const std::vector<RoomSector>& sectors, size_t start_sector,
    const RoomVec3& start, const RoomVec3& desired, double radius,
    double contact_backoff = 1.0, size_t maximum_portal_transitions = 16) {
  RoomSweepResult result;
  result.position = start;
  result.sector = start_sector;
  if (start_sector >= sectors.size() || !std::isfinite(radius) ||
      radius < 0.0 || !std::isfinite(contact_backoff) ||
      contact_backoff < 0.0) {
    return result;
  }

  const RoomVec3 delta = desired - start;
  const double sweep_length = Length(delta);
  if (!std::isfinite(sweep_length)) {
    return result;
  }

  result.valid = true;
  if (sweep_length <= detail::kRoomSweepEpsilon) {
    result.fraction = 1.0;
    result.position = desired;
    return result;
  }

  double current_fraction = 0.0;
  size_t current_sector = start_sector;
  for (size_t transition = 0;
       transition <= maximum_portal_transitions; ++transition) {
    if (current_sector >= sectors.size()) {
      result.valid = false;
      return result;
    }
    const RoomSector& sector = sectors[current_sector];
    double earliest_solid = std::numeric_limits<double>::infinity();
    uint64_t solid_key = sector.key;

    for (size_t plane_index = 0; plane_index < sector.solid_planes.size();
         ++plane_index) {
      const RoomPlane& plane = sector.solid_planes[plane_index];
      const double start_distance = SignedDistance(plane, start);
      const double end_distance = SignedDistance(plane, desired);
      const double current_distance =
          start_distance + (end_distance - start_distance) * current_fraction;
      if (current_distance + detail::kRoomSweepEpsilon < radius) {
        result.started_overlapping = true;
        // A focus can legitimately begin inside the camera sphere's margin.
        // If the requested orbit moves away from that plane, let the arm leave
        // the overlap instead of pinning it to the pivot forever.
        if (end_distance > current_distance + detail::kRoomSweepEpsilon) {
          continue;
        }
        earliest_solid = current_fraction;
        solid_key = sector.key ^ (0x9E3779B97F4A7C15ull + plane_index);
        break;
      }
      if (end_distance + detail::kRoomSweepEpsilon >= radius ||
          end_distance >= current_distance) {
        continue;
      }
      const double denominator = start_distance - end_distance;
      if (denominator <= detail::kRoomSweepEpsilon) {
        continue;
      }
      const double hit_fraction =
          (start_distance - radius) / denominator;
      if (hit_fraction + detail::kRoomSweepEpsilon >= current_fraction &&
          hit_fraction < earliest_solid) {
        earliest_solid = std::clamp(hit_fraction, current_fraction, 1.0);
        solid_key = sector.key ^ (0x9E3779B97F4A7C15ull + plane_index);
      }
    }

    for (const RoomPortal& portal : sector.portals) {
      if (portal.open) {
        continue;
      }
      const double start_distance = SignedDistance(portal.plane, start);
      const double end_distance = SignedDistance(portal.plane, desired);
      const double current_distance =
          start_distance + (end_distance - start_distance) * current_fraction;
      if (current_distance + detail::kRoomSweepEpsilon < radius) {
        result.started_overlapping = true;
        if (end_distance > current_distance + detail::kRoomSweepEpsilon) {
          continue;
        }
        earliest_solid = current_fraction;
        solid_key = portal.key;
        break;
      }
      if (end_distance + detail::kRoomSweepEpsilon >= radius ||
          end_distance >= current_distance) {
        continue;
      }
      const double denominator = start_distance - end_distance;
      if (denominator <= detail::kRoomSweepEpsilon) {
        continue;
      }
      const double hit_fraction =
          (start_distance - radius) / denominator;
      if (hit_fraction + detail::kRoomSweepEpsilon >= current_fraction &&
          hit_fraction < earliest_solid) {
        earliest_solid = std::clamp(hit_fraction, current_fraction, 1.0);
        solid_key = portal.key;
      }
    }

    double earliest_portal = std::numeric_limits<double>::infinity();
    const RoomPortal* selected_portal = nullptr;
    for (const RoomPortal& portal : sector.portals) {
      if (!portal.open || portal.neighbor >= sectors.size()) {
        continue;
      }
      const double start_distance = SignedDistance(portal.plane, start);
      const double end_distance = SignedDistance(portal.plane, desired);
      const double current_distance =
          start_distance + (end_distance - start_distance) * current_fraction;
      if (current_distance < -detail::kRoomSweepEpsilon ||
          end_distance >= -detail::kRoomSweepEpsilon ||
          end_distance >= current_distance) {
        continue;
      }
      const double denominator = start_distance - end_distance;
      if (denominator <= detail::kRoomSweepEpsilon) {
        continue;
      }
      const double crossing_fraction = start_distance / denominator;
      if (crossing_fraction + detail::kRoomSweepEpsilon >= current_fraction &&
          crossing_fraction < earliest_portal) {
        earliest_portal = std::clamp(crossing_fraction, current_fraction, 1.0);
        selected_portal = &portal;
      }
    }

    if (selected_portal &&
        earliest_portal <= earliest_solid + detail::kRoomSweepEpsilon) {
      const RoomVec3 crossing = start + delta * earliest_portal;
      const bool fits_current =
          detail::SphereFitsSector(sector, crossing, radius);
      const bool fits_neighbor = detail::SphereFitsSector(
          sectors[selected_portal->neighbor], crossing, radius);
      if (!fits_current || !fits_neighbor) {
        // The centre can see through the portal but the camera sphere does not
        // fit through its convex aperture. Treat the portal face as the
        // blocker; do not oscillate between the two sectors.
        earliest_solid = earliest_portal;
        solid_key = selected_portal->key;
      } else {
        current_sector = selected_portal->neighbor;
        current_fraction = std::min(
            1.0, earliest_portal + detail::kRoomSweepEpsilon);
        result.portal_transitions += 1;
        if (current_fraction >= 1.0) {
          result.position = desired;
          result.sector = current_sector;
          result.fraction = 1.0;
          return result;
        }
        continue;
      }
    }

    if (std::isfinite(earliest_solid)) {
      const double backed_fraction = std::max(
          current_fraction,
          earliest_solid - contact_backoff / sweep_length);
      result.position = start + delta * backed_fraction;
      result.sector = current_sector;
      result.fraction = backed_fraction;
      result.blocked = true;
      result.blocker_key = solid_key;
      return result;
    }

    result.position = desired;
    result.sector = current_sector;
    result.fraction = 1.0;
    return result;
  }

  result.valid = false;
  result.position = start + delta * current_fraction;
  result.sector = current_sector;
  result.fraction = current_fraction;
  return result;
}

}  // namespace deathtrap_camera
