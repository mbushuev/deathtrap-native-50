#include <filesystem>
#include <iostream>
#include "../src/deathtrap_native_render_patch.cc"

namespace {
void Require(bool ok,const char* message) {
  if (!ok) { std::cerr<<"Interface: "<<message<<'\n';std::exit(1); }
}
void Code(uintptr_t rva,const void* data,size_t size) {
  DWORD old=0; auto* at=g_dungeon_base+ActiveDungeonCodeRva(rva);
  Require(VirtualProtect(at,size,PAGE_EXECUTE_READWRITE,&old)!=0,"code protection");
  std::memcpy(at,data,size);FlushInstructionCache(GetCurrentProcess(),at,size);
  DWORD unused=0;VirtualProtect(at,size,old,&unused);
}
uintptr_t Operand(uintptr_t rva) {
  uintptr_t result=0;Require(SafeReadValue(g_dungeon_base+ActiveDungeonCodeRva(rva),&result),"relocated operand");return result;
}
template<class T> void Put(uintptr_t address,T value) {
  Require(SafeWrite(reinterpret_cast<void*>(address),&value,sizeof(value)),"native fixture data");
}
void Jump(uintptr_t rva,void* target) {
  std::array<uint8_t,5> bytes{{0xE9,0,0,0,0}};
  uint32_t relative=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(target)-
      reinterpret_cast<uintptr_t>(g_dungeon_base+ActiveDungeonCodeRva(rva)+5));
  std::memcpy(bytes.data()+1,&relative,4);Code(rva,bytes.data(),bytes.size());
}
uintptr_t __cdecl Texture(void*,int) { return 0x1234; }
std::array<uint8_t,128> submitted{};
unsigned draws=0;
HRESULT __stdcall Draw(void*,DWORD primitive,DWORD type,void* vertices,DWORD count,DWORD flags) {
  Require(primitive==6 && type==3 && count==4 && flags==0,"real glyph D3D submission");
  std::memcpy(submitted.data(),vertices,submitted.size());++draws;return S_OK;
}
HRESULT __stdcall State(void*,DWORD,DWORD) { return S_OK; }
std::string queued;
void __cdecl Queue(uint32_t,int,int,int,const char* format,...) {
  char text[2048]{};va_list args;va_start(args,format);std::vsnprintf(text,sizeof(text),format,args);va_end(args);queued=text;
}
std::vector<UiSolidQuad> bars;
void __cdecl SolidQuad(UiSolidQuad command) { bars.push_back(command); }
void CheckNative(const wchar_t* path) {
  auto module=LoadLibraryExW(path,nullptr,DONT_RESOLVE_DLL_REFERENCES);
  Require(module!=nullptr,"retail DLL mapping");g_dungeon_base=reinterpret_cast<uint8_t*>(module);
  g_dungeon_image_variant=IdentifyDungeonImage(g_dungeon_base);
  Require(g_dungeon_image_variant!=DungeonImageVariant::kUnsupported,"supported retail variant");
  const auto clip_bounds=Operand(0x88062),font_surface=Operand(0x5A084);
  Require(MH_Initialize()==MH_OK,"hook engine");g_interface_hooks_installed=false;
  Require(InstallInterfaceScaleHooks() && InstallInterfaceScaleHooks(),"localized hooks and idempotence");
  std::array<int,4> viewport{}; std::array<int,10> context{};
  Put(reinterpret_cast<uintptr_t>(g_dungeon_base+kUiViewportPointerRva),reinterpret_cast<uintptr_t>(context.data()));
  Put(clip_bounds,reinterpret_cast<uintptr_t>(viewport.data()));
  Put(Operand(0x766F2),0); // Hardware sprites enabled.
  Put(Operand(0x76748),uintptr_t(1)); // Existing atlas resource.
  Jump(0x32780,reinterpret_cast<void*>(&Texture));
  std::vector<uint8_t> arena(65536);
  const uintptr_t queue_base=reinterpret_cast<uintptr_t>(g_dungeon_base)+static_cast<uintptr_t>(kRenderQueueBaseRva);
  Put(queue_base,reinterpret_cast<uintptr_t>(arena.data()));
  Put(queue_base-4,reinterpret_cast<uintptr_t>(arena.data()+arena.size()));
  using Sprite=void(__cdecl*)(const void*,int,int,int,int,int);
  auto sprite=reinterpret_cast<Sprite>(g_dungeon_base+ActiveDungeonCodeRva(0x766D0));
  const std::array<uint8_t,8> atlas{{32,64,31,31,0,0,0,0}};
  for (const auto size : {std::array<int,2>{640,480},{854,480},{1067,600},{1600,600}}) {
    context[6]=viewport[0]=0;context[7]=viewport[1]=0;
    context[8]=viewport[2]=size[0];context[9]=viewport[3]=size[1];
    for (const auto placement : {std::array<int,4>{2,2,0,0},{34,34,1,1},{3,40,0,1},{-16,300,2,1}}) {
      std::array<uint8_t,24> baseline{};
      for (int percent : {100,75,150}) {
        Put(queue_base-8,reinterpret_cast<uintptr_t>(arena.data()));Put(queue_base-16,0);
        g_interface_scale_percent=percent;g_hud_scaling=percent!=100;
        sprite(atlas.data(),placement[0],placement[1],placement[2],placement[3],0);
        uint32_t count=0;Require(SafeReadValue(reinterpret_cast<void*>(queue_base-16),&count)&&count==1,"retail sprite queued once");
        if (percent==100) std::memcpy(baseline.data(),arena.data(),24);
        const auto expected=deathtrap::ui::Anchored(0,0,size[0],size[1],placement[2],placement[3],percent);
        for (int i=0;i<4;++i) {
          int16_t before=0,after=0;std::memcpy(&before,baseline.data()+8+2*i,2);std::memcpy(&after,arena.data()+8+2*i,2);
          Require(after==std::lround(i%2 ? expected.Y(before) : expected.X(before)),"real native corner/center quad scaling");
        }
        Require(!std::memcmp(baseline.data()+16,arena.data()+16,8),"native atlas UVs remain exact");
      }
    }
  }
  // Exercise the real stamina/status-bar generator and its six-pixel height.
  // Capture only the polygon queue boundary; colour/alpha words must be exact.
  std::array<uint8_t,0x118> owner{};
  std::array<uint8_t,0x308> player{};
  Put(reinterpret_cast<uintptr_t>(owner.data()+0x114),reinterpret_cast<uintptr_t>(player.data()));
  Put(Operand(0x8FE84),reinterpret_cast<uintptr_t>(owner.data()));
  Put(Operand(0x8FEB1),0);
  Put(Operand(0x8FEC1),640);
  g_original_ui_solid_quad=&SolidQuad;
  using Bar=void(__cdecl*)();
  auto bar=reinterpret_cast<Bar>(g_dungeon_base+ActiveDungeonCodeRva(0x8FE80));
  for(int fill:{0,50,100}) {
    player[0x1E0]=static_cast<uint8_t>(fill);player[0x1E1]=100;
    Put(reinterpret_cast<uintptr_t>(player.data()+0x304),3);
    std::vector<UiSolidQuad> baseline;
    for(int percent:{100,75,150}) {
      bars.clear();g_hud_scaling=percent!=100;g_interface_scale_percent=percent;bar();
      Require(bars.size()==4,"stamina fill/background and status fill/background");
      if(percent==100) baseline=bars;
      const auto expected=deathtrap::ui::Anchored(0,0,1600,600,0,0,percent);
      for(size_t q=0;q<bars.size();++q) for(size_t i=0;i<29;++i) {
        const bool x=i>=5 && (i-5)%6==0;
        const bool y=i>=6 && (i-6)%6==0;
        const auto value=x ? std::lround(expected.X(baseline[q].words[i])) :
            y ? std::lround(expected.Y(baseline[q].words[i])) : baseline[q].words[i];
        Require(bars[q].words[i]==value,"real bar coordinates scale; colour/alpha stay exact");
      }
    }
  }
  // Execute the original hardware font loop and the patched CALL bridge.
  // Only the COM driver boundary and cached atlas input are fixtures.
  uintptr_t vtable[32]{};vtable[0x74/4]=reinterpret_cast<uintptr_t>(&Draw);vtable[0x5C/4]=reinterpret_cast<uintptr_t>(&State);
  uintptr_t device=reinterpret_cast<uintptr_t>(vtable);
  Put(Operand(0x5A0C7),reinterpret_cast<uintptr_t>(&device));
  Put(font_surface,uintptr_t(0));
  void* font=reinterpret_cast<void*>(0x123456);
  Put(Operand(0x5A0A9),reinterpret_cast<uintptr_t>(font));
  Put(reinterpret_cast<uintptr_t>(g_dungeon_base+kCurrentFontPointerRva),reinterpret_cast<uintptr_t>(font));
  const uintptr_t cache=Operand(0x5A1DC)-8;
  const int glyph[8]={0,0,10,12,11,0,0,0};
  Require(SafeWrite(reinterpret_cast<void*>(cache+'A'*32),glyph,sizeof(glyph)),"native atlas metrics");
  g_original_ui_text_queue=&Queue;
  g_widescreen_world_render_active=true;
  for(int percent:{100,75,150}) {
    g_hud_text_placements.clear();g_hud_scaling=true;g_interface_scale_percent=percent;
    g_hud_transform={320,480,percent};
    HookUiTextQueue(1,300,180,0,"%s","A");Require(queued=="A","native formatted message content");
    draws=0;HookHardwareText(nullptr,640,300,180,"A",font);
    Require(draws==1,"original glyph path submitted exactly once");
    float x=0,y=0;std::memcpy(&x,submitted.data(),4);std::memcpy(&y,submitted.data()+4,4);
    Require(std::abs(x-(320+(300-320)*percent/100.f))<0.01f &&
        std::abs(y-(480+(180-480)*percent/100.f))<0.01f,"actual glyph positions keep selector anchor");
    float right=0,bottom=0;std::memcpy(&right,submitted.data()+32,4);std::memcpy(&bottom,submitted.data()+68,4);
    Require(std::abs(right-x-10*percent/100.f)<0.01f && std::abs(bottom-y-12*percent/100.f)<0.01f,
        "glyph size scales with its placement");
    g_widescreen_world_render_active=false;draws=0;HookHardwareText(nullptr,640,300,180,"A",font);
    std::memcpy(&x,submitted.data(),4);Require(draws==1 && x==300,"menus remain unscaled");
    g_widescreen_world_render_active=true;
  }
  g_hud_scaling=false;g_widescreen_world_render_active=false;g_interface_scale_percent=100;
  Require(MH_Uninitialize()==MH_OK,"hook cleanup");FreeLibrary(module);g_dungeon_base=nullptr;
  std::wcout<<L"Native HUD sprites, stamina/status bars, digits and font bridge: "<<path<<L" PASS\n";
}
} // namespace
int wmain(int argc,wchar_t** argv) {
  for(int i=1;i<argc;++i) CheckNative(argv[i]);
  if(argc==1) {
    for(int percent=75;percent<=150;percent+=5) {
      auto t=deathtrap::ui::Anchored(0,0,854,480,1,1,percent);
      Require(t.X(854)==854 && t.Y(480)==480,"corner anchors stay fixed");
      Require(std::abs((t.X(834)-854)*100/percent+20)<0.001,"consistent distances");
      for(int height:{480,600}) {
        const auto radial=deathtrap::ui::Radial(0,0,854,height,104,316,percent);
        for(int slot=0;slot<8;++slot) {
          const double angle=slot*3.141592653589793/4;
          const double x=427+std::sin(angle)*104-16,y=height-316-std::cos(angle)*104+16;
          Require(radial.X(x)>=0 && radial.X(x+32)<=854 &&
              radial.Y(y)>=-0.01 && radial.Y(y+32)<=height+0.01,"whole radial remains on screen");
        }
      }
    }
    std::cout<<"Interface layout bounds PASS\n";
  }
}
