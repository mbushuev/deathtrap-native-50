// Single-player recovery at the engine's deferred death transition, never from
// actor traversal or presentation callbacks. Native save files stay untouched.
constexpr DungeonCodeRva kRecoverySaveRva{0x99CB0u}, kRecoveryLoadRva{0x99D40u};
constexpr DungeonCodeRva kRecoveryPlayRva{0x7F5B0u}, kRecoveryPrepareRva{0x7F8A0u};
constexpr DungeonCodeRva kRecoveryDestroyRva{0x7EE30u}, kRecoveryRootMenuRva{0xF1E0u};
constexpr DungeonCodeRva kRecoveryContextRva{0x7EF50u}, kRecoveryPauseRva{0x5C80u};
constexpr DungeonCodeRva kRecoveryMenuModeRva{0x6A310u};
constexpr DungeonCodeRva kRecoveryPathRva{0x99E30u}, kRecoverySlotsRva{0x99E50u};
using RecoveryStateFn = void(__cdecl*)(void*);
using RecoverySaveFn = int(__cdecl*)(const char*, const void*);
using RecoveryLoadFn = int(__cdecl*)(const char*);
struct DeathRecoveryApi {
  void* (__cdecl* context)() = nullptr;
  void (__cdecl* pause)(int, int) = nullptr;
  void (__cdecl* path)(void*, const char*) = nullptr;
  void (__cdecl* slots)(void*) = nullptr;
  bool (*fingerprint)(const char*, WIN32_FILE_ATTRIBUTE_DATA*) = nullptr;
  bool (*prepare_page)() = nullptr;
  void (*release_input)() = nullptr;
  int (__cdecl* menu_mode)(int) = nullptr;
};
DeathRecoveryApi g_death_recovery_api;
RecoverySaveFn g_original_recovery_save = nullptr;
RecoveryLoadFn g_original_recovery_load = nullptr;
RecoveryStateFn g_original_recovery_play = nullptr, g_original_recovery_prepare = nullptr;
RecoveryStateFn g_original_recovery_destroy = nullptr;
KeyboardBindingMenuFn g_original_recovery_root_menu = nullptr;
deathtrap::recovery::Checkpoint g_recovery_checkpoint;
WIN32_FILE_ATTRIBUTE_DATA g_recovery_file{};
bool g_recovery_file_valid = false, g_death_recovery_hooks_installed = false;
bool g_recovery_root_requested = false;
int g_recovery_choice = 1;
deathtrap::menu::Canvas* g_recovery_page = nullptr;

bool RecoveryContext(void* context, uint8_t* level, uint32_t* flags = nullptr) {
  uint16_t mode = 1;
  uint32_t native_flags = 0;
  return context && SafeReadValue(g_dungeon_base + kUiRenderStateRva, &mode) &&
      !(mode & 1u) && SafeReadValue(static_cast<uint8_t*>(context) + 5, level) &&
      SafeReadValue(static_cast<uint8_t*>(context) + 8, &native_flags) &&
      (!flags || (*flags = native_flags, true));
}
bool RecoveryFingerprint(const char* name, WIN32_FILE_ATTRIBUTE_DATA* output) {
  // Use the engine's configured save directory and filename builder, including
  // custom installations. The 0x88-byte context is the serializer's own layout.
  std::array<char, 0x88> file{};
  if (!g_death_recovery_api.path) return false;
  g_death_recovery_api.path(file.data(), name);
  const char* path = file.data() + 6;
  if (!std::memchr(path, 0, file.size() - 6)) return false;
  return GetFileAttributesExA(path, GetFileExInfoStandard, output) &&
      !(output->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
      (output->nFileSizeHigh || output->nFileSizeLow > 36);
}
bool SameRecoveryFile(const WIN32_FILE_ATTRIBUTE_DATA& a,
                      const WIN32_FILE_ATTRIBUTE_DATA& b) {
  return a.nFileSizeHigh == b.nFileSizeHigh && a.nFileSizeLow == b.nFileSizeLow &&
      a.ftLastWriteTime.dwLowDateTime == b.ftLastWriteTime.dwLowDateTime &&
      a.ftLastWriteTime.dwHighDateTime == b.ftLastWriteTime.dwHighDateTime;
}
void RecordRecoveryCheckpoint(const char* basename, bool success, const char* source) {
  if (!success || !g_death_recovery_api.context) return;
  std::array<char, 3> name{};
  uint8_t level = 0;
  void* context = g_death_recovery_api.context();
  if (!RecoveryContext(context, &level) || !SafeRead(basename, name.data(), name.size())) return;
  const int slot = deathtrap::recovery::Slot(name.data());
  if (slot < 0) return;
  g_recovery_checkpoint.Completed(reinterpret_cast<uintptr_t>(context), level, slot, true);
  g_recovery_file_valid = g_death_recovery_api.fingerprint &&
      g_death_recovery_api.fingerprint(name.data(), &g_recovery_file);
  AppendDeathtrapSupportLog("support_recovery checkpoint=%s level=%u slot=%d file=%d",
      source, level, slot, g_recovery_file_valid ? 1 : 0);
}
int __cdecl HookRecoverySave(const char* name, const void* metadata) {
  const int result = g_original_recovery_save ? g_original_recovery_save(name, metadata) : 0;
  RecordRecoveryCheckpoint(name, result != 0, "save");
  return result;
}
int __cdecl HookRecoveryLoad(const char* name) {
  const int result = g_original_recovery_load ? g_original_recovery_load(name) : 0;
  RecordRecoveryCheckpoint(name, result != 0, "load");
  return result;
}
bool RecoveryCheckpointAvailable(void* context, uint8_t level) {
  g_recovery_checkpoint.Observe(reinterpret_cast<uintptr_t>(context), level);
  const int slot = g_recovery_checkpoint.slot;
  if (slot < 0 || !g_recovery_file_valid || !g_death_recovery_api.fingerprint ||
      !g_death_recovery_api.slots) return false;
  const char name[]{'s', static_cast<char>('0' + slot), 0};
  WIN32_FILE_ATTRIBUTE_DATA current{};
  if (!g_death_recovery_api.fingerprint(name, &current) ||
      !SameRecoveryFile(current, g_recovery_file)) return false;
  // A just-created save may not yet be in the menu's cached slot list.
  g_death_recovery_api.slots(static_cast<uint8_t*>(context) + 0xD);
  uint8_t saved_level = 0;
  std::array<char, 3> saved_name{};
  return SafeReadValue(static_cast<uint8_t*>(context) + 0xD8 + slot*0xCB, &saved_level) &&
      saved_level == level &&
      SafeRead(static_cast<uint8_t*>(context) + 0x4F + slot*0xCB,
               saved_name.data(), saved_name.size()) && saved_name == std::array<char,3>{name[0],name[1],0};
}
bool PrepareRecoveryPage() {
  if (!LoadNative50MenuAssets() || !g_native50_menu_assets) return false;
  if (!g_recovery_page) g_recovery_page = new deathtrap::menu::Canvas;
  const auto& paper = g_native50_menu_assets->paper;
  deathtrap::menu::MakeSettingsPage(paper, paper.palette, g_recovery_page);
  std::vector<uint8_t> font;
  return Native50ReadHeadingFont(ModuleDirectory(), &font) &&
      deathtrap::menu::PaintSettingsHeading(font.data(), font.size(), paper.palette,
          g_recovery_page, deathtrap::recovery::Title(ActiveGameTextLanguage()));
}
void DrawRecoveryPage(int selected) {
  const auto& api = g_native50_menu_api;
  const auto language = ActiveGameTextLanguage();
  api.copy(g_recovery_page->data(), {0,0,640,480});
  api.font(0x248Fu);
  for (int row = 0; row < 2; ++row) {
    const int y = deathtrap::recovery::kChoiceTop + row*deathtrap::recovery::kChoiceStep;
    if (selected == row) api.text(116, y, 0, ">");
    Native50WrappedText(134, y, 310, deathtrap::recovery::Choice(language, row));
  }
  api.font(0x2490u);
  const auto secondary = api.secondary_text ? api.secondary_text : api.text;
  Native50WrappedText(134, 306, 310, deathtrap::recovery::Description(language), secondary);
  Native50WrappedText(134, 383, 310, deathtrap::recovery::Help(language), secondary);
}
void RunDeathRecoveryDialog() {
  const auto& api = g_native50_menu_api;
  if (!api.mouse || !api.key || !api.copy || !api.font || !api.text || !api.width ||
      !api.flip || !api.poll || !api.background || !g_recovery_page ||
      !api.background(true)) return;
  deathtrap::recovery::Navigation navigation;
  for (;;) {
    uint32_t quitting = 0;
    if (!SafeReadValue(g_dungeon_base + kMenuQuitRva, &quitting) || quitting) break;
    uint16_t x = 0, y = 0, buttons = 0;
    api.mouse(&x, &y, &buttons);
    const int choice = navigation.Poll(x, y, (buttons & 1) != 0,
        api.key(0xC8) != 0, api.key(0xD0) != 0,
        api.key(0x1C) || api.key(0x9C) || api.key(0x39));
    if (choice >= 0) { g_recovery_choice = choice; break; }
    DrawRecoveryPage(navigation.selected);
    uintptr_t cursor = 0;
    const size_t frame = (GetTickCount64() / 90) % 16;
    if (g_original_menu_sprite &&
        SafeReadValue(g_dungeon_base + kMenuCursorFramesRva + frame*sizeof(uintptr_t), &cursor) && cursor)
      g_original_menu_sprite(reinterpret_cast<void*>(cursor),84,84,x,y);
    api.flip();
    api.poll();
  }
  api.font(0x1485u);
}
void __cdecl HookRecoveryRootMenu() {
  if (!g_recovery_root_requested) {
    if (g_original_recovery_root_menu) g_original_recovery_root_menu();
    return;
  }
  // Same reset as the native root entry; polling can subsequently set quit.
  const uint32_t zero = 0;
  SafeWrite(g_dungeon_base + kMenuQuitRva, &zero, sizeof(zero));
  RunDeathRecoveryDialog();
}
void __cdecl HookRecoveryPlay(void* context) {
  uint8_t level = 0;
  uint32_t flags = 0;
  if (RecoveryContext(context, &level, &flags)) {
    g_recovery_checkpoint.Observe(reinterpret_cast<uintptr_t>(context), level);
    if (flags & 0x800u) {
      int selected_slot = -1;
      if (RecoveryCheckpointAvailable(context, level) && g_death_recovery_api.pause &&
          g_death_recovery_api.menu_mode &&
          g_death_recovery_api.prepare_page && g_death_recovery_api.prepare_page()) {
        g_recovery_choice = 1;
        g_death_recovery_dialog_active.store(true, std::memory_order_release);
        if (g_death_recovery_api.release_input) g_death_recovery_api.release_input();
        // The normal pause caller first leaves the gameplay raster and selects
        // the 640x480 indexed frontend. Pause itself only sets up its resources,
        // viewport, input, audio, fonts and palette; calling it alone corrupts
        // both the background copy and text at the gameplay pitch/color depth.
        const bool menu_ready = g_death_recovery_api.menu_mode(0) != 0;
        if (menu_ready) {
          g_recovery_root_requested = true;
          g_death_recovery_api.pause(0, 1);
          g_recovery_root_requested = false;
        } else {
          AppendDeathtrapSupportLog("support_recovery menu=video_mode_failed fallback=native_restart");
        }
        if (g_death_recovery_api.release_input) g_death_recovery_api.release_input();
        g_death_recovery_dialog_active.store(false, std::memory_order_release);
        if (g_recovery_choice == 0 && RecoveryCheckpointAvailable(context, level))
          selected_slot = g_recovery_checkpoint.slot;
      }
      const int8_t native_slot = static_cast<int8_t>(selected_slot);
      SafeWrite(static_cast<uint8_t*>(context) + 0xC, &native_slot, sizeof(native_slot));
      AppendDeathtrapSupportLog("support_recovery death level=%u action=%s slot=%d",
          level, selected_slot >= 0 ? "load" : "restart", selected_slot);
    }
  }
  // The engine schedules cleanup -> level creation -> optional slot load.
  if (g_original_recovery_play) g_original_recovery_play(context);
}
void __cdecl HookRecoveryPrepare(void* context) {
  uint8_t level = 0; uint32_t flags = 0;
  if (RecoveryContext(context, &level, &flags)) {
    const bool retry = (flags & 0x2000u) && !(flags & 0x801020u);
    g_recovery_checkpoint.Prepare(reinterpret_cast<uintptr_t>(context), level, retry);
    if (!retry) g_recovery_file_valid = false;
  }
  if (g_original_recovery_prepare) g_original_recovery_prepare(context);
}
void __cdecl HookRecoveryDestroy(void* context) {
  g_recovery_checkpoint.Destroy(reinterpret_cast<uintptr_t>(context));
  if (g_original_recovery_destroy) g_original_recovery_destroy(context);
}
bool InstallDeathRecoveryHooks() {
  if (g_death_recovery_hooks_installed) return true;
  if (!g_native50_menu_hooks_installed ||
      !MatchesDungeonCode(kRecoverySaveRva, std::array<uint8_t,10>{0x8B,0x44,0x24,4,0x81,0xEC,0x88,0,0,0}) ||
      !MatchesDungeonCode(kRecoveryLoadRva, std::array<uint8_t,10>{0x8B,0x44,0x24,4,0x81,0xEC,0x88,0,0,0}) ||
      !MatchesDungeonCode(kRecoveryPlayRva, std::array<uint8_t,9>{0x56,0x8B,0x74,0x24,8,0x83,0x66,8,0xBF}) ||
      !MatchesDungeonCode(kRecoveryPrepareRva, std::array<uint8_t,9>{0x53,0x56,0x8B,0x74,0x24,0xC,0x8B,0x46,8}) ||
      !MatchesDungeonCode(kRecoveryDestroyRva, std::array<uint8_t,10>{0x8B,0x54,0x24,4,0x8B,0x82,0xE4,9,0,0}) ||
      !MatchesDungeonCode(kRecoveryRootMenuRva, std::array<uint8_t,9>{0x81,0xEC,0xCC,0,0,0,0x53,0x56,0x57}) ||
      !MatchesDungeonCode(kRecoveryPauseRva, std::array<uint8_t,9>{0x83,0xEC,0x14,0x53,0x56,0x57,0x33,0xDB,0x55}) ||
      !MatchesDungeonCode(kRecoveryMenuModeRva, std::array<uint8_t,2>{0x56,0xA1}) ||
      !MatchesDungeonCode(kRecoveryMenuModeRva+6u, std::array<uint8_t,6>{0x8B,0x74,0x24,8,0x85,0xC0}) ||
      !MatchesDungeonCode(kRecoveryPathRva, std::array<uint8_t,12>{0x8B,0x44,0x24,4,0x8B,0x4C,0x24,8,0x83,0xC0,6,0x50}) ||
      !MatchesDungeonCode(kRecoverySlotsRva, std::array<uint8_t,8>{0x81,0xEC,8,1,0,0,0x53,0x56}) ||
      !MatchesDungeonCode(kRecoveryContextRva+5u, std::array<uint8_t,1>{0xC3}) ||
      !MatchesDungeonCode(kRecoveryContextRva, std::array<uint8_t,1>{0xA1})) {
    AppendDeathtrapSupportLog("support_recovery hook=signature_mismatch fallback=native_restart");
    return false;
  }
  struct Hook { void* target; void* detour; void** original; bool made = false, enabled = false; };
  std::array<Hook,6> hooks{{
    {g_dungeon_base+kRecoverySaveRva, reinterpret_cast<void*>(&HookRecoverySave), reinterpret_cast<void**>(&g_original_recovery_save)},
    {g_dungeon_base+kRecoveryLoadRva, reinterpret_cast<void*>(&HookRecoveryLoad), reinterpret_cast<void**>(&g_original_recovery_load)},
    {g_dungeon_base+kRecoveryPlayRva, reinterpret_cast<void*>(&HookRecoveryPlay), reinterpret_cast<void**>(&g_original_recovery_play)},
    {g_dungeon_base+kRecoveryPrepareRva, reinterpret_cast<void*>(&HookRecoveryPrepare), reinterpret_cast<void**>(&g_original_recovery_prepare)},
    {g_dungeon_base+kRecoveryDestroyRva, reinterpret_cast<void*>(&HookRecoveryDestroy), reinterpret_cast<void**>(&g_original_recovery_destroy)},
    {g_dungeon_base+kRecoveryRootMenuRva, reinterpret_cast<void*>(&HookRecoveryRootMenu), reinterpret_cast<void**>(&g_original_recovery_root_menu)}}};
  const auto rollback = [&]() {
    for (auto& h : hooks) {
      if (h.enabled) MH_DisableHook(h.target);
      if (h.made) { MH_RemoveHook(h.target); *h.original = nullptr; }
    }
  };
  for (auto& h : hooks) {
    if (MH_CreateHook(h.target,h.detour,h.original) != MH_OK) { rollback(); return false; }
    h.made = true;
  }
  g_death_recovery_api = {
    reinterpret_cast<void*(__cdecl*)()>(g_dungeon_base+kRecoveryContextRva),
    reinterpret_cast<void(__cdecl*)(int,int)>(g_dungeon_base+kRecoveryPauseRva),
    reinterpret_cast<void(__cdecl*)(void*,const char*)>(g_dungeon_base+kRecoveryPathRva),
    reinterpret_cast<void(__cdecl*)(void*)>(g_dungeon_base+kRecoverySlotsRva),
    &RecoveryFingerprint, &PrepareRecoveryPage,
    +[]() { CloseControllerSelector(); ReleaseInjectedControllerInput(); },
    reinterpret_cast<int(__cdecl*)(int)>(g_dungeon_base+kRecoveryMenuModeRva)};
  for (auto& h : hooks) {
    if (MH_EnableHook(h.target) != MH_OK) { rollback(); return false; }
    h.enabled = true;
  }
  g_death_recovery_hooks_installed = true;
  AppendDeathtrapSupportLog("support_recovery hook=active policy=current_run_level_last_successful_save_or_load");
  return true;
}
