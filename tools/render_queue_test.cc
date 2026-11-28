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

struct QueueDraw {
  uint8_t reserved = 0;
  uint8_t type = 0;
  uint32_t depth_group = 0;
};

void __cdecl CaptureQueueDraw(void* command) {
  static_cast<QueueDraw*>(command)->depth_group = DeathtrapWorldDepthGroup();
}

void CheckBonePresentation() {
  const auto pose = [](double radians) {
    const int32_t sine = ToFixed(std::sin(radians));
    const int32_t cosine = ToFixed(std::cos(radians));
    return Matrix3x4{{ToFixed(1.0), 0, 0, 0, cosine, sine,
                      0, -sine, cosine, 0, 0, 0}};
  };
  const auto rotation_phase = [](const Matrix3x4& older,
                                 const Matrix3x4& previous,
                                 const Matrix3x4& current, double phase) {
    RigidTransform from, to;
    RequireQueue(DecodeRigid(previous, &from) && DecodeRigid(current, &to),
                 "Fixture must contain rigid transforms");
    return BoneRotationPhase(older, from, to, phase);
  };
  const Matrix3x4 neutral = pose(0.0);
  const Matrix3x4 small_turn = pose(0.03);
  const Matrix3x4 fast_turn = pose(1.0);
  RequireQueue(rotation_phase(neutral, neutral, pose(2.0), 0.25) == 0.25,
               "A rotation discontinuity must keep the snap policy");
  RequireQueue(rotation_phase(neutral, neutral, fast_turn, 0.25) < 0.25,
               "Fast continuous rotations must also ease between poses");
  RequireQueue(rotation_phase(neutral, neutral, fast_turn, 0.0) == 0.0 &&
                   rotation_phase(neutral, neutral, fast_turn, 1.0) == 1.0,
               "Joint interpolation preserves both source endpoints");

  for (const auto& preceding : {neutral, pose(-0.7), pose(0.8)}) {
    double last_phase = 0.0;
    for (int i = 1; i <= 12; ++i) {
      const double phase = rotation_phase(
          preceding, neutral, fast_turn, i / 12.0);
      RequireQueue(phase > last_phase && phase <= 1.0,
                   "A joint must progress monotonically even in slow motion");
      last_phase = phase;
    }
  }

  std::array<uint8_t, kMatrixOffset + sizeof(Matrix3x4)> root{}, child{};
  SceneSnapshot previous;
  previous.player = reinterpret_cast<uintptr_t>(root.data());
  const auto bone = reinterpret_cast<uintptr_t>(child.data());
  previous.nodes[previous.player].local = neutral;
  previous.nodes[previous.player].world = neutral;
  NodeTransform& joint = previous.nodes[bone];
  joint.parent = previous.player;
  // Retail fixed-point composition can differ from recomposition by a unit.
  joint.local = pose(-0.01);
  joint.world = joint.local;
  --joint.world.values[9];
  SceneSnapshot older = previous;
  SceneSnapshot current = previous;
  const auto rendered_bone = [&] {
    Matrix3x4 rendered{};
    std::memcpy(&rendered, child.data() + kMatrixOffset, sizeof(rendered));
    return rendered;
  };
  const auto held = ApplyInterpolatedScene(&older, previous, current, 1.0 / 3.0,
                                            false, 1);
  RequireQueue(held.applied == 2 &&
                   rendered_bone().values == joint.world.values,
               "Held animation must reproduce the exact native world matrix");

  current.nodes[bone].local = small_turn;
  current.nodes[bone].world = small_turn;
  ApplyInterpolatedScene(nullptr, previous, current, 1.0 / 3.0, false, 2);
  const auto linear = rendered_bone();
  ApplyInterpolatedScene(&older, previous, current, 1.0 / 3.0, false, 2);
  RequireQueue(rendered_bone().values[5] < linear.values[5],
               "With pose history, a small joint rotation eases out of a hold");
  RestoreScene(current);
  RequireQueue(rendered_bone().values == small_turn.values,
               "Restoring the scene must restore the exact source endpoint");

  // Turn the same hierarchy into a non-player actor using the ordinary scene
  // traversal. A large but continuous rotation must ease on that rig too.
  for (auto* scene : {&older, &previous, &current}) {
    scene->player = 0;
  }
  current.nodes[bone].local = fast_turn;
  current.nodes[bone].world = fast_turn;
  ApplyInterpolatedScene(nullptr, previous, current, 1.0 / 3.0, false, 3);
  const auto enemy_linear = rendered_bone();
  ApplyInterpolatedScene(&older, previous, current, 1.0 / 3.0, false, 3);
  RequireQueue(rendered_bone().values[5] < enemy_linear.values[5],
               "A fast rotation on another actor must use pose history");
  ++current.nodes[bone].render_resource_handle;
  ApplyInterpolatedScene(&older, previous, current, 1.0 / 3.0, false, 4);
  RequireQueue(rendered_bone().values == enemy_linear.values,
               "A resource replacement must discard history for that joint");
}

void CheckProjectiveQuads() {
  using namespace deathtrap::projective_quad;
  const auto test = [](const std::array<Point, 4>& corners, const std::array<UV, 4>& uv,
                       double zoom, double center) {
    Mapping map;
    RequireQueue(Build(corners, uv, zoom, zoom, center, center, map),
                 "Planar quad must support one projective mapping");
    const auto sample = [&](Point p) {
      TexturePoint result{};
      RequireQueue(map.Project(float(p.x * zoom / p.z + center),
                               float(p.y * zoom / p.z + center), float(1 / p.z), result),
                   "Project corner or clipped vertex");
      return UV{result.u / result.q, result.v / result.q};
    };
    for (size_t i = 0; i < corners.size(); ++i) {
      const UV result = sample(corners[i]);
      RequireQueue(std::abs(result.u - uv[i].u) < 1e-5 &&
                   std::abs(result.v - uv[i].v) < 1e-5, "Preserve authored corner UVs");
    }
    const auto lerp = [](Point a, Point b, double t) {
      return Point{a.x+(b.x-a.x)*t, a.y+(b.y-a.y)*t, a.z+(b.z-a.z)*t};
    };
    const UV a = sample(lerp(corners[0], corners[2], 0.35));
    const UV b = sample(lerp(corners[1], corners[3], 0.55));
    return std::array<UV, 2>{a, b};
  };
  // Two dissimilar trapezoids with non-square and mirrored/repeating mappings.
  const std::array<Point,4> face{{{0, 0, 900}, {1200, 0, 900},
                                 {966, 1000, 700}, {436, 1000, 700}}};
  const std::array<UV,4> tile{{{0, 0}, {1, 0}, {1, 1}, {0, 1}}};
  const auto a = test(face, tile, 900, 320);
  std::array<Point, 4> rotated = face;
  for (auto& p : rotated) p = {p.x * 0.6 - p.z * 0.8, p.y - 40, p.x * 0.8 + p.z * 0.6 + 220};
  const auto b = test(rotated, tile, 1600, 700);
  RequireQueue(std::abs(a[0].u-b[0].u) < 1e-5 && std::abs(a[0].v-b[0].v) < 1e-5 &&
               std::abs(a[1].u-b[1].u) < 1e-5 && std::abs(a[1].v-b[1].v) < 1e-5,
               "Clipped samples must retain UVs under changes in camera and zoom");
  auto wall = face;
  wall[2].x = 1000;
  wall[3].x = 250;
  test(wall, {{{2,0},{2,3},{-1,3},{-1,0}}}, 550, 500);
  wall[2].x = 1200;
  wall[3].x = 0;
  Mapping map;
  RequireQueue(!Build(wall, tile, 800, 800, 320, 240, map),
               "An affine quad already uses correct perspective UVs");
  wall[2].z += 100;
  RequireQueue(!Build(wall, tile, 800, 800, 320, 240, map),
               "A nonplanar quad keeps its triangulated mapping");
}

void CheckRoomFaceProjection() {
  struct TransformedVertex { float x, y, z; };
  struct RoomVertex { uint8_t padding[0x1C]{}; const TransformedVertex* camera; };
  struct RoomCorner {
    int16_t u, v;
    uint32_t padding = 0;
    const RoomVertex* vertex;
    uint32_t tail = 0;
  };
  static_assert(sizeof(RoomCorner) == 16);
  const TransformedVertex view[] = {{0,0,900}, {1200,0,900}, {966,1000,700}, {436,1000,700}};
  const RoomVertex vertices[] = {{{}, view}, {{}, view+1}, {{}, view+2}, {{}, view+3}};
  const RoomCorner corners[] = {{0,0,0,vertices}, {127,0,0,vertices+1},
                                {127,63,0,vertices+2}, {0,63,0,vertices+3}};
  struct RoomFace {
    uint8_t padding[0x28]{};
    uint32_t count = 4;
    const RoomCorner* corners;
  } face{{}, 4, corners};
  uint8_t command[0x24]{1};
  command[0x10] = 127;
  command[0x11] = 63;
  std::array<float, 29> projection{};
  projection[0] = 900;
  projection[5] = 800;
  projection[8] = 320;
  projection[28] = 240;
  RequireQueue(SafeWrite(g_dungeon_base + kRoomProjectionRva, projection.data(),
                         sizeof(projection)), "Fixture room projection parameters");
  g_widescreen_world_render_active = true;
  RecordWorldQuad(reinterpret_cast<const uint8_t*>(&face), command);
  auto mapping = g_world_quad_mappings.find(command);
  RequireQueue(mapping != g_world_quad_mappings.end(), "Record authored room face");
  g_current_world_quad = &mapping->second;
  struct DrawVertex { float x, y, z, rhw; uint32_t color, specular; float u, v; };
  DrawVertex drawn[3]{};
  for (size_t i = 0; i < std::size(drawn); ++i) {
    drawn[i] = {900*view[i].x/view[i].z+320,800*view[i].y/view[i].z+240,
                view[i].z,1/view[i].z,0,0,0,0};
  }
  float uvq[9]{};
  RequireQueue(DeathtrapProjectWorldQuad(drawn, 3, uvq) &&
               std::abs(uvq[6]/uvq[8]-127.0/128) < 1e-5 &&
               std::abs(uvq[7]/uvq[8]-63.0/64) < 1e-5,
               "Submit native UVQ on a clipped fan using a non-square tile");
  g_current_world_quad = nullptr;
  g_widescreen_world_render_active = false;
  ResetWorldRenderQueue();
  RequireQueue(g_world_quad_mappings.empty(), "A new queue must discard previous mappings");
}

void CheckDepthGroups(bool expanded) {
  auto* keys = g_dungeon_base + kRenderQueueKeysRva;
  auto* layers = reinterpret_cast<uint16_t*>(expanded ? g_render_queue_arrays[1]
      : keys + kRenderQueueOriginalOffsets[1]);
  auto* pointers = reinterpret_cast<QueueDraw**>(expanded ? g_render_queue_arrays[2]
      : keys + kRenderQueueOriginalOffsets[2]);
  QueueDraw draws[4]{};
  const uint16_t sorted_layers[] = {65535, 65535, 1, 0};
  std::copy(std::begin(sorted_layers), std::end(sorted_layers), layers);
  for (size_t i = 0; i < std::size(draws); ++i) pointers[i] = &draws[i];
  const auto count = static_cast<uint32_t>(std::size(draws));
  RequireQueue(SafeWrite(g_dungeon_base + kRenderQueueBaseRva - 16,
                        &count, sizeof(count)), "Fixture queue count");
  const auto dispatch_table = g_render_queue_callbacks;
  auto* callback_count = g_dungeon_base + kRenderQueueCallbacksRva - 8;
  uint32_t original_callback_count = 0;
  const uint32_t registered_callbacks = 1;
  RequireQueue(SafeReadValue(callback_count, &original_callback_count) &&
               SafeWrite(callback_count, &registered_callbacks, sizeof(registered_callbacks)),
               "Fixture callback count");
  const RenderQueueCallback callbacks[] = {CaptureQueueDraw};
  g_render_queue_callbacks = callbacks;
  std::array<uint8_t, 5> flush_call{};
  const auto flush = reinterpret_cast<uintptr_t>(g_dungeon_base) + ActiveDungeonCodeRva(0x1B452);
  std::memcpy(flush_call.data(), reinterpret_cast<const void*>(flush), flush_call.size());
  int32_t relative;
  std::memcpy(&relative, flush_call.data() + 1, sizeof(relative));
  const auto flush_world = reinterpret_cast<RenderQueueFlush>(flush + 5 + relative);
  // Execute the real retail flush and patched dispatcher against both layouts.
  g_widescreen_world_render_active = true;
  flush_world();
  RequireQueue(draws[0].depth_group != 0 && draws[0].depth_group == draws[1].depth_group,
               "A whole queue layer must share one depth buffer");
  RequireQueue(draws[1].depth_group == draws[2].depth_group &&
               draws[2].depth_group == draws[3].depth_group,
               "Different layers of one queue must share physical depth");
  RequireQueue(DeathtrapWorldDepthGroup() == 0, "Queue context leaked past the flush");
  const auto first_pass = draws[0].depth_group;
  flush_world();
  RequireQueue(draws[0].depth_group != first_pass, "New flush reused depth");
  g_widescreen_world_render_active = false;
  flush_world();
  for (const auto& draw : draws) {
    RequireQueue(draw.depth_group == 0, "Non-world draws must retain native order");
  }
  g_render_queue_callbacks = dispatch_table;
  RequireQueue(SafeWrite(callback_count, &original_callback_count, sizeof(original_callback_count)),
               "Restore fixture callback count");
}

int wmain(int argc,wchar_t** argv) {
  RequireQueue(argc==2,"Pass one supported original Dungeon.dll");
  // Load only PE sections/relocations, without running game initialization or
  // imports. The radix sorter itself has no external dependencies.
  auto module=LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
  RequireQueue(module!=nullptr,"Cannot map Dungeon.dll");
  g_dungeon_base=reinterpret_cast<uint8_t*>(module);
  g_dungeon_image_variant=IdentifyDungeonImage(g_dungeon_base);
  RequireQueue(g_dungeon_image_variant!=DungeonImageVariant::kUnsupported,"Unsupported fixture");
  RequireQueue(InstallWorldRenderQueueDepth(), "Install world queue depth scope");
  RequireQueue(InstallWorldRenderQueueDepth(), "World queue install must be idempotent");
  RequireQueue(InstallWorldQuadProjection(), "Install world quad projection");
  CheckBonePresentation();
  CheckProjectiveQuads();
  CheckRoomFaceProjection();
  CheckDepthGroups(false);
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
  CheckDepthGroups(true);
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
  std::cout<<"Native RenderQ: sorting, 65536-entry queue, depth groups and projective UV passed\n";
  return 0;
}
