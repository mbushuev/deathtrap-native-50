// Exercise the actual proxy callbacks with a fake DirectInput driver. Only
// gameplay readiness, frontend polling and the camera sink are replaced.
#define DeathtrapModernCameraConsumesMouse TestCameraConsumesMouse
#define PollDeathtrapFrontendXInput TestPollFrontend
#define SubmitDeathtrapPhysicalMouseDelta TestSubmitMouse
#define DeathtrapGameplayAcceptsMouseCombat TestAcceptsCombat
#include "../src/dinput_native_proxy.cc"
#include "../src/controller_vibration.h"
#include <iostream>

static int32_t received_x = 0, received_y = 0;
static bool owns_mouse = true;
static bool accepts_combat = false;
static bool physical_control_down = false;
bool TestCameraConsumesMouse() { return owns_mouse; }
bool TestAcceptsCombat() { return accepts_combat; }
void TestPollFrontend() {}
void TestSubmitMouse(int32_t x, int32_t y) {
  received_x += x; received_y += y;
}
static DIDEVICEOBJECTDATA queued{};
static LONG next_x = 0, next_y = 0;
HRESULT STDMETHODCALLTYPE FakeState(IDirectInputDeviceA*, DWORD size, void* p) {
  std::memset(p, 0, size);
  if (size == 256u) {
    static_cast<uint8_t*>(p)[DIK_LCONTROL] =
        physical_control_down ? 0x80u : 0u;
    return DI_OK;
  }
  auto* state = static_cast<DIMOUSESTATE*>(p);
  state->lX = next_x; state->lY = next_y;
  next_x = next_y = 0;
  return DI_OK;
}
HRESULT STDMETHODCALLTYPE FakeData(IDirectInputDeviceA*, DWORD size,
    LPDIDEVICEOBJECTDATA data, LPDWORD count, DWORD) {
  if (*count) { std::memcpy(data, &queued, size); *count = 1; }
  return DI_OK;
}
int main() {
  if (deathtrap::input::ApplyVibrationOutputGain(0x1000u, 100u) !=
          0x1000u ||
      deathtrap::input::ApplyVibrationOutputGain(0x1000u, 300u) !=
          0x3000u ||
      deathtrap::input::ApplyVibrationOutputGain(0x8000u, 300u) !=
          0xFFFFu ||
      deathtrap::input::ApplyVibrationOutputGain(0u, 300u) != 0u) {
    return 10;
  }
  auto* mouse = reinterpret_cast<IDirectInputDeviceA*>(0x1000);
  auto* keyboard = reinterpret_cast<IDirectInputDeviceA*>(0x2000);
  g_support_mouse_device.store(mouse);
  g_support_mouse_data_size.store(sizeof(DIMOUSESTATE));
  g_direct_input_device_get_state = FakeState;
  g_direct_input_device_get_data = FakeData;
  DIDEVICEOBJECTDATA out{};
  DWORD count = 1;
  queued.dwOfs = DIK_3; queued.dwData = 0x80;
  HookDirectInputDeviceGetData(keyboard, sizeof(out), &out, &count, 0);
  if (count != 1 || out.dwOfs != DIK_3 || out.dwData != 0x80 || received_y)
    return 1;
  // A relative movement read at presentation rate remains in the independent
  // event queue. Reading that queue at the source tick must not replay it.
  next_x = 12; next_y = -5;
  int32_t x = 0, y = 0;
  if (!PollDeathtrapPresentationMouseDeltaInternal(&x, &y) || x != 12 || y != -5)
    return 2;
  TestSubmitMouse(x, y);
  queued.dwOfs = DIMOFS_X; queued.dwData = 12;
  count = 1;
  HookDirectInputDeviceGetData(mouse, sizeof(out), &out, &count, 0);
  if (count || received_x != 12 || received_y != -5) return 3;
  // Peek also hides camera axes without consuming or duplicating motion.
  count = 1;
  HookDirectInputDeviceGetData(mouse, sizeof(out), &out, &count, DIGDD_PEEK);
  if (count || received_x != 12) return 4;
  queued.dwOfs = DIMOFS_BUTTON1; queued.dwData = 0x80;
  count = 1;
  HookDirectInputDeviceGetData(mouse, sizeof(out), &out, &count, 0);
  if (count != 1 || out.dwOfs != DIMOFS_BUTTON1 || out.dwData != 0x80) return 5;
  // Frontend mouse axes must remain available to the native menu.
  owns_mouse = false;
  queued.dwOfs = DIMOFS_X; queued.dwData = 7;
  count = 1;
  HookDirectInputDeviceGetData(mouse, sizeof(out), &out, &count, 0);
  if (count != 1 || out.dwData != 7 || received_x != 12) return 6;
  // Exercise the real post-remap keyboard merge, not just the command helper.
  // Controller caution must reach the active layout's native Ctrl+W/S scans,
  // coexist with jump, suppress synthetic Shift, and release on the next poll.
  using deathtrap::input::Command;
  using deathtrap::input::CommandBit;
  using deathtrap::input::KeyboardAction;
  uint8_t keyboard_state[256]{};
  g_support_keyboard_device.store(keyboard);
  g_xinput_gameplay_commands.store(CommandBit(Command::kStepForward) |
      CommandBit(Command::kRun) | CommandBit(Command::kJumpClimb));
  HookDirectInputDeviceGetState(keyboard, sizeof(keyboard_state), keyboard_state);
  const auto targets = NativeKeyboardTargetScans();
  const auto native_scan = [&targets](KeyboardAction action) {
    return targets[static_cast<size_t>(action)];
  };
  if (!(keyboard_state[DIK_LCONTROL] & 0x80u) ||
      !(keyboard_state[native_scan(KeyboardAction::kMoveForward)] & 0x80u) ||
      !(keyboard_state[DIK_SPACE] & 0x80u) ||
      (keyboard_state[DIK_LSHIFT] & 0x80u)) return 11;
  g_xinput_gameplay_commands.store(CommandBit(Command::kStepBackward));
  HookDirectInputDeviceGetState(keyboard, sizeof(keyboard_state), keyboard_state);
  if (!(keyboard_state[DIK_LCONTROL] & 0x80u) ||
      !(keyboard_state[native_scan(KeyboardAction::kMoveBackward)] & 0x80u) ||
      (keyboard_state[native_scan(KeyboardAction::kMoveForward)] & 0x80u)) return 12;
  g_xinput_gameplay_commands.store(0u);
  HookDirectInputDeviceGetState(keyboard, sizeof(keyboard_state), keyboard_state);
  if ((keyboard_state[DIK_LCONTROL] & 0x80u) ||
      (keyboard_state[native_scan(KeyboardAction::kMoveForward)] & 0x80u) ||
      (keyboard_state[native_scan(KeyboardAction::kMoveBackward)] & 0x80u) ||
      (keyboard_state[DIK_SPACE] & 0x80u)) return 13;
  // Keyboard rebinding must not alter fixed controller commands.
  g_keyboard_bindings[static_cast<size_t>(KeyboardAction::kMoveForward)]
      .store(0x91u);  // R instead of W.
  g_xinput_gameplay_commands.store(CommandBit(Command::kStepForward));
  HookDirectInputDeviceGetState(keyboard, sizeof(keyboard_state), keyboard_state);
  if (!(keyboard_state[DIK_LCONTROL] & 0x80u) ||
      !(keyboard_state[native_scan(KeyboardAction::kMoveForward)] & 0x80u)) return 14;
  g_keyboard_bindings[static_cast<size_t>(KeyboardAction::kCastSpell)]
      .store(0x91u);  // Physical spell rebinding must not rewrite B.
  g_xinput_gameplay_commands.store(CommandBit(Command::kCastSpell));
  HookDirectInputDeviceGetState(keyboard, sizeof(keyboard_state), keyboard_state);
  if (!(keyboard_state[native_scan(KeyboardAction::kCastSpell)] & 0x80u) ||
      (keyboard_state[DIK_LCONTROL] & 0x80u)) return 17;
  g_xinput_gameplay_commands.store(0u);
  HookDirectInputDeviceGetState(keyboard, sizeof(keyboard_state), keyboard_state);
  if (keyboard_state[native_scan(KeyboardAction::kCastSpell)] & 0x80u) return 18;
  g_physical_left_button_press_pending.store(true);
  if (!DeathtrapPhysicalMouseCombatActive() ||
      !g_physical_left_button_press_pending.load()) return 19;
  g_physical_left_button_press_pending.store(false);
  if (DeathtrapPhysicalMouseCombatActive()) return 20;
  g_keyboard_bindings[static_cast<size_t>(KeyboardAction::kSidestepLeft)]
      .store(0x91u);
  g_xinput_gameplay_commands.store(CommandBit(Command::kLeftSidestep));
  HookDirectInputDeviceGetState(keyboard, sizeof(keyboard_state), keyboard_state);
  if (!(keyboard_state[native_scan(KeyboardAction::kSidestepLeft)] & 0x80u) ||
      (keyboard_state[native_scan(KeyboardAction::kSidestepRight)] & 0x80u) ||
      (keyboard_state[native_scan(KeyboardAction::kMoveForward)] & 0x80u) ||
      (keyboard_state[native_scan(KeyboardAction::kMoveBackward)] & 0x80u) ||
      (keyboard_state[DIK_LCONTROL] & 0x80u)) return 21;
  g_xinput_gameplay_commands.store(CommandBit(Command::kRightSidestep));
  HookDirectInputDeviceGetState(keyboard, sizeof(keyboard_state), keyboard_state);
  if (!(keyboard_state[native_scan(KeyboardAction::kSidestepRight)] & 0x80u) ||
      (keyboard_state[native_scan(KeyboardAction::kSidestepLeft)] & 0x80u) ||
      (keyboard_state[native_scan(KeyboardAction::kMoveForward)] & 0x80u) ||
      (keyboard_state[DIK_LCONTROL] & 0x80u)) return 22;
  g_xinput_gameplay_commands.store(0u);
  HookDirectInputDeviceGetState(keyboard, sizeof(keyboard_state), keyboard_state);
  if ((keyboard_state[native_scan(KeyboardAction::kSidestepRight)] & 0x80u) ||
      (keyboard_state[native_scan(KeyboardAction::kSidestepLeft)] & 0x80u)) return 23;
  g_xinput_gameplay_commands.store(CommandBit(Command::kStepForward));
  // A mouse attack owns the direction chord. Synthetic LB Ctrl must be
  // removed, while a genuinely held keyboard Ctrl remains untouched.
  accepts_combat = true;
  g_physical_left_button_down.store(true);
  HookDirectInputDeviceGetState(keyboard, sizeof(keyboard_state), keyboard_state);
  if ((keyboard_state[DIK_LCONTROL] & 0x80u) ||
      !(keyboard_state[native_scan(KeyboardAction::kAttack)] & 0x80u)) return 15;
  physical_control_down = true;
  HookDirectInputDeviceGetState(keyboard, sizeof(keyboard_state), keyboard_state);
  if (!(keyboard_state[DIK_LCONTROL] & 0x80u) ||
      !(keyboard_state[native_scan(KeyboardAction::kAttack)] & 0x80u)) return 16;
  std::cout << "PASS actual callbacks: keyboard 3 preserved; motion counted once; "
               "peek, buttons and menu axes preserved; vibration gain clamps; "
               "LB/RB step chords, spell release/rebinding and mouse priority preserved\n";
}
