#pragma once

#include <algorithm>
#include <array>
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

struct RoomOrbitCandidateOffset {
  double yaw_radians = 0.0;
  double pitch_radians = 0.0;
};

struct RoomOrbitCandidate {
  RoomOrbitCandidateOffset offset{};
  RoomVec3 requested{};
  RoomSweepResult sweep{};
  double safe_distance = 0.0;
  double angular_cost = 0.0;
};

struct RoomOrbitPlan {
  std::vector<RoomOrbitCandidate> candidates;
  size_t selected_index = std::numeric_limits<size_t>::max();
  bool valid = false;
  bool avoidance_required = false;
  bool retained_previous = false;
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
    double contact_backoff = 1.0, size_t maximum_portal_transitions = 16,
    double radius_ramp_distance = 0.0) {
  RoomSweepResult result;
  result.position = start;
  result.sector = start_sector;
  if (start_sector >= sectors.size() || !std::isfinite(radius) ||
      radius < 0.0 || !std::isfinite(contact_backoff) ||
      contact_backoff < 0.0 || !std::isfinite(radius_ramp_distance) ||
      radius_ramp_distance < 0.0) {
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
  const auto radius_at_fraction = [&](double fraction) {
    if (radius_ramp_distance <= detail::kRoomSweepEpsilon) {
      return radius;
    }
    return radius * std::clamp(
        sweep_length * fraction / radius_ramp_distance, 0.0, 1.0);
  };
  const auto plane_contact_fraction = [&](
      double start_distance, double end_distance,
      double from_fraction, bool* started_overlapping,
      double* hit_fraction) {
    if (!started_overlapping || !hit_fraction) {
      return false;
    }
    *started_overlapping = false;
    *hit_fraction = std::numeric_limits<double>::infinity();
    const double ramp_fraction = radius_ramp_distance >
            detail::kRoomSweepEpsilon
        ? std::clamp(radius_ramp_distance / sweep_length, 0.0, 1.0)
        : 0.0;
    std::array<double, 3> boundaries{
        from_fraction,
        std::clamp(ramp_fraction, from_fraction, 1.0),
        1.0};
    size_t segment_count = 2u;
    if (boundaries[1] <= boundaries[0] + detail::kRoomSweepEpsilon ||
        boundaries[1] >= 1.0 - detail::kRoomSweepEpsilon) {
      boundaries[1] = 1.0;
      segment_count = 1u;
    }
    const auto signed_at = [&](double fraction) {
      return start_distance +
          (end_distance - start_distance) * fraction;
    };
    for (size_t segment = 0; segment < segment_count; ++segment) {
      const double a = boundaries[segment];
      const double b = boundaries[segment + 1u];
      if (b <= a + detail::kRoomSweepEpsilon) {
        continue;
      }
      const double clearance_a =
          signed_at(a) - radius_at_fraction(a);
      const double clearance_b =
          signed_at(b) - radius_at_fraction(b);
      if (segment == 0u &&
          clearance_a < -detail::kRoomSweepEpsilon) {
        *started_overlapping = true;
        // Preserve the established depenetration rule. Once clearance is
        // increasing, the radius ramp becomes no steeper after its endpoint,
        // so this plane cannot become a later blocker on the same ray.
        if (clearance_b >
            clearance_a + detail::kRoomSweepEpsilon) {
          return false;
        }
        *hit_fraction = a;
        return true;
      }
      if (clearance_b >= -detail::kRoomSweepEpsilon ||
          clearance_b >= clearance_a) {
        continue;
      }
      const double denominator = clearance_a - clearance_b;
      if (denominator <= detail::kRoomSweepEpsilon) {
        continue;
      }
      *hit_fraction = std::clamp(
          a + (b - a) * clearance_a / denominator, a, b);
      return true;
    }
    return false;
  };
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
      bool started_overlapping = false;
      double hit_fraction = std::numeric_limits<double>::infinity();
      const bool plane_hit = plane_contact_fraction(
          start_distance, end_distance, current_fraction,
          &started_overlapping, &hit_fraction);
      result.started_overlapping =
          result.started_overlapping || started_overlapping;
      if (plane_hit &&
          hit_fraction < earliest_solid) {
        earliest_solid = hit_fraction;
        solid_key = sector.key ^ (0x9E3779B97F4A7C15ull + plane_index);
      }
    }

    for (const RoomPortal& portal : sector.portals) {
      if (portal.open) {
        continue;
      }
      const double start_distance = SignedDistance(portal.plane, start);
      const double end_distance = SignedDistance(portal.plane, desired);
      bool started_overlapping = false;
      double hit_fraction = std::numeric_limits<double>::infinity();
      const bool portal_hit = plane_contact_fraction(
          start_distance, end_distance, current_fraction,
          &started_overlapping, &hit_fraction);
      result.started_overlapping =
          result.started_overlapping || started_overlapping;
      if (portal_hit &&
          hit_fraction < earliest_solid) {
        earliest_solid = hit_fraction;
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
      const double crossing_radius =
          radius_at_fraction(earliest_portal);
      const bool fits_current =
          detail::SphereFitsSector(sector, crossing, crossing_radius);
      const bool fits_neighbor = detail::SphereFitsSector(
          sectors[selected_portal->neighbor], crossing, crossing_radius);
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

inline RoomVec3 RotateRoomOrbit(const RoomVec3& focus,
                                const RoomVec3& requested,
                                const RoomOrbitCandidateOffset& offset) {
  const RoomVec3 delta = requested - focus;
  const double requested_radius = Length(delta);
  const double horizontal = std::hypot(delta.x, delta.z);
  if (!std::isfinite(requested_radius) ||
      requested_radius <= detail::kRoomSweepEpsilon ||
      !std::isfinite(offset.yaw_radians) ||
      !std::isfinite(offset.pitch_radians)) {
    return requested;
  }

  const double yaw = std::atan2(delta.x, delta.z) + offset.yaw_radians;
  constexpr double kMaximumPitch = 1.3089969389957472;  // 75 degrees.
  const double pitch = std::clamp(
      std::atan2(delta.y, std::max(horizontal, detail::kRoomSweepEpsilon)) +
          offset.pitch_radians,
      -kMaximumPitch, kMaximumPitch);
  const double rotated_horizontal = requested_radius * std::cos(pitch);
  return {
      focus.x + std::sin(yaw) * rotated_horizontal,
      focus.y + std::sin(pitch) * requested_radius,
      focus.z + std::cos(yaw) * rotated_horizontal,
  };
}

inline bool ShouldUseRoomOrbitSafetyCut(double applied_safe_distance,
                                        double selected_safe_distance,
                                        double minimum_transition_distance) {
  return std::isfinite(applied_safe_distance) &&
      std::isfinite(selected_safe_distance) &&
      std::isfinite(minimum_transition_distance) &&
      minimum_transition_distance >= 0.0 &&
      applied_safe_distance < minimum_transition_distance &&
      selected_safe_distance >= minimum_transition_distance &&
      selected_safe_distance > applied_safe_distance;
}

// Chooses a collision-safe shot around the current focus without retaining a
// world-space camera point. The caller owns the candidate order: index zero is
// expected to be the unmodified requested orbit, followed by progressively
// less desirable angular alternatives. A candidate first earns up to the
// preferred useful distance; only then does angular displacement break ties.
// This prevents a tiny 15-degree adjustment that still leaves the camera on
// the player's shoulder from beating a wider, actually useful escape shot.
inline RoomOrbitPlan SelectRoomOrbitPlan(
    const std::vector<RoomSector>& sectors, size_t start_sector,
    const RoomVec3& focus, const RoomVec3& requested, double sphere_radius,
    double preferred_useful_distance,
    const std::vector<RoomOrbitCandidateOffset>& offsets,
    size_t previous_selected_index = std::numeric_limits<size_t>::max(),
    double switch_hysteresis_distance = 0.0,
    double contact_backoff = 1.0,
    size_t maximum_portal_transitions = 16,
    bool allow_direct_early_exit = true,
    double radius_ramp_distance = 0.0) {
  RoomOrbitPlan plan;
  if (offsets.empty() || !std::isfinite(preferred_useful_distance) ||
      preferred_useful_distance < 0.0 ||
      !std::isfinite(switch_hysteresis_distance) ||
      switch_hysteresis_distance < 0.0) {
    return plan;
  }

  plan.candidates.reserve(offsets.size());
  size_t best_index = std::numeric_limits<size_t>::max();
  double best_quality = -1.0;
  double best_cost = std::numeric_limits<double>::infinity();
  for (size_t index = 0; index < offsets.size(); ++index) {
    RoomOrbitCandidate candidate;
    candidate.offset = offsets[index];
    candidate.requested = RotateRoomOrbit(focus, requested, offsets[index]);
    candidate.sweep = SweepSphereThroughRooms(
        sectors, start_sector, focus, candidate.requested, sphere_radius,
        contact_backoff, maximum_portal_transitions,
        radius_ramp_distance);
    candidate.safe_distance = candidate.sweep.valid
        ? Length(candidate.sweep.position - focus)
        : 0.0;
    candidate.angular_cost = std::hypot(
        offsets[index].yaw_radians, offsets[index].pitch_radians);
    plan.candidates.push_back(candidate);

    if (!candidate.sweep.valid || !std::isfinite(candidate.safe_distance)) {
      continue;
    }
    if (allow_direct_early_exit && index == 0u &&
        candidate.safe_distance + detail::kRoomSweepEpsilon >=
            preferred_useful_distance) {
      plan.selected_index = 0u;
      plan.valid = true;
      return plan;
    }
    const double quality =
        std::min(candidate.safe_distance, preferred_useful_distance);
    const bool better_quality = quality > best_quality + 1.0e-6;
    const bool equal_quality = std::abs(quality - best_quality) <= 1.0e-6;
    if (better_quality ||
        (equal_quality && candidate.angular_cost < best_cost - 1.0e-9)) {
      best_index = index;
      best_quality = quality;
      best_cost = candidate.angular_cost;
    }
  }

  if (best_index == std::numeric_limits<size_t>::max()) {
    return plan;
  }

  // A completely useful direct shot is always the recovery target. Side
  // hysteresis is only for choosing between competing avoidance shots; it
  // must not turn an old angular offset into a permanent alternate orbit.
  if (best_index != 0u &&
      previous_selected_index < plan.candidates.size() &&
      plan.candidates[previous_selected_index].sweep.valid) {
    const RoomOrbitCandidate& previous =
        plan.candidates[previous_selected_index];
    const double previous_quality =
        std::min(previous.safe_distance, preferred_useful_distance);
    const bool escaping_collapsed_shot =
        previous.safe_distance + detail::kRoomSweepEpsilon <
            sphere_radius * 3.0 &&
        best_quality > previous_quality + detail::kRoomSweepEpsilon;
    if (!escaping_collapsed_shot &&
        previous_quality + switch_hysteresis_distance >= best_quality) {
      best_index = previous_selected_index;
      plan.retained_previous = true;
    }
  }

  plan.selected_index = best_index;
  plan.valid = true;
  plan.avoidance_required = best_index != 0u;
  return plan;
}

}  // namespace deathtrap_camera
