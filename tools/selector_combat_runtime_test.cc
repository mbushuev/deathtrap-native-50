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

uintptr_t __cdecl CombatTestSpecialMovementStage() {
  return test_turns;
}

void CheckInverseHeadJoint() {
  SceneSnapshot scene;
  scene.root = 11;
  scene.player = 1;
  // Topology and flag values observed in the Inverse 1 save. The head is
  // joint 6. The chest also has two arm children and a resource-free neck.
  const auto add = [&](uintptr_t node, uintptr_t parent, uint32_t flags,
                       uintptr_t resource, int32_t radius) {
    auto& transform = scene.nodes[node];
    transform.parent = parent;
    transform.flags = flags;
    transform.render_resource_handle = resource;
    transform.bounds_radius = radius;
    transform.bounds_valid = radius > 0;
  };
  add(1, 0, 0xE50, 0, 0);
  add(2, 1, 0xE50, 1, 80);
  add(3, 2, 0xE58, 0, 0);
  add(4, 3, 0xE58, 1, 93);
  add(5, 4, 0xF58, 0, 0);
  add(6, 5, 0xE58, 1, 76);
  add(7, 6, 0xE58, 1, 9);
  add(8, 4, 0xE58, 1, 74);
  add(9, 4, 0xE58, 1, 74);
  add(10, 6, 0xE00, 1, 50);
  RequireCombat(ResolvePlayerHeadJoint(scene) == 6,
                "Inverse 1 neck and embedded arrow must retain the head joint");
  scene.nodes.at(8).flags = 0xED8;
  RequireCombat(ResolvePlayerHeadJoint(scene) == 6,
                "Flame Lance firing arm must retain the same head joint");

  ImmersiveHeadMountSample known_head;
  known_head.scene_root=scene.root;
  known_head.player=scene.player;
  known_head.head_node=6;
  known_head.neck_node=5;
  known_head.head_resource=1;
  scene.nodes.erase(8);
  RequireCombat(!ResolvePlayerHeadJoint(scene) &&
                    ResolveKnownImmersiveHeadJoint(scene, known_head)==6,
                "an incomplete firing skeleton must keep the previously verified head");
  scene.nodes.at(6).render_resource_handle=2;
  RequireCombat(!ResolveKnownImmersiveHeadJoint(scene, known_head),
                "a recycled render node must not keep the previous head identity");
  scene.nodes.at(6).render_resource_handle=1;
  scene.root=12;
  RequireCombat(!ResolveKnownImmersiveHeadJoint(scene, known_head),
                "a different scene must resolve its own head identity");
}

std::array<bool, 30> ricviper_inventory{};
std::vector<std::pair<int, int>> ricviper_actions;
void* __cdecl RicViperInventory(int id) {
  return ricviper_inventory.at(id) ? ricviper_inventory.data() : nullptr;
}
void __cdecl RicViperMelee(int action, int) { ricviper_actions.emplace_back(1, action); }
void __cdecl RicViperRanged(int action, int) { ricviper_actions.emplace_back(2, action); }
void __cdecl RicViperSpell(int id) { ricviper_actions.emplace_back(3, id); }
void __cdecl RicViperItem(int id) { ricviper_actions.emplace_back(4, id); }
void __cdecl RicViperChalk(void*) { ricviper_actions.emplace_back(2, -1); }

template <typename Function>
void PutSelectorEntry(DungeonCodeRva rva, Function* function) {
  // A Win32 jump to the test callback replaces this fixture's engine entry.
  std::array<uint8_t, 7> code{0xB8, 0, 0, 0, 0, 0xFF, 0xE0};
  const auto destination = reinterpret_cast<uintptr_t>(function);
  static_assert(sizeof(destination) == 4);
  std::memcpy(code.data() + 1, &destination, sizeof(destination));
  auto* entry = g_dungeon_base + rva;
  DWORD previous = 0;
  RequireCombat(VirtualProtect(entry, code.size(), PAGE_EXECUTE_READWRITE, &previous) &&
      SafeWrite(entry, code.data(), code.size()) &&
      FlushInstructionCache(GetCurrentProcess(), entry, code.size()),
      "selector fixture entry");
}

void CheckRicViperSelector() {
  g_dungeon_base = static_cast<uint8_t*>(VirtualAlloc(
      nullptr, 0x368000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
  RequireCombat(g_dungeon_base != nullptr, "selector fixture allocation");
  g_dungeon_image_variant = DungeonImageVariant::kFrenchGog;
  PutSelectorEntry(kInventoryLookupRva, &RicViperInventory);
  PutSelectorEntry(kSelectCloseCombatWeaponRva, &RicViperMelee);
  PutSelectorEntry(kSelectRangedWeaponRva, &RicViperRanged);
  PutSelectorEntry(kSelectSpellRva, &RicViperSpell);
  PutSelectorEntry(kUseConsumableRva, &RicViperItem);
  PutSelectorEntry(kUseChalkRva, &RicViperChalk);
  std::array<uint8_t, 0x118> game_root{};
  PutCombat(game_root.data() + 0x114, uintptr_t{1});
  PutCombat(g_dungeon_base + kGameRootPointerRva,
            reinterpret_cast<uintptr_t>(game_root.data()));
  for (int item : {1, 8, 10, 14, 16, 21, 22, 23}) ricviper_inventory[item] = true;
  const char warding[] = "Amulette";
  const char healing[] = "Potion de sant\x82";
  PutCombat(g_dungeon_base + kInventoryNamesRva + 22 * 4,
            reinterpret_cast<uintptr_t>(warding));
  PutCombat(g_dungeon_base + kInventoryNamesRva + 23 * 4,
            reinterpret_cast<uintptr_t>(healing));
  PutCombat(g_dungeon_base + kActiveSpellRva, int32_t{21});
  g_xinput_ricviper_requested.store(true);
  ApplyControllerLayout();

  XINPUT_GAMEPAD pad{};
  const auto poll = [&](WORD buttons, ULONGLONG elapsed = 0) {
    test_time += elapsed;
    pad.wButtons = buttons;
    UpdateControllerSelector(pad, true);
  };
  const auto tap = [&](WORD direction) { poll(direction); poll(0, 20); };
  const auto hold = [&](WORD direction) {
    poll(direction);
    poll(direction, g_xinput_selector_hold_ms);
    RequireCombat(g_controller_selector.row_open, "RicViper hold opens the radial");
  };
  using Action = std::pair<int, int>;
  hold(XINPUT_GAMEPAD_DPAD_RIGHT);
  poll(0);
  RequireCombat(ricviper_actions.empty(), "neutral right stick must not equip on release");
  hold(XINPUT_GAMEPAD_DPAD_RIGHT);
  pad.sThumbRX = 32767;
  poll(XINPUT_GAMEPAD_DPAD_RIGHT | XINPUT_GAMEPAD_DPAD_UP);
  RequireCombat(g_controller_selector.category == 2 && g_controller_selector.slot == 2,
      "another direction must not replace the active radial");
  const int32_t previous_deadzone = g_xinput_right_deadzone;
  g_xinput_right_deadzone = 0;
  pad.sThumbRX = 0;
  poll(XINPUT_GAMEPAD_DPAD_RIGHT);
  g_xinput_right_deadzone = previous_deadzone;
  poll(0);
  RequireCombat(ricviper_actions == std::vector<Action>{{2, 0x0F}},
      "ranged slot must equip on release after a stick selection");
  pad.sThumbRX = 0;
  hold(XINPUT_GAMEPAD_DPAD_UP);
  pad.sThumbRX = 32767;
  poll(XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_B);
  RequireCombat(g_controller_selector.row_open && g_controller_selector.commit_suppressed &&
                    !g_controller_selector.cancelled,
      "B must cancel the commit and leave the radial open");
  poll(0);
  RequireCombat(ricviper_actions.size() == 1, "release after B must not commit");

  pad.sThumbRX = 0;
  tap(XINPUT_GAMEPAD_DPAD_UP);
  tap(XINPUT_GAMEPAD_DPAD_DOWN);
  tap(XINPUT_GAMEPAD_DPAD_RIGHT);
  tap(XINPUT_GAMEPAD_DPAD_UP);
  RequireCombat(ricviper_actions == std::vector<Action>{
      {2, 0x0F}, {1, 2}, {3, 16}, {2, 0x0F}, {1, 2}},
      "taps must cycle, skip Firefly and restore weapon memory across categories");
  PutCombat(g_dungeon_base + kActiveCloseCombatWeaponRva, int32_t{10});
  g_ricviper_last_weapon_category = 2;
  ricviper_inventory[10] = false;
  tap(XINPUT_GAMEPAD_DPAD_RIGHT);
  RequireCombat(ricviper_actions.back() == Action{2, 0x10},
      "ranged cycling skips Chalk and wraps to an available weapon");
  PutCombat(g_dungeon_base + kActiveCloseCombatWeaponRva, int32_t{8});
  const auto before_wrap = ricviper_actions.size();
  tap(XINPUT_GAMEPAD_DPAD_RIGHT);
  RequireCombat(ricviper_actions.size() == before_wrap,
      "tapping with only the equipped weapon available must not equip it again");
  hold(XINPUT_GAMEPAD_DPAD_RIGHT);
  pad.sThumbRX = -32768;
  pad.sThumbRY = 32767;
  poll(XINPUT_GAMEPAD_DPAD_RIGHT);
  poll(0);
  RequireCombat(ricviper_actions.back() == Action{2, -1} &&
                    g_controller_selector.remembered_slot[1] == 7u &&
                    g_ricviper_equipped_slots[1] == 0,
      "Chalk updates radial memory without replacing the last equipped weapon");
  pad = {};

  tap(XINPUT_GAMEPAD_DPAD_LEFT);
  RequireCombat(ricviper_actions.back() == Action{4, 23},
      "quick heal searches localized item names");
  const char life[] = "Potion of Life";
  PutCombat(g_dungeon_base + kInventoryNamesRva + 23 * 4,
            reinterpret_cast<uintptr_t>(life));
  const auto before_life = ricviper_actions.size();
  tap(XINPUT_GAMEPAD_DPAD_LEFT);
  RequireCombat(ricviper_actions.size() == before_life + 1 &&
                    ricviper_actions.back() == Action{4, 23},
      "quick heal recognizes RicViper's life token");
  g_xinput_quick_heal_slot = 1;
  tap(XINPUT_GAMEPAD_DPAD_LEFT);
  RequireCombat(ricviper_actions.back() == Action{4, 22}, "explicit quick-heal slot wins");
  hold(XINPUT_GAMEPAD_DPAD_LEFT);
  const auto before = ricviper_actions.size();
  poll(XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_B);
  poll(XINPUT_GAMEPAD_DPAD_LEFT);
  poll(XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_A);
  poll(XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_A);
  poll(XINPUT_GAMEPAD_DPAD_LEFT);
  poll(XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_A);
  RequireCombat(ricviper_actions.size() == before + 2 && g_controller_selector.row_open,
      "each new A press uses one item even after B disarms release confirmation");
  poll(0);
  hold(XINPUT_GAMEPAD_DPAD_LEFT);
  const auto before_focus = ricviper_actions.size();
  test_foreground = false;
  PollOpenControllerSelectorForPresentation();
  test_foreground = true;
  RequireCombat(!g_controller_selector.row_open && g_ricviper_dpad_waiting_for_release,
      "presentation polling must discard the radial as soon as focus is lost");
  poll(XINPUT_GAMEPAD_DPAD_LEFT);
  poll(0, 20);
  RequireCombat(ricviper_actions.size() == before_focus,
      "releasing a direction held through focus loss must not quick-heal");
  tap(XINPUT_GAMEPAD_DPAD_LEFT);
  RequireCombat(ricviper_actions.size() == before_focus + 1,
      "a new tap after neutral must quick-heal normally");
  hold(XINPUT_GAMEPAD_DPAD_LEFT);
  g_xinput_ricviper_requested.store(false);
  ApplyControllerLayout();
  RequireCombat(!g_xinput_ricviper && !g_controller_selector.row_open &&
      !captured_gameplay && !captured_combat.Consume(),
      "switching layouts releases pending selector, movement and combat");
  g_xinput_quick_heal_slot = 0;
  VirtualFree(g_dungeon_base, 0, MEM_RELEASE);
  g_dungeon_base = nullptr;
}
}

int main() {
  CheckInverseHeadJoint();
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

  // Selecting head view alone must leave the third-person input route
  // usable. A successful publication also has to survive the interval
  // between source ticks, but must expire if the camera stops updating.
  PutCombat(g_dungeon_base + kGameRootPointerRva,
            reinterpret_cast<uintptr_t>(root.data()));
  PutCombat(g_dungeon_base + kUiOwnerPointerRva,
            reinterpret_cast<uintptr_t>(player.data()));
  PutCombat(controller.data() + 0x2EC,
            reinterpret_cast<uintptr_t>(g_dungeon_base + kPlayerStateDispatcherRva));
  PutCombat(g_dungeon_base + kCameraControllerRva +
                kCameraControllerActiveModeOffset, uint8_t{3});
  g_last_mode3_source_tick_ms.store(test_time);
  g_custom_camera_view_mode.store(static_cast<uint32_t>(CustomCameraViewMode::kHead));
  RequireCombat(!DeathtrapImmersiveFirstPersonActive() &&
                    DeathtrapThirdPersonKeyboardCameraRelativeAvailable(),
                "head request without a published view must retain third-person controls");

  g_custom_head_last_publication_ms.store(test_time);
  g_immersive_body_heading.store(200);
  g_original_player_movement_stage = &CombatTestSpecialMovementStage;
  g_immersive_root_motion_hooks_installed.store(true);
  // A special weapon callback need not run the ordinary ground dispatcher.
  // It should still see the updated aim without a strafe/root transaction.
  PutCombat(controller.data() + 0x2EC, uintptr_t{1});
  PutCombat(controller.data() + kPlayerTurnSourcePointerOffset, uintptr_t{0});
  const auto special_controller = controller;
  RequireCombat(DeathtrapImmersiveFirstPersonActive() &&
                    !DeathtrapImmersiveVectorLocomotionActive() &&
                    !DeathtrapThirdPersonKeyboardCameraRelativeAvailable(),
                "a special action owns its native movement while head view is visible");
  const unsigned turns_before = test_turns;
  RequireCombat(HookImmersivePlayerMovementStage() == turns_before + 1 &&
                    controller == special_controller,
                "special movement callback must see eye steering with temporary fields restored");

  g_scripted_camera_override_active.store(true);
  RequireCombat(HookImmersivePlayerMovementStage() == turns_before + 1,
                "scripted takeover must release immersive aim immediately");
  g_scripted_camera_override_active.store(false);
  test_time += g_camera_source_watchdog_ms.load() + 1;
  g_last_mode3_source_tick_ms.store(test_time);
  PutCombat(controller.data() + 0x2EC,
            reinterpret_cast<uintptr_t>(g_dungeon_base + kPlayerStateDispatcherRva));
  RequireCombat(!DeathtrapImmersiveFirstPersonActive() &&
                    DeathtrapThirdPersonKeyboardCameraRelativeAvailable() &&
                    HookImmersivePlayerMovementStage() == turns_before + 1,
                "expired head publication must restore third-person controls and release aim");
  SetCustomHeadViewSelected(false, "test");
  g_original_player_movement_stage = nullptr;
  g_immersive_root_motion_hooks_installed.store(false);
  PutCombat(controller.data() + kPlayerTurnSourcePointerOffset, turn_source);

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
  CheckRicViperSelector();
  std::cout << "Selector combat: F1-F4, all four controller radials, RT direction/taps, independent selection, attack-facing, frontend/focus/disconnect guards and four layouts passed\n";
}
