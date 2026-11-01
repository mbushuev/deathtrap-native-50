// Scale the retail HUD commands, including atlas sprites and digit quads.
// Alignment is captured per draw, so a whole widget keeps one edge anchor.
using HudRenderFn = void(__cdecl*)();
using UiAlignFn = void(__cdecl*)(int16_t*,int16_t*,int,int);
using UiClipFn = int(__cdecl*)(void*);
// The retail stamina/status bars use coloured polygons, not atlas sprites.
// A cdecl aggregate has the same 29-word stack layout as the native call.
struct UiSolidQuad { int32_t words[29]; };
using UiSolidQuadFn = void(__cdecl*)(UiSolidQuad);
using HardwareTextFn = void(__cdecl*)(void*,int,int,int,const char*,void*);
HudRenderFn g_original_hud_render = nullptr;
UiAlignFn g_original_ui_align = nullptr;
UiClipFn g_original_ui_clip = nullptr;
UiSolidQuadFn g_original_ui_solid_quad = nullptr;
GameTextQueueFn g_original_ui_text_queue = nullptr;
HardwareTextFn g_original_hardware_text = nullptr;
bool g_interface_hooks_installed = false, g_hud_scaling = false;
deathtrap::ui::Transform g_hud_transform{}, g_text_transform{};
bool g_scale_text_draw = false;
struct HudTextPlacement {
  int x,y; uintptr_t font; std::string text;
  deathtrap::ui::Transform transform;
};
std::vector<HudTextPlacement> g_hud_text_placements;

void __cdecl HookHudRender() {
  const bool previous = g_hud_scaling;
  const auto transform = g_hud_transform;
  g_hud_scaling = g_widescreen_world_render_active && g_interface_scale_percent != 100;
  g_hud_text_placements.clear();
  g_hud_transform = {};
  g_original_hud_render();
  g_hud_scaling = previous;
  g_hud_transform = transform;
}

void __cdecl HookUiAlign(int16_t* x,int16_t* y,int horizontal,int vertical) {
  if (g_hud_scaling) {
    uintptr_t viewport=0; std::array<int,4> bounds{};
    if (SafeReadValue(g_dungeon_base+kUiViewportPointerRva,&viewport) && viewport &&
        SafeRead(reinterpret_cast<const void*>(viewport+0x18),bounds.data(),sizeof(bounds))) {
      g_hud_transform = g_interface_radial_draw ? deathtrap::ui::Radial(
          bounds[0],bounds[1],bounds[2],bounds[3],g_xinput_selector_radius,
          g_xinput_selector_center_y,g_interface_scale_percent) :
          deathtrap::ui::Anchored(bounds[0],bounds[1],bounds[2],bounds[3],
              horizontal,vertical,g_interface_scale_percent);
    }
  }
  g_original_ui_align(x,y,horizontal,vertical);
}

int __cdecl HookUiClip(void* command) {
  if (g_hud_scaling && command) {
    auto* rectangle = reinterpret_cast<int16_t*>(static_cast<uint8_t*>(command)+8);
    rectangle[0]=static_cast<int16_t>(std::lround(g_hud_transform.X(rectangle[0])));
    rectangle[1]=static_cast<int16_t>(std::lround(g_hud_transform.Y(rectangle[1])));
    rectangle[2]=static_cast<int16_t>(std::lround(g_hud_transform.X(rectangle[2])));
    rectangle[3]=static_cast<int16_t>(std::lround(g_hud_transform.Y(rectangle[3])));
  }
  return g_original_ui_clip(command);
}

void __cdecl HookUiSolidQuad(UiSolidQuad command) {
  if (g_hud_scaling) {
    for (size_t vertex=0;vertex<4;++vertex) {
      auto* xy=command.words+5+vertex*6;
      xy[0]=static_cast<int32_t>(std::lround(g_hud_transform.X(xy[0])));
      xy[1]=static_cast<int32_t>(std::lround(g_hud_transform.Y(xy[1])));
    }
  }
  g_original_ui_solid_quad(command);
}

void __cdecl HookUiTextQueue(uint32_t flags,int x,int y,int depth,const char* format,...) {
  // Keep the native formatter and bounded queue ownership. Do not retain a
  // transient string pointer: the hardware backend copies strings again.
  char text[2048]{};
  va_list args; va_start(args,format);
  std::vsnprintf(text,sizeof(text),format,args); va_end(args);
  if (g_hud_scaling && g_hud_text_placements.size()<256) {
    uintptr_t font=0;
    if (SafeReadValue(g_dungeon_base+kCurrentFontPointerRva,&font)) {
      auto transform=g_hud_transform;
      // Radial names already compensate the text path's extra context gutter
      // in QueueControllerSelectorName. Their scale pivot needs the same origin.
      if (g_interface_radial_draw) transform.x-=g_centered_ui_context_compensation_x;
      g_hud_text_placements.push_back({x,y,font,text,transform});
    }
  }
  g_original_ui_text_queue(flags,x,y,depth,"%s",text);
}

void __cdecl HookHardwareText(void* destination,int pitch,int x,int y,const char* text,void* font) {
  const bool previous=g_scale_text_draw;
  const auto previous_transform=g_text_transform;
  g_scale_text_draw=false;
  if (g_widescreen_world_render_active && g_interface_scale_percent!=100 && text) {
    for (const auto& entry:g_hud_text_placements) {
      if (entry.x==x && entry.y==y && entry.font==reinterpret_cast<uintptr_t>(font) && entry.text==text) {
        g_text_transform=entry.transform; g_scale_text_draw=true; break;
      }
    }
  }
  g_original_hardware_text(destination,pitch,x,y,text,font);
  g_scale_text_draw=previous; g_text_transform=previous_transform;
}

// The hardware font emits native D3DTLVERTEX quads. Transform positions at
// submission, keeping original atlas UVs, colours and glyph metrics intact.
using UiDrawPrimitiveFn = HRESULT(STDMETHODCALLTYPE*)(void*,DWORD,DWORD,void*,DWORD,DWORD);
HRESULT __stdcall ScaledUiTextDraw(void* device,DWORD primitive,DWORD type,
                                    void* vertices,DWORD count,DWORD flags) {
  std::array<uint8_t,4*32> copy{};
  if (g_scale_text_draw && type==3 && count==4 && vertices) {
    std::memcpy(copy.data(),vertices,copy.size());
    for (size_t i=0;i<4;++i) {
      float coordinates[2]{}; std::memcpy(coordinates,copy.data()+i*32,8);
      coordinates[0]=static_cast<float>(g_text_transform.X(coordinates[0]));
      coordinates[1]=static_cast<float>(g_text_transform.Y(coordinates[1]));
      std::memcpy(copy.data()+i*32,coordinates,8);
    }
    vertices=copy.data();
  }
  const auto table=*reinterpret_cast<uintptr_t**>(device);
  return reinterpret_cast<UiDrawPrimitiveFn>(table[0x74/4])(device,primitive,type,vertices,count,flags);
}
__declspec(naked) void UiTextDrawBridge() {
  __asm {
    pop edx
    push eax
    push edx
    jmp ScaledUiTextDraw
  }
}

bool InstallInterfaceScaleHooks() {
  if (g_interface_hooks_installed) return true;
  struct Hook { uintptr_t rva; void* detour; void** original; };
  const Hook hooks[]={
    {0x76270,reinterpret_cast<void*>(&HookHudRender),reinterpret_cast<void**>(&g_original_hud_render)},
    {0x76630,reinterpret_cast<void*>(&HookUiAlign),reinterpret_cast<void**>(&g_original_ui_align)},
    {0x88060,reinterpret_cast<void*>(&HookUiClip),reinterpret_cast<void**>(&g_original_ui_clip)},
    {0x348C0,reinterpret_cast<void*>(&HookUiSolidQuad),reinterpret_cast<void**>(&g_original_ui_solid_quad)},
    {0x3A080,reinterpret_cast<void*>(&HookUiTextQueue),reinterpret_cast<void**>(&g_original_ui_text_queue)},
    {0x5A080,reinterpret_cast<void*>(&HookHardwareText),reinterpret_cast<void**>(&g_original_hardware_text)}};
  constexpr std::array<uint8_t,6> draw{{0x8B,0x28,0x50,0xFF,0x55,0x74}};
  const std::array<std::vector<uint8_t>,6> prologues{{
    {0x56,0xA1},{0x0F,0xBE,0x44,0x24,0x0C},{0x53,0xA1},{0x83,0xEC,0x04,0x8B,0x15},
    {0x53,0xA1},{0x83,0xEC,0x04,0xA1}}};
  if (!MatchesDungeonCode(ActiveDungeonCodeRva(0x5A374),draw)) return false;
  for (size_t i=0;i<std::size(hooks);++i) {
    std::vector<uint8_t> actual(prologues[i].size());
    if (!SafeRead(g_dungeon_base+ActiveDungeonCodeRva(hooks[i].rva),actual.data(),actual.size()) ||
        actual!=prologues[i]) return false;
  }
  size_t created=0,enabled=0;
  const auto rollback=[&]() {
    while(enabled) MH_DisableHook(g_dungeon_base+ActiveDungeonCodeRva(hooks[--enabled].rva));
    while(created) { const auto& h=hooks[--created]; MH_RemoveHook(g_dungeon_base+ActiveDungeonCodeRva(h.rva)); *h.original=nullptr; }
  };
  for (const auto& h:hooks) {
    if (MH_CreateHook(g_dungeon_base+ActiveDungeonCodeRva(h.rva),h.detour,h.original)!=MH_OK) { rollback();return false; }
    ++created;
  }
  for (const auto& h:hooks) {
    if (MH_EnableHook(g_dungeon_base+ActiveDungeonCodeRva(h.rva))!=MH_OK) { rollback();return false; }
    ++enabled;
  }
  std::array<uint8_t,6> replacement{{0xE8,0,0,0,0,0x90}};
  const auto displacement=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&UiTextDrawBridge)-
      reinterpret_cast<uintptr_t>(g_dungeon_base+ActiveDungeonCodeRva(0x5A374)+5));
  std::memcpy(replacement.data()+1,&displacement,4);
  if (!PatchDungeonCode(ActiveDungeonCodeRva(0x5A374),draw,replacement)) { rollback();return false; }
  g_interface_hooks_installed=true;
  AppendDeathtrapSupportLog("support_interface scale_percent=%d scope=gameplay_hud anchors=native",g_interface_scale_percent);
  return true;
}
