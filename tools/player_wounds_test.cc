#include <windows.h>
#include <iostream>
#include <vector>
#include "../src/deathtrap_native_render_patch.cc"
namespace {
void Require(bool ok,const char* why) {
  if(!ok){std::cerr<<"Player wounds: "<<why<<'\n';std::exit(1);}
}
std::vector<std::array<uint32_t,13>> meshes(10000);
std::array<uint8_t,0x40> player{};
std::array<uint8_t,0x1100> player_data{};
std::array<uint8_t,0x114> player_root{},node{},middle{},enemy{};
uintptr_t player_render_link=0;
uint32_t expected=0,loads=0,draws=0;
template<class T> void Write(void* p,T value) { Require(SafeWrite(p,&value,sizeof(value)),"write fixture"); }
int __cdecl Draw(void* mesh) {
  Require(mesh==meshes[expected].data(),"draw only the expected healthy/native part");++draws;return 147;
}
int __cdecl Node(void* owner,void*) {
  Require(owner==node.data(),"native node argument must be unchanged");return 257;
}
void* __cdecl Load(uint32_t id,uint32_t type,const char*) {
  Require(id==3903 && type==0x42000002u,"load only the required healthy part before drawing");
  ++loads;Write(g_dungeon_base+kRenderResourceTableRva+id*4,reinterpret_cast<uintptr_t>(meshes[id].data()));
  return meshes[id].data();
}
void Render(uint32_t id,bool wounds,bool owns) {
  g_show_body_wounds.store(wounds);
  Write(node.data()+kRenderResourceHandleOffset,id);
  Write(g_dungeon_base+kUiOwnerPointerRva,reinterpret_cast<uintptr_t>(owns?player.data():enemy.data()));
  expected=!wounds && owns?deathtrap::wounds::HealthyPart(id):id;
  const auto before=node;const auto actor_before=player;const auto root_before=player_root;
  const auto health_before=player_data;
  const auto health_mesh=meshes[expected],source_mesh=meshes[id];
  Require(HookHeroinePrtDraw(meshes[id].data())==147 && node==before && player==actor_before &&
      player_root==root_before && player_data==health_before &&
      meshes[expected]==health_mesh && meshes[id]==source_mesh,"render must never edit health, nodes, UVs or meshes");
}
void Cases() {
  loads=0;
  g_model_archive_selection.store(0);
  Write(g_dungeon_base+kRenderResourceCountRva,uint32_t{10000});
  for(uint32_t i=1;i<10000;++i)
    Write(g_dungeon_base+kRenderResourceTableRva+i*4,reinterpret_cast<uintptr_t>(meshes[i].data()));
  Write(g_dungeon_base+kPrtCurrentNodeRva,reinterpret_cast<uintptr_t>(node.data()));
  Write(node.data()+kParentOffset,reinterpret_cast<uintptr_t>(middle.data()));
  Write(middle.data()+kParentOffset,reinterpret_cast<uintptr_t>(player_root.data()));
  // Actual native ownership: actor+0x10 -> render link -> root. Neither the
  // actor nor its health/controller data is an ancestor in the scene tree.
  player_render_link=reinterpret_cast<uintptr_t>(player_root.data());
  Write(player.data()+0x10,reinterpret_cast<uintptr_t>(&player_render_link));
  Write(player.data()+kEntityDataOffset,reinterpret_cast<uintptr_t>(player_data.data()));
  Write(g_dungeon_base+kUiOwnerPointerRva,reinterpret_cast<uintptr_t>(player.data()));
  const int32_t health=13*kHealthFixedScale;
  Write(player_data.data()+kEntityHealthOffset,health);
  g_original_heroine_prt_draw=&Draw;g_original_dual_heroine_node=&Node;g_dual_heroine_load=&Load;
  const auto root=reinterpret_cast<uintptr_t>(player_root.data());
  Require(ResolveLivePlayerRenderRoot()==root && root!=reinterpret_cast<uintptr_t>(player.data()) &&
      IsPlayerVisualNode(reinterpret_cast<uintptr_t>(node.data())) &&
      !IsPlayerVisualNode(reinterpret_cast<uintptr_t>(player.data())),
      "resolve native actor/link/root ownership, never the actor as a scene ancestor");
  int32_t read_health=0;
  Require(ReadEntityHealth(player.data(),&read_health) && read_health==health,
          "fixture must use actual indirect native health data");
  SceneSnapshot scene;scene.nodes[root]={};
  Require(ResolvePlayerRenderNode(scene)==root,"camera and wounds must resolve the same player render root");
  // No startup cache or hit/heal is needed: an already-wounded model loaded
  // with ShowWounds=0 must draw healthy on its first presentation.
  Render(4081,false,true);Render(4081,true,true);Render(4081,false,true);
  for(uint32_t id=3800;id<=9200;++id) {
    const auto clean=deathtrap::wounds::HealthyPart(id);
    Require(deathtrap::wounds::HealthyPart(clean)==clean,"healthy mapping must be idempotent");
    Render(id,true,true);Render(id,false,true);Render(id,false,false);
  }
  // No leaf-time resource I/O; a missing clean part falls back for this draw.
  const uint32_t id=4081;
  Write(node.data()+kRenderResourceHandleOffset,id);
  Write(g_dungeon_base+kUiOwnerPointerRva,reinterpret_cast<uintptr_t>(player.data()));
  Write(g_dungeon_base+kRenderResourceTableRva+3903*4,uintptr_t{0});
  expected=id;g_show_body_wounds.store(false);
  Require(HookHeroinePrtDraw(meshes[id].data())==147 && loads==0,"leaf must never load resources");
  Require(HookDualHeroineNode(node.data(),nullptr)==257 && loads==1,"missing clean part loads before native node preparation");
  expected=3903;Require(HookHeroinePrtDraw(meshes[id].data())==147,"next draw uses loaded clean part");
  Write(middle.data()+kParentOffset,reinterpret_cast<uintptr_t>(node.data()));
  expected=id;Require(HookHeroinePrtDraw(meshes[id].data())==147,"cyclic/unowned hierarchy cannot hide other actors");
  Write(middle.data()+kParentOffset,uintptr_t{0});
  Require(HookHeroinePrtDraw(meshes[id].data())==147,"detached parts remain native");
  Write(middle.data()+kParentOffset,reinterpret_cast<uintptr_t>(player_root.data()));
  player_render_link=reinterpret_cast<uintptr_t>(enemy.data());
  Require(!IsPlayerVisualNode(reinterpret_cast<uintptr_t>(node.data())) &&
      HookHeroinePrtDraw(meshes[id].data())==147,"level/model root replacement cannot reuse stale ownership");
  player_render_link=root;
  Write(player.data()+0x10,uintptr_t{0});
  Require(ResolveLivePlayerRenderRoot()==0 && HookHeroinePrtDraw(meshes[id].data())==147,
          "unloaded actor render link must keep unrelated models native");
  Write(player.data()+0x10,reinterpret_cast<uintptr_t>(&player_render_link));
  // The source resource handle still carries the native injury state, so
  // switching back on restores it without waiting for another hit/heal.
  g_show_body_wounds.store(true);expected=id;
  Require(HookHeroinePrtDraw(meshes[id].data())==147,"enabling wounds immediately restores the actual injury");
}
}
int wmain(int argc,wchar_t** argv) {
  Require(g_show_body_wounds.load() && deathtrap::settings::Controls{}.body_wounds,
          "visual wounds must start enabled without a config key");
  auto* memory=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x380000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
  Require(memory!=nullptr,"allocate fixture");
  for(auto variant:{DungeonImageVariant::kEnglish,DungeonImageVariant::kFrenchGog,
                   DungeonImageVariant::kGermanGog,DungeonImageVariant::kItalianGog}) {
    g_dungeon_base=memory;g_dungeon_image_variant=variant;Cases();
  }
  VirtualFree(memory,0,MEM_RELEASE);
  for(int i=1;i<argc;++i) {
    auto module=LoadLibraryExW(argv[i],nullptr,DONT_RESOLVE_DLL_REFERENCES);
    Require(module!=nullptr,"map retail DLL");
    g_dungeon_base=reinterpret_cast<uint8_t*>(module);g_dungeon_image_variant=IdentifyDungeonImage(g_dungeon_base);
    g_heroine_waist_hook_installed=g_dual_heroine_node_installed=false;
    Require(MH_Initialize()==MH_OK && InstallHeroineWaistHook() && InstallDualHeroineNodeHook(),
            "native render signatures in all localized binaries");
    Require(MH_Uninitialize()==MH_OK,"remove fixture hooks");FreeLibrary(module);
  }
  std::cout<<"Player wounds: default enabled, both characters/teams, immediate render-only toggles, enemy/detached isolation, safe preloading and localized hooks passed\n";
}
