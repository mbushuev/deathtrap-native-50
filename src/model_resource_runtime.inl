// Native resources already have individual offsets and a per-open FILE owner.
// Keep the retail BOX open; redirect only changed resources to our small data
// file through the game's own CRT, so allocation/conversion/free stay native.
constexpr DungeonDataRva kModelResourceEntriesRva{0x247130,0x247B90,0x247600,0x2477E0};
constexpr DungeonDataRva kModelResourceCapacityRva{0x236F94,0x2379F4,0x237464,0x237644};
using ModelArchiveInit=int(__cdecl*)(const char*);
using ModelResourceOpen=void*(__cdecl*)(uint32_t,const char*);
using ModelResourceRegister=int(__cdecl*)(uint32_t,const char*,uint32_t,uint32_t);
using ModelNativeFileOpen=void*(__cdecl*)(const char*,const char*);
using ModelNativeFileSeek=int(__cdecl*)(void*,long,int);
using ModelNativeFileClose=int(__cdecl*)(void*);
ModelArchiveInit g_model_archive_init=nullptr;
ModelResourceOpen g_model_resource_open=nullptr;
ModelResourceRegister g_model_resource_register=nullptr;
ModelNativeFileOpen g_model_native_open=nullptr;
ModelNativeFileSeek g_model_native_seek=nullptr;
ModelNativeFileClose g_model_native_close=nullptr;
std::string g_model_overlay_path;
std::atomic<bool> g_model_overlay_active{false};
bool g_model_resource_hooks_installed=false;

bool ApplyModelResourceOverlay() {
  uint32_t count=0,capacity=0;
  if (g_model_overlay.size()!=g_dual_heroine_pack.resources ||
      !SafeReadValue(g_dungeon_base+kRenderResourceCountRva,&count) || count!=14125 ||
      !SafeReadValue(g_dungeon_base+kModelResourceCapacityRva,&capacity) ||
      g_model_overlay.size()>capacity || !g_model_resource_register) return false;
  auto* table=g_dungeon_base+kModelResourceEntriesRva;
  for (uint32_t id=1;id<g_model_overlay.size();++id) {
    const auto& entry=g_model_overlay[id];
    if (!entry.type && !entry.size) continue;
    uintptr_t name=0;
    if (!SafeReadValue(table+id*16,&name)) return false;
    if (!name) {
      std::array<char,5> kind{};std::memcpy(kind.data(),&entry.type,4);
      if (!g_model_resource_register(id,kind.data(),entry.offset,entry.size)) return false;
    }
    const std::array<uint32_t,2> extent{entry.offset,entry.size};
    if (!SafeWrite(table+id*16+4,extent.data(),sizeof(extent))) return false;
  }
  const auto total=static_cast<uint32_t>(g_model_overlay.size());
  if (!SafeWrite(g_dungeon_base+kRenderResourceCountRva,&total,4)) return false;
  g_model_overlay_active.store(true,std::memory_order_release);
  AppendDeathtrapSupportLog("support_heroine_model resources=%u storage=original_plus_overlay shared_chains=%u",
      total,HeroineSharedChains()?2u:0u);
  return true;
}

int __cdecl HookModelArchiveInit(const char* name) {
  std::wstring path;
  if (name) {
    const int count=MultiByteToWideChar(CP_ACP,0,name,-1,nullptr,0);
    if (count>0 && count<=32768) {
      path.resize(count);MultiByteToWideChar(CP_ACP,0,name,-1,path.data(),count);
    }
  }
  const bool target=!path.empty() && ModelFileIndex(path.c_str())==0;
  if (target) g_model_overlay_active.store(false,std::memory_order_release);
  const int result=g_model_archive_init(name);
  if (!result || !target || SelectModelArchive(true)<=0) return result;
  if (ApplyModelResourceOverlay()) return result;
  AppendDeathtrapSupportLog("support_heroine_model overlay=failed reason=native_resource_table");
  return 0;
}

void* __cdecl HookModelResourceOpen(uint32_t id,const char* mode) {
  if (!g_model_overlay_active.load(std::memory_order_acquire) || !id ||
      id>=g_model_overlay.size() || g_model_overlay[id].source==0)
    return g_model_resource_open(id,mode);
  // Native resources are read-only. Never send an unexpected write to either
  // the original BOX or the reconstructed overlay.
  if (!mode || mode[0]!='r' || std::strchr(mode,'+')) return nullptr;
  auto* file=g_model_native_open(g_model_overlay_path.c_str(),mode);
  if (!file) return nullptr;
  if (g_model_native_seek(file,static_cast<long>(g_model_overlay[id].offset),SEEK_SET)==0) return file;
  g_model_native_close(file);return nullptr;
}

bool InstallModelResourceHooks() {
  if (g_model_resource_hooks_installed) return true;
  if (!g_dungeon_base || g_model_overlay.empty()) return false;
  const auto path=std::filesystem::path(g_model_game_root)/L"native50/models/e3/model-data.bin";
  BOOL substituted=FALSE;
  const int length=WideCharToMultiByte(CP_ACP,WC_NO_BEST_FIT_CHARS,path.c_str(),-1,nullptr,0,nullptr,&substituted);
  if (length<=1 || substituted) return false;
  g_model_overlay_path.resize(length);
  if (!WideCharToMultiByte(CP_ACP,WC_NO_BEST_FIT_CHARS,path.c_str(),-1,
      g_model_overlay_path.data(),length,nullptr,&substituted) || substituted) return false;
  g_model_overlay_path.pop_back();
  const auto match=[](uint32_t rva,std::initializer_list<uint8_t> expected) {
    std::vector<uint8_t> actual(expected.size());
    const bool valid=SafeRead(g_dungeon_base+ActiveDungeonCodeRva(rva),actual.data(),actual.size()) &&
        std::equal(actual.begin(),actual.end(),expected.begin());
    if (!valid) AppendDeathtrapSupportLog("support_heroine_model resource_signature=failed rva=%08lX",static_cast<unsigned long>(rva));
    return valid;
  };
  uintptr_t table=0;
  if (!match(0x39410,{0x8B,0x54,0x24,0x04,0x83,0xEC,0x10,0xB9,0xFF,0xFF,0xFF,0xFF}) ||
      !match(0x39C20,{0x81,0xEC,0x04,0x01,0,0,0x53,0x56}) ||
      !match(0x39DB0,{0x8B,0x44,0x24,0x04,0x81,0xEC,0x04,0x01,0,0}) ||
      !match(0xAB790,{0x8B,0x44,0x24,0x08,0x6A,0x40}) ||
      !match(0xAB960,{0x56,0x57,0x8B,0x7C,0x24,0x0C}) ||
      !match(0x3992B,{0x8B,0x83}) ||
      !SafeReadValue(g_dungeon_base+ActiveDungeonCodeRva(0x3992D),&table) ||
      table!=reinterpret_cast<uintptr_t>(g_dungeon_base+kModelResourceEntriesRva)) return false;
  auto* init=g_dungeon_base+ActiveDungeonCodeRva(0x39410);
  auto* open=g_dungeon_base+ActiveDungeonCodeRva(0x39C20);
  if (MH_CreateHook(init,reinterpret_cast<void*>(&HookModelArchiveInit),reinterpret_cast<void**>(&g_model_archive_init))!=MH_OK) return false;
  if (MH_CreateHook(open,reinterpret_cast<void*>(&HookModelResourceOpen),reinterpret_cast<void**>(&g_model_resource_open))!=MH_OK) {
    MH_RemoveHook(init);return false;
  }
  g_model_resource_register=reinterpret_cast<ModelResourceRegister>(g_dungeon_base+ActiveDungeonCodeRva(0x39DB0));
  g_model_native_open=reinterpret_cast<ModelNativeFileOpen>(g_dungeon_base+ActiveDungeonCodeRva(0xAB790));
  g_model_native_seek=reinterpret_cast<ModelNativeFileSeek>(g_dungeon_base+ActiveDungeonCodeRva(0xAB960));
  g_model_native_close=reinterpret_cast<ModelNativeFileClose>(g_dungeon_base+ActiveDungeonCodeRva(0xAB6B0));
  if (MH_QueueEnableHook(init)!=MH_OK || MH_QueueEnableHook(open)!=MH_OK || MH_ApplyQueued()!=MH_OK) {
    MH_DisableHook(init);MH_DisableHook(open);MH_RemoveHook(init);MH_RemoveHook(open);return false;
  }
  g_model_resource_hooks_installed=true;return true;
}
