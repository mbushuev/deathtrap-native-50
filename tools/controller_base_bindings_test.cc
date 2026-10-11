// Actual controller poll/command generation with a fake Dungeon data image.
// Never inject keyboard/mouse events into Windows while exercising menus.
#include <cstdint>
#include <iostream>
#include <windows.h>

void CaptureControllerCommands(uint32_t commands);
UINT WINAPI CaptureSystemInput(UINT count, LPINPUT, int) { return count; }
#define SubmitDeathtrapXInputGameplayCommands CaptureControllerCommands
#define SendInput CaptureSystemInput
#include "../src/deathtrap_native_render_patch.cc"
#undef SendInput
#undef SubmitDeathtrapXInputGameplayCommands

namespace {
uint32_t captured_commands = 0;

void Require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "controller binding boundary failure: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}
}  // namespace

void CaptureControllerCommands(uint32_t commands) {
  captured_commands = commands;
}

int wmain(int argc, wchar_t** argv) {
  // Inspect real localized DLL images without running their entry points or
  // resolving imports. This exercises the production compatibility gate,
  // not merely the installer's list of accepted hashes.
  if (argc > 1) {
    for (int index = 1; index < argc; ++index) {
      HMODULE image = LoadLibraryExW(argv[index], nullptr,
                                    DONT_RESOLVE_DLL_REFERENCES);
      Require(image != nullptr, "real Dungeon image must map successfully");
      g_dungeon_image_variant = IdentifyDungeonImage(
          reinterpret_cast<uint8_t*>(image));
      Require(g_dungeon_image_variant != DungeonImageVariant::kUnsupported,
              "real Dungeon image must pass the production profile gate");
      std::cout << "Dungeon compatibility profile: "
                << ActiveDungeonProfileName() << " passed\n";
      FreeLibrary(image);
    }
    return 0;
  }
  std::vector<uint8_t> dungeon(0x368000);
  g_dungeon_image_variant = DungeonImageVariant::kEnglish;
  g_dungeon_base = dungeon.data();
  g_debug_log = false;
  g_xinput_enabled = true;
  g_xinput_base_bindings = true;
  g_xinput_vibration_enabled = false;
  g_xinput_movement_response_ms = 0;
  g_native_joystick_hooks_installed.store(true);
  g_immersive_root_motion_hooks_installed.store(true);

  using deathtrap::input::Command;
  using deathtrap::input::CommandBit;
  using deathtrap::input::CommandActive;
  XINPUT_GAMEPAD pad{};
  UpdateControllerBaseBindings(pad, true, false);
  pad.sThumbLY = 17500;
  UpdateControllerBaseBindings(pad, true, false);
  Require(!g_xinput_native_joystick_y_milli.load() &&
              !CommandActive(captured_commands, Command::kRun),
          "moderate raw stick input must not start native forward movement");
  pad.sThumbLY = 29500;
  UpdateControllerBaseBindings(pad, true, false);
  Require(g_xinput_native_joystick_y_milli.load() > 500 &&
              !CommandActive(captured_commands, Command::kRun),
          "strong raw stick input must walk without an accidental run");
  pad.sThumbLY = 32767;
  UpdateControllerBaseBindings(pad, true, false);
  Require(CommandActive(captured_commands, Command::kRun),
          "full stick deflection must still dispatch native run");
  pad.sThumbLY = 28000;
  UpdateControllerBaseBindings(pad, true, false);
  Require(!CommandActive(captured_commands, Command::kRun),
          "relaxing the stick must leave the new narrow run zone");
  pad = {};
  UpdateControllerBaseBindings(pad, true, false);
  pad.wButtons = XINPUT_GAMEPAD_B;
  UpdateControllerBaseBindings(pad, true, false);
  Require(captured_commands == CommandBit(Command::kCastSpell),
          "B must dispatch only the spell command in gameplay");

  for (const auto view : {CustomCameraViewMode::kModernThirdPerson,
                          CustomCameraViewMode::kHead,
                          CustomCameraViewMode::kRetail}) {
    g_custom_camera_view_mode.store(static_cast<uint32_t>(view));
    for (const WORD shoulder : {WORD{XINPUT_GAMEPAD_RIGHT_SHOULDER},
                               WORD{XINPUT_GAMEPAD_RIGHT_SHOULDER |
                                    XINPUT_GAMEPAD_LEFT_SHOULDER}}) {
      pad = {};
      pad.wButtons = shoulder;
      pad.sThumbLX = 32767;
      UpdateControllerBaseBindings(pad, true, false);
      Require(captured_commands == CommandBit(Command::kRightSidestep),
              "RB right must select native K, never a cautious forward driver");
      Require(!g_xinput_camera_relative_intent.load() &&
                  !g_immersive_xinput_movement_x_milli.load() &&
                  !g_immersive_xinput_movement_y_milli.load() &&
                  !g_xinput_native_joystick_x_milli.load() &&
                  !g_xinput_native_joystick_y_milli.load(),
              "RB strafe must not steer the body or redirect animation roots");
      pad.sThumbLX = -32768;
      UpdateControllerBaseBindings(pad, true, false);
      Require(captured_commands == CommandBit(Command::kLeftSidestep),
              "RB left must select native J only");
      pad.sThumbLX = 0;
      pad.sThumbLY = 32767;
      UpdateControllerBaseBindings(pad, true, false);
      Require(captured_commands == CommandBit(Command::kStepForward),
              "RB up must select original Ctrl+W");
      pad.sThumbLY = -32768;
      UpdateControllerBaseBindings(pad, true, false);
      Require(captured_commands == CommandBit(Command::kStepBackward),
              "RB down must select original Ctrl+S");
      pad.sThumbLX = 32767;
      pad.sThumbLY = 12000;
      UpdateControllerBaseBindings(pad, true, false);
      Require(captured_commands == CommandBit(Command::kRightSidestep),
              "horizontal-dominant input must not mix side and forward states");
      UpdateControllerBaseBindings(pad, true, true);
      Require(captured_commands == 0,
              "selector ownership must suppress all RB commands");
      pad.bRightTrigger = 255;
      UpdateControllerBaseBindings(pad, true, false);
      Require(captured_commands == 0,
              "RT must own direction input without simultaneous RB steps");
      pad = {};
      UpdateControllerBaseBindings(pad, true, false);
      Require(captured_commands == 0 && !g_xinput_camera_relative_intent.load(),
              "release must clear the strafe/step chord immediately");
    }
  }
  g_custom_camera_view_mode.store(
      static_cast<uint32_t>(CustomCameraViewMode::kModernThirdPerson));
  pad.wButtons = XINPUT_GAMEPAD_B;
  UpdateControllerBaseBindings(pad, true, true);
  Require(!CommandActive(captured_commands, Command::kCastSpell),
          "selector cancellation must not cast");
  UpdateControllerBaseBindings(pad, true, false);
  Require(!CommandActive(captured_commands, Command::kCastSpell),
          "holding B after cancellation must not cast");
  g_controller_cast_blocked_until_release = false;
  g_controller_selector.direction_down = true;
  g_controller_selector.row_open = true;
  g_controller_selector.cancelled = true;
  g_controller_selector.category = 1;
  UpdateControllerSelector(pad, true);  // B held; D-pad released simultaneously.
  UpdateControllerBaseBindings(pad, true, false);
  Require(!CommandActive(captured_commands, Command::kCastSpell) &&
              !g_controller_selector.direction_down,
          "presentation selector closure must not leak held B into a spell");
  pad.wButtons = 0;
  UpdateControllerBaseBindings(pad, true, false);
  pad.wButtons = XINPUT_GAMEPAD_B;
  UpdateControllerBaseBindings(pad, true, false);
  Require(CommandActive(captured_commands, Command::kCastSpell),
          "fresh B after cancellation must cast normally");
  UpdateControllerBaseBindings(pad, false, false);
  Require(CommandActive(captured_commands, Command::kMenu) &&
              !CommandActive(captured_commands, Command::kCastSpell),
          "B must retain frontend Back without magic");
  UpdateControllerBaseBindings(pad, true, false);
  Require(!CommandActive(captured_commands, Command::kCastSpell),
          "holding menu Back into gameplay must not cast");
  ReleaseInjectedControllerInput();
  Require(captured_commands == 0, "focus-loss/disconnect must release commands");
  std::cout << "RB native J/K and Ctrl+W/S, release and B ownership passed\n";
}
