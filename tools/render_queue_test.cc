#include <windows.h>
#include <iostream>
#include <numeric>
#include <random>
#include "../src/deathtrap_native_render_patch.cc"

void RequireQueue(bool ok,const char* message) {
  if(!ok) { std::cerr<<message<<'\n';std::exit(1); }
}
void SortNativeQueue(uintptr_t sort_function,uint32_t count) {
  __asm {
    mov eax,count
    call sort_function
  }
}
uint32_t queue_allocation_size=0;
void* __cdecl AllocateQueueFixture(uint32_t bytes,const char*) {
  queue_allocation_size=bytes;return HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,bytes);
}
void __cdecl FreeQueueFixture(void* pointer) { HeapFree(GetProcessHeap(),0,pointer); }
int wmain(int argc,wchar_t** argv) {
  RequireQueue(argc==2,"Pass one supported original Dungeon.dll");
  // Load only PE sections/relocations, without running game initialization or
  // imports. The radix sorter itself has no external dependencies.
  auto module=LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
  RequireQueue(module!=nullptr,"Cannot map Dungeon.dll");
  g_dungeon_base=reinterpret_cast<uint8_t*>(module);
  g_dungeon_image_variant=IdentifyDungeonImage(g_dungeon_base);
  RequireQueue(g_dungeon_image_variant!=DungeonImageVariant::kUnsupported,"Unsupported fixture");
  const auto base_rva=static_cast<uintptr_t>(kRenderQueueBaseRva);
  const uintptr_t live=1,empty=0;
  RequireQueue(SafeWrite(g_dungeon_base+base_rva,&live,sizeof(live)),"Fixture write");
  RequireQueue(!InstallRenderQueueExpansion(),"A live command arena must not be replaced");
  RequireQueue(SafeWrite(g_dungeon_base+base_rva,&empty,sizeof(empty)),"Fixture clear");
  auto& first=kRenderQueueReferences[0];
  const auto site=ActiveDungeonCodeRva(first.code);
  uint32_t original=0;SafeReadValue(g_dungeon_base+site,&original);
  RequireQueue(PatchDungeonCode(site,RenderQueueWord(original),RenderQueueWord(original+1)),"Corrupt fixture");
  RequireQueue(!InstallRenderQueueExpansion(),"Changed reference must reject the whole patch");
  RequireQueue(MatchesDungeonCode(ActiveDungeonCodeRva(0x65230),RenderQueueWord(0xBE000)),"Rejected install must leave arena size unchanged");
  RequireQueue(PatchDungeonCode(site,RenderQueueWord(original+1),RenderQueueWord(original)),"Restore fixture");
  RequireQueue(InstallRenderQueueExpansion(),"Queue expansion failed");
  RequireQueue(InstallRenderQueueExpansion(),"Queue expansion must be idempotent");
  RequireQueue(MatchesDungeonCode(ActiveDungeonCodeRva(0x65230),RenderQueueWord(kRenderQueueAllocationBytes)),"Allocation size mismatch");
  RequireQueue(MatchesDungeonCode(ActiveDungeonCodeRva(0x6524B),RenderQueueWord(kRenderQueueCommandBytes)),"Arena limit mismatch");
  RequireQueue(MH_Initialize()==MH_OK,"Initialize allocator fixture hooks");
  void* alloc=g_dungeon_base+ActiveDungeonCodeRva(0x2B9C0);
  void* free=g_dungeon_base+ActiveDungeonCodeRva(0x2B980);
  RequireQueue(MH_CreateHook(alloc,reinterpret_cast<void*>(&AllocateQueueFixture),nullptr)==MH_OK &&
      MH_CreateHook(free,reinterpret_cast<void*>(&FreeQueueFixture),nullptr)==MH_OK &&
      MH_EnableHook(alloc)==MH_OK && MH_EnableHook(free)==MH_OK,"Allocator fixture hooks");
  reinterpret_cast<void(__cdecl*)()>(g_dungeon_base+ActiveDungeonCodeRva(0x65220))();
  uintptr_t arena=0,end=0,cursor=0,pending=0;
  SafeReadValue(g_dungeon_base+base_rva,&arena);
  SafeReadValue(g_dungeon_base+base_rva-4,&end);
  SafeReadValue(g_dungeon_base+base_rva-8,&cursor);
  SafeReadValue(g_dungeon_base+base_rva+4,&pending);
  RequireQueue(arena && queue_allocation_size==kRenderQueueAllocationBytes &&
      end-arena==kRenderQueueCommandBytes && cursor==arena && pending==arena,"Native arena initialization mismatch");
  reinterpret_cast<void(__cdecl*)()>(g_dungeon_base+ActiveDungeonCodeRva(0x65270))();
  MH_Uninitialize();
  for(const auto ref:kRenderQueueReferences) {
    const auto expected=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_render_queue_arrays[ref.array])+ref.bias);
    RequireQueue(MatchesDungeonCode(ActiveDungeonCodeRva(ref.code),RenderQueueWord(expected)),"Array reference not relocated");
  }
  std::mt19937 random(1439);
  std::vector<uint16_t> depth(kRenderQueueEntries),layer(kRenderQueueEntries);
  for(size_t i=0;i<depth.size();++i) { depth[i]=static_cast<uint16_t>(random());layer[i]=static_cast<uint16_t>(random()); }
  auto* d=reinterpret_cast<uint16_t*>(g_render_queue_arrays[0]);
  auto* l=reinterpret_cast<uint16_t*>(g_render_queue_arrays[1]);
  auto* p=reinterpret_cast<uint32_t*>(g_render_queue_arrays[2]);
  // 0, 1, the old boundary, an ordinary wounded model, and the new full limit.
  for(uint32_t count:{0u,1u,8192u,16000u,kRenderQueueEntries}) {
    std::vector<uint32_t> expected(count);std::iota(expected.begin(),expected.end(),0u);
    const auto key=[&](uint32_t i) { return (static_cast<uint32_t>(layer[i])<<16)|depth[i]; };
    std::stable_sort(expected.begin(),expected.end(),[&](uint32_t a,uint32_t z){return key(a)>key(z);});
    for(size_t i=0;i<6;++i) {
      const size_t stride=i%3==2?4:2;
      std::memset(g_render_queue_arrays[i]-8,0xA5,8);
      std::memset(g_render_queue_arrays[i]+stride*kRenderQueueEntries,0xA5,8);
    }
    for(uint32_t i=0;i<count;++i) { d[i]=depth[i];l[i]=layer[i];p[i]=i+1; }
    SortNativeQueue(reinterpret_cast<uintptr_t>(g_dungeon_base)+ActiveDungeonCodeRva(0xA8520),count);
    for(uint32_t i=0;i<count;++i) RequireQueue(p[i]==expected[i]+1 && d[i]==depth[expected[i]] && l[i]==layer[expected[i]],"Native radix ordering changed");
    for(size_t i=0;i<6;++i) {
      const size_t stride=i%3==2?4:2;
      for(size_t j=0;j<8;++j) RequireQueue(g_render_queue_arrays[i][-8+static_cast<ptrdiff_t>(j)]==0xA5 && g_render_queue_arrays[i][stride*kRenderQueueEntries+j]==0xA5,"Sorter crossed an array boundary");
    }
  }
  std::cout<<"Native RenderQ: 65536 entries, original sorting and all buffer guards passed\n";
  return 0;
}
