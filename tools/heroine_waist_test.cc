#include <windows.h>
#include <fstream>
#include <filesystem>
#include <iostream>
#include "../src/deathtrap_native_render_patch.cc"

void RequireWaist(bool ok,const char* message) {
  if (!ok) { std::cerr<<message<<'\n';std::exit(1); }
}
std::vector<uint8_t> WaistResource(std::ifstream& archive,uint32_t id) {
  uint32_t index=0;archive.seekg(0);archive.read(reinterpret_cast<char*>(&index),4);
  std::array<uint32_t,3> entry{};archive.seekg(index+4+(id-1)*12);
  archive.read(reinterpret_cast<char*>(entry.data()),12);
  RequireWaist(entry[1]>=52 && entry[1]<16*1024*1024,"Invalid fixture resource");
  std::vector<uint8_t> result(entry[1]);archive.seekg(entry[0]);archive.read(reinterpret_cast<char*>(result.data()),entry[1]);
  RequireWaist(archive.good(),"Cannot read BOX resource");return result;
}
void InitializeWaistMesh(std::vector<uint8_t>* mesh) {
  reinterpret_cast<void(__cdecl*)(void*,void*,int)>(g_dungeon_base+ActiveDungeonCodeRva(0x8B0D0))(
      mesh->data(),mesh->data(),2);
}
bool draw_expected_changed=false,draw_exception=false;
const std::vector<uint8_t>* draw_before=nullptr;
int __cdecl WaistDrawFixture(void* mesh) {
  const bool changed=std::memcmp(mesh,draw_before->data(),draw_before->size())!=0;
  RequireWaist(changed==draw_expected_changed,"Draw must receive exactly the selected geometry");
  if (draw_exception) RaiseException(0xE0415741,0,0,nullptr);
  return 1457;
}
bool CatchWaistFixture(void* mesh) {
  __try { HookHeroinePrtDraw(mesh); }
  __except(EXCEPTION_EXECUTE_HANDLER) { return true; }
  return false;
}
int wmain(int argc,wchar_t** argv) {
  RequireWaist(argc==4 || argc==5,"Pass original Dungeon.dll, reconstructed BOX, native animation pose fixture and optional pack version");
  g_dual_heroine_pack.version=2;
  RequireWaist(&ActiveE3WaistBind()==&kE3WaistBind,"Legacy pack must retain its rotated waist bind");
  g_dual_heroine_pack.version=3;
  RequireWaist(&ActiveE3WaistBind()==&kE3SymmetricWaistBind,"New pack must use the symmetric waist bind");
  RequireWaist(E3WaistBlendStart()==-20 && E3WaistPelvisEnd()==50,"v3 overlap profile must remain unchanged");
  g_dual_heroine_pack.version=4;
  RequireWaist(&ActiveE3WaistBind()==&kE3SymmetricWaistBind &&
      E3WaistBlendStart()==-12 && E3WaistPelvisEnd()==35,"Shared seam profile must use its actual boundary");
  WaistWarp shifted{};
  for(int i=0;i<3;++i) shifted.rotation[i][i]=1;
  shifted.translation[0]=20;
  WaistGeometry transition{0,-44,0,0,0,16384},shifted_transition{};
  RequireWaist(WarpWaistGeometry(shifted,transition,&shifted_transition) &&
      shifted_transition[0]>0 && shifted_transition[0]<20,"v4 must not rigidly freeze the obsolete overlap ring");
  WaistGeometry shared_seam_point{0,-59,0,0,0,16384},shifted_seam{};
  RequireWaist(WarpWaistGeometry(shifted,shared_seam_point,&shifted_seam) && shifted_seam[0]==20,
      "Actual shared seam must stay fully anchored");
  g_dual_heroine_pack.version=argc==5?static_cast<uint32_t>(_wtoi(argv[4])):2u;
  RequireWaist(g_dual_heroine_pack.version>=2 && g_dual_heroine_pack.version<=4,"Supported fixture pack version");
  const auto& bind=ActiveE3WaistBind();
  auto module=LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
  RequireWaist(module!=nullptr,"Cannot map original Dungeon.dll");
  g_dungeon_base=reinterpret_cast<uint8_t*>(module);g_dungeon_image_variant=IdentifyDungeonImage(g_dungeon_base);
  RequireWaist(g_dungeon_image_variant!=DungeonImageVariant::kUnsupported,"Unsupported original DLL");
  RequireWaist(MH_Initialize()==MH_OK && InstallHeroineWaistHook() && InstallHeroineWaistHook(),"Guarded native hook installation");
  std::ifstream archive(std::filesystem::path(argv[2]),std::ios::binary);
  std::ifstream fixtures(std::filesystem::path(argv[3]),std::ios::binary);
  uint32_t count=0;fixtures.read(reinterpret_cast<char*>(&count),4);RequireWaist(count>10 && count<1000,"Pose fixture count");
  std::vector<std::array<Matrix3x4,2>> poses(count);
  fixtures.read(reinterpret_cast<char*>(poses.data()),count*sizeof(poses[0]));RequireWaist(fixtures.good(),"Pose fixture bytes");
  size_t meshes=0,samples=0,vertices_checked=0;
  for (uint32_t id=1;id<10000;++id) {
    if (HeroineBodyStage(id)<0) continue;
    auto mesh=WaistResource(archive,id);InitializeWaistMesh(&mesh);
    const auto before=mesh;
    const auto* header=reinterpret_cast<const uint32_t*>(mesh.data());
    const auto* vertices=reinterpret_cast<const uint8_t*>(header[11]);
    for (const auto& pose:poses) {
      WaistWarp warp{};RequireWaist(MakeWaistWarp(pose[0],pose[1],&warp),"Native pose warp");
      WaistDrawTransaction transaction;
      const bool prepared=PrepareWaistDraw(mesh.data(),warp,&transaction);
      RequireWaist(prepared,"Every supported body state must prepare");
      RequireWaist(transaction.Apply(),"Apply temporary geometry");
      for (uint32_t i=0;i<header[10];++i) {
        const auto original_offset=reinterpret_cast<uintptr_t>(vertices)-reinterpret_cast<uintptr_t>(mesh.data())+i*32+4;
        WaistGeometry original{},current{};
        std::memcpy(original.data(),before.data()+original_offset,24);std::memcpy(current.data(),vertices+i*32+4,24);
        if (original[1]>=E3WaistBlendStart()) RequireWaist(original==current,"Chest/shoulders/chains above waist must stay byte-identical");
        const double pelvis_y=bind.values[10]+(
            original[0]*bind.values[1]+original[1]*bind.values[4]+
            original[2]*bind.values[7])/16384.;
        // Include the overlap ring above the tip. The former <=33 check
        // missed its up-to-24-unit excursion in running poses.
        if (pelvis_y<=E3WaistPelvisEnd()) {
          const std::array<int32_t,3> point{original[0],original[1],original[2]},changed{current[0],current[1],current[2]};
          const auto seam=CameraMeshPointToWorld(MultiplyAffine(bind,pose[0]),point);
          const auto actual=CameraMeshPointToWorld(pose[1],changed);
          const double error=std::sqrt((seam.x-actual.x)*(seam.x-actual.x)+(seam.y-actual.y)*(seam.y-actual.y)+(seam.z-actual.z)*(seam.z-actual.z));
          RequireWaist(error<2,"Waist must remain anchored to pelvis within fixed-point rounding");
        }
        const double length=std::sqrt(static_cast<double>(current[3])*current[3]+static_cast<double>(current[4])*current[4]+static_cast<double>(current[5])*current[5]);
        RequireWaist(std::abs(length-16384)<4,"Deformed normals must stay normalized");
        ++vertices_checked;
      }
      transaction.Restore();RequireWaist(mesh==before,"Draw restore must preserve every original byte, including injury UVs");++samples;
    }
    ++meshes;
  }
  RequireWaist(meshes==60,"All teams and all 12 injury stages must be covered");
  // Drive the actual hook with a small actor tree and registry entry.
  auto mesh=WaistResource(archive,3903);InitializeWaistMesh(&mesh);const auto baseline=mesh;draw_before=&baseline;
  std::array<uint8_t,256> node{},pelvis{},actor{};
  const uintptr_t n=reinterpret_cast<uintptr_t>(node.data()),p=reinterpret_cast<uintptr_t>(pelvis.data()),a=reinterpret_cast<uintptr_t>(actor.data()),m=reinterpret_cast<uintptr_t>(mesh.data());
  const uint32_t body_id=3903,pelvis_id=3890;
  const uint32_t resources=10000;SafeWrite(g_dungeon_base+kRenderResourceCountRva,&resources,4);
  SafeWrite(node.data()+kParentOffset,&p,4);SafeWrite(pelvis.data()+kParentOffset,&a,4);
  SafeWrite(node.data()+kRenderResourceHandleOffset,&body_id,4);SafeWrite(pelvis.data()+kRenderResourceHandleOffset,&pelvis_id,4);
  SafeWrite(node.data()+kMatrixOffset,&poses.back()[1],48);SafeWrite(pelvis.data()+kMatrixOffset,&poses.back()[0],48);
  SafeWrite(g_dungeon_base+kPrtCurrentNodeRva,&n,4);SafeWrite(g_dungeon_base+kRenderResourceTableRva+body_id*4,&m,4);
  g_original_heroine_prt_draw=WaistDrawFixture;
  g_model_archive_selection.store(0);draw_expected_changed=false;
  RequireWaist(HookHeroinePrtDraw(mesh.data())==1457 && mesh==baseline,"Retail model must bypass skinning");
  g_model_archive_selection.store(1);draw_expected_changed=true;
  RequireWaist(HookHeroinePrtDraw(mesh.data())==1457 && mesh==baseline,"Hook must restore after normal return");
  draw_exception=true;RequireWaist(CatchWaistFixture(mesh.data()) && mesh==baseline,"Hook must restore during SEH exception unwinding");draw_exception=false;
  // The live PST renderer publishes mesh leaves below resource-free bone
  // transforms. Reproduce that hierarchy instead of assuming mesh->mesh.
  std::array<uint8_t,256> body_bone{},pelvis_bone{};
  const uintptr_t bb=reinterpret_cast<uintptr_t>(body_bone.data()),pb=reinterpret_cast<uintptr_t>(pelvis_bone.data());
  uintptr_t render_root=pb;const uintptr_t link=reinterpret_cast<uintptr_t>(&render_root);
  SafeWrite(actor.data()+0x10,&link,4);SafeWrite(g_dungeon_base+kUiOwnerPointerRva,&a,4);
  SafeWrite(pelvis_bone.data()+kChildOffset,&p,4);
  SafeWrite(pelvis.data()+kParentOffset,&pb,4);SafeWrite(pelvis.data()+kSiblingOffset,&bb,4);
  SafeWrite(body_bone.data()+kParentOffset,&pb,4);SafeWrite(body_bone.data()+kChildOffset,&n,4);
  SafeWrite(node.data()+kParentOffset,&bb,4);
  RequireWaist(ResolveHeroinePelvisNode(n)==p,"Resolve pelvis leaf through resource-free animation bones");
  RequireWaist(HookHeroinePrtDraw(mesh.data())==1457 && mesh==baseline,"Live-shaped hierarchy must apply and restore waist binding");
  SafeWrite(node.data()+kParentOffset,&pb,4);SafeWrite(pelvis.data()+kSiblingOffset,&n,4);
  RequireWaist(ResolveHeroinePelvisNode(n)==p,"Resolve sibling mesh leaves under a shared player root");
  RequireWaist(HookHeroinePrtDraw(mesh.data())==1457 && mesh==baseline,"Flat native mesh tree must apply and restore waist binding");
  SafeWrite(pelvis.data()+kSiblingOffset,&bb,4);
  // Detached torsos must follow their native severed-part animation.
  const uintptr_t detached=0;SafeWrite(node.data()+kParentOffset,&detached,4);draw_expected_changed=false;
  RequireWaist(HookHeroinePrtDraw(mesh.data())==1457 && mesh==baseline,"Detached body must bypass waist binding");
  SafeWrite(node.data()+kParentOffset,&bb,4);render_root=a;
  RequireWaist(ResolveHeroinePelvisNode(n)==0,"Foreign tree with a pelvis must not bind a detached torso");
  RequireWaist(HookHeroinePrtDraw(mesh.data())==1457 && mesh==baseline,"Foreign tree must bypass waist binding");
  // A bad reference must reject the entire transaction before any write.
  auto* header=reinterpret_cast<uint32_t*>(mesh.data());auto* face=reinterpret_cast<uint32_t*>(header[7]);
  auto* ref=reinterpret_cast<uint32_t*>(face[11]);const auto saved=ref[2];ref[2]=1;
  const auto corrupt=mesh;WaistWarp warp{};MakeWaistWarp(poses.back()[0],poses.back()[1],&warp);WaistDrawTransaction rejected;
  RequireWaist(!PrepareWaistDraw(mesh.data(),warp,&rejected) && mesh==corrupt,"Bad resource must fail without mutation");ref[2]=saved;
  MH_Uninitialize();
  std::cout<<"Waist: "<<meshes<<" body variants, "<<samples<<" native animation samples, "<<vertices_checked<<" vertices; anchoring, normals, exact restoration and exception guards passed\n";
}
