// The retail briefing polls the current Space level after each draw and wait.
// Catch a fresh keydown from the game thread's message pump so a tap between
// polls still acknowledges this prompt. The native keyboard bitmap is left
// alone, so the accepted tap cannot become a jump in the next state.
constexpr DungeonCodeRva kLevelBriefingWaitRva{0x6A3E0u};
constexpr std::array<DungeonCodeRva, 2> kLevelBriefingSpaceCalls{{
    {0x6A4AEu}, {0x6A532u},
}};
using LevelBriefingWaitFn = void(__cdecl*)(int);
LevelBriefingWaitFn g_original_level_briefing_wait = nullptr;
thread_local bool g_level_briefing_space_tapped = false;

LRESULT CALLBACK LevelBriefingKeyMessage(int code, WPARAM removal, LPARAM data) {
  if (code == HC_ACTION && removal == PM_REMOVE) {
    const auto& message = *reinterpret_cast<const MSG*>(data);
    if (message.hwnd && message.message == WM_KEYDOWN &&
        message.wParam == VK_SPACE && !(message.lParam & (1L << 30))) {
      g_level_briefing_space_tapped = true;
    }
  }
  return CallNextHookEx(nullptr, code, removal, data);
}

void __cdecl HookLevelBriefingWait(int page) {
  g_level_briefing_space_tapped = false;
  const HHOOK keyboard_messages = SetWindowsHookExW(
      WH_GETMESSAGE, &LevelBriefingKeyMessage, nullptr, GetCurrentThreadId());
  g_original_level_briefing_wait(page);
  if (keyboard_messages) UnhookWindowsHookEx(keyboard_messages);
  g_level_briefing_space_tapped = false;
}

int __cdecl HookLevelBriefingSpaceDown(uint8_t scan) {
  if (std::exchange(g_level_briefing_space_tapped, false)) {
    AppendDeathtrapSupportLog("support_level_briefing_confirm source=keyboard_message");
    return 1;
  }
  // Take a held physical Space even if the player rebound the jump key.
  if (IsGameForeground() && (GetAsyncKeyState(VK_SPACE) & 0x8000)) return 1;
  const auto key_down = reinterpret_cast<int(__cdecl*)(uint8_t)>(
      g_dungeon_base + kHardwareKeyDownRva);
  return key_down(scan);
}

bool InstallLevelBriefingInputHook() {
  constexpr std::array<uint8_t, 9> kWaitPrologue = {
      0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x10, 0x53, 0x56, 0x57};
  if (!MatchesDungeonCode(kLevelBriefingWaitRva, kWaitPrologue)) return false;
  const auto base = reinterpret_cast<uintptr_t>(g_dungeon_base);
  const auto native_key_down = base + kHardwareKeyDownRva;
  for (const auto site : kLevelBriefingSpaceCalls) {
    if (!MatchesDungeonCode(site, RenderQueueCall<5>(base + site, native_key_down))) {
      return false;
    }
  }
  auto* const wait = g_dungeon_base + kLevelBriefingWaitRva;
  if (MH_CreateHook(wait, reinterpret_cast<void*>(&HookLevelBriefingWait),
                    reinterpret_cast<void**>(&g_original_level_briefing_wait)) != MH_OK) {
    return false;
  }
  size_t patched = 0;
  const auto replacement = reinterpret_cast<uintptr_t>(&HookLevelBriefingSpaceDown);
  for (const auto site : kLevelBriefingSpaceCalls) {
    if (!PatchDungeonCode(site, RenderQueueCall<5>(base + site, native_key_down),
                         RenderQueueCall<5>(base + site, replacement))) break;
    ++patched;
  }
  if (patched == kLevelBriefingSpaceCalls.size() && MH_EnableHook(wait) == MH_OK) {
    return true;
  }
  while (patched != 0) {
    const auto site = kLevelBriefingSpaceCalls[--patched];
    PatchDungeonCode(site, RenderQueueCall<5>(base + site, replacement),
                     RenderQueueCall<5>(base + site, native_key_down));
  }
  MH_RemoveHook(wait);
  g_original_level_briefing_wait = nullptr;
  return false;
}
