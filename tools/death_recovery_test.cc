#include <windows.h>
#include <filesystem>
#include <iostream>
#include <memory>
#include "../src/deathtrap_native_render_patch.cc"

namespace {
void Require(bool ok, const char* message) {
  if (!ok) { std::cerr << "death recovery: " << message << '\n'; std::exit(1); }
}
std::array<uint8_t,0x9EC> context{};
std::array<WIN32_FILE_ATTRIBUTE_DATA,10> files{};
std::array<uint8_t,10> saved_levels{};
bool serializer_ok = true, file_exists = true, page_ok = true, mode_ok = true;
int raster_width=2880,raster_height=1800,raster_pitch=11520,raster_bpp=32;
int mode_calls=0;
int pause_calls = 0, play_calls = 0, release_calls = 0, root_calls = 0;
int wanted_choice = 0, input_frame = 0, drawn_frames = 0;
template<class T> void Put(size_t offset, T value) {
  std::memcpy(context.data()+offset,&value,sizeof(value));
}
void* __cdecl Context() { return context.data(); }
int __cdecl Save(const char*, const void*) { return serializer_ok ? 1 : 0; }
int __cdecl Load(const char*) { return serializer_ok ? 1 : 0; }
void __cdecl State(void*) { ++play_calls; }
void __cdecl Root() { ++root_calls; }
bool Fingerprint(const char* name, WIN32_FILE_ATTRIBUTE_DATA* out) {
  const int slot = deathtrap::recovery::Slot(name);
  if (!file_exists || slot<0) return false;
  *out=files[slot];return true;
}
void __cdecl Slots(void* list) {
  Require(list==context.data()+0xD,"refresh must address current native slot list");
  for (int i=0;i<10;++i) {
    context[0xD8+i*0xCB]=saved_levels[i];
    const char name[]{'s',static_cast<char>('0'+i),0};
    std::memcpy(context.data()+0x4F+i*0xCB,name,3);
  }
}
bool Page() { return page_ok; }
void Release() { ++release_calls; }
int __cdecl EnterMenu(int mode) {
  Require(mode==0,"use native frontend transition, not gameplay resume");
  ++mode_calls;
  if (!mode_ok) return 0;
  raster_width=raster_pitch=640;raster_height=480;raster_bpp=8;
  return 1;
}
void __cdecl Mouse(uint16_t* x,uint16_t* y,uint16_t* buttons) { *x=*y=*buttons=0; }
int __cdecl Key(uint32_t scan) {
  // A gameplay confirmation held on entry must be released before choosing.
  return scan==0x1C ? (input_frame==0 || input_frame==4) :
      scan==0xD0 ? (wanted_choice==1 && input_frame==2) : 0;
}
void __cdecl Font(uint32_t) {}
void __cdecl Text(int,int,int,const char*) {}
int32_t __cdecl Width(const char* s) { return static_cast<int32_t>(std::strlen(s))*7; }
void Copy(const uint8_t*,deathtrap::menu::Rect rect) {
  Require(rect.x==0 && rect.y==0 && rect.width==640 && rect.height==480,
          "dialog must repaint the entire native canvas");++drawn_frames;
}
void __cdecl Flip() {}
void __cdecl Poll() { Require(++input_frame<8,"dialog must accept a fresh press"); }
bool Background(bool page) { Require(page,"death dialog never returns through Setup");return true; }
void __cdecl Pause(int kind,int allow_return) {
  ++pause_calls;input_frame=0;
  Require(kind==0 && allow_return==1 && g_death_recovery_dialog_active.load(),
          "dialog must use the native pause transaction");
  Require(raster_width==640 && raster_height==480 && raster_pitch==640 && raster_bpp==8,
          "switch native gameplay raster to 640x480 menu before loading/drawing the page");
  Require(!DeathtrapGameplayReady(false),"modal must reject live gameplay input");
  g_xinput_menu_mode.store(false);ReconcileXInputFrontendOwnership(true);
  Require(g_xinput_menu_mode.load(),"modal must force controller menu ownership");
  HookRecoveryRootMenu();
}
void Reset() {
  context.fill(0);Put(5,uint8_t{3});Put(0xC,int8_t{-1});
  for(int i=0;i<10;++i) {files[i]={};files[i].nFileSizeLow=1000+i;
    files[i].ftLastWriteTime.dwLowDateTime=123+i;saved_levels[i]=3;}
  g_recovery_checkpoint={};g_recovery_file={};g_recovery_file_valid=false;
  g_death_recovery_dialog_active.store(false);
  serializer_ok=file_exists=page_ok=mode_ok=true;
  raster_width=2880;raster_height=1800;raster_pitch=11520;raster_bpp=32;mode_calls=0;
  pause_calls=play_calls=release_calls=root_calls=drawn_frames=0;wanted_choice=0;
  g_original_recovery_save=&Save;g_original_recovery_load=&Load;
  g_original_recovery_play=&State;g_original_recovery_prepare=&State;
  g_original_recovery_destroy=&State;g_original_recovery_root_menu=&Root;
  g_death_recovery_api={&Context,&Pause,nullptr,&Slots,&Fingerprint,&Page,&Release,&EnterMenu};
  g_native50_menu_api={&Mouse,&Key,&Font,&Text,&Width,&Copy,&Flip,&Poll,nullptr,&Background,&Text};
  const uint16_t no_network=0;
  Require(SafeWrite(g_dungeon_base+kUiRenderStateRva,&no_network,2),"fixture network flag");
}
void Death(int choice) {
  raster_width=2880;raster_height=1800;raster_pitch=11520;raster_bpp=32;
  wanted_choice=choice;Put(8,uint32_t{0x800});HookRecoveryPlay(context.data());
  Require(!g_death_recovery_dialog_active.load() && !g_recovery_root_requested,
          "pause must finish before returning to engine transition");
}
void RuntimeCases() {
  Reset();Death(0);
  Require(!mode_calls && !pause_calls && context[0xC]==0xFF && play_calls==1,
          "no session checkpoint: automatic restart without changing video mode");
  Require(HookRecoveryLoad("s4")==1 && g_recovery_checkpoint.slot==4,"successful load establishes baseline");
  Death(0);
  Require(mode_calls==1 && pause_calls==1 && context[0xC]==4 && release_calls==2 && drawn_frames==4,
          "load baseline, held press blocked, full redraw and input cleanup");
  Put(8,uint32_t{0x2800});HookRecoveryPrepare(context.data());
  Require(g_recovery_checkpoint.slot==4,"death retry preserves baseline");
  HookRecoveryLoad("s4");HookRecoverySave("s2",nullptr);serializer_ok=false;
  Require(!HookRecoverySave("s7",nullptr) && !HookRecoveryLoad("s8"),"native failures must pass through");
  Require(g_recovery_checkpoint.slot==2,"failed save/load cannot replace latest successful save");
  serializer_ok=true;Death(1);
  Require(context[0xC]==0xFF && g_recovery_checkpoint.slot==2,"restart bypasses load and preserves run checkpoint");
  Death(0);Require(context[0xC]==2,"subsequent death may reload latest successful save");
  const int prompts=pause_calls;
  file_exists=false;Death(0);Require(pause_calls==prompts && context[0xC]==0xFF,"deleted slot rejected");
  file_exists=true;++files[2].ftLastWriteTime.dwLowDateTime;Death(0);
  Require(pause_calls==prompts,"externally overwritten slot rejected");
  HookRecoverySave("s2",nullptr);saved_levels[2]=4;Death(0);
  Require(pause_calls==prompts,"slot metadata for another level rejected");
  saved_levels[2]=3;page_ok=false;Death(0);
  Require(pause_calls==prompts,"unavailable menu assets fall back safely");
  page_ok=true;mode_ok=false;Death(0);
  Require(pause_calls==prompts && context[0xC]==0xFF && g_recovery_checkpoint.slot==2,
          "failed video transition restarts without drawing or losing checkpoint");
  mode_ok=true;Put(8,uint32_t{0x1000});HookRecoveryPrepare(context.data());Death(0);
  Require(pause_calls==prompts && g_recovery_checkpoint.slot<0,"fresh same-level start clears prior run");
  HookRecoverySave("s5",nullptr);Put(5,uint8_t{4});Death(0);
  Require(pause_calls==prompts && g_recovery_checkpoint.slot<0,"level change clears checkpoint");
  Put(5,uint8_t{3});HookRecoverySave("s4",nullptr);HookRecoveryDestroy(context.data());
  Death(0);Require(pause_calls==prompts,"destroyed/reused context cannot inherit checkpoint");
  HookRecoveryRootMenu();Require(root_calls==1,"ordinary menu forwards unchanged");
  uint16_t network=1;SafeWrite(g_dungeon_base+kUiRenderStateRva,&network,2);
  HookRecoverySave("s6",nullptr);Death(0);
  Require(pause_calls==prompts && g_recovery_checkpoint.slot<0,"network game keeps native respawn");
}
void NavigationCases() {
  deathtrap::recovery::Navigation n;
  Require(n.Poll(200,240,true,false,false,true)<0,"held mouse/A cannot auto-confirm");
  n.Poll(200,240,false,false,false,false);
  Require(n.Poll(200,240,true,false,false,false)==1,"mouse chooses restart");
  n={};n.Poll(0,0,false,false,false,false);n.Poll(0,0,false,true,false,false);
  Require(n.selected==1 && n.Poll(0,0,false,true,false,false)<0 && n.selected==1,"direction edge selects once");
  Require(n.Poll(0,0,false,false,false,true)==1,"keyboard/controller fresh confirmation");
}
void __cdecl NativeNoop() {}
void __cdecl NativeClear(int) {}
int __cdecl NativeVideoMode(int mode,int depth,int flags) {
  Require(mode==4 && depth==0 && flags==1,
          "native frontend transition must select menu mode and indexed raster");
  ++mode_calls;
  raster_width=raster_pitch=640;raster_height=480;raster_bpp=8;
  return 1;
}
void __cdecl NativeContinue(void* owner) {
  Require(owner==context.data(),"native load must resume the same game context");++play_calls;
}
void StubNative(uintptr_t rva, void* replacement) {
  void* trampoline=nullptr;
  void* target=g_dungeon_base+ActiveDungeonCodeRva(rva);
  Require(MH_CreateHook(target,replacement,&trampoline)==MH_OK &&
      MH_EnableHook(target)==MH_OK,"isolate native resource work in mapped lifecycle test");
}
void NativeLifecycle() {
  const auto native_play=g_original_recovery_play;
  const auto native_prepare=g_original_recovery_prepare;
  const auto native_menu_mode=g_death_recovery_api.menu_mode;
  Reset();
  g_original_recovery_play=native_play;g_original_recovery_prepare=native_prepare;
  g_death_recovery_api.menu_mode=native_menu_mode;
  // Execute the real retail state machine, while replacing only resource/world
  // callbacks that require a running game. No real save files or OS input change.
  StubNative(0x7F6C0,reinterpret_cast<void*>(&NativeNoop));
  StubNative(0x695D0,reinterpret_cast<void*>(&NativeNoop));
  StubNative(0x6A200,reinterpret_cast<void*>(&NativeNoop));
  StubNative(0x80670,reinterpret_cast<void*>(&NativeContinue));
  StubNative(0x80650,reinterpret_cast<void*>(&NativeNoop));
  StubNative(0x6A790,reinterpret_cast<void*>(&NativeNoop));
  StubNative(0x9A030,reinterpret_cast<void*>(&NativeClear));
  StubNative(0x9A1F0,reinterpret_cast<void*>(&NativeNoop));
  StubNative(0x37700,reinterpret_cast<void*>(&NativeVideoMode));
  HookRecoveryLoad("s4");Death(0);
  Require(mode_calls==1 && pause_calls==1,
          "execute real native video transition before drawing recovery menu");
  uintptr_t callback=0;uint32_t flags=0;
  std::memcpy(&callback,context.data(),4);std::memcpy(&flags,context.data()+8,4);
  if (callback!=reinterpret_cast<uintptr_t>(g_dungeon_base+kRecoveryPrepareRva) || context[0xC]!=4)
    std::cerr<<"native flow callback="<<std::hex<<callback<<" expected="<<
        reinterpret_cast<uintptr_t>(g_dungeon_base+kRecoveryPrepareRva)<<" flags="<<flags<<
        " slot="<<static_cast<int>(context[0xC])<<" prompts="<<pause_calls<<'\n';
  Require(callback==reinterpret_cast<uintptr_t>(g_dungeon_base+kRecoveryPrepareRva) &&
      (flags&0x2000) && context[0xC]==4,"native death schedules preparation with selected slot");
  reinterpret_cast<RecoveryStateFn>(callback)(context.data());
  std::memcpy(&callback,context.data(),4);std::memcpy(&flags,context.data()+8,4);
  Require(callback==reinterpret_cast<uintptr_t>(g_dungeon_base+ActiveDungeonCodeRva(0x7F080)) &&
      (flags&0x200000) && !(flags&0x800) && context[0xC]==4 &&
      g_recovery_checkpoint.slot==4,"native retry schedules level initialization and preserves pending load");
  reinterpret_cast<RecoveryStateFn>(g_dungeon_base+ActiveDungeonCodeRva(0x7F3C0))(context.data());
  Require(context[0xC]==0xFF && g_recovery_checkpoint.slot==4 && play_calls==1,
          "real native load state consumes slot, records baseline and resumes");
  Death(1);
  Require(context[0xC]==0xFF,"real native restart choice cannot reload a slot");
}
void OriginalImages(int argc,wchar_t** argv) {
  for(int i=1;i<argc;++i) {
    Require(MH_Initialize()==MH_OK,"initialize mapped native hooks");
    HMODULE module=LoadLibraryExW(argv[i],nullptr,DONT_RESOLVE_DLL_REFERENCES);
    Require(module!=nullptr,"map native localized image");
    g_dungeon_base=reinterpret_cast<uint8_t*>(module);
    g_dungeon_image_variant=IdentifyDungeonImage(g_dungeon_base);
    g_native50_menu_hooks_installed=false;g_death_recovery_hooks_installed=false;
    Require(g_dungeon_image_variant!=DungeonImageVariant::kUnsupported &&
        InstallNative50MenuHooks() && InstallDeathRecoveryHooks() && InstallDeathRecoveryHooks(),
        "native hooks and menu transaction signatures work in every localization");
    NativeLifecycle();
    Require(MH_Uninitialize()==MH_OK,"remove mapped test hooks");FreeLibrary(module);
  }
}
}
int wmain(int argc,wchar_t** argv) {
  NavigationCases();
  auto* image=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x380000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
  Require(image!=nullptr,"allocate fixture");
  auto page=std::make_unique<deathtrap::menu::Canvas>();g_recovery_page=page.get();
  for(const auto variant:{DungeonImageVariant::kEnglish,DungeonImageVariant::kFrenchGog,
                         DungeonImageVariant::kGermanGog,DungeonImageVariant::kItalianGog}) {
    g_dungeon_base=image;g_dungeon_image_variant=variant;RuntimeCases();
  }
  VirtualFree(image,0,MEM_RELEASE);OriginalImages(argc,argv);g_recovery_page=nullptr;
  std::cout<<"Death recovery: save/load tracking, retries, failures, modal input and localized hooks passed\n";
}
