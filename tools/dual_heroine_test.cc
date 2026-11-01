#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include "../src/deathtrap_native_render_patch.cc"

void RequireDual(bool ok,const char* message) {
  if(!ok) { std::cerr<<message<<'\n';std::exit(1); }
}
std::ifstream dual_archive;
std::map<uint32_t,std::vector<uint8_t>> dual_owned;
uint32_t dual_loads=0,dual_draws=0,dual_chain_draws=0,expected_draw_id=0;
uintptr_t fixture_node=0;
void* __cdecl LoadDualFixture(uint32_t id,uint32_t type,const char*) {
  RequireDual(!LoadedHeroineResource(id),"Warm loader must not reload a resident resource");
  uint32_t index=0;dual_archive.seekg(0);dual_archive.read(reinterpret_cast<char*>(&index),4);
  std::array<uint32_t,3> entry{};dual_archive.seekg(index+4+(id-1)*12);dual_archive.read(reinterpret_cast<char*>(entry.data()),12);
  const bool mesh=_strnicmp(reinterpret_cast<const char*>(&entry[2]),".prt",4)==0;
  RequireDual((mesh && type==0x42000002u) || (!mesh && type==0x41000002u && entry[1]==65536),"Native resource type must match alias");
  auto& bytes=dual_owned[id];bytes.resize(entry[1]);dual_archive.seekg(entry[0]);dual_archive.read(reinterpret_cast<char*>(bytes.data()),entry[1]);
  RequireDual(dual_archive.good(),"Read actual dual resource");
  if(mesh) {
    reinterpret_cast<void(__cdecl*)(void*,void*,int)>(g_dungeon_base+ActiveDungeonCodeRva(0x8B0D0))(bytes.data(),bytes.data(),2);
  }
  const auto ptr=reinterpret_cast<uintptr_t>(bytes.data());
  SafeWrite(g_dungeon_base+kRenderResourceTableRva+id*4,&ptr,4);++dual_loads;return bytes.data();
}
int __cdecl DrawDualFixture(void* mesh) {
  const int chain=HeroineBodyStage(expected_draw_id)>=0?0:HeroinePelvisResource(expected_draw_id)?1:-1;
  const bool shared=HeroineSharedChains() && HeroineE3VisualActive() && chain>=0 &&
      mesh==reinterpret_cast<void*>(LoadedHeroineResource(HeroineChainResource(chain)));
  RequireDual(shared || mesh==reinterpret_cast<void*>(LoadedHeroineResource(expected_draw_id)),"Selected part/injury state must match its independent resident resource");
  if (shared) ++dual_chain_draws;
  ++dual_draws;return 1927;
}
int __cdecl NodeDualFixture(void* node,void*) {
  RequireDual(reinterpret_cast<uintptr_t>(node)==fixture_node,"Native node argument preserved");
  SafeWrite(g_dungeon_base+kPrtCurrentNodeRva,&fixture_node,4);
  uint32_t id=0;SafeReadValue(static_cast<uint8_t*>(node)+kRenderResourceHandleOffset,&id);
  const auto mesh=LoadedHeroineResource(id);RequireDual(mesh!=0,"Both models warm before native node preparation");
  const auto before=dual_chain_draws;
  const int result=HookHeroinePrtDraw(reinterpret_cast<void*>(mesh));
  const bool chain=HeroineSharedChains() && HeroineE3VisualActive() &&
      (HeroineBodyStage(expected_draw_id)>=0 || HeroinePelvisResource(expected_draw_id));
  RequireDual(dual_chain_draws==before+(chain?1u:0u),"Exactly one shared chain follows its bone; Original never draws E3 metalwork");
  return result;
}
std::vector<uint8_t> ReadDualFile(const std::filesystem::path& path) {
  std::ifstream file(path,std::ios::binary|std::ios::ate);RequireDual(file.good(),"Read file fixture");
  const auto size=file.tellg();file.seekg(0);std::vector<uint8_t> bytes(static_cast<size_t>(size));
  file.read(reinterpret_cast<char*>(bytes.data()),bytes.size());return bytes;
}
void WriteDualFile(const std::filesystem::path& path,const std::vector<uint8_t>& bytes) {
  std::filesystem::create_directories(path.parent_path());std::ofstream f(path,std::ios::binary);
  f.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());RequireDual(f.good(),"Write private fixture");
}
uint32_t OpenDualArchive(const std::filesystem::path& path,bool ansi) {
  HANDLE file=ansi ? HookModelCreateFileA(path.string().c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr)
      : HookModelCreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
  RequireDual(file!=INVALID_HANDLE_VALUE,"Open routed dual archive");uint32_t word=0;DWORD got=0;
  RequireDual(ReadFile(file,&word,4,&got,nullptr)&&got==4,"Routed archive header");CloseHandle(file);return word;
}
unsigned native_base_opens=0,native_overlay_opens=0,native_registers=0;
int __cdecl FixtureArchiveInit(const char*) {
  const uint32_t count=14125,capacity=16384;
  SafeWrite(g_dungeon_base+kRenderResourceCountRva,&count,4);
  SafeWrite(g_dungeon_base+kModelResourceCapacityRva,&capacity,4);
  static const char kind[]=".prt";const auto name=reinterpret_cast<uintptr_t>(kind);
  for (uint32_t id=1;id<count;++id) SafeWrite(g_dungeon_base+kModelResourceEntriesRva+id*16,&name,4);
  return 1;
}
int __cdecl FixtureRegister(uint32_t id,const char*,uint32_t,uint32_t) {
  ++native_registers;static const char kind[]=".prt";const auto name=reinterpret_cast<uintptr_t>(kind);
  return SafeWrite(g_dungeon_base+kModelResourceEntriesRva+id*16,&name,4)?1:0;
}
void* __cdecl FixtureBaseOpen(uint32_t id,const char* mode) {
  ++native_base_opens;
  FILE* file=nullptr;_wfopen_s(&file,(std::filesystem::path(g_model_game_root)/kModelFiles[0]).c_str(),L"rb");
  if (file) std::fseek(file,static_cast<long>(g_model_overlay[id].offset),SEEK_SET);
  (void)mode;return file;
}
void* __cdecl FixtureOverlayOpen(const char* path,const char* mode) {
  ++native_overlay_opens;FILE* file=nullptr;fopen_s(&file,path,mode);return file;
}
int __cdecl FixtureSeek(void* file,long offset,int origin) { return std::fseek(static_cast<FILE*>(file),offset,origin); }
int __cdecl FixtureClose(void* file) { return std::fclose(static_cast<FILE*>(file)); }
void CheckNativeOverlayRouting(const std::filesystem::path& root) {
  RequireDual(InstallModelResourceHooks(),"Four-language native resource entry/open/register/CRT signature guards");
  g_model_archive_init=&FixtureArchiveInit;g_model_resource_register=&FixtureRegister;
  g_model_resource_open=&FixtureBaseOpen;g_model_native_open=&FixtureOverlayOpen;
  g_model_native_seek=&FixtureSeek;g_model_native_close=&FixtureClose;
  const auto old=g_model_overlay[1],alias=g_model_overlay[g_dual_heroine_pack.first_mesh];
  g_model_overlay[1]={4,4,old.type,0};
  g_model_overlay[g_dual_heroine_pack.first_mesh]={4,alias.size,alias.type,0};
  g_model_archive_selection.store(-1);
  RequireDual(HookModelArchiveInit((root/kModelFiles[0]).string().c_str())==1 &&
      g_model_overlay_active.load() && native_registers>0,"Extend original table after native initialization");
  for (const auto id:{1u,g_dual_heroine_pack.first_mesh}) {
    auto* stream=HookModelResourceOpen(id,"rb");uint32_t word=0;
    RequireDual(stream && std::fread(&word,4,1,static_cast<FILE*>(stream))==1 && word==0x52525252,
        "Unchanged resource and Original alias read source BOX, not copied model data");
    FixtureClose(stream);
  }
  auto* stream=HookModelResourceOpen(3903,"rb");
  std::array<uint8_t,52> header{};
  RequireDual(stream && std::fread(header.data(),header.size(),1,static_cast<FILE*>(stream))==1,
      "Changed mesh opens the native overlay stream at the correct offset");
  FixtureClose(stream);
  RequireDual(native_base_opens==2 && native_overlay_opens==1 &&
      HookModelResourceOpen(3903,"r+")==nullptr,"No redirection of original resources or writes");
  const auto path=g_model_overlay_path;g_model_overlay_path+=".missing";
  RequireDual(HookModelResourceOpen(3903,"rb")==nullptr && native_base_opens==2,
      "Missing changed-resource data never falls back to unrelated original bytes");
  g_model_overlay_path=path;g_model_overlay[1]=old;g_model_overlay[g_dual_heroine_pack.first_mesh]=alias;
  g_model_overlay_active.store(false);
}
int RunDualTest(int argc,wchar_t** argv) {
  RequireDual(argc==3,"Pass original Dungeon.dll and dual-model fixture directory");
  const auto pack=std::filesystem::path(argv[2]);
  auto module=LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);RequireDual(module!=nullptr,"Map original DLL");
  g_dungeon_base=reinterpret_cast<uint8_t*>(module);g_dungeon_image_variant=IdentifyDungeonImage(g_dungeon_base);
  RequireDual(g_dungeon_image_variant!=DungeonImageVariant::kUnsupported,"Supported original fixture");
  RequireDual(MH_Initialize()==MH_OK && InstallTexturePoolExpansion() && InstallHeroineWaistHook() &&
      InstallDualHeroineNodeHook() && InstallDualHeroineNodeHook() && HeroineLiveDrawReady(),"Guarded native live-model hook installation");
  const auto temporary=std::filesystem::temp_directory_path();
  const auto root=temporary/(L"native50-dual-"+std::to_wstring(GetCurrentProcessId()));
  RequireDual(!std::filesystem::exists(root),"Private fixture must be new");std::filesystem::create_directory(root);
  g_model_game_root=root.wstring();g_model_create_w=&CreateFileW;g_model_create_a=&CreateFileA;
  for(size_t i=0;i<std::size(kModelFiles);++i) {
    WriteDualFile(root/kModelFiles[i],std::vector<uint8_t>(i==0?4096:12,'R'));
    const auto dest=std::filesystem::path(ModelPackPath(i));std::filesystem::create_directories(dest.parent_path());
    if(i==0) {
      const auto blob=ReadDualFile(pack/L"DUNGEON-E3-RECONSTRUCTION.BOX");
      WriteDualFile(root/L"native50/models/e3/model-data.bin",blob);
      uint32_t offset=0,count=0;std::memcpy(&offset,blob.data(),4);std::memcpy(&count,blob.data()+offset,4);
      std::vector<uint8_t> map(12+(count-1)*16);const uint32_t head[]={0x4F30354Eu,1u,count};
      std::memcpy(map.data(),head,12);
      for(uint32_t id=1;id<count;++id) {
        std::memcpy(map.data()+12+(id-1)*16,blob.data()+offset+4+(id-1)*12,12);
        const uint32_t source=1;std::memcpy(map.data()+12+(id-1)*16+12,&source,4);
      }
      WriteDualFile(root/L"native50/models/e3/resource-map.bin",map);
    }
    else std::filesystem::copy_file(pack/L"patched-pst"/std::filesystem::path(kModelFiles[i]).filename(),dest);
  }
  const auto metadata=root/L"native50/models/e3/dual-models.bin";
  const auto valid_meta=ReadDualFile(pack/L"dual-models.bin");WriteDualFile(metadata,valid_meta);
  RequireDual(ModelPackComplete() && ReadDualHeroinePack() && g_dual_heroine_pack.mesh_count==1200,"Read matching archive/preload/metadata set");
  auto broken=valid_meta;broken[4]=99;WriteDualFile(metadata,broken);
  RequireDual(!ReadDualHeroinePack() && !g_dual_heroine_pack.version,"Reject incompatible dual metadata");
  WriteDualFile(metadata,valid_meta);RequireDual(ReadDualHeroinePack(),"Restore dual metadata");
  CheckNativeOverlayRouting(root);
  std::array<uint8_t,16> system_ansi{},system_wide{};
  RequireDual(SafeRead(reinterpret_cast<void*>(&CreateFileA),system_ansi.data(),system_ansi.size()) &&
      SafeRead(reinterpret_cast<void*>(&CreateFileW),system_wide.data(),system_wide.size()),"Read system entry points");
  RequireDual(InstallModelFileImports() && !g_model_file_imports.empty(),"Route game imports without global API detours");
  const auto imports=g_model_file_imports;
  g_model_archive_selection.store(2);
  bool checked_import=false;
  for(const auto& slot:imports) if(slot.replacement==reinterpret_cast<void*>(&HookModelCreateFileA)) {
    const auto open=reinterpret_cast<ModelCreateFileA>(*slot.slot);
    const auto path=(root/kModelFiles[1]).string();
    HANDLE file=open(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    RequireDual(file!=INVALID_HANDLE_VALUE,"Actual game import opens redirected preload");
    uint32_t count=0;DWORD got=0;
    RequireDual(ReadFile(file,&count,sizeof(count),&got,nullptr) && got==sizeof(count),"Read routed preload count");
    CloseHandle(file);
    const auto preload=ReadDualFile(std::filesystem::path(ModelPackPath(1)));uint32_t expected=0;
    std::memcpy(&expected,preload.data(),sizeof(expected));
    RequireDual(count==expected,"Game import reads reconstructed list, not original fixture");
    file=open(path.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    RequireDual(file!=INVALID_HANDLE_VALUE && GetFileSize(file,nullptr)==12,"Write-capable opens remain on the original file");
    CloseHandle(file);checked_import=true;
  }
  RequireDual(checked_import && InstallModelFileImports() && g_model_file_imports.size()==imports.size(),
      "Import routing is present and idempotent");
  std::array<uint8_t,16> after_ansi{},after_wide{};
  RequireDual(SafeRead(reinterpret_cast<void*>(&CreateFileA),after_ansi.data(),after_ansi.size()) &&
      SafeRead(reinterpret_cast<void*>(&CreateFileW),after_wide.data(),after_wide.size()) &&
      after_ansi==system_ansi && after_wide==system_wide,"System file API instructions remain byte-exact");
  RequireDual(RestoreModelFileImports() && g_model_file_imports.empty(),"Restore every owned import");
  for(const auto& slot:imports) RequireDual(*slot.slot==slot.original,"Import rollback is byte-exact");
  g_e3_model_requested.store(false);g_model_archive_selection.store(-1);
  std::ifstream header_file(pack/L"DUNGEON-E3-RECONSTRUCTION.BOX",std::ios::binary);uint32_t expected_header=0;header_file.read(reinterpret_cast<char*>(&expected_header),4);
  RequireDual(OpenDualArchive(root/kModelFiles[0],true)==0x52525252 && g_model_archive_selection.load()==2,
      "Original selection opens the unchanged retail BOX at startup");
  g_e3_model_requested.store(true);RequireDual(OpenDualArchive(root/kModelFiles[0],false)==0x52525252 && g_model_archive_selection.load()==2,
      "Live choice keeps the original archive open; changed resources use overlay");
  g_model_archive_selection.store(-1);g_dual_heroine_node_installed=false;
  RequireDual(OpenDualArchive(root/kModelFiles[0],false)==0x52525252 && g_model_archive_selection.load()==0,
      "Missing live support must fall back consistently to retail files");
  g_dual_heroine_node_installed=true;g_model_archive_selection.store(2);g_e3_model_available.store(true);
  dual_archive.open(pack/L"DUNGEON-E3-RECONSTRUCTION.BOX",std::ios::binary);RequireDual(dual_archive.good(),"Actual dual archive fixture");
  const auto count=g_dual_heroine_pack.resources;SafeWrite(g_dungeon_base+kRenderResourceCountRva,&count,4);
  g_dual_heroine_load=&LoadDualFixture;g_original_heroine_prt_draw=&DrawDualFixture;g_original_dual_heroine_node=&NodeDualFixture;
  std::array<uint8_t,256> node{},pelvis{};fixture_node=reinterpret_cast<uintptr_t>(node.data());
  const auto parent=reinterpret_cast<uintptr_t>(pelvis.data());const uint32_t pelvis_id=3890;
  SafeWrite(node.data()+kParentOffset,&parent,4);SafeWrite(pelvis.data()+kRenderResourceHandleOffset,&pelvis_id,4);
  Matrix3x4 parent_matrix{{16384,0,0,0,16384,0,0,0,16384,1300,420,830}};
  const auto body_matrix=MultiplyAffine(ActiveE3WaistBind(),parent_matrix);
  SafeWrite(node.data()+kMatrixOffset,&body_matrix,48);SafeWrite(pelvis.data()+kMatrixOffset,&parent_matrix,48);
  // Warm each team through the actual node hook before checking every native
  // body part and all 12 injury states. The resource loader is instrumented;
  // PRT fixups and geometry initialization execute the original x86 code.
  g_e3_model_requested.store(true);
  for(uint32_t group=0;group<5;++group) {
    const uint32_t id=HeroineResourceFromIndex(group*240);expected_draw_id=id;
    SafeWrite(node.data()+kRenderResourceHandleOffset,&id,4);
    RequireDual(HookDualHeroineNode(node.data(),nullptr)==1927 && g_dual_heroine_groups[group].ready,"Both families resident before scene-node draw");
  }
  const auto loads_after_warm=dual_loads;
  const uint32_t texture_sets=g_dual_heroine_pack.first_mesh-g_dual_heroine_pack.e3_texture_first+
      g_dual_heroine_pack.texture_count;
  RequireDual(loads_after_warm==2*g_dual_heroine_pack.mesh_count+texture_sets+(HeroineSharedChains()?2u:0u),
      "Two model sets, declared texture sets and only two shared chains load once");
  const auto settings=root/L"settings.ini";
  for(int repeat=0;repeat<3;++repeat) for(int e3=0;e3<2;++e3) {
    RequireDual(ChangeModernControlSetting(deathtrap::settings::ControlRow::kHeroineModel,e3,settings.c_str()) &&
        HeroineE3VisualActive()==(e3!=0) && g_model_archive_selection.load()==2,"Menu choice applies now without changing archive ownership");
    RequireDual(GetPrivateProfileIntW(L"Models",L"E3Heroine",7,settings.c_str())==static_cast<UINT>(e3),"Live menu choice persists");
    for(uint32_t i=0;i<1200;++i) {
      const uint32_t id=HeroineResourceFromIndex(i);expected_draw_id=e3?id:g_dual_heroine_pack.first_mesh+i;
      SafeWrite(node.data()+kRenderResourceHandleOffset,&id,4);const auto before=node;const auto before_parent=pelvis;
      RequireDual(HookDualHeroineNode(node.data(),nullptr)==1927 && node==before && pelvis==before_parent,
          "Live swap must preserve pose, scene hierarchy and native resource handles");
    }
    RequireDual(dual_loads==loads_after_warm,"Switches and injury states must not perform new resource I/O");
  }
  // Hide every injury stage without changing the source node, health, pose or
  // resource ownership; both E3 and Original must select the matching clean part.
  std::array<uint8_t,0x40> actor{};
  uintptr_t render_link=reinterpret_cast<uintptr_t>(pelvis.data());
  const auto link=reinterpret_cast<uintptr_t>(&render_link);
  SafeWrite(actor.data()+0x10,&link,4);
  const auto player=reinterpret_cast<uintptr_t>(actor.data());
  SafeWrite(g_dungeon_base+kUiOwnerPointerRva,&player,4);
  for (int e3=0;e3<2;++e3) {
    RequireDual(ChangeModernControlSetting(deathtrap::settings::ControlRow::kHeroineModel,e3,settings.c_str()) &&
        ChangeModernControlSetting(deathtrap::settings::ControlRow::kBodyWounds,0,settings.c_str()),
        "wound visibility and model choice apply independently");
    for(uint32_t i=0;i<1200;++i) {
      const auto id=HeroineResourceFromIndex(i),clean=deathtrap::wounds::HealthyPart(id);
      expected_draw_id=e3?clean:g_dual_heroine_pack.first_mesh+static_cast<uint32_t>(HeroineMeshIndex(clean));
      SafeWrite(node.data()+kRenderResourceHandleOffset,&id,4);
      const auto before=node,before_parent=pelvis;
      RequireDual(HookDualHeroineNode(node.data(),nullptr)==1927 && node==before && pelvis==before_parent,
          "hiding wounds must preserve bones, hierarchy, native state and costume/team");
    }
  }
  RequireDual(ChangeModernControlSetting(deathtrap::settings::ControlRow::kBodyWounds,1,settings.c_str()) &&
      dual_loads==loads_after_warm,"showing/hiding wounds must not reload resident resources");
  // A weapon/death animation can publish its default native hand after the
  // actor has already switched to the full-burn torso. Use actual initialized
  // resources and an animated bone/leaf hierarchy; never mutate that hierarchy.
  std::array<uint8_t,256> torso_bone{},torso_leaf{},hand_bone{};
  const auto tb=reinterpret_cast<uintptr_t>(torso_bone.data());
  const auto tl=reinterpret_cast<uintptr_t>(torso_leaf.data());
  const auto hb=reinterpret_cast<uintptr_t>(hand_bone.data());
  SafeWrite(pelvis.data()+kChildOffset,&tb,4);
  SafeWrite(torso_bone.data()+kParentOffset,&parent,4);SafeWrite(torso_bone.data()+kChildOffset,&tl,4);
  SafeWrite(torso_leaf.data()+kParentOffset,&tb,4);
  SafeWrite(torso_bone.data()+kSiblingOffset,&hb,4);SafeWrite(hand_bone.data()+kParentOffset,&parent,4);
  SafeWrite(hand_bone.data()+kChildOffset,&fixture_node,4);SafeWrite(node.data()+kParentOffset,&hb,4);
  for(uint32_t family=0;family<5;++family) {
    const uint32_t body=family==0?4281:8139+(family-1)*240+220;
    SafeWrite(torso_leaf.data()+kRenderResourceHandleOffset,&body,4);
    for(int side=0;side<2;++side) for(int stage=0;stage<12;++stage) {
      const uint32_t id=family==0 ? (stage==0?(side?3893:3886):4070+(stage-1)*20+(side?17:14)) :
          8139+(family-1)*240+stage*20+(side?11:4);
      const auto burned=family==0?(side?4287u:4284u):body+(side?11u:4u);
      for(int e3=0;e3<2;++e3) for(int wounds=0;wounds<2;++wounds) {
        g_e3_model_requested.store(e3!=0);g_show_body_wounds.store(wounds!=0);
        const auto visible=!wounds?deathtrap::wounds::HealthyPart(id):e3?burned:id;
        expected_draw_id=e3?visible:g_dual_heroine_pack.first_mesh+static_cast<uint32_t>(HeroineMeshIndex(visible));
        SafeWrite(node.data()+kRenderResourceHandleOffset,&id,4);
        const auto before_node=node,before_torso=torso_leaf,before_pelvis=pelvis;
        RequireDual(HookDualHeroineNode(node.data(),nullptr)==1927 && node==before_node &&
            torso_leaf==before_torso && pelvis==before_pelvis,
            "Death/weapon reset retains burned hands; Original and wound toggle stay independent");
      }
    }
  }
  g_e3_model_requested.store(true);g_show_body_wounds.store(true);
  const uint32_t source_hand=3893,burned_body=4281,burned_hand=4287;
  SafeWrite(node.data()+kRenderResourceHandleOffset,&source_hand,4);
  SafeWrite(torso_leaf.data()+kRenderResourceHandleOffset,&burned_body,4);
  expected_draw_id=burned_hand;
  RequireDual(HookDualHeroineNode(node.data(),nullptr)==1927,"Burned death hand before animation replacement");
  for(uint32_t body:{3903u,4081u,4261u,0u}) {
    SafeWrite(torso_leaf.data()+kRenderResourceHandleOffset,&body,4);expected_draw_id=source_hand;
    RequireDual(HookDualHeroineNode(node.data(),nullptr)==1927,
        "Respawn, partial injuries and missing torso never retain a cached burn");
  }
  SafeWrite(torso_leaf.data()+kRenderResourceHandleOffset,&burned_body,4);
  const uintptr_t no_parent=0;SafeWrite(node.data()+kParentOffset,&no_parent,4);expected_draw_id=source_hand;
  RequireDual(HookDualHeroineNode(node.data(),nullptr)==1927,"Detached hand never borrows the live actor's burn");
  SafeWrite(node.data()+kParentOffset,&hb,4);
  const auto saved_hand=LoadedHeroineResource(burned_hand);const uintptr_t absent=0;
  SafeWrite(g_dungeon_base+kRenderResourceTableRva+burned_hand*4,&absent,4);
  SafeWrite(g_dungeon_base+kPrtCurrentNodeRva,&fixture_node,4);
  RequireDual(HookHeroinePrtDraw(reinterpret_cast<void*>(LoadedHeroineResource(source_hand)))==1927 &&
      dual_loads==loads_after_warm,"Missing burn mesh falls back without leaf-render I/O");
  SafeWrite(g_dungeon_base+kRenderResourceTableRva+burned_hand*4,&saved_hand,4);
  SafeWrite(node.data()+kParentOffset,&parent,4);SafeWrite(pelvis.data()+kChildOffset,&no_parent,4);
  RequireDual(dual_loads==loads_after_warm,"Burn correction uses existing resident resources only");
  const uintptr_t no_player=0;SafeWrite(g_dungeon_base+kUiOwnerPointerRva,&no_player,4);
  // Simulate a resource family being freed at a level boundary. The next node
  // entry must reload it before drawing, even if other family members survive.
  const uint32_t id=3904;const auto alias=g_dual_heroine_pack.first_mesh+static_cast<uint32_t>(HeroineMeshIndex(id));
  const uintptr_t empty=0;SafeWrite(g_dungeon_base+kRenderResourceTableRva+alias*4,&empty,4);dual_owned.erase(alias);
  g_e3_model_requested.store(false);expected_draw_id=alias;SafeWrite(node.data()+kRenderResourceHandleOffset,&id,4);
  RequireDual(HookDualHeroineNode(node.data(),nullptr)==1927 && dual_loads==loads_after_warm+1,"Reload freed aliases before native render-state preparation");
  // The leaf hook itself must never try to load missing resources.
  SafeWrite(g_dungeon_base+kRenderResourceTableRva+alias*4,&empty,4);dual_owned.erase(alias);const auto before_leaf=dual_loads;
  expected_draw_id=id;SafeWrite(g_dungeon_base+kPrtCurrentNodeRva,&fixture_node,4);
  RequireDual(HookHeroinePrtDraw(reinterpret_cast<void*>(LoadedHeroineResource(id)))==1927 && dual_loads==before_leaf,"No allocation or disk read in leaf renderer");
  MH_Uninitialize();dual_archive.close();
  const auto absolute_root=std::filesystem::canonical(root);
  RequireDual(absolute_root.parent_path()==std::filesystem::canonical(temporary) &&
      absolute_root.filename()==L"native50-dual-"+std::to_wstring(GetCurrentProcessId()),"Fixture cleanup stays in its exact temporary directory");
  std::filesystem::remove_all(absolute_root);
  std::cout<<"Dual heroine: 1200 parts/states, five teams, live model/wound toggles, burned death hands and respawn isolation, zero reloads, unchanged pose/ownership, level reload recovery and guarded archive routing passed\n";
  return 0;
}
int CheckStartup(int argc,wchar_t** argv) {
  RequireDual(argc==3,"Pass --startup and original Dungeon.dll; run beside the installed model pack");
  auto module=LoadLibraryExW(argv[2],nullptr,DONT_RESOLVE_DLL_REFERENCES);
  RequireDual(module!=nullptr,"Map original DLL for startup");
  g_dungeon_base=reinterpret_cast<uint8_t*>(module);
  g_dungeon_image_variant=IdentifyDungeonImage(g_dungeon_base);
  RequireDual(g_dungeon_image_variant!=DungeonImageVariant::kUnsupported,"Supported startup profile");
  RequireDual(MH_Initialize()==MH_OK,"Startup MinHook initialization");
  g_model_game_root=ModelFullPath(ModuleDirectory().c_str());
  std::cout<<"Startup complete="<<ModelPackComplete()<<" metadata="<<ReadDualHeroinePack()<<'\n';
  RequireDual(InstallTexturePoolExpansion() && InstallHeroineWaistHook() &&
      InstallDualHeroineNodeHook() && HeroineLiveDrawReady(),"Startup live draw hooks");
  const bool ready=InstallHeroineModelHooks();
  std::cout<<"Startup router="<<ready<<" available="<<g_e3_model_available.load()
      <<" metadata_version="<<g_dual_heroine_pack.version<<" native_hooks="<<g_model_resource_hooks_installed<<'\n';
  RequireDual(RestoreModelFileImports(),"Startup game imports restored");
  RequireDual(MH_Uninitialize()==MH_OK,"Startup hooks removed");
  FreeLibrary(module);
  return ready?0:1;
}
int wmain(int argc,wchar_t** argv) {
  try {
    if(argc>1 && std::wcscmp(argv[1],L"--startup")==0) return CheckStartup(argc,argv);
    return RunDualTest(argc,argv);
  }
  catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
