#include <windows.h>
#include <filesystem>
#include <iostream>
#include <string>
#include <tuple>
#include <fstream>

#include "../src/deathtrap_native_render_patch.cc"

namespace {
using deathtrap::settings::ControlRow;
using deathtrap::settings::Controls;
std::filesystem::path ini_path;
uint16_t mouse_x = 0, mouse_y = 0, mouse_buttons = 0;
std::array<bool, 256> keys{};
std::vector<std::tuple<int, int, std::string>> drawn;
unsigned restores = 0, native_calls = 0, writes = 0;
uint8_t native_result = 0;
std::vector<std::string> repaint_events;
std::vector<uint32_t> fonts;
std::array<uint8_t, 0x600> localization{};
constexpr char background_name[] = "HARDSET2.PCX";
constexpr char atlas_name[] = "SETHARD6.PCX";

void Require(bool value, const char* message) {
  if (!value) { std::cerr << "modern controls: " << message << '\n'; std::exit(1); }
}
void __cdecl Mouse(uint16_t* x, uint16_t* y, uint16_t* buttons) {
  *x = mouse_x; *y = mouse_y; *buttons = mouse_buttons;
}
uint8_t __cdecl Hit(int x1, int y1, int x2, int y2,
                    uint16_t x, uint16_t y, uint16_t buttons) {
  return buttons && x >= x1 && x <= x2 && y >= y1 && y <= y2;
}
int __cdecl Key(uint32_t scan) { return scan < keys.size() && keys[scan]; }
void __cdecl Font(uint32_t font) { fonts.push_back(font); }
void __cdecl Text(int x, int y, int, const char* value) {
  drawn.emplace_back(x, y, value);
}
int32_t __cdecl Width(const char* value) {
  return static_cast<int32_t>(std::strlen(value) * 8);
}
void __cdecl Restore(const void*, int w, int h, int sx, int sy, int dx, int dy) {
  Require(w == 640 && h == 480 && sx == 0 && sy == 0 && dx == 0 && dy == 0,
          "redraw must clear the whole pending page from the original background");
  Require(repaint_events.size() % 6 == 1 && repaint_events.back() == background_name,
          "background must be decoded before clearing a page");
  repaint_events.emplace_back("restore");
  ++restores;
}
char* __cdecl BuildPath(const char* name) { return const_cast<char*>(name); }
void __cdecl LoadImage(const char* name, void* surface) {
  Require(surface == reinterpret_cast<void*>(1), "must use registered scratch surface");
  repaint_events.emplace_back(name);
}
void __cdecl Labels() {
  Require(repaint_events.back() == atlas_name,
          "native widget atlas must be restored before drawing native labels");
  repaint_events.emplace_back("labels");
}
void __cdecl JoystickLabels() { repaint_events.emplace_back("joystick-labels"); }
void __cdecl JoystickFrame() { repaint_events.emplace_back("joystick-frame"); }
void SetDrawingFixture() {
  const uintptr_t table = reinterpret_cast<uintptr_t>(localization.data());
  const uintptr_t background = reinterpret_cast<uintptr_t>(background_name);
  const uintptr_t atlas = reinterpret_cast<uintptr_t>(atlas_name);
  const uintptr_t surface = 1;
  Require(SafeWrite(g_dungeon_base + kLocalizationTablePointerRva, &table, sizeof(table)) &&
              SafeWrite(localization.data() + kHardwareBackgroundNameOffset,
                        &background, sizeof(background)) &&
              SafeWrite(localization.data() + kHardwareAtlasNameOffset, &atlas, sizeof(atlas)) &&
              SafeWrite(g_dungeon_base + kKeyboardBindingAtlasSurfaceRva, &surface, sizeof(surface)),
          "localized menu drawing fixture");
}
bool Change(ControlRow row, int requested) {
  return ChangeModernControlSetting(row, requested, ini_path.c_str());
}
uint8_t __cdecl Input() { ++native_calls; return native_result; }

Controls ReadSaved() {
  return {GetPrivateProfileIntW(L"KeyboardMouse", L"CameraRelativeMovement", 1, ini_path.c_str()) != 0,
          GetPrivateProfileIntW(L"XInput", L"CameraRelativeMovement", 1, ini_path.c_str()) != 0,
          GetPrivateProfileIntW(L"Camera", L"InvertX", 0, ini_path.c_str()) != 0,
          GetPrivateProfileIntW(L"Camera", L"InvertY", 0, ini_path.c_str()) != 0,
          GetPrivateProfileIntW(L"Gameplay", L"KeepBodies", 0, ini_path.c_str()) != 0,
          GetPrivateProfileIntW(L"Models", L"E3Heroine", 0, ini_path.c_str()) != 0,
          GetPrivateProfileIntW(L"Gameplay", L"ShowWounds", 1, ini_path.c_str()) != 0,
          GetPrivateProfileIntW(L"Selector", L"SlowMotion", 1, ini_path.c_str()) != 0,
          int(GetPrivateProfileIntW(L"Selector", L"SlowMotionPercent", 25, ini_path.c_str())),
          int(GetPrivateProfileIntW(L"Interface", L"ScalePercent", 100, ini_path.c_str()))};
}
void Reset(bool modern = true) {
  g_keyboard_mouse_camera_relative_movement = g_xinput_camera_relative_movement = modern;
  g_third_person_orbit_invert_x = g_third_person_orbit_invert_y = false;
  g_keep_bodies_enabled.store(false);
  g_e3_model_requested.store(false);
  g_show_body_wounds.store(true);
  g_selector_slow_motion_enabled=true; g_selector_slow_motion_percent=25; g_interface_scale_percent=100;
  g_hardware_menu_active = true;
  g_hardware_mouse_latched = g_hardware_keyboard_latched = true;
  g_hardware_control_row = g_hardware_save_error_row = -1;
  g_hardware_repaint_frames = 2;
  g_hardware_mouse_x = g_hardware_mouse_y = UINT16_MAX;
  mouse_x = mouse_y = mouse_buttons = 0;
  keys.fill(false);
  native_result = 0;
  g_hardware_menu_api = {&Mouse, &Hit, &Key, &Font, &Text, &Restore, &Width, &Change,
                         &BuildPath, &LoadImage, &JoystickLabels, &JoystickFrame};
  g_original_hardware_input = &Input;
  g_original_hardware_labels = &Labels;
  SetDrawingFixture();
}
void Release() { mouse_buttons = 0; keys.fill(false); HookHardwareMenuInput(); }

BOOL WINAPI FailSecond(LPCWSTR section, LPCWSTR key, LPCWSTR value, LPCWSTR path) {
  if (++writes == 2) { SetLastError(ERROR_WRITE_FAULT); return FALSE; }
  return WritePrivateProfileStringW(section, key, value, path);
}

void CheckPersistence() {
  Require(Change(ControlRow::kSelectorSlowMotion,0) && !ReadSaved().selector_slow_motion &&
      !g_selector_slow_motion_enabled,"selector slowdown off persists and applies");
  Require(Change(ControlRow::kSelectorSpeed,1) && ReadSaved().selector_speed==30 &&
      g_selector_slow_motion_percent==30 && !g_selector_slow_motion_enabled,
      "speed changes preserve the on/off toggle");
  Require(Change(ControlRow::kSelectorSlowMotion,1) && g_selector_slow_motion_enabled,
      "selector slowdown on applies");
  for(int i=0;i<30;++i) Require(Change(ControlRow::kInterfaceScale,1),"increase interface scale");
  Require(ReadSaved().interface_scale==150 && g_interface_scale_percent==150,"interface scale upper bound");
  for(int i=0;i<40;++i) Require(Change(ControlRow::kInterfaceScale,0),"decrease interface scale");
  Require(ReadSaved().interface_scale==75 && g_interface_scale_percent==75,"interface scale lower bound");
  Require(Change(ControlRow::kInterfaceScale,-1) && g_interface_scale_percent==80,"click steps interface scale");
  for(int i=0;i<30;++i) Require(Change(ControlRow::kSelectorSpeed,0),"decrease selection speed");
  Require(ReadSaved().selector_speed==10,"selection speed lower bound");
  for(int i=0;i<30;++i) Require(Change(ControlRow::kSelectorSpeed,1),"increase selection speed");
  Require(ReadSaved().selector_speed==75,"selection speed upper bound");
  Require(Change(ControlRow::kSelectorSpeed,-1) && g_selector_slow_motion_percent==10,"click cycles percentage");
  Reset();

  Controls defaults;
  Require(defaults.keyboard_modern && defaults.controller_modern &&
              !defaults.invert_x && !defaults.invert_y && !defaults.keep_bodies && !defaults.e3_model &&
              defaults.body_wounds,
          "new installations must default to modern, uninverted controls");
  WritePrivateProfileStringW(L"Audio", L"FixMusicTracks", L"0", ini_path.c_str());
  Require(Change(ControlRow::kMovement, 0), "tank mode must save");
  Require(!g_keyboard_mouse_camera_relative_movement && !g_xinput_camera_relative_movement &&
              !ReadSaved().keyboard_modern && !ReadSaved().controller_modern,
          "keyboard/controller mode must change together and survive reload");
  g_keyboard_camera_relative_intent.store(true);
  g_xinput_camera_relative_intent.store(true);
  g_xinput_walk_active = g_xinput_run_active = true;
  g_xinput_filtered_movement_magnitude = 1.0;
  Require(Change(ControlRow::kMovement, 1), "modern mode must save after starting tank");
  Require(!g_keyboard_camera_relative_intent.load() && !g_xinput_camera_relative_intent.load() &&
              !g_xinput_walk_active && !g_xinput_run_active &&
              g_xinput_filtered_movement_magnitude == 0.0,
          "mode switch must clear stale movement, not keep running/turning");
  Require(Change(ControlRow::kInvertX, 1), "X inversion must save");
  Require(ReadSaved().invert_x && !ReadSaved().invert_y && g_third_person_orbit_invert_x,
          "X inversion must not affect Y");
  Require(Change(ControlRow::kInvertY, 1), "Y inversion must save");
  Require(ReadSaved().invert_x && ReadSaved().invert_y && g_third_person_orbit_invert_y,
          "Y inversion must preserve X and survive reload");
  Require(Change(ControlRow::kKeepBodies,1) && ReadSaved().keep_bodies &&
              CurrentModernControlSettings().keep_bodies && ReadSaved().invert_x &&
              ReadSaved().invert_y && ReadSaved().keyboard_modern,
          "corpse setting must persist without changing movement/camera");
  Require(Change(ControlRow::kKeepBodies,-1) && !ReadSaved().keep_bodies &&
              !g_keep_bodies_enabled.load(),"corpse toggle must be reversible");
  Require(Change(ControlRow::kBodyWounds,0) && !ReadSaved().body_wounds &&
      !g_show_body_wounds.load() && ReadSaved().invert_x && ReadSaved().invert_y,
      "wounds must disable, persist and preserve unrelated settings");
  Require(Change(ControlRow::kBodyWounds,-1) && ReadSaved().body_wounds &&
      g_show_body_wounds.load(),"wounds must re-enable immediately and persist");
  Require(!ChangeModernControlSetting((ControlRow::kBodyWounds),0,
      (ini_path.parent_path()/L"no-such-directory"/L"wounds.ini").c_str()) &&
      g_show_body_wounds.load(),"failed save must not hide wounds");
  Require(GetPrivateProfileIntW(L"Audio", L"FixMusicTracks", 1, ini_path.c_str()) == 0,
          "saving controls must preserve unrelated configuration");
  writes = 0;
  Require(!SaveModernControlSettings(ini_path.c_str(), ControlRow::kMovement,
                                    {false, false, true, true}, &FailSecond),
          "a failed second write must not report success");
  Require(ReadSaved().keyboard_modern && ReadSaved().controller_modern,
          "failed shared-mode save must roll back both existing keys");
  const auto missing = ini_path.parent_path() / L"missing.ini";
  writes = 0;
  Require(!SaveModernControlSettings(missing.c_str(), ControlRow::kMovement,
                                    {false, false, false, false}, &FailSecond),
          "failed save of initially absent keys must fail");
  Require(GetPrivateProfileIntW(L"KeyboardMouse", L"CameraRelativeMovement", 77,
                                missing.c_str()) == 77,
          "rollback must restore absence, not force a new default value");
  std::filesystem::remove(missing);
  const auto invalid = ini_path.parent_path() / L"no-such-directory" / L"settings.ini";
  Require(!ChangeModernControlSetting(ControlRow::kMovement, 0, invalid.c_str()) &&
              g_keyboard_mouse_camera_relative_movement && g_xinput_camera_relative_movement &&
              g_hardware_save_error_row == 0,
          "failed persistence must not apply or silently accept runtime changes");
  Require(!ChangeModernControlSetting(ControlRow::kKeepBodies,1,invalid.c_str()) &&
              !g_keep_bodies_enabled.load(),"failed body-setting save must not publish runtime state");
  Require(!Change(ControlRow::kCount, -1), "invalid menu rows must not write settings");
  const auto before_interface=CurrentModernControlSettings();
  Require(!ChangeModernControlSetting(ControlRow::kInterfaceScale,1,invalid.c_str()) &&
      g_interface_scale_percent==before_interface.interface_scale,"failed UI scale save must not apply");
  Require(!ChangeModernControlSetting(ControlRow::kSelectorSpeed,1,invalid.c_str()) &&
      g_selector_slow_motion_percent==static_cast<uint32_t>(before_interface.selector_speed),"failed speed save must not apply");
  Require(!ChangeModernControlSetting(ControlRow::kSelectorSlowMotion,0,invalid.c_str()) &&
      g_selector_slow_motion_enabled==before_interface.selector_slow_motion,"failed slowdown save must not apply");
  g_e3_model_available.store(false);
  Require(!Change(ControlRow::kHeroineModel,1) && !ReadSaved().e3_model && !g_e3_model_requested.load(),
          "missing optional assets must not enable E3 or modify saved choice");
  g_e3_model_available.store(true);
  g_model_archive_selection.store(0);
  Require(Change(ControlRow::kHeroineModel,1) && ReadSaved().e3_model && g_e3_model_requested.load() &&
              g_model_archive_selection.load()==0, "E3 choice persists without swapping the open archive");
  Require(!ChangeModernControlSetting(ControlRow::kHeroineModel,0,invalid.c_str()) &&
              g_e3_model_requested.load() && ReadSaved().e3_model,
          "failed model save must preserve the previous choice");
  Require(Change(ControlRow::kHeroineModel,0) && !ReadSaved().e3_model &&
              !g_e3_model_requested.load(), "original model must be selectable again");
}

void CheckModelFiles() {
  const auto root=ini_path.parent_path()/L"model-routing";
  std::filesystem::create_directory(root);
  g_model_game_root=root.wstring();
  g_model_create_w=&CreateFileW;g_model_create_a=&CreateFileA;
  for (size_t i=0;i<std::size(kModelFiles);++i) {
    for (const auto& path : {root/kModelFiles[i],std::filesystem::path(ModelPackPath(i))}) {
      std::filesystem::create_directories(path.parent_path());
      const bool pack=path.wstring().find(L"native50")!=std::wstring::npos;
      std::ofstream out(path,std::ios::binary);out<<std::string(i==0 ? 4096 : 12,pack ? 'E' : 'R');
    }
  }
  const auto archive=root/kModelFiles[0];const auto preload=root/kModelFiles[1];
  for (const auto* name:{L"model-data.bin",L"resource-map.bin"}) {
    std::ofstream out(root/L"native50/models/e3"/name,std::ios::binary);out<<std::string(12,'E');
  }
  auto read=[&](const std::filesystem::path& path,bool ansi=false) {
    HANDLE file=ansi ? HookModelCreateFileA(path.string().c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr)
                     : HookModelCreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    Require(file!=INVALID_HANDLE_VALUE,"model file must open");
    char value=0;DWORD got=0;Require(ReadFile(file,&value,1,&got,nullptr)&&got==1,"model marker read");
    CloseHandle(file);return value;
  };
  Require(ModelPackComplete(),"all optional files available");
  Require(ModelFileIndex((root/L"ASYLUM"/L".."/L"asylum"/L"DUNGEON.BOX").c_str())==0 &&
              ModelFileIndex((root/L"different"/L"DUNGEON.BOX").c_str())==-1 &&
              ModelFileIndex((root/L"ASYLUM"/L"DUNGEON.BOX.bak").c_str())==-1,
          "routing must normalize the exact game paths, never match by filename or prefix");
  Require(!ModelReadOnlyOpen(GENERIC_READ|GENERIC_WRITE,OPEN_EXISTING) &&
              !ModelReadOnlyOpen(GENERIC_READ,CREATE_ALWAYS) &&
              !ModelReadOnlyOpen(GENERIC_READ|DELETE,OPEN_EXISTING),"writes and destructive opens bypass model routing");
  g_e3_model_requested.store(true);g_model_archive_selection.store(-1);
  Require(read(archive,true)=='R' && read(preload)=='E',"Retail archive remains original; Unicode preloads use E3 lists");
  g_e3_model_requested.store(false);
  Require(read(preload,true)=='E' && read(archive)=='R',"menu choice retains the original archive and matching overlay preloads");
  std::filesystem::remove(std::filesystem::path(ModelPackPath(1)));
  SetLastError(0);
  HANDLE failed=HookModelCreateFileW(preload.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
  Require(failed==INVALID_HANDLE_VALUE && GetLastError()==ERROR_FILE_NOT_FOUND,
          "a disappearing pack file must fail, never silently use incompatible retail preloads");
  g_e3_model_requested.store(true);g_model_archive_selection.store(-1);
  Require(!ModelPackComplete() && read(archive)=='R' && read(preload)=='R',"incomplete pack selects retail consistently");
  { std::ofstream out(std::filesystem::path(ModelPackPath(1)),std::ios::binary);out<<std::string(12,'E'); }
  g_model_archive_selection.store(-1);
  Require(read(preload)=='R' && read(archive)=='R',"late initialization must freeze retail, not mix a previously open archive");
  g_e3_model_requested.store(false);g_model_archive_selection.store(-1);
  Require(read(archive)=='R' && read(preload,true)=='R',"original selection opens original files");
  g_model_game_root.clear();g_model_archive_selection.store(-1);g_e3_model_available.store(false);
  std::filesystem::remove_all(root);
}

void CheckInputAndDrawing() {
  Reset();
  const unsigned before = restores;
  Release();
  Release();
  Require(restores == before + 2 && g_hardware_repaint_frames == 0,
          "entry must rebuild both alternating DirectDraw pages exactly once");
  drawn.clear();
  Release();
  Require(restores == before + 2 && drawn.empty(),
          "idle frames must not accumulate text or repeatedly decode backgrounds");
  mouse_x = 400; mouse_y = 178;
  HookHardwareMenuInput();
  Require(g_hardware_repaint_frames == 2 && drawn.empty(),
          "hover changes must queue two clean pages, not overprint an old page");
  HookHardwareMenuInput(); HookHardwareMenuInput();
  Require(restores == before + 4 && g_hardware_repaint_frames == 0,
          "selection redraw must clear both old chevrons and old values");
  mouse_x = 400; mouse_y = 178; mouse_buttons = 1;
  Require(HookHardwareMenuInput() == 0 && !g_xinput_camera_relative_movement,
          "mouse click must choose tank mode");
  HookHardwareMenuInput();
  mouse_y = 250; HookHardwareMenuInput();
  mouse_y = 178; HookHardwareMenuInput();
  Require(!g_xinput_camera_relative_movement,
          "held click or leaving/reentering must not toggle twice");
  Release();
  mouse_y = 200; mouse_buttons = 1; HookHardwareMenuInput();
  Require(g_third_person_orbit_invert_x && !g_third_person_orbit_invert_y,
          "independent X checkbox must work");
  Release(); mouse_y = 224; mouse_buttons = 1; HookHardwareMenuInput();
  Require(g_third_person_orbit_invert_y, "independent Y checkbox must work");
  Release();
  mouse_y = 150; native_result = 2; mouse_buttons = 1;
  Require(HookHardwareMenuInput() == 2, "native first-person inversion must stay native");
  Release();
  native_result = 99; keys[1] = true;
  Require(HookHardwareMenuInput() == 99, "Escape/Back must keep native ownership");
  Release(); Reset(false); Release();
  keys[0xD0] = true; HookHardwareMenuInput();
  Require(g_hardware_control_row == 0, "Down must focus the first control");
  Release(); keys[0xCD] = true; HookHardwareMenuInput();
  Require(g_xinput_camera_relative_movement, "Right must choose modern without restart");
  HookHardwareMenuInput();
  Require(g_xinput_camera_relative_movement, "held key must not toggle repeatedly");
  Release(); keys[0xCB] = true; HookHardwareMenuInput();
  Require(!g_xinput_camera_relative_movement, "Left must choose tank");
  Release(); keys[0x1C] = true; HookHardwareMenuInput();
  Require(g_xinput_camera_relative_movement, "Enter must toggle the selected option");
  Require(restores && native_calls, "native frame handling and redraw must still run");
  for (const auto variant : {DungeonImageVariant::kEnglish, DungeonImageVariant::kFrenchGog,
                             DungeonImageVariant::kGermanGog, DungeonImageVariant::kItalianGog}) {
    g_dungeon_image_variant = variant;
    SetDrawingFixture();
    drawn.clear();
    g_hardware_save_error_row = -1;
    Require(DrawModernControlSettings(), "each supported localization must draw");
    for (const auto& item : drawn) {
      const auto& value = std::get<2>(item);
      Require(std::get<1>(item) >= 174 && std::get<1>(item) <= 222,
              "new controls must fit between existing native options");
      Require(std::get<0>(item) + Width(value.c_str()) <= 510,
              "localized labels and values must fit the native panel");
    }
  }
  for (const auto font : fonts) {
    Require(font == 0x1485u, "new option values must use the normal native font, not red");
  }
}

void CheckOriginalImages(int argc, wchar_t** argv) {
  for (int index = 1; index < argc; ++index) {
    Require(MH_Initialize() == MH_OK, "MinHook initialization");
    const HMODULE image = LoadLibraryExW(argv[index], nullptr, DONT_RESOLVE_DLL_REFERENCES);
    Require(image != nullptr, "original game image must map");
    g_dungeon_base = reinterpret_cast<uint8_t*>(image);
    g_dungeon_image_variant = IdentifyDungeonImage(g_dungeon_base);
    g_hardware_menu_hooks_installed = false;
    Require(g_dungeon_image_variant != DungeonImageVariant::kUnsupported &&
                InstallModernControlMenuHooks(),
            "new menu must pass real profile, operand and hook signatures");
    Require(InstallModernControlMenuHooks(), "installation must be idempotent");
    std::cout << "Hardware controls menu: " << ActiveDungeonProfileName() << " passed\n";
    Require(MH_Uninitialize() == MH_OK, "mapped test hooks must be removed");
    FreeLibrary(image);
  }
  g_dungeon_base = nullptr;
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
  if (argc > 1) { CheckOriginalImages(argc, argv); return 0; }
  const auto temporary = std::filesystem::temp_directory_path() /
      (L"deathtrap-controls-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
       std::to_wstring(GetTickCount64()));
  Require(std::filesystem::create_directory(temporary), "own test directory must be new");
  ini_path = temporary / L"settings.ini";
  g_debug_log = false;
  g_dungeon_image_variant = DungeonImageVariant::kEnglish;
  std::vector<uint8_t> fixture(0x368000);
  g_dungeon_base = fixture.data();
  uintptr_t background = 1;
  Require(SafeWrite(g_dungeon_base + kHardwareBackgroundSurfaceRva, &background,
                     sizeof(background)), "background fixture");
  CheckPersistence();
  CheckModelFiles();
  CheckInputAndDrawing();
  Require(deathtrap::settings::ControlRowAt(400, 169) == -1 &&
              deathtrap::settings::ControlRowAt(400, 242) == -1 &&
              deathtrap::settings::ControlRowAt(400, 174) == 0 &&
              deathtrap::settings::ControlRowAt(400, 198) == 1 &&
              deathtrap::settings::ControlRowAt(400, 222) == 2,
          "new row hitboxes must exclude existing native options");
  g_dungeon_base = nullptr;
  std::filesystem::remove(ini_path);
  std::filesystem::remove(temporary);
  std::cout << "Modern/tank, independent X/Y, persistence/rollback, menu input and localization passed\n";
  return 0;
}
