#include <windows.h>
#include <iostream>
#include "../src/deathtrap_native_render_patch.cc"

namespace {
void Require(bool ok,const char* message) {
  if (!ok) { std::cerr<<"Corpse lifecycle: "<<message<<'\n';std::exit(1); }
}
template<typename T> void Put(uintptr_t p,size_t offset,T value) {
  Require(WriteCorpseField(p,offset,value),"fixture write");
}
template<typename T> T Get(uintptr_t p,size_t offset) {
  T value{};Require(ReadCorpseField(p,offset,&value),"fixture read");return value;
}
uintptr_t Game() { return reinterpret_cast<uintptr_t>(g_dungeon_base); }
void __cdecl ActorSceneFixture(uintptr_t node);
struct Actor {
  std::array<uint8_t,0x180> actor{};
  std::array<uint8_t,0x13D4> inner{};
  std::array<uint8_t,0x200> root{},mesh{},attachment{};
  std::array<uint8_t,0x60> definition{},registration{};
  std::array<uint8_t,0xC0> metadata{};
  std::array<uint8_t,16> options{};
  std::array<uintptr_t,2> owner{};
  uintptr_t address() { return reinterpret_cast<uintptr_t>(actor.data()); }
  uintptr_t node() { return reinterpret_cast<uintptr_t>(root.data()); }
  uintptr_t child() { return reinterpret_cast<uintptr_t>(mesh.data()); }
  uintptr_t extra() { return reinterpret_cast<uintptr_t>(attachment.data()); }
  uintptr_t state() { return reinterpret_cast<uintptr_t>(inner.data()); }
  uintptr_t record() { return reinterpret_cast<uintptr_t>(registration.data()); }
  uintptr_t meta() { return reinterpret_cast<uintptr_t>(metadata.data()); }
  void Init(uint16_t flags=0x10) {
    *this={};owner={node(),child()};
    Put(address(),0,reinterpret_cast<uintptr_t>(g_dungeon_base+kCorpseDeathUpdateRva));
    Put(address(),0x10,reinterpret_cast<uintptr_t>(owner.data()));Put(address(),0x2C,state());
    Put(address(),0xC4,meta());Put(address(),0x14,uint32_t{1});
    Put(state(),0x1C,reinterpret_cast<uintptr_t>(g_dungeon_base+kCorpseDamageRva));
    Put(state(),0x24,reinterpret_cast<uintptr_t>(definition.data()));
    Put(reinterpret_cast<uintptr_t>(definition.data()),0x54,reinterpret_cast<uintptr_t>(options.data()));
    Put(reinterpret_cast<uintptr_t>(options.data()),4,flags);
    Put(node(),0x6C,int16_t{2});Put(node(),0x30,child());Put(child(),0x2C,node());
    Put(child(),0x30,extra());Put(extra(),0x2C,child());
    Put(node(),0x58,uint16_t{12});Put(child(),0x3C,uintptr_t{0x123});
    Put(node(),0x100,node()+0x180);Put(child(),0x100,node()+0x180);
    Put(extra(),0x3C,uintptr_t{0x124});Put(extra(),0x100,node()+0x180);
    for (const auto mesh_node:{node(),child(),extra()}) {
      Put(mesh_node,0x10C,reinterpret_cast<uintptr_t>(&ActorSceneFixture));
      Put(mesh_node,0x68,address());
    }
    Put(record(),8,address());Put(record(),0xC,state());
  }
};
std::array<Actor,40> actors;
std::array<uintptr_t,2> free_inner{};
std::array<uint8_t,0x2C> node_pool{};
std::array<std::array<uint8_t,0x200>,64> fragment_nodes{};
std::array<std::array<uint8_t,0x100>,32> fragment_effects{};
bool allocate_fragment_nodes=false;
size_t fragment_node_count=0,fragment_effect_count=0;
unsigned attached_fragments=0;
int fragment_sector_valid=1,fragment_visible=1;
bool fragment_validate_world=false;
std::array<int32_t,3> fragment_world_offset{};
uintptr_t effect_active_list=0;
uintptr_t spawn_table=0;
size_t fixture_count=0;
int animation_done=1;
unsigned ai_unlinks=0,create_calls=0,reset_calls=0,items=0;
std::vector<uintptr_t> destroyed;
std::vector<std::array<uintptr_t,8>> drops;
uintptr_t caught_scene_fault=0;
unsigned scene_callback_calls=0;
int CaptureSceneFault(EXCEPTION_POINTERS* exception);
void* native_render_effect_tail=nullptr;
uint16_t actor_render_effect=0;
unsigned rendered_static_frames=0;
__declspec(naked) void NativeRenderEffectTail(uintptr_t) {
  // Match 3B2F0's frame and execute the original 3B998 post-draw block.
  // The leaf fixture has no children, so it returns via the retail epilogue.
  __asm {
    sub esp,40h
    push ebx
    push esi
    mov esi,dword ptr [esp+4Ch]
    push edi
    push ebp
    xor edi,edi
    mov dword ptr [esp+34h],0
    jmp dword ptr [native_render_effect_tail]
  }
}
DWORD RenderEffectsSafely(uintptr_t leaf) {
  __try { NativeRenderEffectTail(leaf);return 0; }
  __except(CaptureSceneFault(GetExceptionInformation())) { return GetExceptionCode(); }
}
void __cdecl ActorSceneFixture(uintptr_t node) {
  ++scene_callback_calls;
  const uintptr_t actor=Get<uintptr_t>(node,0x68);
  if (actor && !IsRegisteredCorpseActor(actor)) {
    // The user's dump faults at 4A119 after 3B0C0 -> 46900 -> 46C70.
    // Execute that exact native failing leaf on a reclaimed collider.
    reinterpret_cast<uintptr_t(__cdecl*)(uintptr_t)>(
        g_dungeon_base+ActiveDungeonCodeRva(0x4A110u))(actor);
  }
}
void SceneUpdate(uintptr_t root) {
  reinterpret_cast<CorpseActorFn>(g_dungeon_base+ActiveDungeonCodeRva(0x3B0C0u))(root);
}
int CaptureSceneFault(EXCEPTION_POINTERS* exception) {
  caught_scene_fault=reinterpret_cast<uintptr_t>(exception->ExceptionRecord->ExceptionAddress);
  return EXCEPTION_EXECUTE_HANDLER;
}
DWORD SceneUpdateSafely(uintptr_t root) {
  __try { SceneUpdate(root);return 0; }
  __except(CaptureSceneFault(GetExceptionInformation())) { return GetExceptionCode(); }
}
int __cdecl AnimationDone(uintptr_t) { return animation_done; }
int __cdecl ReturnZero() { return 0; }
void __cdecl UnlinkAiFixture(uintptr_t) { ++ai_unlinks; }
void __cdecl DropFixture(uint8_t a,uint8_t b,uint8_t c,uint8_t d,uint8_t e,uint8_t f,uintptr_t p,uintptr_t sector) {
  drops.push_back({a,b,c,d,e,f,p,sector});items+=a+b+c;
}
void __cdecl DestroyModelFixture(uintptr_t root) {
  destroyed.push_back(root);
  Put(root,0x3C,uintptr_t{0});
  auto p=reinterpret_cast<uintptr_t>(node_pool.data());
  Put(p,0x20,Get<int32_t>(p,0x20)+2);
}
uintptr_t __cdecl CreateModelFixture(uintptr_t mesh) {
  ++create_calls;
  if (!allocate_fragment_nodes) return 0x1234;
  Require(fragment_node_count<fragment_nodes.size(),"fragment node fixture capacity");
  auto& storage=fragment_nodes[fragment_node_count++];storage={};
  const auto node=reinterpret_cast<uintptr_t>(storage.data());
  Put(node,0x3C,mesh);Put(node,0x6C,int16_t{1});
  return node;
}
uintptr_t __cdecl AllocateFragmentEffect(uintptr_t) {
  Require(fragment_effect_count<fragment_effects.size(),"fragment effect fixture capacity");
  auto& storage=fragment_effects[fragment_effect_count++];storage={};
  return reinterpret_cast<uintptr_t>(storage.data());
}
void __cdecl AttachFragment(uintptr_t,uintptr_t root) {
  Require(Get<uintptr_t>(root,0x68)!=0 && Get<uintptr_t>(root,0x30)!=0,
      "native fragment must attach its own physics owner and mesh");
  ++attached_fragments;
}
bool ValidFragmentPosition(const int32_t* point) {
  if (!fragment_validate_world) return true;
  for (size_t axis=0;axis<3;++axis)
    if (std::abs(static_cast<int64_t>(point[axis])-fragment_world_offset[axis])>2000) return false;
  return true;
}
uintptr_t __cdecl FragmentSector(const int32_t* point,uintptr_t seed) {
  return fragment_sector_valid && ValidFragmentPosition(point) ? seed:0;
}
int __cdecl FragmentVisible(uintptr_t start_sector,const int32_t* start,uintptr_t end_sector,const int32_t* end) {
  return start_sector>0x10000u && end_sector>0x10000u && fragment_visible &&
      ValidFragmentPosition(start) && ValidFragmentPosition(end);
}
void __cdecl IgnoreSound(uintptr_t,uintptr_t) {}
void __cdecl ResetFixtureBackend() { ++reset_calls; }
void PatchBytes(uintptr_t rva,const void* data,size_t size) {
  auto* p=g_dungeon_base+rva;DWORD old=0,ignored=0;
  Require(VirtualProtect(p,size,PAGE_EXECUTE_READWRITE,&old)!=FALSE,"code fixture protection");
  Require(SafeWrite(p,data,size),"code fixture");
  FlushInstructionCache(GetCurrentProcess(),p,size);VirtualProtect(p,size,old,&ignored);
}
void Jump(uintptr_t rva,void* target) {
  std::array<uint8_t,5> bytes{{0xE9}};
  const int32_t relative=static_cast<int32_t>(reinterpret_cast<uint8_t*>(target)-(g_dungeon_base+rva+5));
  std::memcpy(bytes.data()+1,&relative,4);PatchBytes(rva,bytes.data(),bytes.size());
}
void ResetFixture(size_t count=1) {
  g_retained_corpses.clear();g_corpse_model_transfer=nullptr;g_corpse_releasing=false;
  g_corpse_dispatch_depth=0;g_active_presentation_trace={};g_keep_bodies_enabled.store(true);
  g_corpse_hit_root=0;allocate_fragment_nodes=false;
  fragment_node_count=fragment_effect_count=0;attached_fragments=0;
  fragment_sector_valid=fragment_visible=1;
  fragment_validate_world=false;fragment_world_offset={};
  if (effect_active_list) for (size_t offset=0;offset<16;offset+=4)
    Put(effect_active_list-8,offset,uintptr_t{0});
  fixture_count=count;animation_done=1;items=ai_unlinks=create_calls=0;
  destroyed.clear();drops.clear();free_inner={};node_pool={};
  caught_scene_fault=0;scene_callback_calls=0;
  rendered_static_frames=0;
  Put(reinterpret_cast<uintptr_t>(node_pool.data()),0x20,int32_t{100});
  Put(Game(),kCorpseNodePoolRva,reinterpret_cast<uintptr_t>(node_pool.data()));
  std::array<uint8_t,240> empty{};Require(SafeWrite(reinterpret_cast<void*>(spawn_table),empty.data(),empty.size()),"spawn table");
  for (size_t i=0;i<count;++i) {
    auto& a=actors[i];a.Init();
    Put(a.address(),8,i+1<count ? actors[i+1].address():uintptr_t{0});
    Put(a.address(),0xC,i ? actors[i-1].address():uintptr_t{0});
    Put(a.record(),0,i ? actors[i-1].record():uintptr_t{0});
    Put(a.record(),4,i+1<count ? actors[i+1].record():uintptr_t{0});
    if (i<20) { Put(spawn_table,i*12,a.child());Put(spawn_table,i*12+4,a.address()); }
  }
  Put(Game(),kUiOwnerPointerRva,uintptr_t{0});
  Put(Game(),kCorpseRegisteredRva,count ? actors[0].record():uintptr_t{0});
  Put(Game(),kCorpseAiHeadRva,count ? actors[0].address():uintptr_t{0});
  Put(Game(),kCorpseRegisteredRva+0xC,uintptr_t{0});
  Put(Game(),kCorpseRegisteredRva+0x1C,reinterpret_cast<uintptr_t>(free_inner.data()));
  Put(Game(),kCorpseRegisteredRva+0x20,static_cast<uint16_t>(count));
  Put(Game(),kCorpseRegisteredRva+0x26,uint16_t{0});
}
void GiveItems(size_t i=0);
void Tick();
unsigned Finish(bool check_visual);
DWORD SectorGeometrySafely(uintptr_t sector,int32_t* sphere) {
  __try {
    reinterpret_cast<uintptr_t(__cdecl*)(uintptr_t,int32_t*)>(
        g_dungeon_base+ActiveDungeonCodeRva(0x3DA40u))(sector,sphere);
    return 0;
  } __except(CaptureSceneFault(GetExceptionInformation())) { return GetExceptionCode(); }
}
void CheckDismemberment() {
  // Execute actual 6DB90 cloning and 6DC90 physics/root creation, 3A650 model
  // parenting, 92C90 effect registration and 6E430 callback selection. Replace
  // only allocation, world/sector and sound boundaries with fixed fixtures.
  ResetFixture();GiveItems();Finish(true);allocate_fragment_nodes=true;
  const auto limb=actors[0].child(),tip=actors[0].extra();
  Put(limb,0x6C,int16_t{1});Put(limb,0x6E,int16_t{1});
  Put(tip,0x6C,int16_t{1});Put(tip,0x6E,int16_t{2});
  Put(limb,0x80,CorpseSphere{100,0,0,20});Put(tip,0x80,CorpseSphere{400,0,0,20});
  Actor player;player.Init();
  Put(player.node(),0x80,std::array<int32_t,3>{0,0,0});
  Put(player.address(),0xFC,uint32_t{7});Put(player.address(),0x44,uint8_t{0});
  Put(Game(),kUiOwnerPointerRva,player.address());Put(Game(),kCorpseGoreEnabledRva,int32_t{1});
  std::array<uint8_t,0x340> weapon{};std::array<uint8_t,16> descriptor{};
  std::array<uint8_t,0x200> weapon_node{};
  const auto weapon_address=reinterpret_cast<uintptr_t>(weapon.data());
  const auto weapon_model=reinterpret_cast<uintptr_t>(weapon_node.data());
  Put(player.state(),0x48,weapon_address);
  Put(weapon_address,4,reinterpret_cast<uintptr_t>(descriptor.data()));
  Put(reinterpret_cast<uintptr_t>(descriptor.data()),5,int8_t{1});
  Put(weapon_address,8,weapon_model);
  const auto aim=[&](int32_t x) {
    Put(weapon_model,0x80,CorpseSphere{x,0,0,10});
    for (size_t i=0;i<3;++i) Put(weapon_address,0x58+i*0xF0,std::array<int32_t,3>{x,0,0});
  };
  const auto strike=[&]() {
    return reinterpret_cast<int(__cdecl*)(uintptr_t)>(g_dungeon_base+kCorpseMeleeContactsRva)(player.address());
  };
  aim(1000);strike();
  Require(fragment_effect_count==0 && Get<uintptr_t>(limb,0x3C)==0x123,
      "a swing outside native weapon bounds must not cut a retained corpse");
  aim(100);g_active_presentation_trace.stage=DeathtrapNativePresentationStage::kMidpoint;strike();
  g_active_presentation_trace.stage=DeathtrapNativePresentationStage::kNone;
  Require(fragment_effect_count==0,"synthetic frames cannot sever body parts");
  Put(Game(),kCorpseGoreEnabledRva,int32_t{0});strike();
  Put(Game(),kCorpseGoreEnabledRva,int32_t{1});
  g_retained_corpses[0].death_flags|=0x20u;strike();
  g_retained_corpses[0].death_flags&=~0x20u;
  Require(fragment_effect_count==0,"retail violence and no-gore flags must be respected");
  fragment_sector_valid=0;strike();fragment_sector_valid=1;
  fragment_visible=0;strike();fragment_visible=1;
  Require(fragment_effect_count==0 && Get<uintptr_t>(limb,0x3C)==0x123,
      "invalid sector or an intervening wall must leave the limb intact");
  Put(reinterpret_cast<uintptr_t>(node_pool.data()),0x20,int32_t{17});strike();
  Require(fragment_effect_count==0 && g_retained_corpses.size()==1,
      "low geometry capacity must not free the body in the middle of a hit");
  Put(reinterpret_cast<uintptr_t>(node_pool.data()),0x20,int32_t{100});
  const int retail_result=g_original_corpse_melee_contacts(player.address());
  Require(strike()==retail_result && fragment_effect_count==1 && attached_fragments==1 &&
      Get<uintptr_t>(limb,0x3C)==0 && Get<uintptr_t>(tip,0x3C)==0x124,
      "native contact stage must create one separate limb and preserve its original return value");
  const auto effect=reinterpret_cast<uintptr_t>(fragment_effects[0].data());
  const auto fragment_owner=Get<uintptr_t>(effect,0x10);
  const auto fragment_root=Get<uintptr_t>(fragment_owner,0);
  const auto fragment_mesh=Get<uintptr_t>(fragment_root,0x30);
  const auto corpse_sector=Get<uintptr_t>(actors[0].node(),0x100);
  Require(Get<uintptr_t>(effect,0x38)==player.address() && Get<uintptr_t>(fragment_root,0x68)==effect &&
      Get<uintptr_t>(fragment_mesh,0x3C)==0x123 && Get<uintptr_t>(fragment_mesh,0x68)==0 &&
      Get<uintptr_t>(fragment_root,0x100)==corpse_sector &&
      Get<uintptr_t>(fragment_root,0x104)==corpse_sector &&
      Get<uintptr_t>(effect,0)!=0 && (Get<uintptr_t>(effect_active_list,0)==effect ||
      Get<uintptr_t>(effect_active_list-8,0)==effect),
      "fragment must have retail physics/callbacks and a live striker source, never its deleted NPC");
  const auto native_contains=reinterpret_cast<int(__cdecl*)(const int32_t*,uintptr_t)>(
      g_dungeon_base+kCorpseSectorContainsPointRva);
  std::array<int32_t,4> geometry_sphere{100,0,0,20};
  Require(native_contains(geometry_sphere.data(),corpse_sector)==1 && corpse_sector!=1,
      "ORIGINAL 2FB00 returns boolean 1, not its input sector address");
  Require(SectorGeometrySafely(1,geometry_sphere.data())==EXCEPTION_ACCESS_VIOLATION &&
      caught_scene_fault==reinterpret_cast<uintptr_t>(g_dungeon_base+ActiveDungeonCodeRva(0x3D964u)),
      "negative control must reproduce the user's exact invalid-sector crash at 3D964");
  Require(SectorGeometrySafely(Get<uintptr_t>(fragment_root,0x100),geometry_sphere.data())==0,
      "actual fragment sector must survive the same ORIGINAL native geometry path");
  Tick();Tick();
  Require(Get<uintptr_t>(limb,0x3C)==0 && items==15 && drops.size()==1 && ai_unlinks==1 &&
      Get<uint32_t>(spawn_table,8)==1,"cut meshes must stay cut without repeating loot, AI or spawn processing");
  aim(400);strike();
  Require(fragment_effect_count==1,"one attack ID must not repeatedly sever the same corpse");
  Put(player.address(),0xFC,uint32_t{8});strike();
  Require(fragment_effect_count==2 && Get<uintptr_t>(tip,0x3C)==0,
      "a later swing must still cut another part of the same retained body");

  // Current node misses, but one of the SAME intermediate weapon points used
  // by native 6B310 intersects the body. Fast swings must not skip contacts.
  ResetFixture();Finish(true);allocate_fragment_nodes=true;
  Put(limb,0x6C,int16_t{1});Put(limb,0x6E,int16_t{1});
  Put(limb,0x80,CorpseSphere{100,0,0,20});
  Put(tip,0x6C,int16_t{1});Put(tip,0x6E,int16_t{2});
  Put(tip,0x80,CorpseSphere{400,0,0,20});
  Put(Game(),kUiOwnerPointerRva,player.address());Put(Game(),kCorpseGoreEnabledRva,int32_t{1});
  aim(1000);Put(weapon_address,0x58+0xF0,std::array<int32_t,3>{100,0,0});strike();
  Require(fragment_effect_count==1 && Get<uintptr_t>(limb,0x3C)==0,
      "native intermediate sweep contact must sever even if the current pose misses");
  g_keep_bodies_enabled.store(false);MaintainRetainedCorpses();
  Require(g_retained_corpses.empty() && fragment_effect_count==1,
      "disabling corpse retention must leave detached pieces under ordinary effect ownership");

  // Real animation roots are resource-less grouping nodes. Their own-mesh
  // sphere at +80 stays zero even when the actor and its weapon are far from
  // world origin. The former implementation used that zero as the LOS start,
  // so a permissive all-visible sector mock concealed the live-game failure.
  ResetFixture();GiveItems();Finish(true);allocate_fragment_nodes=true;
  fragment_validate_world=true;fragment_world_offset={50000,2000,-30000};
  Put(player.node(),0,std::array<int32_t,3>{49900,2000,-30000});
  Put(player.node(),0x80,CorpseSphere{0,0,0,0});Put(player.node(),0x3C,uintptr_t{0});
  Put(actors[0].node(),0,fragment_world_offset);
  Put(limb,0x6C,int16_t{1});Put(limb,0x6E,int16_t{1});
  Put(limb,0x80,CorpseSphere{50100,2000,-30000,20});
  Put(tip,0x6C,int16_t{1});Put(tip,0x6E,int16_t{2});
  Put(tip,0x80,CorpseSphere{50400,2000,-30000,20});
  Put(weapon_model,0x80,CorpseSphere{50100,2000,-30000,10});
  for (size_t i=0;i<3;++i) Put(weapon_address,0x58+i*0xF0,std::array<int32_t,3>{50100,2000,-30000});
  Put(Game(),kUiOwnerPointerRva,player.address());Put(Game(),kCorpseGoreEnabledRva,int32_t{1});
  Require(!FragmentVisible(Get<uintptr_t>(player.node(),0x100),
      reinterpret_cast<const int32_t*>(player.node()+0x80),Get<uintptr_t>(actors[0].node(),0x100),
      reinterpret_cast<const int32_t*>(limb+0x80)),"negative control: zero own-mesh bounds must fail real room visibility");
  Require(!SeverCorpseVisual(actors[0].node(),limb,player.address(),{0,0,0},{0,0,0}) &&
      fragment_effect_count==0 && Get<uintptr_t>(limb,0x3C)==0x123,
      "negative control: the previous zero-root-origin route must reproduce the rejected cut");
  strike();
  Require(fragment_effect_count==1 && Get<uintptr_t>(limb,0x3C)==0,
      "a real weapon strike must cut a mature corpse far from world origin despite zero root bounds");
  const auto displaced_effect=reinterpret_cast<uintptr_t>(fragment_effects[0].data());
  Require(Get<int32_t>(displaced_effect,0x54)==200 && Get<int32_t>(displaced_effect,0x58)==0 &&
      Get<int32_t>(displaced_effect,0x5C)==0 && items==15 && drops.size()==1 &&
      Get<uint32_t>(spawn_table,8)==1,"fragment force must use actual actor position, preserving loot/spawn completion");
  Put(player.address(),0xFC,uint32_t{9});
  Put(weapon_model,0x80,CorpseSphere{50400,2000,-30000,10});
  for (size_t i=0;i<3;++i) Put(weapon_address,0x58+i*0xF0,std::array<int32_t,3>{50400,2000,-30000});
  strike();
  Require(fragment_effect_count==2 && Get<uintptr_t>(tip,0x3C)==0,
      "later real-position swings must keep cutting after native actor removal");

  // Genuine native cuts DURING fade must not be mistaken for dissolve zeros
  // and reattached by the saved appearance on the next dispatcher update.
  ResetFixture();GiveItems();Tick();allocate_fragment_nodes=true;
  Require(g_retained_corpses.size()==1 && g_retained_corpses[0].actor!=0,"pending fade fixture");
  const auto clone=reinterpret_cast<uintptr_t(__cdecl*)(uintptr_t)>(g_dungeon_base+kCorpseMeshDetachRva)(limb);
  Require(clone && Get<uintptr_t>(clone,0x3C)==0x123,"real native mesh clone during fade");
  for (int i=0;i<40;++i) Tick();
  Require(Get<uintptr_t>(limb,0x3C)==0 && g_retained_corpses[0].actor==0 && items==15 && drops.size()==1,
      "retail fade must finish normally while preserving a previous real cut");
}
void GiveItems(size_t i) {
  Put(actors[i].meta(),0xB8,uint32_t{11u|(3u<<7)|(13u<<12)|(5u<<19)|(17u<<24)});
  Put(actors[i].meta(),0xBC,uint32_t{7});
}
void GiveActorRenderEffect(size_t i=0) {
  for (const auto node:{actors[i].node(),actors[i].child(),actors[i].extra()}) {
    Put(node,0x24,Get<uint32_t>(node,0x24)|0x80u);
    Put(node,0x28,static_cast<uint16_t>((actor_render_effect<<8)|0x2Au));
  }
}
void MakeLiving(size_t i) {
  Put(actors[i].state(),0x1030,int32_t{50});
  Put(actors[i].address(),0,reinterpret_cast<uintptr_t>(g_dungeon_base+ActiveDungeonCodeRva(0x80F90u)));
}
void Tick() {
  reinterpret_cast<uintptr_t(__cdecl*)()>(g_dungeon_base+kPlayerMovementStageRva)();
  // Real scene traversal follows actor updates in 39100. This must run AFTER
  // deletion too: a retained mesh is still visited when its actor has gone.
  for (const auto& corpse:g_retained_corpses) {
    SceneUpdate(corpse.root);
    if (!corpse.actor) for (const auto& node:corpse.nodes) if (!Get<uintptr_t>(node.node,0x30)) {
      NativeRenderEffectTail(node.node);++rendered_static_frames;
    }
  }
}
unsigned Finish(bool check_visual) {
  unsigned ticks=0;
  while (IsRegisteredCorpseActor(actors[0].address()) && ticks<64) {
    Tick();++ticks;
    if (check_visual) Require(Get<uintptr_t>(actors[0].child(),0x3C)==0x123 &&
        Get<uintptr_t>(actors[0].extra(),0x3C)==0x124 &&
        Get<uint32_t>(actors[0].child(),0x128)==0,
        "original mesh/material/tint must stay visible throughout retail dissolve");
  }
  Require(ticks<64 && !IsRegisteredCorpseActor(actors[0].address()),"dead actor must leave the active list");
  return ticks;
}
void CheckLifecycle() {
  ResetFixture(2);MakeLiving(1);GiveItems();g_keep_bodies_enabled.store(false);
  const unsigned retail_ticks=Finish(false);
  Require(items==15 && drops.size()==1 && Get<uint32_t>(spawn_table,8)==1 && ai_unlinks==1 &&
      Get<uint16_t>(Game(),kCorpseRegisteredRva+0x20)==1 && destroyed.size()==1,
      "retail baseline must drop items, signal spawner and release actor/model");
  const auto retail_drop=drops[0];

  ResetFixture(2);MakeLiving(1);GiveItems();GiveActorRenderEffect();
  const unsigned held_ticks=Finish(true);
  Require(held_ticks==retail_ticks && drops.size()==1 && drops[0]==retail_drop && items==15,
      "visual retention must keep exact retail deletion timing and original item drops");
  Require(Get<uint32_t>(spawn_table,8)==1 && ai_unlinks==1 && destroyed.empty() &&
      Get<uint16_t>(Game(),kCorpseRegisteredRva+0x20)==1 &&
      Get<uintptr_t>(Game(),kCorpseAiHeadRva)==actors[1].address() &&
      Get<uintptr_t>(Game(),kCorpseRegisteredRva+0x1C)==actors[0].state() &&
      Get<uintptr_t>(actors[0].record(),8)==0 && Get<uintptr_t>(actors[0].record(),0xC)==0 &&
      g_retained_corpses.size()==1 && g_retained_corpses[0].actor==0,
      "FULL native C130/C2A0 must notify spawner, unlink AI and reclaim both pools while only geometry survives");
  for (const auto node:{actors[0].node(),actors[0].child(),actors[0].extra()})
    Require(Get<uintptr_t>(node,0x10C)==0 && Get<uintptr_t>(node,0x68)==0 &&
        !(Get<uint32_t>(node,0x24)&0x80u) && Get<uint16_t>(node,0x28)==0x2A,
        "all nodes must remove actor callbacks/effects while retaining the palette byte");
  const auto callbacks_before=scene_callback_calls;
  for (int i=0;i<10;++i) Tick();
  Require(items==15 && drops.size()==1 && ai_unlinks==1 && scene_callback_calls==callbacks_before &&
      rendered_static_frames>=10,
      "remaining visual must not rerun actor scene callbacks or death/drop/spawn processing");
  // Negative control: restore the stale scene link and reproduce the user's
  // exact native crash, caught locally in this test executable. Then detach
  // again and verify the same scene traversal is safe.
  Put(actors[0].node(),0x68,actors[0].address());
  Put(actors[0].node(),0x10C,reinterpret_cast<uintptr_t>(&ActorSceneFixture));
  Require(SceneUpdateSafely(actors[0].node())==EXCEPTION_ACCESS_VIOLATION &&
      caught_scene_fault==reinterpret_cast<uintptr_t>(g_dungeon_base+ActiveDungeonCodeRva(0x4A119u)),
      "negative control must reproduce the actual dump's freed-collider crash at 4A119");
  Require(MakeCorpseModelStatic(g_retained_corpses[0]) &&
      SceneUpdateSafely(actors[0].node())==0,"detached static corpse must survive the exact failing scene path");
  // The second user dump followed the independent post-render effect path:
  // 3B998 -> 4DFA0 -> 84710 -> 858F0 -> null actor at 85901.
  const auto leaf=actors[0].extra();
  Put(leaf,0x24,Get<uint32_t>(leaf,0x24)|0x80u);
  Put(leaf,0x28,static_cast<uint16_t>((actor_render_effect<<8)|0x2Au));
  Require(RenderEffectsSafely(leaf)==EXCEPTION_ACCESS_VIOLATION &&
      caught_scene_fault==reinterpret_cast<uintptr_t>(g_dungeon_base+ActiveDungeonCodeRva(0x85901u)),
      "negative control must reproduce the actual post-render crash at 85901");
  Require(MakeCorpseModelStatic(g_retained_corpses[0]) && RenderEffectsSafely(leaf)==0,
      "static corpse must disable actor-dependent native render effects");
  Tick();
  Require(!(Get<uint32_t>(leaf,0x24)&0x80u) && Get<uint16_t>(leaf,0x28)==0x2A &&
      RenderEffectsSafely(leaf)==0,"appearance restoration must not re-enable stale render effects");
  g_keep_bodies_enabled.store(false);MaintainRetainedCorpses();
  Require(destroyed.size()==1 && g_retained_corpses.empty() && items==15 && ai_unlinks==1,
      "turning off must destroy only geometry, with no second actor cleanup");

  ResetFixture();GiveItems();Tick();Tick(); // In the middle of the native fade.
  g_keep_bodies_enabled.store(false);Finish(false);
  Require(items==15 && Get<uint32_t>(spawn_table,8)==1 && g_retained_corpses.empty(),
      "disabling mid-fade must resume native appearance/timing and still signal/drop");

  ResetFixture();GiveItems();
  Put(actors[0].address(),0,uintptr_t{0});Put(actors[0].node(),0x24,uint32_t{0x80000});
  Finish(true);
  Require(items==15 && Get<uint32_t>(spawn_table,8)==1 && g_retained_corpses[0].actor==0,
      "old frozen test-save actors must resume normal death cleanup and unblock spawns");
  // A global scene destructor must never be intercepted as an actor transfer.
  reinterpret_cast<CorpseActorFn>(g_dungeon_base+kCorpseModelDestroyRva)(actors[0].node());
  Require(destroyed.size()==1 && g_retained_corpses.empty(),"scene teardown must forget and destroy retained geometry");

  ResetFixture();Finish(true);
  Put(reinterpret_cast<uintptr_t>(node_pool.data()),0x20,int32_t{15});
  Require(HookCorpseModelCreate(7)==0x1234 && create_calls==1 && destroyed.size()==1 &&
      g_retained_corpses.empty() && ai_unlinks==1,"geometry pressure must release an old visual without blocking a new model");

  ResetFixture();animation_done=0;Tick();
  Require(g_retained_corpses.empty(),"unfinished death animation must remain native");
  animation_done=1;Finish(true);
  const auto old_reset=g_original_corpse_reset;g_original_corpse_reset=&ResetFixtureBackend;
  HookCorpseReset();g_original_corpse_reset=old_reset;
  Require(g_retained_corpses.empty() && reset_calls>0 && destroyed.size()==1,"level reset must release visual ownership");

  for (const uint16_t flags:{uint16_t{0x40},uint16_t{0x8000},uint16_t{4},uint16_t{0x12}}) {
    ResetFixture();actors[0].Init(flags);ObserveCorpseDeath(actors[0].address());
    Require(g_retained_corpses.empty(),"native persistent/special deaths must not be retained");
  }
  ResetFixture();MakeLiving(0);ObserveCorpseDeath(actors[0].address());
  Require(g_retained_corpses.empty(),"living entities must never be retained");
  ResetFixture();g_active_presentation_trace.stage=DeathtrapNativePresentationStage::kMidpoint;
  ObserveCorpseDeath(actors[0].address());
  Require(g_retained_corpses.empty(),"synthetic presentation cannot acquire models");
  g_active_presentation_trace.stage=DeathtrapNativePresentationStage::kNone;
  ResetFixture(33);
  for (size_t i=0;i<33;++i) ObserveCorpseDeath(actors[i].address());
  Require(g_retained_corpses.size()==32,"retention memory must be bounded");
}
void CheckOriginal(const wchar_t* path) {
  Require(MH_Initialize()==MH_OK,"MinHook initialization");
  const HMODULE module=LoadLibraryExW(path,nullptr,DONT_RESOLVE_DLL_REFERENCES);
  Require(module!=nullptr,"original image mapping");
  g_dungeon_base=reinterpret_cast<uint8_t*>(module);g_dungeon_image_variant=IdentifyDungeonImage(g_dungeon_base);
  Require(g_dungeon_image_variant!=DungeonImageVariant::kUnsupported,"known localized profile");
  g_corpse_retention_hooks_installed=false;g_original_player_movement_stage=nullptr;
  g_original_corpse_decision=nullptr;g_immersive_root_motion_hooks_installed.store(false);
  const uint8_t before=g_dungeon_base[kCorpseDecisionRva],bad=0x90;
  PatchBytes(kCorpseDecisionRva,&bad,1);
  Require(!InstallCorpseRetentionHooks() && !g_original_corpse_decision,"signature mismatch must stay native");
  PatchBytes(kCorpseDecisionRva,&before,1);
  void* conflict=nullptr;
  Require(MH_CreateHook(g_dungeon_base+kCorpseModelDestroyRva,
      reinterpret_cast<void*>(&DestroyModelFixture),&conflict)==MH_OK,"rollback fixture");
  Require(!InstallCorpseRetentionHooks() && !g_original_corpse_decision && !g_original_corpse_delete,
      "partial installation must roll back all created hooks");
  Require(MH_RemoveHook(g_dungeon_base+kCorpseModelDestroyRva)==MH_OK,"rollback fixture removal");
  Require(InstallCorpseRetentionHooks() && InstallCorpseRetentionHooks(),"verified idempotent hook installation");
  Require(InstallImmersiveRootMotionHooks(),"shared movement-stage ownership");
  Require(SafeReadValue(g_dungeon_base+ActiveDungeonCodeRva(0x1FB20u)+5,&spawn_table),"real spawn-table operand");
  native_render_effect_tail=g_dungeon_base+ActiveDungeonCodeRva(0x3B998u);
  reinterpret_cast<void(__cdecl*)(uintptr_t)>(g_dungeon_base+ActiveDungeonCodeRva(0x3A210u))(
      reinterpret_cast<uintptr_t>(g_dungeon_base+ActiveDungeonCodeRva(0x4DFA0u)));
  uintptr_t effect_table=0;
  Require(SafeReadValue(g_dungeon_base+ActiveDungeonCodeRva(0x4DFA0u)+0x16,&effect_table),"native effect-table operand");
  bool actor_effect_found=false;
  for (unsigned i=0;i<256;++i) if (Get<uintptr_t>(effect_table,i*4)==
      reinterpret_cast<uintptr_t>(g_dungeon_base+ActiveDungeonCodeRva(0x84710u))) {
    actor_render_effect=static_cast<uint16_t>(i);actor_effect_found=true;break;
  }
  Require(actor_effect_found,"real actor-dependent post-render effect must exist");
  Jump(ActiveDungeonCodeRva(0x5BAB0u),reinterpret_cast<void*>(&AnimationDone));
  Jump(ActiveDungeonCodeRva(0x38A30u),reinterpret_cast<void*>(&DropFixture));
  Jump(ActiveDungeonCodeRva(0x95810u),reinterpret_cast<void*>(&UnlinkAiFixture));
  Jump(ActiveDungeonCodeRva(0x403F0u),reinterpret_cast<void*>(&ReturnZero));
  // Resolve the real physics-effect list operand before replacing allocator.
  Require(SafeReadValue(g_dungeon_base+ActiveDungeonCodeRva(0x92C90u)+2,&effect_active_list),
      "native fragment active-list operand");
  Put(effect_active_list,0,uintptr_t{0});Put(effect_active_list,4,uintptr_t{0});
  Jump(ActiveDungeonCodeRva(0x92D50u),reinterpret_cast<void*>(&AllocateFragmentEffect));
  // Leave ORIGINAL 2FB00 in place: it returns BOOL, unlike the former mock
  // which returned its seed pointer and concealed the exact live-game crash.
  Jump(kResolveCameraSectorRva,reinterpret_cast<void*>(&FragmentSector));
  Jump(kCameraRoomTraceVisibleRva,reinterpret_cast<void*>(&FragmentVisible));
  Jump(ActiveDungeonCodeRva(0x39040u),reinterpret_cast<void*>(&AttachFragment));
  Jump(ActiveDungeonCodeRva(0x3A5B0u),reinterpret_cast<void*>(&IgnoreSound));
  Jump(ActiveDungeonCodeRva(0x7B740u),reinterpret_cast<void*>(&ReturnZero));
  Jump(ActiveDungeonCodeRva(0x7BC80u),reinterpret_cast<void*>(&ReturnZero));
  Jump(ActiveDungeonCodeRva(0xAB850u),reinterpret_cast<void*>(&ReturnZero)); // CRT is not initialized by DONT_RESOLVE.
  const auto old_destroy=g_original_corpse_model_destroy;
  const auto old_create=g_original_corpse_model_create;
  g_original_corpse_model_destroy=&DestroyModelFixture;g_original_corpse_model_create=&CreateModelFixture;
  CheckLifecycle();
  CheckDismemberment();
  g_original_corpse_model_destroy=old_destroy;g_original_corpse_model_create=old_create;
  std::cout<<"Retail fade + full C130/C2A0 + spawn signal + drops + visual retention + native melee/fragments: "
      <<ActiveDungeonProfileName()<<" passed\n";
  Require(MH_Uninitialize()==MH_OK,"hook teardown");
  g_retained_corpses.clear();FreeLibrary(module);g_dungeon_base=nullptr;
  g_corpse_retention_hooks_installed=false;g_original_player_movement_stage=nullptr;
}
}
int wmain(int argc,wchar_t** argv) {
  if (argc==1) {
    Require(!InstallCorpseRetentionHooks(),"missing image must stay native");
    MaintainRetainedCorpses();BeginCorpseFadeUpdate();EndCorpseFadeUpdate();
    std::cout<<"Corpse lifecycle unavailable-image safety passed\n";return 0;
  }
  for (int i=1;i<argc;++i) CheckOriginal(argv[i]);
}
