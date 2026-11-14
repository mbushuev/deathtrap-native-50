// Only the scene model survives. Retail fade timing, item drops, spawn signals,
// actor/AI/collision unlinking and entity-slot reclamation run unconditionally.
constexpr DungeonCodeRva kCorpseDecisionRva{0x5C969u};
constexpr DungeonCodeRva kCorpseDeleteRva{0xC130u};
constexpr DungeonCodeRva kCorpseResetRva{0xAD70u};
constexpr DungeonCodeRva kCorpseRestartActorsRva{0xAFE0u};
constexpr DungeonCodeRva kCorpseLoadActorsRva{0x6AE70u};
constexpr DungeonCodeRva kCorpseDeathUpdateRva{0x5C8E0u};
constexpr DungeonCodeRva kCorpseDamageRva{0x5C050u};
constexpr DungeonCodeRva kCorpseModelDestroyRva{0x3A8F0u};
constexpr DungeonCodeRva kCorpseModelCreateRva{0x3A220u};
constexpr DungeonCodeRva kCorpseMeshDetachRva{0x6DB90u};
constexpr DungeonCodeRva kCorpseMeleeContactsRva{0x1C680u};
constexpr DungeonCodeRva kCorpseFragmentCreateRva{0x6DC90u};
constexpr DungeonCodeRva kCorpseSphereContactRva{0x1AD60u};
constexpr DungeonCodeRva kCorpseSectorContainsPointRva{0x2FB00u};
constexpr DungeonDataRva kCorpseGoreEnabledRva{0x1F1404u,0x1F1E64u,0x1F18D4u,0x1F1AB4u};
constexpr DungeonDataRva kCorpseRegisteredRva{0x34F9C0u,0x350420u,0x34FE90u,0x350070u};
constexpr DungeonDataRva kCorpseAiHeadRva{0x1D7A70u,0x1D84D0u,0x1D7F40u,0x1D8120u};
constexpr DungeonDataRva kCorpseNodePoolRva{0x236F8Cu,0x2379ECu,0x23745Cu,0x23763Cu};
constexpr size_t kMaximumRetainedCorpses=32,kMaximumCorpseNodes=256;
constexpr uint32_t kBonesDeath=2215,kBoneArcherDeath=2178;
constexpr uint32_t kBoneChampionDeath=6478,kGhostDeath=7117;
constexpr size_t kSkeletonFallBones=16;
constexpr unsigned kSkeletonFallTicks=9;
using SkeletonTransform=std::array<int32_t,9>;
struct SkeletonFallBone {
  uintptr_t node=0;
  SkeletonTransform native{},start{},end{};
};
struct SkeletonFall {
  std::array<SkeletonFallBone,kSkeletonFallBones> bones{};
  uint32_t animation=0;
  unsigned ticks=0;
  bool applied=false;
};
struct CorpseAppearance {
  uintptr_t resource=0;
  uint32_t fade_flag=0,tint=0;
  uint16_t material=0;
};
struct CorpseVisualNode { uintptr_t node=0; CorpseAppearance visible{},fade{}; };
struct RetainedCorpse {
  uintptr_t actor=0,root=0; // actor becomes zero after FULL retail cleanup.
  uint16_t gore_style=0,death_flags=0;
  uint32_t last_melee_attack=0;
  bool struck=false;
  std::vector<CorpseVisualNode> nodes;
  std::unique_ptr<SkeletonFall> fall;
};
std::vector<RetainedCorpse> g_retained_corpses;
using CorpseActorFn=void(__cdecl*)(uintptr_t);
CorpseActorFn g_original_corpse_delete=nullptr,g_original_corpse_model_destroy=nullptr;
void(__cdecl* g_original_corpse_reset)()=nullptr;
void(__cdecl* g_original_corpse_restart_actors)()=nullptr;
int(__cdecl* g_original_corpse_load_actors)(void*)=nullptr;
uintptr_t(__cdecl* g_original_corpse_model_create)(uintptr_t)=nullptr;
uintptr_t(__cdecl* g_original_corpse_mesh_detach)(uintptr_t)=nullptr;
int(__cdecl* g_original_corpse_melee_contacts)(uintptr_t)=nullptr;
void* g_original_corpse_decision=nullptr;
bool g_corpse_releasing=false;
uintptr_t g_corpse_hit_root=0; // Pin geometry while native fragment allocation runs.
struct CorpseModelTransfer { uintptr_t root=0; bool held=false; };
CorpseModelTransfer* g_corpse_model_transfer=nullptr;

template<typename T> bool ReadCorpseField(uintptr_t p,size_t offset,T* value) {
  return p && p<=UINTPTR_MAX-offset && SafeReadValue(reinterpret_cast<void*>(p+offset),value);
}
template<typename T> bool WriteCorpseField(uintptr_t p,size_t offset,T value) {
  return p && p<=UINTPTR_MAX-offset && SafeWrite(reinterpret_cast<void*>(p+offset),&value,sizeof(value));
}
bool IsCorpseFadeCallback(uintptr_t callback) {
  return callback==reinterpret_cast<uintptr_t>(g_dungeon_base+kCorpseDeathUpdateRva) ||
      callback==reinterpret_cast<uintptr_t>(g_dungeon_base+ActiveDungeonCodeRva(0x221F0u)) ||
      callback==reinterpret_cast<uintptr_t>(g_dungeon_base+ActiveDungeonCodeRva(0x221A0u)) ||
      callback==reinterpret_cast<uintptr_t>(g_dungeon_base+ActiveDungeonCodeRva(0x22150u));
}
bool ReadCorpseIdentity(uintptr_t actor,uintptr_t* root,uintptr_t* owner=nullptr) {
  uintptr_t callback=0,inner=0,damage=0,definition=0,options=0,link=0,player=0;
  uint16_t flags=0;int32_t health=1;
  if (!g_dungeon_base || !actor || !root ||
      !SafeReadValue(g_dungeon_base+kUiOwnerPointerRva,&player) || player==actor ||
      !ReadCorpseField(actor,0,&callback) || (callback && !IsCorpseFadeCallback(callback)) ||
      !ReadCorpseField(actor,0x2C,&inner) || !ReadCorpseField(inner,0x1030,&health) || health!=0 ||
      !ReadCorpseField(inner,0x1C,&damage) || damage!=reinterpret_cast<uintptr_t>(g_dungeon_base+kCorpseDamageRva) ||
      !ReadCorpseField(inner,0x24,&definition) || !ReadCorpseField(definition,0x54,&options) ||
      !ReadCorpseField(options,4,&flags) || (flags&(0x8000u|0x40u|2u)) || ((flags&4u) && !(flags&0x10u)) ||
      !ReadCorpseField(actor,0x10,&link) || !ReadCorpseField(link,0,root) || !*root) return false;
  if (owner) *owner=link;
  return true;
}
template<typename Visitor> void VisitCorpseActors(Visitor visit) {
  uintptr_t link=0;
  if (!g_dungeon_base || !SafeReadValue(g_dungeon_base+kCorpseRegisteredRva,&link)) return;
  for (size_t count=0;link && count<1024;++count) {
    uintptr_t actor=0,next=0;
    if (!ReadCorpseField(link,8,&actor) || !ReadCorpseField(link,4,&next) || next==link) return;
    if (visit(actor)) return;
    link=next;
  }
}
bool IsRegisteredCorpseActor(uintptr_t actor) {
  bool found=false;
  VisitCorpseActors([&](uintptr_t current) { found=current==actor; return found; });
  return found;
}
RetainedCorpse* FindCorpseModel(uintptr_t root) {
  for (auto& corpse:g_retained_corpses) if (corpse.root==root) return &corpse;
  return nullptr;
}
void ForgetCorpseModel(uintptr_t root) {
  g_retained_corpses.erase(std::remove_if(g_retained_corpses.begin(),g_retained_corpses.end(),
      [&](const RetainedCorpse& c) { return c.root==root; }),g_retained_corpses.end());
}
bool ReadCorpseAppearance(uintptr_t node,CorpseAppearance* state) {
  uint32_t flags=0;
  if (!state || !ReadCorpseField(node,0x24,&flags) || !ReadCorpseField(node,0x28,&state->material) ||
      !ReadCorpseField(node,0x3C,&state->resource) || !ReadCorpseField(node,0x128,&state->tint)) return false;
  state->fade_flag=flags&0x80u;
  return true;
}
void WriteCorpseAppearance(uintptr_t node,const CorpseAppearance& state,bool visible) {
  uint32_t flags=0;
  if (!ReadCorpseField(node,0x24,&flags)) return;
  // Preserve transform/cache flags. Only retail dissolve's flag/material,
  // mesh handle and tint are virtualized; visible corpses keep the final pose.
  WriteCorpseField(node,0x24,(flags&~0x80u)|state.fade_flag|(visible ? 0x80000u:0u));
  WriteCorpseField(node,0x28,state.material);
  WriteCorpseField(node,0x3C,state.resource);
  WriteCorpseField(node,0x128,state.tint);
}

// The skeleton and archer have stock prone poses on their own rigs. Read
// their packed ANM tracks from the loaded level; no game assets are embedded.
uintptr_t SkeletonAnimationResource(uint32_t id,uint16_t frames) {
  int32_t count=0;
  uintptr_t resource=0;
  uint32_t flags=0;
  uint16_t actual_frames=0,bones=0;
  if (!SafeReadValue(g_dungeon_base+kRenderResourceCountRva,&count) ||
      count<=static_cast<int32_t>(id) ||
      !SafeReadValue(g_dungeon_base+kRenderResourceTableRva+id*sizeof(uintptr_t),&resource) ||
      !ReadCorpseField(resource,0,&flags) || flags!=2 ||
      !ReadCorpseField(resource,16,&actual_frames) || actual_frames!=frames ||
      !ReadCorpseField(resource,18,&bones) || bones!=kSkeletonFallBones) return 0;
  return resource;
}
int32_t UnpackSkeletonCoordinate(uint32_t value,unsigned bits) {
  const uint32_t sign=1u<<(bits-1);
  return static_cast<int32_t>(value^sign)-static_cast<int32_t>(sign);
}
bool ReadSkeletonPose(uintptr_t resource,size_t size,size_t bone,unsigned frame,
                      SkeletonTransform* pose) {
  constexpr size_t tracks_start=20+kSkeletonFallBones*8;
  uint32_t track=0,rotation=0,translation=0;
  if (!ReadCorpseField(resource,24+bone*8,&track) || track>=(size-tracks_start)/4) return false;
  size_t cursor=tracks_start+track*4;
  bool has_translation=false;
  for (unsigned i=0;i<=frame;++i) {
    if (cursor+4>size || !ReadCorpseField(resource,cursor,&rotation)) return false;
    cursor+=4;
    if (!(rotation&0x40000000u)) {
      if (cursor+4>size || !ReadCorpseField(resource,cursor,&translation)) return false;
      cursor+=4;
      has_translation=true;
    }
  }
  if (!has_translation) return false;
  *pose={UnpackSkeletonCoordinate(translation>>21,11),
         UnpackSkeletonCoordinate((translation>>10)&2047u,11),
         UnpackSkeletonCoordinate(translation&1023u,10),0,0,0,
         static_cast<int32_t>(rotation&1023u),
         static_cast<int32_t>((rotation>>10)&1023u),
         static_cast<int32_t>((rotation>>20)&1023u)};
  return true;
}
bool PrepareSkeletonFall(RetainedCorpse& corpse,uint32_t animation) {
  const bool archer=animation==kBoneArcherDeath;
  const auto from=SkeletonAnimationResource(animation,23);
  const auto to=SkeletonAnimationResource(archer ? 2165u:2196u,33);
  if (!from || !to) return false;
  auto fall=std::make_unique<SkeletonFall>();
  fall->animation=animation;
  for (const auto& visual:corpse.nodes) {
    if (visual.node==corpse.root) continue; // Leave actor placement and timing on the scene root.
    uint32_t current=0,flags=0;
    int16_t index=-1;
    uint16_t frame=0;
    if (!ReadCorpseField(visual.node,0x50,&current) || !ReadCorpseField(visual.node,0x24,&flags) ||
        !ReadCorpseField(visual.node,0x6E,&index)) return false;
    if (current!=animation || (flags&4u) || index<0 || index>=kSkeletonFallBones) continue;
    auto& bone=fall->bones[index];
    if (bone.node || !ReadCorpseField(visual.node,0x58,&frame) ||
        !ReadSkeletonPose(from,2448,index,(std::min)(unsigned{frame},22u),&bone.start) ||
        !ReadSkeletonPose(to,archer ? 3152u:3344u,index,16,&bone.end)) return false;
    bone.node=visual.node;
  }
  for (const auto& bone:fall->bones) if (!bone.node) return false;
  auto& pelvis=fall->bones.front();
  // Keep the corpse where it died, including when the archer's stock
  // prone clip would slide forward. Raise the resting rig a little so its
  // lowest mesh vertices clear the floor.
  pelvis.end[0]=pelvis.start[0];
  pelvis.end[1]+=24;
  pelvis.end[2]=pelvis.start[2];
  corpse.fall=std::move(fall);
  return true;
}
void InvalidateCorpseLocalTransforms(const RetainedCorpse& corpse) {
  for (const auto& visual:corpse.nodes) {
    uint32_t flags=0;
    if (ReadCorpseField(visual.node,0x24,&flags))
      WriteCorpseField(visual.node,0x24,flags&~0x100000u);
  }
}
void RestoreSkeletonAnimation(RetainedCorpse& corpse) {
  if (!corpse.fall || !corpse.fall->applied) return;
  auto& fall=*corpse.fall;
  for (const auto& bone:fall.bones) {
    WriteCorpseField(bone.node,0,bone.native);
    WriteCorpseField(bone.node,0x50,fall.animation);
  }
  fall.applied=false;
  InvalidateCorpseLocalTransforms(corpse);
}
void ApplySkeletonFall(RetainedCorpse& corpse) {
  if (!corpse.fall) return;
  auto& fall=*corpse.fall;
  if (fall.applied && fall.ticks==kSkeletonFallTicks) return;
  if (!fall.applied) {
    // Capture the native inputs after each actor update, before temporarily
    // replacing the animation with local transforms for scene rendering.
    for (auto& bone:fall.bones)
      if (!ReadCorpseField(bone.node,0,&bone.native)) return;
  }
  fall.ticks=(std::min)(fall.ticks+1,kSkeletonFallTicks);
  const float t=static_cast<float>(fall.ticks)/kSkeletonFallTicks;
  const float blend=t*t*(3.0f-2.0f*t);
  for (const auto& bone:fall.bones) {
    auto pose=bone.start;
    for (size_t i=0;i<pose.size();++i) {
      int32_t delta=bone.end[i]-bone.start[i];
      if (i>=6) delta=((delta+512)&1023)-512; // Shortest arc in retail angle units.
      pose[i]+=static_cast<int32_t>(std::lround(delta*blend));
    }
    WriteCorpseField(bone.node,0,pose);
    WriteCorpseField(bone.node,0x50,uint32_t{0});
  }
  fall.applied=true;
  InvalidateCorpseLocalTransforms(corpse);
}
bool RememberCorpseModel(uintptr_t actor) {
  uintptr_t root=0;int16_t count=0;
  if (!ReadCorpseIdentity(actor,&root) || !IsRegisteredCorpseActor(actor)) return false;
  if (FindCorpseModel(root)) return true;
  // Retail also reports animation completion when the root has no clip.
  // Leave these deaths on the original path, including effects and removal.
  uint32_t animation=0;
  if (!ReadCorpseField(root,0x50,&animation) || !animation ||
      animation==kBoneChampionDeath || animation==kGhostDeath) return false;
  if (g_retained_corpses.size()>=kMaximumRetainedCorpses ||
      !ReadCorpseField(root,0x6C,&count) || count<=0 || count>kMaximumCorpseNodes) return false;
  RetainedCorpse corpse;corpse.actor=actor;corpse.root=root;corpse.nodes.reserve(count);
  uintptr_t inner=0,definition=0,options=0;
  if (!ReadCorpseField(actor,0x2C,&inner) || !ReadCorpseField(inner,0x24,&definition) ||
      !ReadCorpseField(definition,0x54,&options) ||
      !ReadCorpseField(options,2,&corpse.gore_style) ||
      !ReadCorpseField(options,4,&corpse.death_flags)) return false;
  // Traverse the actual retained subtree, just like 3B0C0/3A8F0. Attached
  // weapons/effects can have extra nodes outside the base animation array.
  // The root's sibling belongs to the surrounding scene and is not ours.
  std::vector<uintptr_t> pending{root};
  while (!pending.empty()) {
    CorpseVisualNode node;
    node.node=pending.back();pending.pop_back();
    if (corpse.nodes.size()>=kMaximumCorpseNodes ||
        std::any_of(corpse.nodes.begin(),corpse.nodes.end(),
            [&](const CorpseVisualNode& known) { return known.node==node.node; }) ||
        !ReadCorpseAppearance(node.node,&node.visible)) return false;
    uintptr_t child=0,sibling=0;
    if (!ReadCorpseField(node.node,0x30,&child) || !ReadCorpseField(node.node,0x34,&sibling)) return false;
    if (child) pending.push_back(child);
    if (node.node!=root && sibling) pending.push_back(sibling);
    node.fade=node.visible;corpse.nodes.push_back(node);
  }
  if (animation==kBonesDeath || animation==kBoneArcherDeath) {
    if (!PrepareSkeletonFall(corpse,animation)) return false;
    AppendDeathtrapSupportLog("support_bodies skeleton_fall death_animation=%u",animation);
  }
  g_retained_corpses.push_back(std::move(corpse));
  return true;
}
bool MakeCorpseModelStatic(RetainedCorpse& corpse) {
  for (auto& node:corpse.nodes) {
    // 3B0C0 invokes node+10C each scene update. NPC handlers 46710/46900
    // dereference the actor in node+68, whose collider C2A0 has now freed.
    // 3B998 separately dispatches render effects through 4DFA0 when flag 80
    // is set. Their material selector (+28 high byte) can invoke 858F0, which
    // ALSO reads actor+68. Stop both mechanisms before removing the backpointer.
    // Update the saved visible state too, so EndCorpseFadeUpdate cannot restore
    // the actor-dependent effect on the next frame. Preserve the palette byte.
    node.visible.fade_flag=0;
    node.visible.material&=0x00FFu;
    uint32_t flags=0;
    if (!ReadCorpseField(node.node,0x24,&flags) ||
        !WriteCorpseField(node.node,0x24,flags&~0x80u) ||
        !WriteCorpseField(node.node,0x28,node.visible.material) ||
        !WriteCorpseField(node.node,0x10C,uintptr_t{0}) ||
        !WriteCorpseField(node.node,0x68,uintptr_t{0})) return false;
  }
  return true;
}
void __cdecl HookCorpseModelDestroy(uintptr_t root) {
  if (g_corpse_model_transfer && root==g_corpse_model_transfer->root) {
    if (auto* corpse=FindCorpseModel(root); corpse && MakeCorpseModelStatic(*corpse)) {
      g_corpse_model_transfer->held=true;
      return; // Retain a static model, detached from the reclaimed actor.
    }
  }
  // Partial model destruction (e.g. scripted dismemberment) also invalidates
  // the saved appearance. Never write a recycled child-node address later.
  g_retained_corpses.erase(std::remove_if(g_retained_corpses.begin(),g_retained_corpses.end(),
      [&](RetainedCorpse& c) {
        if (c.root==root) return true;
        const bool removing_child=std::any_of(c.nodes.begin(),c.nodes.end(),
            [&](const CorpseVisualNode& n) { return n.node==root; });
        if (removing_child && c.actor) RestoreSkeletonAnimation(c);
        return removing_child;
      }),g_retained_corpses.end());
  g_original_corpse_model_destroy(root);
}
bool ReleaseOldestCorpseVisual() {
  if (g_corpse_releasing || !g_original_corpse_model_destroy) return false;
  for (const auto& corpse:g_retained_corpses) if (!corpse.actor && corpse.root!=g_corpse_hit_root) {
    const auto root=corpse.root;ForgetCorpseModel(root);
    g_corpse_releasing=true;g_original_corpse_model_destroy(root);g_corpse_releasing=false;
    return true;
  }
  return false;
}
void __cdecl HookCorpseDelete(uintptr_t actor) {
  uintptr_t root=0;
  for (const auto& corpse:g_retained_corpses) if (corpse.actor==actor) { root=corpse.root; break; }
  uintptr_t actual_root=0;
  if (!root || !g_keep_bodies_enabled.load(std::memory_order_relaxed) ||
      !ReadCorpseIdentity(actor,&actual_root) || root!=actual_root ||
      g_active_presentation_trace.stage!=DeathtrapNativePresentationStage::kNone) {
    g_original_corpse_delete(actor);return;
  }
  CorpseModelTransfer transfer{root,false};
  auto* previous=g_corpse_model_transfer;g_corpse_model_transfer=&transfer;
  // This executes ALL original cleanup, including items, spawn notifications,
  // collision/AI removal, active-actor lists and both reusable actor pools.
  g_original_corpse_delete(actor);
  g_corpse_model_transfer=previous;
  if (auto* corpse=FindCorpseModel(root)) {
    if (transfer.held) corpse->actor=0;
    else ForgetCorpseModel(root);
  }
  AppendDeathtrapSupportLog("support_bodies actor_cleanup=complete visual=%u scene_callbacks=%s render_effects=%s",
      transfer.held ? 1u:0u,transfer.held ? "detached":"native",transfer.held ? "static":"native");
}
void BeginCorpseFadeUpdate() {
  if (!g_corpse_retention_hooks_installed || g_corpse_dispatch_depth ||
      g_active_presentation_trace.stage!=DeathtrapNativePresentationStage::kNone) return;
  for (auto& corpse:g_retained_corpses) if (corpse.actor) {
    RestoreSkeletonAnimation(corpse);
    for (const auto& node:corpse.nodes) WriteCorpseAppearance(node.node,node.fade,false);
  }
}
void EndCorpseFadeUpdate() {
  if (!g_corpse_retention_hooks_installed || g_corpse_dispatch_depth!=1 ||
      g_active_presentation_trace.stage!=DeathtrapNativePresentationStage::kNone) return;
  for (auto& corpse:g_retained_corpses) {
    for (auto& node:corpse.nodes) {
      if (corpse.actor) ReadCorpseAppearance(node.node,&node.fade);
      WriteCorpseAppearance(node.node,node.visible,true);
    }
    ApplySkeletonFall(corpse);
  }
}
void MaintainRetainedCorpses() {
  if (!g_corpse_retention_hooks_installed || g_corpse_dispatch_depth || g_corpse_releasing ||
      g_active_presentation_trace.stage!=DeathtrapNativePresentationStage::kNone) return;
  // Recover actors frozen by earlier test builds, without repeating death
  // triggers or manufacturing item drops. Their native post-death path resumes.
  std::vector<uintptr_t> legacy;
  VisitCorpseActors([&](uintptr_t actor) {
    uintptr_t callback=1,root=0;uint32_t flags=0;
    if (ReadCorpseField(actor,0,&callback) && !callback && ReadCorpseIdentity(actor,&root) &&
        ReadCorpseField(root,0x24,&flags) && (flags&0x80000u)) legacy.push_back(actor);
    return false;
  });
  for (auto actor:legacy) {
    if (g_keep_bodies_enabled.load()) RememberCorpseModel(actor);
    WriteCorpseField(actor,0,reinterpret_cast<uintptr_t>(g_dungeon_base+kCorpseDeathUpdateRva));
  }
  g_retained_corpses.erase(std::remove_if(g_retained_corpses.begin(),g_retained_corpses.end(),
      [](const RetainedCorpse& c) {
        uintptr_t root=0;
        return c.actor && (!IsRegisteredCorpseActor(c.actor) ||
            !ReadCorpseIdentity(c.actor,&root) || root!=c.root);
      }),g_retained_corpses.end());
  if (!g_keep_bodies_enabled.load(std::memory_order_relaxed)) {
    for (auto& corpse:g_retained_corpses) if (corpse.actor) {
      RestoreSkeletonAnimation(corpse);
      for (const auto& node:corpse.nodes) WriteCorpseAppearance(node.node,node.fade,false);
    }
    while (ReleaseOldestCorpseVisual()) {}
    g_retained_corpses.clear(); // Pending actors finish their retail fade normally.
  }
}
void __cdecl ObserveCorpseDeath(uintptr_t actor) {
  if (g_corpse_retention_hooks_installed && g_keep_bodies_enabled.load(std::memory_order_relaxed) &&
      g_active_presentation_trace.stage==DeathtrapNativePresentationStage::kNone)
    RememberCorpseModel(actor);
}
__declspec(naked) void HookCorpseDecision() {
  __asm {
    pushfd
    pushad
    push esi
    call ObserveCorpseDeath
    add esp,4
    popad
    popfd
    jmp dword ptr [g_original_corpse_decision]
  }
}
void __cdecl HookCorpseReset() {
  while (ReleaseOldestCorpseVisual()) {}
  g_retained_corpses.clear();g_original_corpse_reset();
}
void ClearCorpsesBeforeActorReplacement(const char* transition) {
  if (g_retained_corpses.empty()) return;
  const size_t retained = g_retained_corpses.size();
  while (ReleaseOldestCorpseVisual()) {}
  for (auto& corpse : g_retained_corpses) if (corpse.actor) {
    RestoreSkeletonAnimation(corpse);
    for (const auto& node : corpse.nodes)
      WriteCorpseAppearance(node.node, node.fade, false);
  }
  // The upcoming native actor teardown must destroy pending models normally.
  g_retained_corpses.clear();
  AppendDeathtrapSupportLog("support_bodies transition=%s cleared=%zu",
                            transition, retained);
}
void __cdecl HookCorpseRestartActors() {
  ClearCorpsesBeforeActorReplacement("restart");
  g_original_corpse_restart_actors();
}
int __cdecl HookCorpseLoadActors(void* save) {
  ClearCorpsesBeforeActorReplacement("load");
  return g_original_corpse_load_actors(save);
}
uintptr_t __cdecl HookCorpseModelCreate(uintptr_t resource) {
  uintptr_t pool=0;int32_t last_free=0;
  if (g_corpse_retention_hooks_installed && !g_corpse_releasing &&
      g_active_presentation_trace.stage==DeathtrapNativePresentationStage::kNone &&
      SafeReadValue(g_dungeon_base+kCorpseNodePoolRva,&pool)) {
    while (ReadCorpseField(pool,0x20,&last_free) && last_free<16 && ReleaseOldestCorpseVisual()) {}
  }
  return g_original_corpse_model_create(resource);
}

uintptr_t __cdecl HookCorpseMeshDetach(uintptr_t node) {
  uintptr_t root=0;
  for (const auto& corpse:g_retained_corpses)
    if (std::any_of(corpse.nodes.begin(),corpse.nodes.end(),
        [&](const CorpseVisualNode& n) { return n.node==node; })) { root=corpse.root;break; }
  const auto previous=g_corpse_hit_root;
  if (root) g_corpse_hit_root=root;
  const uintptr_t fragment=g_original_corpse_mesh_detach(node);
  // 6DB90 removes a REAL severed mesh, unlike the fade's temporary zero at
  // 22120. Remember cuts even while the NPC is still in the retail fade path.
  // Re-find after allocation: pressure can evict another corpse from the vector.
  if (fragment && root) if (auto* corpse=FindCorpseModel(root))
    for (auto& visual:corpse->nodes) if (visual.node==node) {
      visual.visible.resource=0;visual.fade.resource=0;break;
    }
  g_corpse_hit_root=previous;
  return fragment;
}

using CorpseSphere=std::array<int32_t,4>;
bool ReadCorpseSphere(uintptr_t p,size_t offset,CorpseSphere* sphere) {
  return ReadCorpseField(p,offset,sphere) && (*sphere)[3]>=0 && (*sphere)[3]<=16384;
}
std::vector<CorpseSphere> ReadCorpseAttackSpheres(uintptr_t player) {
  uintptr_t inner=0,weapon=0,descriptor=0;
  int8_t count=0;
  std::vector<CorpseSphere> spheres;
  if (!ReadCorpseField(player,0x2C,&inner) || !ReadCorpseField(inner,0x48,&weapon) ||
      !ReadCorpseField(weapon,4,&descriptor) || !ReadCorpseField(descriptor,5,&count) ||
      count<=0 || count>20) return spheres;
  spheres.reserve(static_cast<size_t>(count)*4);
  // The same live weapon nodes and three intermediate sweep points used by
  // retail 6B310/6C6A0. No raw button, camera direction or arbitrary hit radius.
  for (int i=0;i<count;++i) {
    uintptr_t node=0;CorpseSphere sphere{};
    if (!ReadCorpseField(weapon,8+i*4,&node) || !ReadCorpseSphere(node,0x80,&sphere)) continue;
    spheres.push_back(sphere);
    for (size_t sample=0;sample<3;++sample) {
      std::array<int32_t,3> point{};
      if (ReadCorpseField(weapon,0x58+i*12+sample*0xF0,&point))
        spheres.push_back({point[0],point[1],point[2],50});
    }
  }
  return spheres;
}
bool ReserveCorpseFragmentNodes(size_t count) {
  uintptr_t pool=0;int32_t last_free=0;
  if (!SafeReadValue(g_dungeon_base+kCorpseNodePoolRva,&pool)) return false;
  // A detached mesh plus its independent physics root each need one node.
  // Keep the same reserve as ordinary model allocation and never evict the
  // body being cut while a native routine is holding its node pointers.
  while (ReadCorpseField(pool,0x20,&last_free)) {
    if (last_free>=static_cast<int32_t>(16+count*2)) return true;
    if (!ReleaseOldestCorpseVisual()) return false;
  }
  return false;
}
bool SeverCorpseVisual(uintptr_t root,uintptr_t hit,uintptr_t player,
                       const std::array<int32_t,3>& origin,
                       const std::array<int32_t,3>& strike_position) {
  auto* corpse=FindCorpseModel(root);
  int16_t count=0,key=0;
  if (!corpse || corpse->actor || !ReadCorpseField(hit,0x6C,&count) || count<1 || count>3 ||
      !ReadCorpseField(hit,0x6E,&key)) return false;
  const uint32_t effect=corpse->gore_style+0x1FEu;
  uintptr_t player_owner=0,player_model=0,player_sector=0;
  if (!ReadCorpseField(player,0x10,&player_owner) || !ReadCorpseField(player_owner,0,&player_model) ||
      !ReadCorpseField(player_model,0x100,&player_sector) || !player_sector) return false;
  std::vector<uintptr_t> parts;
  // Native 6DA90 severs this small limb's consecutive model keys, including
  // its children. Never use a reclaimed animation-owner or enemy pointer.
  for (int index=key;index<key+count;++index) {
    for (const auto& visual:corpse->nodes) {
      int16_t candidate=0;uintptr_t mesh=0;
      if (visual.node!=root && ReadCorpseField(visual.node,0x6E,&candidate) && candidate==index &&
          ReadCorpseField(visual.node,0x3C,&mesh) && mesh) { parts.push_back(visual.node);break; }
    }
  }
  if (parts.empty()) return false;
  const auto previous=g_corpse_hit_root;g_corpse_hit_root=root;
  bool severed=false;
  if (ReserveCorpseFragmentNodes(parts.size())) for (const auto node:parts) {
    std::array<int32_t,3> position{};uintptr_t seed=0;
    if (!ReadCorpseField(node,0x80,&position) || !ReadCorpseField(root,0x100,&seed) || !seed) continue;
    // 2FB00 is a BOOLEAN containment test, not a sector resolver. Retail
    // 6DAE0 checks its return and passes the ORIGINAL sector to 6DC90. Passing
    // its successful return (1) as node+100 crashes native geometry at 3D964.
    const auto contains=reinterpret_cast<int(__cdecl*)(const int32_t*,uintptr_t)>(
        g_dungeon_base+kCorpseSectorContainsPointRva);
    if (!contains(position.data(),seed)) continue;
    const uintptr_t sector=seed;
    const auto resolve=reinterpret_cast<ResolveCameraSectorFn>(g_dungeon_base+kResolveCameraSectorRva);
    // Trace the actual weapon contact, not the actor group's OWN-mesh bounds.
    // Animation roots commonly have resource=0 and bounds80={0,0,0,0}; they
    // have no own mesh. The hit spheres belong to the animated weapon nodes.
    const uintptr_t strike_sector=resolve(strike_position.data(),player_sector);
    const auto visible=reinterpret_cast<CameraRoomTraceVisibleFn>(g_dungeon_base+kCameraRoomTraceVisibleRva);
    if (!strike_sector || !visible(strike_sector,strike_position.data(),sector,position.data())) continue;
    const uintptr_t fragment=HookCorpseMeshDetach(node);
    if (!fragment) continue;
    // The retail fragment owns a NEW physics object/root. Use the living
    // striker as source; neither the saved visual nor its pieces refer to the
    // destroyed NPC, its collider, its AI or its already-issued loot.
    reinterpret_cast<void(__cdecl*)(const int32_t*,const int32_t*,uintptr_t,uint32_t,
        const int32_t*,uintptr_t,uintptr_t)>(g_dungeon_base+kCorpseFragmentCreateRva)(
        position.data(),origin.data(),fragment,effect,
        reinterpret_cast<const int32_t*>(fragment+0x18),sector,player);
    severed=true;
  }
  g_corpse_hit_root=previous;
  return severed;
}
void ProcessRetainedCorpseMelee(uintptr_t player) {
  uintptr_t actual_player=0,owner=0,model=0;int32_t gore=0;uint32_t attack=0;
  std::array<int32_t,3> origin{};
  if (!g_corpse_retention_hooks_installed || !g_keep_bodies_enabled.load(std::memory_order_relaxed) ||
      g_corpse_hit_root || g_active_presentation_trace.stage!=DeathtrapNativePresentationStage::kNone ||
      !SafeReadValue(g_dungeon_base+kUiOwnerPointerRva,&actual_player) || !player || player!=actual_player ||
      !SafeReadValue(g_dungeon_base+kCorpseGoreEnabledRva,&gore) || !gore ||
      !ReadCorpseField(player,0xFC,&attack) || !ReadCorpseField(player,0x10,&owner) ||
      !ReadCorpseField(owner,0,&model) || !ReadCorpseField(model,0,&origin)) return;
  const auto spheres=ReadCorpseAttackSpheres(player);
  if (spheres.empty()) return;
  struct Hit { uintptr_t root,node;std::array<int32_t,3> strike_position; };
  std::vector<Hit> hits;
  const auto contact=reinterpret_cast<int(__cdecl*)(const int32_t*,const int32_t*)>(
      g_dungeon_base+kCorpseSphereContactRva);
  for (const auto& corpse:g_retained_corpses) {
    if (corpse.actor || (corpse.death_flags&0x20u) || (corpse.struck && corpse.last_melee_attack==attack)) continue;
    uintptr_t closest=0;double distance=std::numeric_limits<double>::max();
    std::array<int32_t,3> strike_position{};
    for (const auto& visual:corpse.nodes) {
      uintptr_t mesh=0;int16_t count=0;CorpseSphere bounds{};
      if (visual.node==corpse.root || !ReadCorpseField(visual.node,0x3C,&mesh) || !mesh ||
          !ReadCorpseField(visual.node,0x6C,&count) || count<1 || count>3 ||
          !ReadCorpseSphere(visual.node,0x80,&bounds)) continue;
      for (const auto& sphere:spheres) if (contact(sphere.data(),bounds.data())) {
        double squared=0;
        for (size_t axis=0;axis<3;++axis) {
          const double delta=static_cast<double>(sphere[axis])-bounds[axis];squared+=delta*delta;
        }
        if (squared<distance) {
          closest=visual.node;distance=squared;
          strike_position={sphere[0],sphere[1],sphere[2]};
        }
      }
    }
    if (closest) hits.push_back({corpse.root,closest,strike_position});
  }
  // No vector reference survives an allocation or old-geometry eviction.
  for (const auto& hit:hits) if (SeverCorpseVisual(hit.root,hit.node,player,origin,hit.strike_position))
    if (auto* corpse=FindCorpseModel(hit.root)) {
      corpse->struck=true;corpse->last_melee_attack=attack;
      AppendDeathtrapSupportLog("support_bodies melee=severed root=%08llX node=%08llX attack=%u",
          static_cast<unsigned long long>(hit.root),static_cast<unsigned long long>(hit.node),attack);
    }
}
int __cdecl HookCorpseMeleeContacts(uintptr_t actor) {
  const int result=g_original_corpse_melee_contacts(actor);
  // Called by retail only inside its strike window, after weapon sweeps and
  // ordinary actor contacts. Keep its result and living-enemy behaviour intact.
  ProcessRetainedCorpseMelee(actor);
  return result;
}
bool InstallCorpseRetentionHooks() {
  if (g_corpse_retention_hooks_installed) return true;
  uintptr_t registered=0,ai=0,pool=0,gore=0;int32_t model_call=0;
  const bool signatures=g_dungeon_base && g_dungeon_image_variant!=DungeonImageVariant::kUnsupported &&
      MatchesDungeonCode(kCorpseDecisionRva,std::array<uint8_t,8>{{0xA8,0x40,0x74,0x20,0x6A,1,0x8B,0x46}}) &&
      MatchesDungeonCode(kCorpseDeleteRva,std::array<uint8_t,6>{{0x56,0x57,0x8B,0x7C,0x24,0x0C}}) &&
      MatchesDungeonCode(kCorpseResetRva,std::array<uint8_t,5>{{0x53,0x33,0xC9,0x66,0x89}}) &&
      MatchesDungeonCode(kCorpseRestartActorsRva,std::array<uint8_t,2>{{0x56,0xA1}}) &&
      MatchesDungeonCode(kCorpseRestartActorsRva+6,std::array<uint8_t,4>{{0x85,0xC0,0x74,0x19}}) &&
      MatchesDungeonCode(kCorpseLoadActorsRva,std::array<uint8_t,8>{{0x83,0xEC,0x38,0x53,0x56,0x57,0x55,0xE8}}) &&
      MatchesDungeonCode(kCorpseModelDestroyRva,std::array<uint8_t,6>{{0x56,0x57,0x8B,0x7C,0x24,0x0C}}) &&
      MatchesDungeonCode(kCorpseModelCreateRva,std::array<uint8_t,4>{{0x83,0xEC,0x24,0xA1}}) &&
      MatchesDungeonCode(kCorpseMeshDetachRva,std::array<uint8_t,6>{{0x53,0x56,0x8B,0x74,0x24,0x0C}}) &&
      MatchesDungeonCode(kCorpseMeleeContactsRva,std::array<uint8_t,5>{{0x83,0xEC,0x1C,0x53,0x56}}) &&
      MatchesDungeonCode(kCorpseFragmentCreateRva,std::array<uint8_t,6>{{0x53,0x56,0x57,0x55,0x33,0xFF}}) &&
      MatchesDungeonCode(kCorpseSphereContactRva,std::array<uint8_t,7>{{0x8B,0x4C,0x24,4,0x83,0xEC,0x0C}}) &&
      MatchesDungeonCode(kCorpseSectorContainsPointRva,std::array<uint8_t,6>{{0x8B,0x4C,0x24,8,0x53,0x56}}) &&
      MatchesDungeonCode(kResolveCameraSectorRva,std::array<uint8_t,7>{{0x8B,0x44,0x24,8,0x83,0xEC,0x10}}) &&
      MatchesDungeonCode(kCameraRoomTraceVisibleRva,std::array<uint8_t,7>{{0x8B,0x4C,0x24,8,0x83,0xEC,0x18}}) &&
      SafeReadValue(g_dungeon_base+kCorpseDamageRva+0x11,&gore) &&
      gore==reinterpret_cast<uintptr_t>(g_dungeon_base+kCorpseGoreEnabledRva) &&
      MatchesDungeonCode(kPlayerMovementStageRva,std::array<uint8_t,4>{{0x56,0x57,0x8B,0x3D}}) &&
      MatchesDungeonCode(ActiveDungeonCodeRva(0x3B0C0u),
          std::array<uint8_t,10>{{0x56,0x8B,0x74,0x24,8,0x8B,0x46,0x24,0xA8,8}}) &&
      MatchesDungeonCode(ActiveDungeonCodeRva(0x3B998u),
          std::array<uint8_t,7>{{0x8B,0x46,0x24,0xA8,0x80,0x74,0x0F}}) &&
      MatchesDungeonCode(ActiveDungeonCodeRva(0x221F0u),std::array<uint8_t,5>{{0x56,0x8B,0x74,0x24,8}}) &&
      MatchesDungeonCode(ActiveDungeonCodeRva(0x221A0u),std::array<uint8_t,5>{{0x56,0x8B,0x74,0x24,8}}) &&
      MatchesDungeonCode(ActiveDungeonCodeRva(0x22150u),std::array<uint8_t,5>{{0x56,0x8B,0x74,0x24,8}}) &&
      MatchesDungeonCode(0xC30Eu,std::array<uint8_t,1>{{0xE8}}) &&
      SafeReadValue(g_dungeon_base+0xC30Fu,&model_call) &&
      reinterpret_cast<uintptr_t>(g_dungeon_base+0xC313u)+model_call==
          reinterpret_cast<uintptr_t>(g_dungeon_base+kCorpseModelDestroyRva) &&
      SafeReadValue(g_dungeon_base+0xC102u,&registered) &&
      registered==reinterpret_cast<uintptr_t>(g_dungeon_base+kCorpseRegisteredRva) &&
      SafeReadValue(g_dungeon_base+kPlayerMovementStageRva+4,&ai) &&
      ai==reinterpret_cast<uintptr_t>(g_dungeon_base+kCorpseAiHeadRva) &&
      SafeReadValue(g_dungeon_base+kCorpseModelCreateRva+4,&pool) &&
      pool==reinterpret_cast<uintptr_t>(g_dungeon_base+kCorpseNodePoolRva);
  if (!signatures || g_original_player_movement_stage) {
    AppendDeathtrapSupportLog("support_bodies state=signature_mismatch fallback=native");return false;
  }
  struct Hook { uintptr_t rva;void* detour;void** original;bool made=false,enabled=false; };
  std::array<Hook,10> hooks{{
    {kCorpseDecisionRva,reinterpret_cast<void*>(&HookCorpseDecision),&g_original_corpse_decision},
    {kCorpseDeleteRva,reinterpret_cast<void*>(&HookCorpseDelete),reinterpret_cast<void**>(&g_original_corpse_delete)},
    {kCorpseResetRva,reinterpret_cast<void*>(&HookCorpseReset),reinterpret_cast<void**>(&g_original_corpse_reset)},
    {kCorpseRestartActorsRva,reinterpret_cast<void*>(&HookCorpseRestartActors),reinterpret_cast<void**>(&g_original_corpse_restart_actors)},
    {kCorpseLoadActorsRva,reinterpret_cast<void*>(&HookCorpseLoadActors),reinterpret_cast<void**>(&g_original_corpse_load_actors)},
    {kCorpseModelDestroyRva,reinterpret_cast<void*>(&HookCorpseModelDestroy),reinterpret_cast<void**>(&g_original_corpse_model_destroy)},
    {kCorpseModelCreateRva,reinterpret_cast<void*>(&HookCorpseModelCreate),reinterpret_cast<void**>(&g_original_corpse_model_create)},
    {kCorpseMeshDetachRva,reinterpret_cast<void*>(&HookCorpseMeshDetach),reinterpret_cast<void**>(&g_original_corpse_mesh_detach)},
    {kCorpseMeleeContactsRva,reinterpret_cast<void*>(&HookCorpseMeleeContacts),reinterpret_cast<void**>(&g_original_corpse_melee_contacts)},
    {kPlayerMovementStageRva,reinterpret_cast<void*>(&HookImmersivePlayerMovementStage),reinterpret_cast<void**>(&g_original_player_movement_stage)}}};
  const auto rollback=[&]() { for (auto& h:hooks) {
    if (h.enabled) MH_DisableHook(g_dungeon_base+h.rva);
    if (h.made) { MH_RemoveHook(g_dungeon_base+h.rva);*h.original=nullptr; }
  }};
  for (auto& h:hooks) {
    if (MH_CreateHook(g_dungeon_base+h.rva,h.detour,h.original)!=MH_OK) { rollback();return false; }
    h.made=true;
  }
  for (auto& h:hooks) {
    if (MH_EnableHook(g_dungeon_base+h.rva)!=MH_OK) { rollback();return false; }
    h.enabled=true;
  }
  g_corpse_retention_hooks_installed=true;
  AppendDeathtrapSupportLog("support_bodies state=active mode=visual_with_melee limit=32 native_actor_cleanup=1");
  return true;
}
