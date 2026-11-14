// Retail RenderQ has a 750 KiB command arena and six fixed 8192-entry
// key/pointer arrays. Expand both together; retain its checks and radix sorter.
constexpr uint32_t kRenderQueueEntries = 8192u * 8u;
constexpr uint32_t kRenderQueueCommandBytes = 0xBB800u * 8u;
constexpr uint32_t kRenderQueueAllocationBytes = 0xBE000u * 8u;
constexpr DungeonDataRva kRenderQueueBaseRva{
    0x1F4AFCu,0x1F555Cu,0x1F4FCCu,0x1F51ACu};
constexpr DungeonDataRva kRenderQueueKeysRva{
    0x19F66Cu,0x1A00CCu,0x19FB3Cu,0x19FD1Cu};
struct RenderQueueReference { uintptr_t code; uint8_t array; int8_t bias; };
constexpr RenderQueueReference kRenderQueueReferences[] = {
    {0x3C4D6,0,-2},{0xA86A5,0,-2},
    {0x340FC,0,0},{0x3445D,0,0},{0x3538B,0,0},{0x35C22,0,0},
    {0x3A110,0,0},{0x3CD98,0,0},{0x5A98C,0,0},{0x765CB,0,0},
    {0x7682E,0,0},{0x76A47,0,0},{0x8B895,0,0},{0x8C072,0,0},
    {0xA853F,0,0},{0xA85DC,0,0},{0xA865D,0,0},{0xA871C,0,0},
    {0x3C4F1,1,-2},{0xA85F5,1,-2},
    {0x34108,1,0},{0x34469,1,0},{0x35397,1,0},{0x35C2E,1,0},
    {0x3A12A,1,0},{0x3CDA4,1,0},{0x5A993,1,0},{0x765D4,1,0},
    {0x76837,1,0},{0x76A50,1,0},{0x8B888,1,0},{0x8C07E,1,0},
    {0xA8544,1,0},{0xA866C,1,0},{0xA868C,1,0},{0xA870D,1,0},
    {0x34124,2,-4},{0x34485,2,-4},{0x353B3,2,-4},{0x35C4A,2,-4},
    {0x3A144,2,-4},{0x3C4E9,2,-4},{0x3CDC0,2,-4},{0x5A9AA,2,-4},
    {0x765F2,2,-4},{0x76851,2,-4},{0x76A6A,2,-4},{0x8C09A,2,-4},
    {0xA860B,2,-4},{0xA86BB,2,-4},
    {0x65303,2,0},{0x8B8A2,2,0},{0xA8673,2,0},{0xA8723,2,0},
    {0xA86FE,3,-2},{0xA8604,3,0},{0xA8630,3,0},{0xA86C3,3,0},
    {0xA864E,4,-2},{0xA8613,4,0},{0xA86B4,4,0},{0xA86E0,4,0},
    {0xA8664,5,-4},{0xA8714,5,-4},{0xA861A,5,0},{0xA86CA,5,0},
};
constexpr uintptr_t kRenderQueueOriginalOffsets[]={0,0x4000,0x8000,0x10000,0x14000,0x18000};
std::array<uint8_t*,6> g_render_queue_arrays{};
bool g_render_queue_expanded=false;

constexpr DungeonDataRva kRenderQueueCallbacksRva{
    0x107B50u, 0x1085B0u, 0x108020u, 0x108200u};
using RenderQueueCallback = void(__cdecl*)(void*);
const RenderQueueCallback* g_render_queue_callbacks = nullptr;
using RenderQueueFlush = void(__cdecl*)();
RenderQueueFlush g_render_queue_flush = nullptr;

thread_local uint32_t g_world_queue_group = 0;
thread_local uint32_t g_next_world_depth_group = 0;
thread_local uint32_t g_world_depth_group = 0;
thread_local std::unordered_map<const void*, deathtrap::projective_quad::Mapping>
    g_world_quad_mappings;
thread_local const deathtrap::projective_quad::Mapping* g_current_world_quad = nullptr;

constexpr DungeonDataRva kRoomProjectionRva{
    0x104B58u, 0x1055B8u, 0x105028u, 0x105208u};
uintptr_t* g_render_queue_next_cursor = nullptr;
RenderQueueFlush g_render_queue_reset = nullptr;

void __cdecl ResetWorldRenderQueue() {
  g_world_quad_mappings.clear();
  g_render_queue_reset();
}

void __cdecl RecordWorldQuad(const uint8_t* face, const uint8_t* command) {
  using namespace deathtrap::projective_quad;
  if (!g_widescreen_world_render_active || !(command[0] & 1) ||
      *reinterpret_cast<const uint32_t*>(face + 0x28) != 4) {
    return;
  }
  const auto* corners = *reinterpret_cast<const uint8_t* const*>(face + 0x2C);
  const auto* projection = reinterpret_cast<const float*>(g_dungeon_base + kRoomProjectionRva);
  const double width = command[0x10] + 1, height = command[0x11] + 1;
  std::array<Point, 4> positions;
  std::array<UV, 4> coordinates;
  for (size_t i = 0; i < positions.size(); ++i) {
    const auto* corner = corners + i * 16;
    const auto* vertex = *reinterpret_cast<const uint8_t* const*>(corner + 8);
    const auto* camera = *reinterpret_cast<const float* const*>(vertex + 0x1C);
    positions[i] = {camera[0], camera[1], camera[2]};
    const auto* uv = reinterpret_cast<const int16_t*>(corner);
    coordinates[i] = {uv[0] / width, uv[1] / height};
  }
  Mapping mapping;
  if (Build(positions, coordinates, projection[0], projection[5],
            projection[8], projection[28], mapping)) {
    g_world_quad_mappings.emplace(command, mapping);
  }
}

// Both paths commit the polygon after clipping but still have the original
// face descriptor and all four camera-space corners. The overwritten MOV
// loads the next free command address.
__declspec(naked) void RecordRoomQuadBridge() {
  __asm {
    pushfd
    pushad
    push edx
    push dword ptr [ebp-4]
    call RecordWorldQuad
    add esp, 8
    popad
    popfd
    mov eax, g_render_queue_next_cursor
    mov eax, [eax]
    ret
  }
}

__declspec(naked) void RecordNearClippedRoomQuadBridge() {
  __asm {
    pushfd
    pushad
    push ebx
    push dword ptr [esp+84]
    call RecordWorldQuad
    add esp, 8
    popad
    popfd
    mov eax, g_render_queue_next_cursor
    mov eax, [eax]
    ret
  }
}

void __cdecl FlushWorldRenderQueue() {
  const uint32_t previous_queue = g_world_queue_group;
  const uint32_t previous_group = g_world_depth_group;
  g_world_queue_group = 0;
  if (g_widescreen_world_render_active) {
    if (++g_next_world_depth_group == 0) ++g_next_world_depth_group;
    g_world_queue_group = g_next_world_depth_group;
  }
  g_world_depth_group = 0;
  g_render_queue_flush();
  g_world_depth_group = previous_group;
  g_world_queue_group = previous_queue;
}

void __cdecl DispatchRenderQueue(uint32_t type, void* command) {
  const uint32_t previous_group = g_world_depth_group;
  const auto* previous_quad = g_current_world_quad;
  g_current_world_quad = nullptr;
  if (g_world_queue_group) {
    g_world_depth_group = g_world_queue_group;
    if (type == 0 && !g_world_quad_mappings.empty()) {
      const auto mapping = g_world_quad_mappings.find(command);
      if (mapping != g_world_quad_mappings.end()) g_current_world_quad = &mapping->second;
    }
  }
  g_render_queue_callbacks[type](command);
  g_current_world_quad = previous_quad;
  g_world_depth_group = previous_group;
}

// Retail has already validated ECX and pushed the command.
__declspec(naked) void RenderQueueDispatchBridge() {
  __asm {
    push dword ptr [esp+4]
    push ecx
    call DispatchRenderQueue
    add esp, 8
    ret
  }
}

template <size_t Size>
std::array<uint8_t, Size> RenderQueueCall(uintptr_t site, uintptr_t target) {
  static_assert(Size >= 5);
  std::array<uint8_t, Size> bytes;
  bytes.fill(0x90);
  bytes[0] = 0xE8;
  const auto relative = static_cast<uint32_t>(target - site - 5);
  std::memcpy(bytes.data() + 1, &relative, sizeof(relative));
  return bytes;
}

bool InstallWorldRenderQueueDepth() {
  if (g_render_queue_flush) return true;
  if (!g_dungeon_base || g_dungeon_image_variant == DungeonImageVariant::kUnsupported) {
    return false;
  }
  const auto dispatch = ActiveDungeonCodeRva(0x6531A);
  const auto flush = ActiveDungeonCodeRva(0x1B452);
  const auto base = reinterpret_cast<uintptr_t>(g_dungeon_base);
  const auto native_flush = base + ActiveDungeonCodeRva(0x652D0);
  const auto* callbacks = reinterpret_cast<const RenderQueueCallback*>(
      g_dungeon_base + kRenderQueueCallbacksRva);
  const auto table = reinterpret_cast<uintptr_t>(callbacks);
  std::array<uint8_t, 7> original_dispatch{0xFF, 0x14, 0x8D};
  std::memcpy(original_dispatch.data() + 3, &table, sizeof(uint32_t));
  const auto original_flush = RenderQueueCall<5>(base + flush, native_flush);
  if (!MatchesDungeonCode(dispatch, original_dispatch) ||
      !MatchesDungeonCode(flush, original_flush)) {
    AppendDeathtrapSupportLog("support_world_depth state=skipped reason=queue_signature");
    return false;
  }
  const auto replacement_dispatch = RenderQueueCall<7>(base + dispatch,
      reinterpret_cast<uintptr_t>(&RenderQueueDispatchBridge));
  const auto replacement_flush = RenderQueueCall<5>(base + flush,
      reinterpret_cast<uintptr_t>(&FlushWorldRenderQueue));
  g_render_queue_callbacks = callbacks;
  if (!PatchDungeonCode(dispatch, original_dispatch, replacement_dispatch)) return false;
  g_render_queue_flush = reinterpret_cast<RenderQueueFlush>(native_flush);
  if (!PatchDungeonCode(flush, original_flush, replacement_flush)) {
    g_render_queue_flush = nullptr;
    PatchDungeonCode(dispatch, replacement_dispatch, original_dispatch);
    return false;
  }
  AppendDeathtrapSupportLog("support_world_depth state=shared_queue");
  return true;
}

bool InstallWorldQuadProjection() {
  if (g_render_queue_reset) return true;
  if (!g_render_queue_flush) return false;

  const auto base = reinterpret_cast<uintptr_t>(g_dungeon_base);
  const auto reset = ActiveDungeonCodeRva(0x1B34F);
  const auto native_reset = base + ActiveDungeonCodeRva(0x652B0);
  g_render_queue_next_cursor = reinterpret_cast<uintptr_t*>(
      g_dungeon_base + kRenderQueueBaseRva + 4);
  std::array<uint8_t, 5> native_load{0xA1};
  const auto cursor = reinterpret_cast<uintptr_t>(g_render_queue_next_cursor);
  std::memcpy(native_load.data() + 1, &cursor, sizeof(uint32_t));
  struct Change {
    uintptr_t rva;
    std::array<uint8_t, 5> before;
    uintptr_t target;
  };
  const Change changes[] = {
      {reset, RenderQueueCall<5>(base + reset, native_reset),
       reinterpret_cast<uintptr_t>(&ResetWorldRenderQueue)},
      {ActiveDungeonCodeRva(0x3C4FB), native_load,
       reinterpret_cast<uintptr_t>(&RecordRoomQuadBridge)},
      {ActiveDungeonCodeRva(0x3CD9C), native_load,
       reinterpret_cast<uintptr_t>(&RecordNearClippedRoomQuadBridge)}};
  for (const auto& change : changes) {
    if (!MatchesDungeonCode(change.rva, change.before)) {
      AppendDeathtrapSupportLog("support_world_uv state=skipped reason=queue_signature");
      return false;
    }
  }
  g_render_queue_reset = reinterpret_cast<RenderQueueFlush>(native_reset);
  for (size_t i = 0; i < std::size(changes); ++i) {
    const auto& change = changes[i];
    if (PatchDungeonCode(change.rva, change.before,
                         RenderQueueCall<5>(base + change.rva, change.target))) continue;
    while (i) {
      const auto& installed = changes[--i];
      PatchDungeonCode(installed.rva,
                       RenderQueueCall<5>(base + installed.rva, installed.target),
                       installed.before);
    }
    g_render_queue_reset = nullptr;
    return false;
  }
  AppendDeathtrapSupportLog("support_world_uv state=projective_quads");
  return true;
}

void LogRenderQueueUsage() {
  if (!g_render_queue_expanded || !g_extended_log_enabled.load(std::memory_order_relaxed)) return;
  uint32_t count=0;uintptr_t base=0,cursor=0,end=0;
  const auto rva=static_cast<uintptr_t>(kRenderQueueBaseRva);
  if (!SafeReadValue(g_dungeon_base+rva,&base) || !base ||
      !SafeReadValue(g_dungeon_base+rva-8,&cursor) || cursor<base ||
      !SafeReadValue(g_dungeon_base+rva-4,&end) || end<base ||
      !SafeReadValue(g_dungeon_base+rva-16,&count)) return;
  static uint32_t logged_entries=0,logged_bytes=0;
  const auto bytes=static_cast<uint32_t>(cursor-base);
  const uint32_t entries_bucket=count/1024u+1u,bytes_bucket=bytes/131072u+1u;
  if (entries_bucket<=logged_entries && bytes_bucket<=logged_bytes) return;
  logged_entries=std::max(logged_entries,entries_bucket);logged_bytes=std::max(logged_bytes,bytes_bucket);
  AppendDeathtrapSupportLog("support_render_queue usage_entries=%u capacity_entries=%u usage_bytes=%u capacity_bytes=%u",
      count,kRenderQueueEntries,bytes,static_cast<unsigned>(end-base));
}

std::array<uint8_t,4> RenderQueueWord(uint32_t value) {
  std::array<uint8_t,4> bytes{};
  std::memcpy(bytes.data(),&value,sizeof(value));return bytes;
}

bool InstallRenderQueueExpansion() {
  if (g_render_queue_expanded) return true;
  if (!g_dungeon_base || g_dungeon_image_variant==DungeonImageVariant::kUnsupported) return false;
  uintptr_t current=0;
  // This is installed before resource initialization. Replacing a live arena
  // would invalidate queued pointers, so a late install is deliberately refused.
  if (!SafeReadValue(g_dungeon_base+kRenderQueueBaseRva,&current) || current) {
    AppendDeathtrapSupportLog("support_render_queue state=skipped reason=already_initialized");return false;
  }
  struct Change { uintptr_t rva; std::array<uint8_t,4> before,after; };
  std::vector<Change> changes;
  changes.push_back({ActiveDungeonCodeRva(0x65230),RenderQueueWord(0xBE000),RenderQueueWord(kRenderQueueAllocationBytes)});
  changes.push_back({ActiveDungeonCodeRva(0x6524B),RenderQueueWord(0xBB800),RenderQueueWord(kRenderQueueCommandBytes)});
  for (const auto ref:kRenderQueueReferences) {
    const auto old_address=reinterpret_cast<uintptr_t>(g_dungeon_base)+
        static_cast<uintptr_t>(kRenderQueueKeysRva)+kRenderQueueOriginalOffsets[ref.array]+ref.bias;
    changes.push_back({ActiveDungeonCodeRva(ref.code),RenderQueueWord(static_cast<uint32_t>(old_address)),{}});
  }
  for (const auto& change:changes) {
    if (!MatchesDungeonCode(change.rva,change.before)) {
      AppendDeathtrapSupportLog("support_render_queue state=skipped reason=signature rva=%08lX",static_cast<unsigned long>(change.rva));return false;
    }
  }
  std::array<uint8_t*,6> arrays{};
  const auto release=[&]() { for (auto p:arrays) if(p) VirtualFree(p-8,0,MEM_RELEASE); };
  for (size_t i=0;i<arrays.size();++i) {
    const size_t stride=(i%3==2)?4:2;
    auto allocation=static_cast<uint8_t*>(VirtualAlloc(nullptr,8+stride*kRenderQueueEntries+8,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if (!allocation) { release();return false; }
    arrays[i]=allocation+8;
  }
  for (size_t i=0;i<std::size(kRenderQueueReferences);++i) {
    const auto ref=kRenderQueueReferences[i];
    changes[i+2].after=RenderQueueWord(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(arrays[ref.array])+ref.bias));
  }
  size_t written=0;
  for (const auto& change:changes) {
    if (!PatchDungeonCode(change.rva,change.before,change.after)) break;
    ++written;
  }
  if (written!=changes.size()) {
    bool restored=true;
    while(written) { const auto& c=changes[--written];restored=PatchDungeonCode(c.rva,c.after,c.before)&&restored; }
    // Keep storage alive if an external modification prevented full rollback.
    if(restored) release(); else g_render_queue_arrays=arrays;
    AppendDeathtrapSupportLog("support_render_queue state=failed rollback=%u",restored?1u:0u);return false;
  }
  g_render_queue_arrays=arrays;g_render_queue_expanded=true;
  AppendDeathtrapSupportLog("support_render_queue state=expanded entries=%u command_bytes=%u allocation_bytes=%u array_references=%u checks=preserved",
      kRenderQueueEntries,kRenderQueueCommandBytes,kRenderQueueAllocationBytes,static_cast<unsigned>(std::size(kRenderQueueReferences)));
  return true;
}
