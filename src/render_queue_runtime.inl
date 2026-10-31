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
