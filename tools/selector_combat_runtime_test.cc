#include <windows.h>
#include <array>
#include <iostream>
#include <vector>

void CaptureSelectorCombat(bool, bool, bool, bool, bool);
void ClearSelectorCombat();
void CaptureSelectorGameplay(uint32_t);
UINT WINAPI CombatTestSystemInput(UINT count, LPINPUT, int) { return count; }

namespace {
bool test_foreground = true, test_cursor = false;
ULONGLONG test_time = 1000;
HWND WINAPI CombatTestForeground() {
  return test_foreground ? reinterpret_cast<HWND>(1) : nullptr;
}
DWORD WINAPI CombatTestWindowProcess(HWND, LPDWORD process) {
  if (process) *process = GetCurrentProcessId();
  return GetCurrentThreadId();
}
BOOL WINAPI CombatTestCursor(PCURSORINFO info) {
  info->flags = test_cursor ? CURSOR_SHOWING : 0;
  return TRUE;
}
ULONGLONG WINAPI CombatTestTime() { return test_time; }
}

// Exercise production gates and the complete attack-facing transaction without
// creating windows, sending input, or depending on the user's active desktop.
#define GetForegroundWindow CombatTestForeground
#define GetWindowThreadProcessId CombatTestWindowProcess
#define GetCursorInfo CombatTestCursor
#define GetTickCount64 CombatTestTime
#define SendInput CombatTestSystemInput
#define SubmitDeathtrapXInputCombatState CaptureSelectorCombat
#define ClearDeathtrapXInputCombatState ClearSelectorCombat
#define SubmitDeathtrapXInputGameplayCommands CaptureSelectorGameplay
#include "../src/deathtrap_native_render_patch.cc"
#undef SubmitDeathtrapXInputGameplayCommands
#undef ClearDeathtrapXInputCombatState
#undef SubmitDeathtrapXInputCombatState
#undef SendInput
#undef GetTickCount64
#undef GetCursorInfo
#undef GetWindowThreadProcessId
#undef GetForegroundWindow

deathtrap::input::ControllerCombatInput captured_combat;
uint32_t captured_gameplay = 0;
XINPUT_STATE captured_pad{};
DWORD captured_pad_result = ERROR_SUCCESS;
void CaptureSelectorCombat(bool attack, bool forward, bool backward, bool left, bool right) {
  captured_combat.Publish(static_cast<uint8_t>(attack ? 1u | (forward ? 2u : 0u) |
      (backward ? 4u : 0u) | (left ? 8u : 0u) | (right ? 16u : 0u) : 0u));
}
void ClearSelectorCombat() { captured_combat.Clear(); }
void CaptureSelectorGameplay(uint32_t commands) { captured_gameplay = commands; }
DWORD WINAPI CombatTestPad(DWORD, XINPUT_STATE* state) {
  *state = captured_pad; return captured_pad_result;
}

namespace {
void RequireCombat(bool ok, const char* message) {
  if (!ok) { std::cerr << "selector combat: " << message << '\n'; std::exit(1); }
}
template <typename T> void PutCombat(void* where, const T& value) {
  RequireCombat(SafeWrite(where, &value, sizeof(value)), "fixture write");
}
uintptr_t test_heading_node = 0;
unsigned test_turns = 0;
void __cdecl CombatTestTurn(void* input) {
  const auto controller = reinterpret_cast<uintptr_t>(input);
  uintptr_t source = 0;
  int32_t delta = 0, heading = 0;
  RequireCombat(SafeReadValue(reinterpret_cast<void*>(controller + kPlayerTurnSourcePointerOffset), &source) &&
      SafeReadValue(reinterpret_cast<void*>(source), &delta) &&
      SafeReadValue(reinterpret_cast<void*>(test_heading_node + kPlayerHeadingOffset), &heading), "canonical heading fixture");
  PutCombat(reinterpret_cast<void*>(test_heading_node + kPlayerHeadingOffset), NormalizePlayerHeading(heading + delta));
  ++test_turns;
}
}

int main() {
  std::vector<uint8_t> image(0x368000);
  std::array<uint8_t, 0x120> root{}, player{};
  std::array<uint8_t, 0x1100> context{};
  std::array<uint8_t, 0x400> controller{};
  std::array<uint8_t, 0x100> heading_node{};
  uintptr_t link = reinterpret_cast<uintptr_t>(heading_node.data());
  test_heading_node = link;
  g_dungeon_base = image.data();
  g_display_windowed = false;
  g_xinput_controller_present.store(false);
  g_xinput_menu_mode.store(false);
  g_third_person_orbit_enabled = true;
  g_keyboard_mouse_camera_relative_movement = true;
  g_custom_camera_view_mode.store(static_cast<uint32_t>(CustomCameraViewMode::kModernThirdPerson));
  g_player_state_dispatcher_hook_installed.store(true);
  g_third_person_heading_reference_valid.store(true);
  g_player_turn_writer = &CombatTestTurn;
  PutCombat(root.data(), uintptr_t{1});
  PutCombat(root.data() + 0x2C, reinterpret_cast<uintptr_t>(context.data()));
  PutCombat(context.data() + 0x1030, uintptr_t{1});
  PutCombat(player.data() + kPlayerMovementControllerOffset, reinterpret_cast<uintptr_t>(controller.data()));
  PutCombat(controller.data() + kPlayerRenderLinkOffset, reinterpret_cast<uintptr_t>(&link));
  const uintptr_t turn_source = reinterpret_cast<uintptr_t>(controller.data() + kPlayerWalkTurnSourceOffset);
  PutCombat(controller.data() + kPlayerTurnSourcePointerOffset, turn_source);
  PutCombat(controller.data() + kPlayerTurnLimitOffset, int32_t{321});
  PutCombat(controller.data() + kPlayerIdleTurnOffset, int32_t{-37});

  for (const auto variant : {DungeonImageVariant::kEnglish, DungeonImageVariant::kFrenchGog,
                            DungeonImageVariant::kGermanGog, DungeonImageVariant::kItalianGog}) {
    g_dungeon_image_variant = variant;
    PutCombat(g_dungeon_base + kGameRootPointerRva, reinterpret_cast<uintptr_t>(root.data()));
    PutCombat(g_dungeon_base + kUiOwnerPointerRva, reinterpret_cast<uintptr_t>(player.data()));
    PutCombat(controller.data() + 0x2EC, reinterpret_cast<uintptr_t>(g_dungeon_base + kPlayerStateDispatcherRva));
    PutCombat(g_dungeon_base + kCameraControllerRva + kCameraControllerActiveModeOffset, uint8_t{3});
    g_last_mode3_source_tick_ms.store(test_time);
    for (unsigned mode = 0; mode < 256; ++mode) {
      PutCombat(g_dungeon_base + kUiSelectorModeRva, static_cast<uint8_t>(mode));
      RequireCombat(DeathtrapGameplayAcceptsMouseCombat() == (mode <= 4), "only gameplay and keyboard F1-F4 rows may accept combat");
    }
    for (uint8_t mode = 0; mode <= 4; ++mode) {
      PutCombat(g_dungeon_base + kUiSelectorModeRva, mode);
      RequireCombat(DeathtrapThirdPersonKeyboardCameraRelativeAvailable() == (mode == 0), "selector must retain existing locomotion ownership");
      RequireCombat(DeathtrapThirdPersonMouseAttackCameraRelativeAvailable(), "attack-facing must work with each keyboard row open");
      for (bool ranged : {false, true}) {
        const auto plan = deathtrap::input::ResolveMouseCombatKeyboardPlan(true,
            DeathtrapGameplayAcceptsMouseCombat(), false, false, false, false, false, ranged);
        RequireCombat(plan.modifier_down && plan.attack == (ranged ? deathtrap::input::MouseCombatAttack::kRanged : deathtrap::input::MouseCombatAttack::kForward),
            "melee and ranged clicks must retain their native combat chords");
      }
      int32_t target = 0;
      RequireCombat(ResolveMouseAttackCameraFacingHeading(&target), "camera heading remains available");
      PutCombat(heading_node.data() + kPlayerHeadingOffset, NormalizePlayerHeading(target + 256));
      const auto saved_controller = controller;
      g_third_person_mouse_attack_facing_phase.store(DeathtrapMouseAttackFacingPhase::kIdle);
      RequestDeathtrapThirdPersonMouseAttackFacing();
      RequireCombat(GetDeathtrapThirdPersonMouseAttackFacingPhase() == DeathtrapMouseAttackFacingPhase::kPending, "single click queues its turn");
      for (unsigned tick = 0; tick < 16 && GetDeathtrapThirdPersonMouseAttackFacingPhase() != DeathtrapMouseAttackFacingPhase::kReady; ++tick) {
        test_time += 60; g_last_mode3_source_tick_ms.store(test_time);
        UpdateMouseAttackCameraFacing(player.data());
      }
      int32_t result = -1; uint8_t mode_after = 255;
      RequireCombat(SafeReadValue(heading_node.data() + kPlayerHeadingOffset, &result) && result == target &&
          GetDeathtrapThirdPersonMouseAttackFacingPhase() == DeathtrapMouseAttackFacingPhase::kReady &&
          controller == saved_controller, "attack reaches camera direction and restores temporary turn fields");
      RequireCombat(SafeReadValue(g_dungeon_base + kUiSelectorModeRva, &mode_after) && mode_after == mode,
          "attack must neither close nor switch the keyboard selector");
      CompleteDeathtrapThirdPersonMouseAttackFacing(DeathtrapMouseAttackFacingPhase::kReady);
      test_foreground = false;
      RequireCombat(!DeathtrapGameplayAcceptsMouseCombat(), "background window must reject combat"); test_foreground = true;
      test_cursor = true;
      RequireCombat(!DeathtrapGameplayAcceptsMouseCombat(), "frontend cursor must reject combat"); test_cursor = false;
      g_xinput_controller_present.store(true); g_xinput_menu_mode.store(true);
      RequireCombat(!DeathtrapGameplayAcceptsMouseCombat(), "controller pause/menu must reject combat");
      g_xinput_controller_present.store(false); g_xinput_menu_mode.store(false);
      g_controller_selector_overlay.store(1);
      RequireCombat(!DeathtrapGameplayAcceptsMouseCombat() && !DeathtrapThirdPersonMouseAttackCameraRelativeAvailable(), "controller radial must retain exclusive ownership");
      g_controller_selector_overlay.store(0);
      PutCombat(g_dungeon_base + kGameRootPointerRva, uintptr_t{0});
      RequireCombat(!DeathtrapGameplayAcceptsMouseCombat(), "loading/missing gameplay must reject combat");
      PutCombat(g_dungeon_base + kGameRootPointerRva, reinterpret_cast<uintptr_t>(root.data()));
    }

    g_xinput_controller_present.store(true);
    g_xinput_menu_mode.store(false);
    g_xinput_base_bindings = g_xinput_enabled = true;
    g_xinput_vibration_enabled = false;
    g_native_joystick_hooks_installed.store(true);
    g_xinput_get_state = &CombatTestPad;
    constexpr std::array<WORD,4> dpad{{XINPUT_GAMEPAD_DPAD_UP, XINPUT_GAMEPAD_DPAD_RIGHT,
        XINPUT_GAMEPAD_DPAD_DOWN, XINPUT_GAMEPAD_DPAD_LEFT}};
    struct DirectionCase { SHORT x, y; uint8_t command; };
    constexpr DirectionCase directions[]={{0,0,1},{0,32767,3},{0,-32768,5},{-32768,0,9},{32767,0,17}};
    for (uint8_t category=1;category<=4;++category) {
      PutCombat(g_dungeon_base+kUiSelectorModeRva,category);
      g_controller_selector={};
      g_controller_selector.direction_down=g_controller_selector.row_open=true;
      g_controller_selector.category=category;
      PublishControllerSelector(true,category,0,true,category==2 || category==4);
      RequireCombat(DeathtrapGameplayAcceptsControllerCombat() && !DeathtrapGameplayAcceptsMouseCombat(),
          "radial permits RT while retaining physical mouse ownership");
      for (const auto direction:directions) {
        captured_combat.Clear();
        XINPUT_GAMEPAD pad{};pad.wButtons=dpad[category-1];pad.bRightTrigger=255;
        pad.sThumbLX=direction.x;pad.sThumbLY=direction.y;
        UpdateControllerBaseBindings(pad,true,true);
        RequireCombat(captured_combat.Consume()==direction.command && captured_gameplay==0 &&
            g_xinput_native_joystick_active.load() && !g_xinput_native_joystick_x_milli.load() &&
            !g_xinput_native_joystick_y_milli.load() && !g_xinput_camera_relative_intent.load(),
            "radial RT retains every attack direction without movement/jump/cast");
        RequireCombat(g_controller_selector.row_open && g_controller_selector.category==category &&
            g_controller_selector.slot==0,"RT must not close, switch or commit the item selector");
      }
    }
    // A complete short press occurs between two source ticks. The right stick
    // selects the always-available fists slot while the left chooses RT-right.
    g_controller_selector.category=1;g_controller_selector.slot=0;
    PutCombat(g_dungeon_base+kUiSelectorModeRva,uint8_t{1});
    captured_combat.Clear();captured_pad={};
    captured_pad.Gamepad.wButtons=XINPUT_GAMEPAD_DPAD_UP;
    captured_pad.Gamepad.bRightTrigger=255;
    captured_pad.Gamepad.sThumbLX=32767;
    captured_pad.Gamepad.sThumbRX=-32768;captured_pad.Gamepad.sThumbRY=32767;
    PollOpenControllerSelectorForPresentation();
    RequireCombat(g_controller_selector.row_open && g_controller_selector.slot==7,
        "right-stick radial selection remains independent of the RT attack direction");
    captured_pad.Gamepad.bRightTrigger=0;captured_pad.Gamepad.sThumbLX=0;
    PollOpenControllerSelectorForPresentation();
    UpdateControllerBaseBindings(captured_pad.Gamepad,true,true);
    RequireCombat(captured_combat.Consume()==17 && captured_combat.Consume()==0,
        "presentation RT tap survives release and the next source poll exactly once");
    captured_pad.Gamepad.bRightTrigger=255;
    PollOpenControllerSelectorForPresentation();
    test_foreground=false;PollOpenControllerSelectorForPresentation();test_foreground=true;
    RequireCombat(captured_combat.Consume()==0,"focus loss discards an unsent RT tap");
    PollOpenControllerSelectorForPresentation();
    captured_pad_result=ERROR_DEVICE_NOT_CONNECTED;PollOpenControllerSelectorForPresentation();
    captured_pad_result=ERROR_SUCCESS;
    RequireCombat(captured_combat.Consume()==0,"disconnection discards an unsent RT tap");
    PollOpenControllerSelectorForPresentation();
    g_xinput_menu_mode.store(true);
    UpdateControllerBaseBindings(captured_pad.Gamepad,false,false);
    RequireCombat(captured_combat.Consume()==0 && !DeathtrapGameplayAcceptsControllerCombat(),
        "frontend transition discards pending combat and blocks dispatch");
    g_xinput_menu_mode.store(false);
    CloseControllerSelector();
    g_xinput_controller_present.store(false);
  }
  RequireCombat(test_turns > 0, "bounded turn writer must be exercised");
  std::cout << "Selector combat: F1-F4, all four controller radials, RT direction/taps, independent selection, attack-facing, frontend/focus/disconnect guards and four layouts passed\n";
}
