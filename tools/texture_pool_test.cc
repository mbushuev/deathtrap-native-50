#include <windows.h>
#include <iostream>
#include <set>
#include <map>
#include "../src/deathtrap_native_render_patch.cc"

void RequireTexture(bool ok,const char* message) {
  if (!ok) { std::cerr<<message<<'\n';std::exit(1); }
}
std::map<void*,uint32_t> texture_allocations;
uint32_t raw_texture_allocation=0,backend_capacity=0,backend_prepares=0,backend_registers=0,backend_flushes=0,renderer_flushes=0;
std::set<uint32_t> backend_live;
bool backend_fail_once=false;
void* __cdecl AllocateTextureFixture(uint32_t size) {
  auto* p=static_cast<uint8_t*>(HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,size+32));
  RequireTexture(p!=nullptr,"Fixture allocation");std::memset(p,0xA7,16);std::memset(p+16+size,0xA7,16);
  texture_allocations[p+16]=size;return p+16;
}
void* __cdecl AllocateTextureArena(uint32_t size,const char*,int) {
  raw_texture_allocation=size;return AllocateTextureFixture(size);
}
void __cdecl FreeTextureFixture(void* p) {
  const auto it=texture_allocations.find(p);RequireTexture(it!=texture_allocations.end(),"Unknown/double native free");
  const auto* bytes=static_cast<uint8_t*>(p);
  for (size_t i=0;i<16;++i) RequireTexture(bytes[-16+static_cast<ptrdiff_t>(i)]==0xA7 && bytes[it->second+i]==0xA7,"Native allocation guard overwritten");
  texture_allocations.erase(it);HeapFree(GetProcessHeap(),0,static_cast<uint8_t*>(p)-16);
}
void __cdecl TextureFatal(const char*,...) { RequireTexture(false,"Unexpected native texture fatal/error"); }
void __cdecl TextureLog(const char*,...) {}
int __cdecl TextureBackendInit(uint32_t count) { backend_capacity=count;return 1; }
uint32_t __cdecl TextureBackendPrepare(uint32_t key) {
  ++backend_prepares;if(backend_fail_once) { backend_fail_once=false;return 0; }
  return 0xB0000000u+key;
}
void __cdecl TextureBackendRegister(uint32_t handle,void*,uint32_t) { RequireTexture(backend_live.insert(handle).second,"Duplicate backend handle");++backend_registers; }
void __cdecl TextureBackendUnregister(uint32_t handle) { RequireTexture(backend_live.erase(handle)==1,"Invalid backend unregister"); }
void __cdecl TextureBackendFlush() { ++backend_flushes; }
void __cdecl TextureRendererFlush() { ++renderer_flushes; }

template<typename Function> Function NativeTexture(uintptr_t rva) {
  return reinterpret_cast<Function>(g_dungeon_base+ActiveDungeonCodeRva(rva));
}
void HookTextureFixture(uintptr_t rva,void* replacement) {
  auto* target=g_dungeon_base+ActiveDungeonCodeRva(rva);
  RequireTexture(MH_CreateHook(target,replacement,nullptr)==MH_OK && MH_EnableHook(target)==MH_OK,"Native fixture hook");
}
void SetTextureCallback(uint32_t offset,uintptr_t value) {
  const auto base=static_cast<uintptr_t>(kTexturePoolAllocationRva)-0x1C;
  RequireTexture(SafeWrite(g_dungeon_base+base+offset,&value,4),"Set texture callback");
}
void CheckTextureArrays(const std::array<uint8_t,180>& original_free,const std::array<uint8_t,180>& original_age) {
  RequireTexture(std::memcmp(g_dungeon_base+kTexturePoolFreeRva,original_free.data(),180)==0 &&
      std::memcmp(g_dungeon_base+kTexturePoolAgeRva,original_age.data(),180)==0,"Native operation still touched old 45-entry arrays");
  for (const auto* array:g_texture_pool_arrays) for (size_t i=0;i<8;++i)
    RequireTexture(array[-8+static_cast<ptrdiff_t>(i)]==0x91 && array[kTexturePoolPages*4+i]==0x91,"Expanded array guard overwritten");
}
int wmain(int argc,wchar_t** argv) {
  RequireTexture(argc==2,"Pass one supported original Dungeon.dll");
  auto module=LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
  RequireTexture(module!=nullptr,"Map original DLL without game/import initialization");
  g_dungeon_base=reinterpret_cast<uint8_t*>(module);g_dungeon_image_variant=IdentifyDungeonImage(g_dungeon_base);
  RequireTexture(g_dungeon_image_variant!=DungeonImageVariant::kUnsupported,"Supported original fixture");
  uintptr_t live=1,empty=0;SafeWrite(g_dungeon_base+kTexturePoolAllocationRva,&live,4);
  RequireTexture(!InstallTexturePoolExpansion(),"Reject changing a live texture pool");
  SafeWrite(g_dungeon_base+kTexturePoolAllocationRva,&empty,4);
  TexturePoolChange tamper{ActiveDungeonCodeRva(0x32617),45,46,1};
  RequireTexture(ApplyTexturePoolChange(tamper),"Tamper fixture");
  RequireTexture(!InstallTexturePoolExpansion(),"Reject changed signature atomically");
  RequireTexture(MatchesTexturePoolChange({ActiveDungeonCodeRva(0x32660),46*65536u,0,4}),"Rejected install changed allocation size");
  RequireTexture(ApplyTexturePoolChange(tamper,true),"Restore tamper");
  RequireTexture(InstallTexturePoolExpansion() && InstallTexturePoolExpansion(),"Install/idempotence");
  std::array<uint8_t,180> old_free{},old_age{};old_free.fill(0x61);old_age.fill(0x62);
  SafeWrite(g_dungeon_base+kTexturePoolFreeRva,old_free.data(),180);SafeWrite(g_dungeon_base+kTexturePoolAgeRva,old_age.data(),180);
  // The age end address is another subsystem's scalar, not a pool entry.
  uint32_t neighbor_before=0;SafeReadValue(g_dungeon_base+kTexturePoolAgeRva+180,&neighbor_before);
  for (auto* array:g_texture_pool_arrays) { std::memset(array-8,0x91,8);std::memset(array+kTexturePoolPages*4,0x91,8); }
  RequireTexture(MH_Initialize()==MH_OK,"Initialize fixture allocator hooks");
  HookTextureFixture(0x2B8E0,reinterpret_cast<void*>(&AllocateTextureFixture));
  HookTextureFixture(0x2B9C0,reinterpret_cast<void*>(&AllocateTextureArena));
  HookTextureFixture(0x2B980,reinterpret_cast<void*>(&FreeTextureFixture));
  HookTextureFixture(0x2BA90,reinterpret_cast<void*>(&TextureFatal));
  HookTextureFixture(0x2BB20,reinterpret_cast<void*>(&TextureFatal));
  HookTextureFixture(0x2BBC0,reinterpret_cast<void*>(&TextureLog));
  HookTextureFixture(0x59A30,reinterpret_cast<void*>(&TextureRendererFlush));
  SetTextureCallback(0,reinterpret_cast<uintptr_t>(&TextureBackendInit));
  SetTextureCallback(4,reinterpret_cast<uintptr_t>(&TextureBackendPrepare));
  SetTextureCallback(8,reinterpret_cast<uintptr_t>(&TextureBackendRegister));
  SetTextureCallback(16,reinterpret_cast<uintptr_t>(&TextureBackendFlush));
  SetTextureCallback(20,reinterpret_cast<uintptr_t>(&TextureBackendUnregister));
  auto init=NativeTexture<int(__cdecl*)(int)>(0x32610);
  auto allocate=NativeTexture<void*(__cdecl*)()>(0x32700);
  auto release=NativeTexture<int(__cdecl*)(void*)>(0x32740);
  auto bind=NativeTexture<uint32_t(__cdecl*)(void*,uint32_t)>(0x32780);
  auto flush=NativeTexture<void(__cdecl*)()>(0x32C10);
  auto shutdown=NativeTexture<void(__cdecl*)()>(0x326B0);
  auto* free=reinterpret_cast<uint32_t*>(g_texture_pool_arrays[0]);
  auto* age=reinterpret_cast<uint32_t*>(g_texture_pool_arrays[1]);
  constexpr DungeonDataRva frame_rva{0x1D24DC,0x1D2F3C,0x1D29AC,0x1D2B8C};
  const uint32_t frame=77;SafeWrite(g_dungeon_base+frame_rva,&frame,4);
  for (int cycle=0;cycle<2;++cycle) {
    RequireTexture(init(kTexturePoolPages)==1 && backend_capacity==kTexturePoolPages,"Native backend init capacity");
    RequireTexture(raw_texture_allocation==kTexturePoolAllocationBytes,"Native arena allocation capacity");
    uintptr_t aligned=0,meta=0;SafeReadValue(g_dungeon_base+kTexturePoolAlignedRva,&aligned);SafeReadValue(g_dungeon_base+kTexturePoolMetadataRva,&meta);
    RequireTexture(aligned && (aligned&65535)==0 && meta,"Native alignment/metadata");
    std::set<void*> unique;std::array<void*,kTexturePoolPages> pages{};
    for (uint32_t i=0;i<kTexturePoolPages;++i) {
      pages[i]=allocate();RequireTexture(pages[i] && unique.insert(pages[i]).second,"Allocate unique page beyond old boundary");
      RequireTexture(reinterpret_cast<uintptr_t>(pages[i])==aligned+i*65536 && free[i]==0,"64 KiB page addressing");
      std::memset(pages[i],static_cast<int>(i+1),65536);
    }
    RequireTexture(allocate()==nullptr,"Full native pool must retain overflow check");
    CheckTextureArrays(old_free,old_age);
    for (uint32_t i:{0u,44u,45u,60u,119u}) { release(pages[i]);RequireTexture(free[i]==reinterpret_cast<uintptr_t>(pages[i]),"Return low/high page to free array"); }
    for (uint32_t i:{0u,44u,45u,60u,119u}) RequireTexture(allocate()==pages[i],"Reuse returned page without collision");
    const auto register_before=backend_registers,prepare_before=backend_prepares;
    for (uint32_t i=0;i<kTexturePoolPages;++i) {
      const uint32_t key=0x1000+cycle*0x100+i;
      RequireTexture(bind(pages[i],key)==0xB0000000u+key && age[i]==frame,"Bind hardware cache entry beyond page 45");
      RequireTexture(bind(pages[i],key)==0xB0000000u+key,"Reuse existing hardware cache entry");
      RequireTexture(*(reinterpret_cast<uint32_t*>(meta)+i*4+1)==1,"Native page cache record");
    }
    RequireTexture(backend_registers-register_before==kTexturePoolPages && backend_prepares-prepare_before==kTexturePoolPages,"Native cache lookup must not duplicate registration");
    // Simulate a full graphics cache. Only the last expanded page is old
    // enough to evict; the native LRU scan must reach it and recover cleanly.
    age[kTexturePoolPages-1]=1;backend_fail_once=true;
    const uint32_t recovery_key=0x7000+cycle;
    RequireTexture(bind(pages[0],recovery_key)==0xB0000000u+recovery_key,"Recover from backend pressure using last-page LRU eviction");
    RequireTexture(*(reinterpret_cast<uint32_t*>(meta)+(kTexturePoolPages-1)*4+1)==0 && age[kTexturePoolPages-1]==0,
        "LRU eviction must clear page 119 beyond the old capacity");
    RequireTexture(backend_live.size()==kTexturePoolPages,"LRU recovery must release only the oldest handle");
    CheckTextureArrays(old_free,old_age);
    const auto flush_before=backend_flushes;
    flush();RequireTexture(backend_live.empty() && backend_flushes==flush_before+1,"Flush all 120 hardware pages");
    for (uint32_t i=0;i<kTexturePoolPages;++i) {
      RequireTexture(age[i]==0 && *(reinterpret_cast<uint32_t*>(meta)+i*4+1)==0,"Clear complete cache/age range");
      const uint32_t key=0x4000+cycle*0x100+i;
      RequireTexture(bind(pages[i],key)==0xB0000000u+key,"Rebind after native flush");
    }
    for (uint32_t i=0;i<kTexturePoolPages;++i) {
      const auto* bytes=static_cast<uint8_t*>(pages[i]);
      RequireTexture(bytes[0]==static_cast<uint8_t>(i+1) && bytes[65535]==static_cast<uint8_t>(i+1),"Texture pixels corrupted during cache lifecycle");
      release(pages[i]);
    }
    RequireTexture(backend_live.empty(),"Per-page free must unregister hardware handles");
    CheckTextureArrays(old_free,old_age);
    shutdown();RequireTexture(texture_allocations.empty(),"Native teardown must free every expanded allocation");
    CheckTextureArrays(old_free,old_age);
    // Retail teardown leaves stale globals because full initialization normally
    // happens once per process. Reset them only in this repeated-cycle fixture.
    SafeWrite(g_dungeon_base+kTexturePoolAllocationRva,&empty,4);
    SafeWrite(g_dungeon_base+kTexturePoolAlignedRva,&empty,4);
    SafeWrite(g_dungeon_base+kTexturePoolMetadataRva,&empty,4);
  }
  uint32_t neighbor_after=0;SafeReadValue(g_dungeon_base+kTexturePoolAgeRva+180,&neighbor_after);
  RequireTexture(neighbor_before==neighbor_after,"Unrelated adjacent global changed");
  RequireTexture(renderer_flushes>=4,"Native renderer flush path exercised");
  MH_Uninitialize();
  std::cout<<"Textures: 120 pages; native init, alignment, exhaustion/reuse, GPU cache lookup/flush/unregister, two teardown cycles and all memory guards passed\n";
}
