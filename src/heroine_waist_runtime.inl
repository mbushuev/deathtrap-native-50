// Render-only two-bone skinning for the reconstructed torso's lower seam.
// Native animation, resource ownership and all gameplay transforms stay intact.
constexpr DungeonDataRva kPrtCurrentNodeRva{
    0x1D41BCu,0x1D4C1Cu,0x1D468Cu,0x1D486Cu};
constexpr Matrix3x4 kE3WaistBind{{
    16200,-996,2235,1279,16204,-2053,-2086,2205,16100,11,94,-30}};
constexpr Matrix3x4 kE3SymmetricWaistBind{{
    16384,0,0,0,16384,0,0,0,16384,0,94,-30}};
const Matrix3x4& ActiveE3WaistBind() {
  // Version 3+ meshes are built around the centred pelvis seam. Older
  // installed packs retain their original rotated, offset bind transform.
  return g_dual_heroine_pack.version>=3?kE3SymmetricWaistBind:kE3WaistBind;
}
double E3WaistBlendStart() {
  return g_dual_heroine_pack.version>=4?-12.:g_dual_heroine_pack.version==3?-20.:-28.;
}
double E3WaistPelvisEnd() {
  return g_dual_heroine_pack.version>=4?35.:g_dual_heroine_pack.version==3?50.:33.;
}
using HeroinePrtDraw = int(__cdecl*)(void*);
HeroinePrtDraw g_original_heroine_prt_draw = nullptr;
bool g_heroine_waist_hook_installed = false;
using HeroineNodeDraw=int(__cdecl*)(void*,void*);
using HeroineResourceLoad=void*(__cdecl*)(uint32_t,uint32_t,const char*);
HeroineNodeDraw g_original_dual_heroine_node=nullptr;
HeroineResourceLoad g_dual_heroine_load=nullptr;
bool g_dual_heroine_node_installed=false;
struct DualHeroineGroup { uintptr_t e3_marker=0,retail_marker=0;bool ready=false; };
std::array<DualHeroineGroup,5> g_dual_heroine_groups{};

uintptr_t LoadedHeroineResource(uint32_t id) {
  uint32_t count=0;uintptr_t pointer=0;
  if (!g_dungeon_base || !SafeReadValue(g_dungeon_base+kRenderResourceCountRva,&count) || id>=count ||
      !SafeReadValue(g_dungeon_base+kRenderResourceTableRva+id*4u,&pointer)) return 0;
  return pointer;
}
bool WarmDualHeroineGroup(uint32_t group) {
  if (!HeroineLiveSwitchEnabled() || !g_dual_heroine_load || group>=g_dual_heroine_groups.size()) return false;
  auto& state=g_dual_heroine_groups[group];const uint32_t first=group*240;
  const auto e3_marker=LoadedHeroineResource(HeroineResourceFromIndex(first));
  const auto retail_marker=LoadedHeroineResource(g_dual_heroine_pack.first_mesh+first);
  const bool chains_ready=!HeroineSharedChains() ||
      (LoadedHeroineResource(HeroineChainResource(0)) && LoadedHeroineResource(HeroineChainResource(1)));
  if (state.ready && chains_ready && e3_marker && retail_marker && state.e3_marker==e3_marker && state.retail_marker==retail_marker) return true;
  static thread_local bool warming=false;if(warming) return false;
  struct LoadingScope { bool& value;explicit LoadingScope(bool& v):value(v){value=true;}~LoadingScope(){value=false;} } loading(warming);
  state.ready=false;unsigned loaded=0;
  const auto load=[&](uint32_t id,uint32_t type) {
    if (LoadedHeroineResource(id)) return true;
    const auto pointer=reinterpret_cast<uintptr_t>(g_dual_heroine_load(id,type,"Native 50 dual heroine"));
    ++loaded;return pointer && pointer==LoadedHeroineResource(id);
  };
  if (HeroineSharedChains()) for (uint32_t kind=0;kind<2;++kind)
    if (!load(HeroineChainResource(kind),0x42000002u)) return false;
  // Warm both texture sets and all native damage states before the original
  // scene-node renderer prepares its matrices/material state. The leaf draw
  // and the menu toggle never perform resource I/O or exchange owned pointers.
  for (uint32_t id=g_dual_heroine_pack.e3_texture_first;id<g_dual_heroine_pack.first_mesh;++id)
    if (!load(id,0x41000002u)) return false;
  for (uint32_t i=0;i<g_dual_heroine_pack.texture_count;++i)
    if (!load(g_dual_heroine_pack.first_texture+i,0x41000002u)) return false;
  for (uint32_t i=first;i<first+240;++i)
    if (!load(HeroineResourceFromIndex(i),0x42000002u) || !load(g_dual_heroine_pack.first_mesh+i,0x42000002u)) return false;
  state.e3_marker=LoadedHeroineResource(HeroineResourceFromIndex(first));
  state.retail_marker=LoadedHeroineResource(g_dual_heroine_pack.first_mesh+first);state.ready=true;
  AppendDeathtrapSupportLog("support_heroine_model group=%u ready=both e3_meshes=240 original_meshes=240 loaded_now=%u",group,loaded);
  return true;
}
bool IsPlayerVisualNode(uintptr_t node) {
  // The UI owner is the gameplay actor, not a transform node. Resolve the
  // same actor -> render link -> root used by camera collision/fading first.
  const auto root=ResolveLivePlayerRenderRoot();
  if (!node || !root) return false;
  for (unsigned depth=0;depth<32 && node;++depth) {
    if (node==root) return true;
    if (!SafeReadValue(reinterpret_cast<void*>(node+kParentOffset),&node)) return false;
  }
  return false;
}
int __cdecl HookDualHeroineNode(void* node,void* camera) {
  uint32_t id=0;
  if (!g_show_body_wounds.load(std::memory_order_relaxed) && node && g_dual_heroine_load &&
      SafeReadValue(static_cast<uint8_t*>(node)+kRenderResourceHandleOffset,&id)) {
    const auto healthy=deathtrap::wounds::HealthyPart(id);
    // Resource work belongs before the native renderer prepares matrices and
    // materials. The leaf draw below never loads or exchanges owned resources.
    if (healthy!=id && IsPlayerVisualNode(reinterpret_cast<uintptr_t>(node)) &&
        !LoadedHeroineResource(healthy))
      g_dual_heroine_load(healthy,0x42000002u,"Native 50 healthy player part");
  }
  if (HeroineLiveSwitchEnabled() && node &&
      SafeReadValue(static_cast<uint8_t*>(node)+kRenderResourceHandleOffset,&id)) {
    const int index=HeroineMeshIndex(id);
    if (index>=0) {
      if (!LoadedHeroineResource(g_dual_heroine_pack.first_mesh+static_cast<uint32_t>(index)))
        g_dual_heroine_groups[static_cast<uint32_t>(index)/240].ready=false;
      WarmDualHeroineGroup(static_cast<uint32_t>(index)/240);
    }
  }
  return g_original_dual_heroine_node(node,camera);
}

int HeroineBodyStage(uint32_t id) {
  if (id==3903) return 0;
  if (id>=4081 && id<=4281 && (id-4081)%20==0)
    return 1+static_cast<int>((id-4081)/20);
  if (id>=8139 && id<=9079 && (id-8139)%20==0)
    return static_cast<int>(((id-8139)/20)%12);
  return -1;
}
int HeroineHandSide(uint32_t id) {
  if (id==3886) return 0;
  if (id==3893) return 1;
  if (id>=4070 && id<=4289) {
    const auto part=(id-4070)%20;
    return part==14?0:part==17?1:-1;
  }
  if (id>=8139 && id<=9098) {
    const auto part=(id-8139)%20;
    return part==4?0:part==11?1:-1;
  }
  return -1;
}
uint32_t HeroineBurnedHand(uint32_t body,int side) {
  if (side<0 || side>1 || HeroineBodyStage(body)!=11) return 0;
  if (body==4281) return side?4287u:4284u;
  // Team families put torso at offset zero, with hands at four and eleven.
  return body+(side?11u:4u);
}
uint32_t ResolveHeroineHandAppearance(uintptr_t node,uint32_t resource) {
  const int side=HeroineHandSide(resource);
  if (side<0 || !HeroineE3VisualActive() ||
      !g_show_body_wounds.load(std::memory_order_relaxed) || !IsPlayerVisualNode(node)) return resource;
  const auto root=ResolveLivePlayerRenderRoot();
  if (!root) return resource;
  // Weapon/animation changes can restore the native hand mesh even while the
  // body remains fully burned. Resolve appearance from this actor's current
  // torso, never from a cached death state or a nearby/detached model.
  std::array<uintptr_t,128> pending{},visited{};
  size_t count=1,seen=0;pending[0]=root;uint32_t selected=0;
  while (count && seen<visited.size()) {
    const auto current=pending[--count];
    if (std::find(visited.begin(),visited.begin()+seen,current)!=visited.begin()+seen) return resource;
    visited[seen++]=current;
    uint32_t id=0;uintptr_t child=0,sibling=0;
    if (!SafeReadValue(reinterpret_cast<void*>(current+kRenderResourceHandleOffset),&id) ||
        !SafeReadValue(reinterpret_cast<void*>(current+kChildOffset),&child)) return resource;
    const auto burned=HeroineBurnedHand(id,side);
    if (burned) {
      if (selected && selected!=burned) return resource;
      selected=burned;
    }
    if (child) {
      uintptr_t parent=0;
      if (!SafeReadValue(reinterpret_cast<void*>(child+kParentOffset),&parent) || parent!=current || count==pending.size()) return resource;
      pending[count++]=child;
    }
    if (current!=root) {
      if (!SafeReadValue(reinterpret_cast<void*>(current+kSiblingOffset),&sibling)) return resource;
      if (sibling) {
        uintptr_t parent=0,other_parent=0;
        if (!SafeReadValue(reinterpret_cast<void*>(current+kParentOffset),&parent) ||
            !SafeReadValue(reinterpret_cast<void*>(sibling+kParentOffset),&other_parent) ||
            parent!=other_parent || count==pending.size()) return resource;
        pending[count++]=sibling;
      }
    }
  }
  // Reject a truncated/cyclic tree instead of inferring another actor's state.
  if (count || !selected) return resource;
  return selected;
}

bool HeroinePelvisResource(uint32_t id) {
  return id==3890 || (id>=4070 && id<=4270 && (id-4070)%20==0) ||
      (id>=8147 && id<=9087 && (id-8147)%20==0);
}

uintptr_t ResolveHeroinePelvisNode(uintptr_t node) {
  uintptr_t parent=0;
  if (!node || !SafeReadValue(reinterpret_cast<void*>(node+kParentOffset),&parent) || !parent) return 0;
  uint32_t id=0;
  if (!SafeReadValue(reinterpret_cast<void*>(parent+kRenderResourceHandleOffset),&id)) return 0;
  if (HeroinePelvisResource(id)) return parent;
  // Live PSTs can put a resource-free transform between each animated bone
  // and its mesh leaf. The pelvis mesh is then a child of an ancestor bone,
  // rather than the torso leaf's immediate parent. Stay inside the current
  // player tree so a severed torso cannot bind to an unrelated nearby actor.
  const auto root=ResolveLivePlayerRenderRoot();
  if (!root || !IsPlayerVisualNode(node)) return 0;
  for (unsigned depth=0;parent && depth<16;++depth) {
    if (!SafeReadValue(reinterpret_cast<void*>(parent+kRenderResourceHandleOffset),&id)) return 0;
    if (HeroinePelvisResource(id)) return parent;
    if (id!=0) return 0;
    uintptr_t child=0;
    if (!SafeReadValue(reinterpret_cast<void*>(parent+kChildOffset),&child)) return 0;
    for (unsigned sibling=0;child && sibling<32;++sibling) {
      uintptr_t owner=0;
      if (!SafeReadValue(reinterpret_cast<void*>(child+kParentOffset),&owner) || owner!=parent ||
          !SafeReadValue(reinterpret_cast<void*>(child+kRenderResourceHandleOffset),&id)) return 0;
      if (HeroinePelvisResource(id)) return child;
      if (!SafeReadValue(reinterpret_cast<void*>(child+kSiblingOffset),&child)) return 0;
    }
    if (parent==root) break;
    if (!SafeReadValue(reinterpret_cast<void*>(parent+kParentOffset),&parent)) return 0;
  }
  return 0;
}

int DrawSharedHeroineChain(uint32_t resource,int result) {
  if (!HeroineSharedChains() || !HeroineE3VisualActive()) return result;
  const int kind=HeroineBodyStage(resource)>=0?0:HeroinePelvisResource(resource)?1:-1;
  if (kind<0) return result;
  const auto shared=LoadedHeroineResource(HeroineChainResource(static_cast<uint32_t>(kind)));
  // The native node has already published its transform/material state. A
  // shared chain follows that same bone, including detached-part animation.
  // No resource allocation, file access or mutation of owned pointers here.
  if (shared) g_original_heroine_prt_draw(reinterpret_cast<void*>(shared));
  return result;
}

int DrawHeroinePartAndChain(void* mesh,uint32_t resource) {
  const int result=g_original_heroine_prt_draw(mesh);
  return DrawSharedHeroineChain(resource,result);
}

using WaistGeometry = std::array<int32_t,6>; // position/center, then Q14 normal
struct WaistWarp { double rotation[3][3]{},translation[3]{}; };

bool InvertWaistMatrix(const double a[3][3],double out[3][3]) {
  const double determinant=a[0][0]*(a[1][1]*a[2][2]-a[1][2]*a[2][1])-
      a[0][1]*(a[1][0]*a[2][2]-a[1][2]*a[2][0])+
      a[0][2]*(a[1][0]*a[2][1]-a[1][1]*a[2][0]);
  if (!std::isfinite(determinant) || std::abs(determinant)<1e-6) return false;
  for (int row=0;row<3;++row) for (int col=0;col<3;++col) {
    const int i=(row+1)%3,j=(row+2)%3,k=(col+1)%3,l=(col+2)%3;
    out[col][row]=(a[i][k]*a[j][l]-a[i][l]*a[j][k])/determinant;
  }
  return true;
}

bool MakeWaistWarp(const Matrix3x4& parent,const Matrix3x4& body,WaistWarp* warp) {
  if (!warp) return false;
  *warp={};
  double inverse[3][3]{},br[3][3]{},target[3][3]{},translation[3]{};
  const auto& bind=ActiveE3WaistBind();
  for (int i=0;i<9;++i) br[i/3][i%3]=body.values[i]/16384.;
  if (!InvertWaistMatrix(br,inverse)) return false;
  // Stored matrices use row vectors. Transform the fixed pelvis-space seam
  // into this draw's torso space, including interpolated animation samples.
  for (int i=0;i<3;++i) for (int j=0;j<3;++j)
    for (int k=0;k<3;++k)
      target[i][j]+=bind.values[i*3+k]/16384.*parent.values[k*3+j]/16384.;
  for (int j=0;j<3;++j) {
    translation[j]=static_cast<double>(parent.values[9+j])-body.values[9+j];
    for (int k=0;k<3;++k)
      translation[j]+=bind.values[9+k]*parent.values[k*3+j]/16384.;
  }
  for (int i=0;i<3;++i) for (int j=0;j<3;++j) {
    for (int k=0;k<3;++k) warp->rotation[i][j]+=target[i][k]*inverse[k][j];
    if (!std::isfinite(warp->rotation[i][j]) || std::abs(warp->rotation[i][j])>3) return false;
  }
  for (int j=0;j<3;++j) {
    for (int k=0;k<3;++k) warp->translation[j]+=translation[k]*inverse[k][j];
    if (!std::isfinite(warp->translation[j]) || std::abs(warp->translation[j])>300) return false;
  }
  return true;
}

bool WarpWaistGeometry(const WaistWarp& warp,const WaistGeometry& original,WaistGeometry* result) {
  *result=original;
  // v3 retains the legacy overlap-band anchor. v4 has a continuous shared
  // seam at pelvis y=35: spread the transition over the exposed abdomen,
  // and fully anchor only that actual seam. Freezing the old overlap band
  // above it concentrated the idle pelvis/chest offset into a waist kink.
  const double blend_start=E3WaistBlendStart();
  const double pelvis_end=E3WaistPelvisEnd();
  if (original[1]>=blend_start) return true;
  // End the blend at the actual pelvis-space seam plane, not a fixed torso
  // height. The bind rotation makes that height vary around the waist. A
  // short fixed band folded the back onto itself in a deep native attack.
  const auto& bind=ActiveE3WaistBind();
  const double rx=bind.values[1]/16384.,ry=bind.values[4]/16384.,
      rz=bind.values[7]/16384.;
  const double span=bind.values[10]-pelvis_end+blend_start*ry+rx*original[0]+rz*original[2];
  if (!std::isfinite(span) || span<8 || span>128) return false;
  const double ratio=(blend_start-static_cast<double>(original[1]))*ry/span;
  const double u=std::clamp(ratio,0.,1.),weight=u*u*(3-2*u);
  const double slope=ratio>0 && ratio<1?6*u*(1-u):0.;
  const double gradient[3]={-slope*u*rx/span,-slope*ry/span,-slope*u*rz/span};
  double delta[3]{},jacobian[3][3]{},inverse[3][3]{},normal[3]{};
  for (int j=0;j<3;++j) {
    delta[j]=warp.translation[j]-original[j];
    for (int k=0;k<3;++k) delta[j]+=original[k]*warp.rotation[k][j];
    const double value=original[j]+weight*delta[j];
    if (!std::isfinite(value) || std::abs(value)>4096) return false;
    (*result)[j]=static_cast<int32_t>(std::lround(value));
    for (int k=0;k<3;++k)
      jacobian[k][j]=(k==j?1.:0.)+weight*(warp.rotation[k][j]-(k==j?1.:0.));
    for (int k=0;k<3;++k) jacobian[k][j]+=gradient[k]*delta[j];
  }
  if (!InvertWaistMatrix(jacobian,inverse)) return false;
  double length=0;
  for (int j=0;j<3;++j) {
    for (int k=0;k<3;++k) normal[j]+=original[3+k]*inverse[j][k];
    length+=normal[j]*normal[j];
  }
  length=std::sqrt(length);
  if (!std::isfinite(length) || length<1) return false;
  for (int j=0;j<3;++j) (*result)[3+j]=static_cast<int32_t>(std::lround(normal[j]*16384./length));
  return true;
}

struct WaistGeometryChange { uint8_t* address=nullptr; WaistGeometry before{},after{}; };
struct WaistDrawTransaction {
  std::vector<WaistGeometryChange> changes;
  size_t written=0;
  void Restore() {
    while (written) { const auto& c=changes[--written];SafeWrite(c.address,c.before.data(),sizeof(c.before)); }
  }
  bool Apply() {
    for (const auto& c:changes) {
      // Count this write before attempting it: a guarded memcpy may fail
      // after a partial write, which must be restored as well.
      ++written;
      if (!SafeWrite(c.address,c.after.data(),sizeof(c.after))) { Restore();return false; }
    }
    return true;
  }
};

bool PrepareWaistDraw(void* mesh,const WaistWarp& warp,WaistDrawTransaction* transaction) {
  if (!mesh || !transaction) return false;
  std::array<uint32_t,13> header{};
  if (!SafeRead(mesh,header.data(),sizeof(header)) || !(header[0]&1) ||
      (header[0]&0x24) || header[6]<32 || header[6]>65536 ||
      header[10]<24 || header[10]>32768 || !header[7] || !header[11]) return false;
  std::vector<uint8_t> vertices(header[10]*32u),faces(header[6]*52u);
  if (!SafeRead(reinterpret_cast<void*>(header[11]),vertices.data(),vertices.size()) ||
      !SafeRead(reinterpret_cast<void*>(header[7]),faces.data(),faces.size())) return false;
  std::vector<bool> changed(header[10],false);
  for (size_t i=0;i<header[10];++i) {
    WaistGeometryChange c{};
    c.address=reinterpret_cast<uint8_t*>(header[11])+i*32+4;
    std::memcpy(c.before.data(),vertices.data()+i*32+4,sizeof(c.before));
    if (c.before[1]>=E3WaistBlendStart()) continue;
    if (!WarpWaistGeometry(warp,c.before,&c.after)) return false;
    if (c.before!=c.after) { changed[i]=true;transaction->changes.push_back(c); }
  }
  // The native backface test uses a face center/normal separately from the
  // vertices. Recompute that geometry too; preserve UVs, texture IDs and
  // the renderer's per-face/per-vertex projection and lighting caches.
  for (size_t i=0;i<header[6];++i) {
    const auto* face=faces.data()+i*52;
    uint32_t count=0,references=0;
    std::memcpy(&count,face+40,4);std::memcpy(&references,face+44,4);
    if (count<3 || count>16 || !references) return false;
    std::array<uint32_t,64> refs{};
    if (!SafeRead(reinterpret_cast<void*>(references),refs.data(),count*16)) return false;
    bool touched=false;double center[3]{};std::array<WaistGeometry,16> points{};
    for (size_t j=0;j<count;++j) {
      const uint32_t address=refs[j*4+2];
      if (address<header[11] || (address-header[11])%32 ||
          (address-header[11])/32>=header[10]) return false;
      const auto vi=(address-header[11])/32;
      WaistGeometry original{},warped{};
      std::memcpy(original.data(),vertices.data()+vi*32+4,sizeof(original));
      if (!WarpWaistGeometry(warp,original,&warped)) return false;
      points[j]=warped;
      touched=touched||changed[vi];
      for (int axis=0;axis<3;++axis) center[axis]+=warped[axis];
    }
    if (!touched) continue;
    WaistGeometryChange c{};c.address=reinterpret_cast<uint8_t*>(header[7])+i*52;
    std::memcpy(c.before.data(),face,sizeof(c.before));
    if (!WarpWaistGeometry(warp,c.before,&c.after)) return false;
    for (int axis=0;axis<3;++axis) c.after[axis]=static_cast<int32_t>(std::lround(center[axis]/count));
    for (size_t j=1;j+1<count;++j) {
      double a[3]{},z[3]{},n[3]{};
      for (int k=0;k<3;++k) { a[k]=static_cast<double>(points[j][k])-points[0][k];z[k]=static_cast<double>(points[j+1][k])-points[0][k]; }
      double length=0,direction=0;
      for (int k=0;k<3;++k) { n[k]=a[(k+1)%3]*z[(k+2)%3]-a[(k+2)%3]*z[(k+1)%3];length+=n[k]*n[k];direction+=n[k]*c.after[3+k]; }
      if (length<1) continue;
      length=std::sqrt(length)*(direction<0?-1.:1.);
      for (int k=0;k<3;++k) c.after[3+k]=static_cast<int32_t>(std::lround(n[k]*16384./length));
      break;
    }
    transaction->changes.push_back(c);
  }
  return !transaction->changes.empty();
}

int CallWaistDrawAndRestore(void* mesh,WaistDrawTransaction* transaction) {
  int result=0;
  __try { result=g_original_heroine_prt_draw(mesh); }
  __finally { transaction->Restore(); }
  return result;
}

void LogHeroineVisualStage(uintptr_t node,uint32_t id,uint32_t visible_id) {
  if (!g_extended_log_enabled.load(std::memory_order_relaxed)) return;
  uintptr_t player=0;
  if (!IsPlayerVisualNode(node) || !ReadPlayerObject(&player)) return;
  int32_t hp=0;if (!ReadEntityHealth(reinterpret_cast<void*>(player),&hp)) return;
  static uintptr_t previous_player=0;static uint32_t previous_id=0,previous_visible=0;
  static int32_t previous_hp=-1;static bool previous_e3=false,previous_wounds=true;
  const int32_t whole_hp=hp/kHealthFixedScale;
  const bool e3=HeroineE3VisualActive();
  const bool wounds=g_show_body_wounds.load(std::memory_order_relaxed);
  if (player==previous_player && id==previous_id && whole_hp==previous_hp && e3==previous_e3 &&
      visible_id==previous_visible && wounds==previous_wounds) return;
  previous_player=player;previous_id=id;previous_hp=whole_hp;previous_e3=e3;
  previous_visible=visible_id;previous_wounds=wounds;
  AppendDeathtrapSupportLog("support_heroine_visual hp=%d body_resource=%u body_stage=%d model=%s show_wounds=%u visible_resource=%u visible_stage=%d",
      whole_hp,id,HeroineBodyStage(id),e3?"e3":"original",wounds?1u:0u,visible_id,HeroineBodyStage(visible_id));
}

void LogHeroineWaistRuntime(unsigned state,uint32_t resource,void* mesh) {
  if (state>=8 || HeroineBodyStage(resource)<0) return;
  static unsigned seen=0;
  if (seen&(1u<<state)) return;
  seen|=1u<<state;
  uint32_t flags=0;SafeReadValue(mesh,&flags);
  uintptr_t node=0,parent=0;uint32_t parent_resource=0;
  SafeReadValue(g_dungeon_base+kPrtCurrentNodeRva,&node);
  if(node) SafeReadValue(reinterpret_cast<void*>(node+kParentOffset),&parent);
  if(parent) SafeReadValue(reinterpret_cast<void*>(parent+kRenderResourceHandleOffset),&parent_resource);
  const char* names[]={"applied","unregistered","inactive","no_pelvis","bad_matrices","prepare_failed","write_failed","first_draw"};
  AppendDeathtrapSupportLog("support_heroine_waist_draw state=%s resource=%u flags=%08X pack=%u node=%08X parent=%08X parent_resource=%u",
      names[state],resource,flags,g_dual_heroine_pack.version,static_cast<unsigned>(node),static_cast<unsigned>(parent),parent_resource);
}

int __cdecl HookHeroinePrtDraw(void* mesh) {
  uintptr_t node=0,parent=0,registered=0;uint32_t resource=0;
  if (!g_dungeon_base || !SafeReadValue(g_dungeon_base+kPrtCurrentNodeRva,&node) || !node ||
      !SafeReadValue(reinterpret_cast<void*>(node+kRenderResourceHandleOffset),&resource))
    return g_original_heroine_prt_draw(mesh);
  const auto healthy=g_show_body_wounds.load(std::memory_order_relaxed) ? resource :
      deathtrap::wounds::HealthyPart(resource);
  if ((HeroineMeshIndex(resource)<0 && healthy==resource) ||
      !(registered=LoadedHeroineResource(resource)) ||
      registered!=reinterpret_cast<uintptr_t>(mesh)) {
    LogHeroineWaistRuntime(1,resource,mesh);return g_original_heroine_prt_draw(mesh);
  }
  const auto native_resource=resource;
  if (healthy!=resource && IsPlayerVisualNode(node)) {
    const auto clean_mesh=LoadedHeroineResource(healthy);
    if (clean_mesh) { resource=healthy;mesh=reinterpret_cast<void*>(clean_mesh); }
  }
  const auto appearance=ResolveHeroineHandAppearance(node,resource);
  if (appearance!=resource) {
    const auto burned=LoadedHeroineResource(appearance);
    if (burned) {
      static uint32_t previous_source=0,previous_target=0;
      if (resource!=previous_source || appearance!=previous_target) {
        AppendDeathtrapSupportLog("support_heroine_burn_hand native_resource=%u visible_resource=%u",resource,appearance);
        previous_source=resource;previous_target=appearance;
      }
      resource=appearance;mesh=reinterpret_cast<void*>(burned);
    }
  }
  if (HeroineMeshIndex(resource)<0) return g_original_heroine_prt_draw(mesh);
  if (HeroineBodyStage(native_resource)>=0) LogHeroineVisualStage(node,native_resource,resource);
  if (HeroineLiveSwitchEnabled() && !g_e3_model_requested.load()) {
    const auto index=static_cast<uint32_t>(HeroineMeshIndex(resource));
    const auto alias=LoadedHeroineResource(g_dual_heroine_pack.first_mesh+index);
    if (g_dual_heroine_groups[index/240].ready && alias)
      return g_original_heroine_prt_draw(reinterpret_cast<void*>(alias));
    // A missing/unloaded family is retried at the next node entry. Never
    // allocate resources while native leaf-render state is already prepared.
    g_dual_heroine_groups[index/240].ready=false;
  }
  if (HeroineBodyStage(resource)<0) return DrawHeroinePartAndChain(mesh,resource);
  Matrix3x4 parent_world{},world{};WaistWarp warp{};
  if (!HeroineE3VisualActive()) {
    LogHeroineWaistRuntime(2,resource,mesh);return DrawHeroinePartAndChain(mesh,resource);
  }
  if (!(parent=ResolveHeroinePelvisNode(node))) {
    LogHeroineWaistRuntime(3,resource,mesh);return DrawHeroinePartAndChain(mesh,resource);
  }
  if (!SafeRead(reinterpret_cast<void*>(parent+kMatrixOffset),&parent_world,sizeof(parent_world)) ||
      !SafeRead(reinterpret_cast<void*>(node+kMatrixOffset),&world,sizeof(world)) ||
      !MakeWaistWarp(parent_world,world,&warp)) {
    LogHeroineWaistRuntime(4,resource,mesh);return DrawHeroinePartAndChain(mesh,resource);
  }
  WaistDrawTransaction transaction;
  if (!PrepareWaistDraw(mesh,warp,&transaction)) {
    LogHeroineWaistRuntime(5,resource,mesh);return DrawHeroinePartAndChain(mesh,resource);
  }
  if (!transaction.Apply()) {
    LogHeroineWaistRuntime(6,resource,mesh);return DrawHeroinePartAndChain(mesh,resource);
  }
  LogHeroineWaistRuntime(0,resource,mesh);
  const int result=CallWaistDrawAndRestore(mesh,&transaction);
  return DrawSharedHeroineChain(resource,result);
}

bool InstallHeroineWaistHook() {
  if (g_heroine_waist_hook_installed) return true;
  if (!g_dungeon_base || g_dungeon_image_variant==DungeonImageVariant::kUnsupported) return false;
  // Validate the leaf draw's prologue and the node publication immediately
  // before the caller uses the resource table. All four supported DLLs checked.
  const std::array<uint8_t,14> prologue{0x55,0xB8,0xFF,0xFF,0xFF,0xFF,0x8B,0xEC,0x81,0xEC,0x9C,0,0,0};
  std::array<uint8_t,14> actual{};
  auto* target=g_dungeon_base+ActiveDungeonCodeRva(0x8B1E0);
  uint16_t store=0;uint32_t address=0;
  if (!SafeRead(target,actual.data(),actual.size()) || actual!=prologue ||
      !SafeReadValue(g_dungeon_base+ActiveDungeonCodeRva(0x3B72E),&store) || store!=0x3589 ||
      !SafeReadValue(g_dungeon_base+ActiveDungeonCodeRva(0x3B730),&address) ||
      address!=reinterpret_cast<uintptr_t>(g_dungeon_base+kPrtCurrentNodeRva)) {
    AppendDeathtrapSupportLog("support_heroine_waist state=skipped reason=signature");return false;
  }
  if (MH_CreateHook(target,reinterpret_cast<void*>(&HookHeroinePrtDraw),
        reinterpret_cast<void**>(&g_original_heroine_prt_draw))!=MH_OK) return false;
  if (MH_EnableHook(target)!=MH_OK) { MH_RemoveHook(target);return false; }
  g_heroine_waist_hook_installed=true;
  AppendDeathtrapSupportLog("support_heroine_waist state=ready mode=render_only stage_thresholds=native");
  AppendDeathtrapSupportLog("support_player_wounds state=ready enabled=%u owner=actor_render_root apply=immediate",
      g_show_body_wounds.load(std::memory_order_relaxed)?1u:0u);
  return true;
}

bool InstallDualHeroineNodeHook() {
  if (g_dual_heroine_node_installed) return true;
  if (!g_dungeon_base || !g_heroine_waist_hook_installed) return false;
  const std::array<uint8_t,17> node_prefix{0x83,0xEC,0x40,0x53,0x56,0x8B,0x74,0x24,0x4C,0x57,0x55,0x8B,0x8E,0,1,0,0};
  const std::array<uint8_t,9> load_prefix{0x83,0xEC,0x14,0x53,0x56,0x8B,0x74,0x24,0x20};
  std::array<uint8_t,17> node_bytes{};std::array<uint8_t,9> load_bytes{};
  auto* node=g_dungeon_base+ActiveDungeonCodeRva(0x3B2F0);
  auto* load=g_dungeon_base+ActiveDungeonCodeRva(0x39900);
  if (!SafeRead(node,node_bytes.data(),node_bytes.size()) || node_bytes!=node_prefix ||
      !SafeRead(load,load_bytes.data(),load_bytes.size()) || load_bytes!=load_prefix) return false;
  if (MH_CreateHook(node,reinterpret_cast<void*>(&HookDualHeroineNode),
        reinterpret_cast<void**>(&g_original_dual_heroine_node))!=MH_OK) return false;
  if (MH_EnableHook(node)!=MH_OK) { MH_RemoveHook(node);return false; }
  g_dual_heroine_load=reinterpret_cast<HeroineResourceLoad>(load);
  g_dual_heroine_node_installed=true;return true;
}
