// Keep the native 64 KiB pages and texture-cache lifecycle, but relocate both
// fixed arrays and expand every allocation/scan/cleanup boundary together.
constexpr uint32_t kTexturePoolPages=120u;
static_assert(kTexturePoolPages>45 && kTexturePoolPages<=127,
    "The original init's signed imm8 operands require a capacity <= 127");
constexpr uint32_t kTexturePoolPageBytes=65536u;
constexpr uint32_t kTexturePoolAllocationBytes=(kTexturePoolPages+1)*kTexturePoolPageBytes;
constexpr DungeonDataRva kTexturePoolAllocationRva{0xC7DB0,0xC7DB8,0xC7DA8,0xC7DB8};
constexpr DungeonDataRva kTexturePoolAlignedRva{0xC7DB4,0xC7DBC,0xC7DAC,0xC7DBC};
constexpr DungeonDataRva kTexturePoolMetadataRva{0xC7DB8,0xC7DC0,0xC7DB0,0xC7DC0};
constexpr DungeonDataRva kTexturePoolFreeRva{0x104728,0x105188,0x104BF8,0x104DD8};
constexpr DungeonDataRva kTexturePoolAgeRva{0x1047E0,0x105240,0x104CB0,0x104E90};
struct TexturePoolReference { uintptr_t operand;uint8_t array;bool end; };
constexpr TexturePoolReference kTexturePoolReferences[]={
    {0x32691,0,false},{0x326A1,0,true},{0x32703,0,false},{0x32712,0,true},
    {0x3271E,1,false},{0x32729,0,false},{0x32730,0,false},{0x3275B,0,false},
    {0x327B9,1,false},{0x3285A,1,false},{0x3288B,1,true},{0x328B7,1,false},
    {0x328F8,1,false},{0x32C1C,1,false}};
// 0x104894 and the biased 0x1047E8 resource-handler table are unrelated globals;
// their neighboring addresses must NOT be relocated with the texture ages.
std::array<uint8_t*,2> g_texture_pool_arrays{};
bool g_texture_pool_expanded=false;

struct TexturePoolChange {
  uintptr_t rva;uint32_t before,after;uint8_t size;
};
bool MatchesTexturePoolChange(const TexturePoolChange& change) {
  if (change.size==1) return MatchesDungeonCode(change.rva,
      std::array<uint8_t,1>{static_cast<uint8_t>(change.before)});
  return MatchesDungeonCode(change.rva,RenderQueueWord(change.before));
}
bool ApplyTexturePoolChange(const TexturePoolChange& change,bool reverse=false) {
  const uint32_t before=reverse?change.after:change.before,after=reverse?change.before:change.after;
  if (change.size==1) return PatchDungeonCode(change.rva,
      std::array<uint8_t,1>{static_cast<uint8_t>(before)},std::array<uint8_t,1>{static_cast<uint8_t>(after)});
  return PatchDungeonCode(change.rva,RenderQueueWord(before),RenderQueueWord(after));
}

bool InstallTexturePoolExpansion() {
  if (g_texture_pool_expanded) return true;
  if (!g_dungeon_base || g_dungeon_image_variant==DungeonImageVariant::kUnsupported) return false;
  uintptr_t allocation=0,metadata=0;
  if (!SafeReadValue(g_dungeon_base+kTexturePoolAllocationRva,&allocation) || allocation ||
      !SafeReadValue(g_dungeon_base+kTexturePoolMetadataRva,&metadata) || metadata) {
    AppendDeathtrapSupportLog("support_texture_pool state=skipped reason=already_initialized");return false;
  }
  std::vector<TexturePoolChange> changes{
      {ActiveDungeonCodeRva(0x32617),45,kTexturePoolPages,1},
      {ActiveDungeonCodeRva(0x32631),45,kTexturePoolPages,1},
      {ActiveDungeonCodeRva(0x32660),46*kTexturePoolPageBytes,kTexturePoolAllocationBytes,4},
      {ActiveDungeonCodeRva(0x3267C),45,kTexturePoolPages,1},
      {ActiveDungeonCodeRva(0x326E4),45*16,kTexturePoolPages*16,4},
      {ActiveDungeonCodeRva(0x32C4F),45*16,kTexturePoolPages*16,4}};
  for (const auto ref:kTexturePoolReferences) {
    const auto old=reinterpret_cast<uintptr_t>(g_dungeon_base)+
        static_cast<uintptr_t>(ref.array?kTexturePoolAgeRva:kTexturePoolFreeRva)+(ref.end?45*4:0);
    changes.push_back({ActiveDungeonCodeRva(ref.operand),static_cast<uint32_t>(old),0,4});
  }
  for (const auto& change:changes) if (!MatchesTexturePoolChange(change)) {
    AppendDeathtrapSupportLog("support_texture_pool state=skipped reason=signature rva=%08lX",static_cast<unsigned long>(change.rva));return false;
  }
  std::array<uint8_t*,2> arrays{};
  const auto release=[&]() { for (auto array:arrays) if(array) VirtualFree(array-8,0,MEM_RELEASE); };
  for (auto& array:arrays) {
    auto* memory=static_cast<uint8_t*>(VirtualAlloc(nullptr,16+4*kTexturePoolPages,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if (!memory) { release();return false; }array=memory+8;
  }
  for (size_t i=0;i<std::size(kTexturePoolReferences);++i) {
    const auto ref=kTexturePoolReferences[i];
    changes[6+i].after=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(arrays[ref.array])+(ref.end?kTexturePoolPages*4:0));
  }
  size_t written=0;
  for (const auto& change:changes) { if(!ApplyTexturePoolChange(change)) break;++written; }
  if (written!=changes.size()) {
    bool restored=true;
    while (written) restored=ApplyTexturePoolChange(changes[--written],true)&&restored;
    if (restored) release();else g_texture_pool_arrays=arrays;
    AppendDeathtrapSupportLog("support_texture_pool state=failed rollback=%u",restored?1u:0u);return false;
  }
  g_texture_pool_arrays=arrays;g_texture_pool_expanded=true;
  AppendDeathtrapSupportLog("support_texture_pool state=expanded pages=%u page_bytes=%u allocation_bytes=%u array_references=%u cleanup=expanded",
      kTexturePoolPages,kTexturePoolPageBytes,kTexturePoolAllocationBytes,static_cast<unsigned>(std::size(kTexturePoolReferences)));
  return true;
}

void LogTexturePoolUsage() {
  if (!g_texture_pool_expanded || !g_extended_log_enabled.load(std::memory_order_relaxed)) return;
  uintptr_t arena=0,metadata=0;
  if (!SafeReadValue(g_dungeon_base+kTexturePoolAllocationRva,&arena) || !arena ||
      !SafeReadValue(g_dungeon_base+kTexturePoolMetadataRva,&metadata) || !metadata) return;
  std::array<uint32_t,kTexturePoolPages> free{};
  if (!SafeRead(g_texture_pool_arrays[0],free.data(),sizeof(free))) return;
  const auto used=static_cast<uint32_t>(std::count(free.begin(),free.end(),0u));
  static uint32_t highest_bucket=0;const uint32_t bucket=used/4+1;
  if (bucket<=highest_bucket) return;highest_bucket=bucket;
  AppendDeathtrapSupportLog("support_texture_pool usage_pages=%u capacity_pages=%u",used,kTexturePoolPages);
}
