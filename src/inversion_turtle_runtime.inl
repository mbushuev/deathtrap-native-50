constexpr DungeonCodeRva kInversionPlayerLoadPlacement{0x82BCDu};
constexpr DungeonCodeRva kInversionPlacePlayer{0x44C90u};
constexpr DungeonCodeRva kInversionFloorProbe{0x69910u};
constexpr DungeonCodeRva kInversionTurtleFlight{0x98CF0u};
constexpr DungeonCodeRva kInversionUpdatePlatformRooms{0x66660u};
constexpr DungeonCodeRva kInversionRefreshPlatformCollision{0x4A190u};
constexpr DungeonCodeRva kInversionPlatformByIndex{0x78220u};
constexpr DungeonCodeRva kInversionPlatformCleanup{0x78280u};
constexpr uint8_t kInversionFlightPhase = 2;

using InversionPosition = std::array<int32_t, 3>;

struct InversionFloorContact {
  int32_t height;
  uintptr_t supporting_actor;
  std::array<uint32_t, 6> detail;
};

struct InversionTurtleWaypoint {
  InversionTurtleWaypoint* next;
  uint32_t unused;
  InversionPosition position;
  uint32_t tag;
  int32_t wait;
  uintptr_t occupant;
};

struct InversionTurtlePose {
  InversionPosition position;
  std::array<uint32_t, 3> padding;
  InversionPosition angles;
};

struct InversionTurtleActor {
  uintptr_t update;
  std::array<uintptr_t, 3> actor_links;
  uintptr_t model_link;
  std::array<uintptr_t, 5> actor_state;
  uintptr_t collision;
  uint8_t phase, model, bobbing, flags;
  uint32_t route;
  std::array<int32_t, 3> bobbing_state;
  int32_t wait;
  InversionTurtleWaypoint* waypoint;
  InversionTurtlePose* pose;
  std::array<uint32_t, 19> motion;
};

static_assert(offsetof(InversionTurtleActor, phase) == 0x2C);
static_assert(offsetof(InversionTurtleActor, collision) == 0x28);
static_assert(offsetof(InversionTurtleActor, waypoint) == 0x44);
static_assert(sizeof(InversionTurtleActor) == 0x98);
static_assert(sizeof(InversionTurtleWaypoint) == 0x20);

struct InversionTurtlePlacement {
  InversionTurtleActor actor{};
  InversionTurtlePose pose{};
  int64_t score = std::numeric_limits<int64_t>::max();
  unsigned ticks = 0;
};

uintptr_t g_inversion_platform_count = 0;
uintptr_t g_inversion_platform_table = 0;

InversionFloorContact InversionFloorAt(InversionPosition point, uintptr_t room) {
  using Probe = void(__cdecl*)(const InversionPosition*, uintptr_t, InversionFloorContact*);
  InversionFloorContact contact{};
  reinterpret_cast<Probe>(g_dungeon_base + kInversionFloorProbe)(
      &point, room, &contact);
  return contact;
}

InversionTurtlePlacement FindInversionRidingPose(
    const InversionTurtleActor& actor, const InversionTurtlePose& pose,
    const InversionPosition& player) {
  std::array<InversionTurtleWaypoint, 32> route{};
  std::array<InversionTurtleWaypoint*, route.size()> originals{};
  size_t length = 0;
  auto* waypoint = actor.waypoint;
  do {
    if (!waypoint || length == route.size() ||
        !SafeReadValue(waypoint, &route[length])) {
      return {};
    }
    originals[length] = waypoint;
    waypoint = route[length++].next;
  } while (waypoint != actor.waypoint);
  for (size_t i = 0; i < length; ++i) {
    route[i].next = &route[(i + 1) % length];
    route[i].occupant = 0;
  }

  // A pose with zero animation nodes and a detached collision record let the
  // native flight update advance a copy without touching live models or rooms.
  struct {
    InversionTurtlePose pose;
    std::array<uint32_t, (0x70 - sizeof(InversionTurtlePose)) / 4> animation{};
  } preview{pose};
  InversionTurtlePose* model = &preview.pose;
  InversionTurtleActor flight = actor;
  flight.model_link = reinterpret_cast<uintptr_t>(&model);
  flight.collision = 0;
  flight.waypoint = route.data();
  flight.pose = &preview.pose;

  const auto advance = reinterpret_cast<void(__cdecl*)(InversionTurtleActor*)>(
      g_dungeon_base + kInversionTurtleFlight);
  InversionTurtlePlacement best;
  for (unsigned tick = 1; tick <= 1600; ++tick) {
    advance(&flight);
    if (flight.phase != kInversionFlightPhase) {
      if (tick != 1) break;
      // Position loading can run before the newborn turtle's first update.
      // The first update seats it at its dock and the native launch starts
      // the next leg from that dock.
      flight.phase = kInversionFlightPhase;
      flight.waypoint = flight.waypoint->next;
      flight.wait = flight.waypoint->wait;
    }
    const int64_t dx = static_cast<int64_t>(player[0]) - preview.pose.position[0];
    const int64_t dy = static_cast<int64_t>(player[1]) - preview.pose.position[1];
    const int64_t dz = static_cast<int64_t>(player[2]) - preview.pose.position[2];
    if (std::abs(dx) > 450 || std::abs(dz) > 450 || dy < 100 || dy > 550) continue;
    const int64_t shell_height_error = dy - 300;
    const int64_t score = dx * dx + dz * dz + shell_height_error * shell_height_error + tick;
    if (score >= best.score) continue;
    best = {flight, preview.pose, score, tick};
    best.actor.model_link = actor.model_link;
    best.actor.collision = actor.collision;
    best.actor.pose = actor.pose;
    best.actor.waypoint = originals[flight.waypoint - route.data()];
  }
  return best;
}

bool ApplyInversionTurtlePose(InversionTurtleActor* live,
                              const InversionTurtleActor& actor,
                              const InversionTurtlePose& pose) {
  constexpr size_t kFlightOffset = offsetof(InversionTurtleActor, phase);
  constexpr size_t kFlightSize = sizeof(InversionTurtleActor) - kFlightOffset;
  if (!SafeWrite(reinterpret_cast<uint8_t*>(live) + kFlightOffset,
                 reinterpret_cast<const uint8_t*>(&actor) + kFlightOffset, kFlightSize) ||
      !SafeWrite(actor.pose, &pose, sizeof(pose))) return false;

  // Rebuild the moved model's local and world transforms for the native
  // collision query that runs before the player is placed in the world.
  auto* flags = reinterpret_cast<uint32_t*>(actor.pose + 1);
  uint32_t cached = 0;
  if (!SafeReadValue(flags, &cached)) return false;
  cached &= ~0x130000u;
  if (!SafeWrite(flags, &cached, sizeof(cached))) return false;
  reinterpret_cast<RenderCacheUpdateFn>(
      g_dungeon_base + kCameraNodeLocalUpdateRva)(actor.pose);
  reinterpret_cast<RenderCacheUpdateFn>(
      g_dungeon_base + kCameraNodeWorldUpdateRva)(actor.pose);
  reinterpret_cast<void(__cdecl*)(InversionTurtleActor*)>(
      g_dungeon_base + kInversionUpdatePlatformRooms)(live);
  uintptr_t collision_cache = 0;
  if (actor.collision &&
      SafeReadValue(reinterpret_cast<const void*>(actor.collision + 0xCu), &collision_cache) &&
      collision_cache) {
    // The first floor query may have cached the platform's hull at its dock
    // for this same frame. Rebuild it in place after moving the platform.
    reinterpret_cast<void(__cdecl*)(InversionTurtleActor*)>(
        g_dungeon_base + kInversionRefreshPlatformCollision)(live);
  }
  return true;
}

void RestoreInversionRidingSupport(const InversionPosition& position, uintptr_t room) {
  constexpr int32_t kRestoreHeightTolerance = 110;
  const int64_t saved_height = position[1];
  if (std::abs(saved_height - InversionFloorAt(position, room).height) <= kRestoreHeightTolerance) {
    return;
  }

  int32_t count = 0;
  InversionTurtleActor** table = nullptr;
  if (!SafeReadValue(reinterpret_cast<const void*>(g_inversion_platform_count), &count) ||
      count <= 0 || count > 32 ||
      !SafeReadValue(reinterpret_cast<const void*>(g_inversion_platform_table), &table) ||
      !table) return;

  const auto flight = reinterpret_cast<uintptr_t>(g_dungeon_base + kInversionTurtleFlight);
  for (int32_t index = 0; index < count; ++index) {
    InversionTurtleActor* turtle = nullptr;
    InversionTurtleActor actor{};
    InversionTurtlePose pose{};
    if (!SafeReadValue(table + index, &turtle) || !turtle ||
        !SafeReadValue(turtle, &actor) || actor.update != flight ||
        actor.phase != kInversionFlightPhase || !actor.pose ||
        !SafeReadValue(actor.pose, &pose)) continue;

    const auto candidate = FindInversionRidingPose(actor, pose, position);
    if (!candidate.ticks) continue;
    if (ApplyInversionTurtlePose(turtle, candidate.actor, candidate.pose)) {
      const auto floor = InversionFloorAt(position, room);
      if (floor.supporting_actor == reinterpret_cast<uintptr_t>(turtle) &&
          std::abs(saved_height - floor.height) <= kRestoreHeightTolerance) {
        AppendDeathtrapSupportLog("support_inversion_rider platform=%d ticks=%u",
                                  index, candidate.ticks);
        return;
      }
    }
    ApplyInversionTurtlePose(turtle, actor, pose);
  }
}

void __cdecl HookInversionLoadPlacement(const InversionPosition* position, uintptr_t room) {
  uint8_t level = 0;
  void* context = g_death_recovery_api.context ? g_death_recovery_api.context() : nullptr;
  if (position && room && RecoveryContext(context, &level) && level == kInversionLevel) {
    RestoreInversionRidingSupport(*position, room);
  }
  reinterpret_cast<void(__cdecl*)(const InversionPosition*, uintptr_t)>(
      g_dungeon_base + kInversionPlacePlayer)(position, room);
}

bool InstallInversionRiderLoadHook() {
  if (!g_death_recovery_hooks_installed ||
      !MatchesDungeonCode(kInversionTurtleFlight,
          std::array<uint8_t, 8>{0x83, 0xEC, 0x1C, 0x53, 0x56, 0x8B, 0x74, 0x24}) ||
      !MatchesDungeonCode(kInversionPlatformByIndex, std::array<uint8_t, 2>{0x8B, 0x0D}) ||
      !MatchesDungeonCode(kInversionPlatformCleanup, std::array<uint8_t, 2>{0x56, 0xA1}) ||
      !SafeReadValue(g_dungeon_base + kInversionPlatformByIndex + 2, &g_inversion_platform_table) ||
      !SafeReadValue(g_dungeon_base + kInversionPlatformCleanup + 2, &g_inversion_platform_count)) {
    return false;
  }
  const auto base = reinterpret_cast<uintptr_t>(g_dungeon_base);
  const auto site = base + kInversionPlayerLoadPlacement;
  const auto hook = RenderQueueCall<5>(site, reinterpret_cast<uintptr_t>(&HookInversionLoadPlacement));
  return MatchesDungeonCode(kInversionPlayerLoadPlacement, hook) ||
      PatchDungeonCode(kInversionPlayerLoadPlacement,
          RenderQueueCall<5>(site, base + kInversionPlacePlayer), hook);
}
