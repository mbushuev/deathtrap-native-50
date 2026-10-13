#include <windows.h>
#include <cstdarg>
#include <filesystem>
#include <iostream>

bool test_foreground = true;
HWND WINAPI TestForegroundWindow() {
  return test_foreground ? reinterpret_cast<HWND>(1) : nullptr;
}
DWORD WINAPI TestWindowProcess(HWND, LPDWORD process) {
  if (process) *process = GetCurrentProcessId();
  return 1;
}
UINT WINAPI TestSystemInput(UINT count, LPINPUT, int) { return count; }
#define GetForegroundWindow TestForegroundWindow
#define GetWindowThreadProcessId TestWindowProcess
#define SendInput TestSystemInput
#include "../src/deathtrap_native_render_patch.cc"
#undef GetForegroundWindow
#undef GetWindowThreadProcessId
#undef SendInput

namespace {
void Require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "Controller UI test failed: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

deathtrap::camera::RecenterContext Eligible() {
  deathtrap::camera::RecenterContext context;
  context.gameplay = context.foreground = context.modern_third_person =
      context.orbit_engaged = true;
  return context;
}

void CheckRecenter() {
  for (int32_t heading = 0; heading < kPlayerHeadingUnitsPerTurn; ++heading) {
    const auto target = deathtrap::camera::BehindActorRecenterTarget(
        heading, kPlayerHeadingUnitsPerTurn, 1400.0);
    Require(target.valid, "every actor heading must have a valid rear target");
    const auto forward = CameraRelativeHeadingFromOrbit(
        target.yaw, 0.0, 1.0, kPlayerHeadingUnitsPerTurn, true);
    Require(forward.valid && forward.heading == heading,
            "after reset, third-person stick-up/W must keep the actor's course");
    const double immersive_yaw = ImmersiveOrbitYawFromPlayerHeading(
        heading, kPlayerHeadingUnitsPerTurn);
    const auto head_view = BuildImmersiveFirstPersonPose(
        {0, 0, 0}, immersive_yaw, 0.0, 0, 0);
    const auto visible_forward = ImmersiveFirstPersonLookAtVector(head_view.forward);
    const double horizontal = 1400.0 * std::cos(target.pitch);
    const double rear_dot = horizontal * (
        std::sin(target.yaw) * visible_forward[0] +
        std::cos(target.yaw) * visible_forward[2]);
    Require(rear_dot < -1300.0,
            "third-person endpoint must be behind the actual head-view direction");
    Require(std::abs(1400.0 * std::sin(target.pitch) - 336.0) < 1.0e-8 &&
                target.pitch < 15.0 * kOrbitPi / 180.0,
            "default framing must be just above the head, not a high overhead tilt");
  }
  for (const double radius : {650.0, 1400.0, 1800.0}) {
    const auto target = deathtrap::camera::BehindActorRecenterTarget(
        -128, kPlayerHeadingUnitsPerTurn, radius, 210.0);
    Require(target.valid && std::abs(radius * std::sin(target.pitch) - 306.0) < 1.0e-8,
            "camera height must stay just above the resolved head at each orbit distance");
  }
  Require(!deathtrap::camera::BehindActorRecenterTarget(0, 0, 1400).valid &&
              !deathtrap::camera::BehindActorRecenterTarget(0, 1024, 0).valid,
          "invalid actor units or orbit radius must not create a reset target");
  auto context = Eligible();
  deathtrap::camera::Recenter state;
  double yaw = 179.0 * kOrbitPi / 180.0;
  double pitch = 0.5;
  const double target = -179.0 * kOrbitPi / 180.0;
  Require(state.Begin(context, yaw, pitch, target, 0.2, 0.42),
          "wraparound recenter must start");
  double previous = yaw;
  for (int tick = 0; tick < 7; ++tick) {
    Require(state.Advance(context, 0.06, &yaw, &pitch),
            "recenter must advance on each native source tick");
    const double step = std::remainder(yaw - previous, 2.0 * kOrbitPi);
    Require(step >= 0 && step < 0.01,
            "179 to -179 must take the two-degree arc, not a full revolution");
    previous = yaw;
  }
  Require(!state.Active() && std::abs(yaw - target) < 1.0e-8 &&
              std::abs(pitch - 0.2) < 1.0e-8,
          "recenter must finish at the exact heading and default tilt");
  for (int mode = 0; mode < 8; ++mode) {
    context = Eligible();
    Require(state.Begin(context, 0.0, 0.0, 2.0, 0.3, 0.42),
            "recenter must start before cancellation");
    switch (mode) {
      case 0: context.gameplay = false; break;
      case 1: context.foreground = false; break;
      case 2: context.modern_third_person = false; break;
      case 3: context.orbit_engaged = false; break;
      case 4: context.selector = true; break;
      case 5: context.scripted = true; break;
      case 6: context.first_person = true; break;
      default: context.manual_look = true; break;
    }
    yaw = 0.4; pitch = 0.1;
    Require(!state.Advance(context, 0.06, &yaw, &pitch) &&
                !state.Active() && yaw == 0.4 && pitch == 0.1,
            "every excluded owner must cancel without writing angles");
    Require(!state.Begin(context, 0.0, 0.0, 2.0, 0.3, 0.42),
            "excluded owners must not accept a pending reset");
  }
}

struct TextCommand { int32_t x; int32_t y; std::string text; };
std::vector<TextCommand> text_commands;
std::vector<NativeInventorySlotDrawState> drawn_slots;
void __cdecl SetFont(uint32_t font) {
  Require(font == 0x1112u, "radial names must use the original yellow gameplay font");
  const uintptr_t marker = 0x1234u;
  SafeWrite(g_dungeon_base + kCurrentFontPointerRva, &marker, 4);
}
int32_t __cdecl TextWidth(const char* text) {
  return static_cast<int32_t>(std::strlen(text)) * 5;
}
void __cdecl QueueText(uint32_t flags, int32_t x, int32_t y, int32_t z,
                       const char* format, ...) {
  Require(flags == 1u && z == 0 && std::strcmp(format, "%s") == 0,
          "native text must be queued without treating a name as a format string");
  char text[128]{};
  va_list args;
  va_start(args, format);
  std::vsnprintf(text, sizeof(text), format, args);
  va_end(args);
  text_commands.push_back({x, y, text});
}
void __cdecl DrawSlot(void* slot) {
  drawn_slots.push_back(*static_cast<NativeInventorySlotDrawState*>(slot));
}

void CheckRuntime() {
  std::vector<uint8_t> image(0x368000);
  g_dungeon_base = image.data();
  g_dungeon_image_variant = DungeonImageVariant::kFrenchGog;
  std::array<std::string, 30> names{};
  names.fill("Original 100% localized name");
  names[22] = "Potion de sant\x82";
  for (size_t index = 0; index < names.size(); ++index) {
    const uintptr_t address = reinterpret_cast<uintptr_t>(names[index].c_str());
    SafeWrite(g_dungeon_base + kInventoryNamesRva + index * 4u, &address, 4);
  }
  std::array<int32_t, 10> viewport{};
  viewport[6] = 0; viewport[7] = 0; viewport[8] = 640; viewport[9] = 480;
  const uintptr_t viewport_ptr = reinterpret_cast<uintptr_t>(viewport.data());
  SafeWrite(g_dungeon_base + kUiViewportPointerRva, &viewport_ptr, 4);
  const uintptr_t original_font = 0x5678u;
  SafeWrite(g_dungeon_base + kCurrentFontPointerRva, &original_font, 4);
  g_selector_text_set_font = &SetFont;
  g_selector_text_width = &TextWidth;
  g_selector_text_queue = &QueueText;
  g_original_inventory_slot_draw = &DrawSlot;
  g_controller_selector.row_open = true;
  g_controller_selector.cancelled = false;
  g_controller_selector.category = 4;
  g_controller_selector.slot = 0;
  for (uint8_t index = 0; index < 8; ++index) {
    NativeInventorySlotDrawState slot{};
    slot.slot = index; slot.show_quantity = 1; slot.icon = index; slot.quantity = 3;
    const auto original = slot;
    HookInventorySlotDraw(&slot);
    Require(std::memcmp(&slot, &original, sizeof(slot)) == 0,
            "name rendering must not mutate native slot or quantity data");
  }
  Require(drawn_slots.size() == 8 && text_commands.size() == 1 &&
              text_commands[0].text == names[22],
          "the selected slot must queue one original accented name per radial draw");
  Require(text_commands[0].x + TextWidth(text_commands[0].text.c_str()) / 2 == 320,
          "radial name must be horizontally centered in the native viewport");
  Require(text_commands[0].y == 480 - 316 + 32 - 9,
          "radial name must match the actual upward-aligned cell center");
  uintptr_t restored_font = 0;
  SafeReadValue(g_dungeon_base + kCurrentFontPointerRva, &restored_font);
  Require(restored_font == original_font, "caller font must be restored");
  for (int scenario = 0; scenario < 4; ++scenario) {
    text_commands.clear();
    NativeInventorySlotDrawState slot{};
    slot.slot = 0; slot.show_quantity = 1; slot.icon = 1;
    g_controller_selector.row_open = true;
    g_controller_selector.cancelled = false;
    if (scenario == 0) slot.icon = 0xFFu;
    if (scenario == 1) slot.blank = 1;
    if (scenario == 2) g_controller_selector.row_open = false;
    if (scenario == 3) g_controller_selector.cancelled = true;
    HookInventorySlotDraw(&slot);
    Require(text_commands.empty(),
            "empty/unavailable/cancelled/nonradial selections must not render names");
  }
  g_controller_selector = {};
  g_controller_selector.row_open = true;
  g_controller_selector.category = 2; g_controller_selector.slot = 7;
  NativeInventorySlotDrawState chalk{}; chalk.slot = 7; chalk.show_quantity = 1; chalk.icon = 31;
  text_commands.clear(); HookInventorySlotDraw(&chalk);
  Require(text_commands.size() == 1 && text_commands[0].text == "Craie",
          "special chalk slot must use the game's language");
  g_controller_selector.category = 1; g_controller_selector.slot = 0;
  names[0] = "Marteau de Guerre Magique avec un tres tres tres long nom";
  const uintptr_t long_name = reinterpret_cast<uintptr_t>(names[0].c_str());
  SafeWrite(g_dungeon_base + kInventoryNamesRva, &long_name, 4);
  viewport[6] = -100; viewport[8] = 800;
  NativeInventorySlotDrawState long_slot{}; long_slot.show_quantity = 0; long_slot.icon = 16;
  g_centered_ui_context_compensation_x = 80;
  text_commands.clear(); HookInventorySlotDraw(&long_slot);
  Require(text_commands.size() == 2, "long names must use two bounded lines");
  for (const auto& command : text_commands)
    Require(TextWidth(command.text.c_str()) <= 144 &&
                command.x + TextWidth(command.text.c_str()) / 2 == 270,
            "long names must fit and compensate the widened text-context gutter");
  g_centered_ui_context_compensation_x = 0;

  // Real production Y-edge routing against a fake player/camera graph. No
  // input is injected into Windows and no engine entry point is executed.
  std::array<uint8_t, 0x200> player{};
  std::array<uint8_t, 0x100> actor{};
  std::array<uint8_t, 0x80> node{};
  uintptr_t link = reinterpret_cast<uintptr_t>(node.data());
  uintptr_t actor_ptr = reinterpret_cast<uintptr_t>(actor.data());
  uintptr_t player_ptr = reinterpret_cast<uintptr_t>(player.data());
  std::memcpy(player.data() + kPlayerMovementControllerOffset, &actor_ptr, 4);
  const uintptr_t link_ptr = reinterpret_cast<uintptr_t>(&link);
  std::memcpy(actor.data() + kPlayerRenderLinkOffset, &link_ptr, 4);
  const int32_t heading = 128;
  std::memcpy(node.data() + kPlayerHeadingOffset, &heading, 4);
  SafeWrite(g_dungeon_base + kUiOwnerPointerRva, &player_ptr, 4);
  std::array<uintptr_t, 12> root{};
  std::array<uint8_t, 0x1100> gameplay{};
  root[0] = 1;
  root[11] = reinterpret_cast<uintptr_t>(gameplay.data());
  uintptr_t dummy = 1;
  std::memcpy(gameplay.data() + 0x1030, &dummy, 4);
  const uintptr_t root_ptr = reinterpret_cast<uintptr_t>(root.data());
  SafeWrite(g_dungeon_base + kGameRootPointerRva, &root_ptr, 4);
  g_controller_selector = {};
  g_third_person_orbit_enabled = true;
  g_third_person_orbit_min_pitch_radians = -35.0 * kOrbitPi / 180.0;
  g_third_person_orbit_max_pitch_radians = 55.0 * kOrbitPi / 180.0;
  g_xinput_enabled = g_xinput_base_bindings = true;
  g_xinput_menu_mode.store(false);
  g_custom_camera_view_mode.store(0);
  g_third_person_orbit_state = {};
  g_third_person_orbit_state.engaged = true;
  g_third_person_orbit_state.pitch = 55.0 * kOrbitPi / 180.0;
  g_third_person_orbit_state.radius = 1400;
  g_third_person_orbit_state.collision_radius = 700;
  g_third_person_orbit_state.filtered_input_x = 0.2;
  XINPUT_GAMEPAD pad{};
  g_previous_xinput_buttons = 0;
  UpdateControllerBaseBindings(pad, true, false);
  pad.wButtons = XINPUT_GAMEPAD_Y;
  UpdateControllerBaseBindings(pad, true, false);
  Require(g_camera_recenter.Active() &&
              g_third_person_orbit_state.filtered_input_x == 0,
          "valid Y edge must schedule camera recenter and clear stale stick drift");
  const auto pose = g_third_person_orbit_state;
  Require(AdvanceCameraRecenterSourceTick(true, false),
          "real source routing must advance the recenter");
  Require(g_third_person_orbit_state.yaw != pose.yaw &&
              g_third_person_orbit_state.radius == pose.radius &&
              g_third_person_orbit_state.collision_radius == pose.collision_radius,
          "recenter must change only camera angles, never collision radius");
  const double before = g_third_person_orbit_state.yaw;
  UpdateControllerBaseBindings(pad, true, false);
  Require(g_camera_recenter.Active() && g_third_person_orbit_state.yaw == before,
          "releasing or holding Y without a new edge must not restart progress");
  int32_t after_heading = 0;
  Require(ReadLivePlayerHeading(&after_heading) && after_heading == heading,
          "Y must never turn the actor");
  for (int tick = 1; tick < 7; ++tick)
    Require(AdvanceCameraRecenterSourceTick(true, false),
            "Y reset must finish through the real source routing");
  const auto reset_forward = CameraRelativeHeadingFromOrbit(
      g_third_person_orbit_state.yaw, 0.0, 1.0, kPlayerHeadingUnitsPerTurn, true);
  Require(!g_camera_recenter.Active() && reset_forward.valid &&
              reset_forward.heading == heading &&
              std::abs(1400.0 * std::sin(g_third_person_orbit_state.pitch) - 336.0) < 1.0e-8,
          "real Y reset must end behind the actor above head height, ignoring the old steep pitch");
  pad.wButtons = 0;
  UpdateControllerBaseBindings(pad, true, false);
  g_third_person_orbit_state.yaw = 0.0;
  pad.wButtons = XINPUT_GAMEPAD_Y;
  UpdateControllerBaseBindings(pad, true, false);
  g_camera_recenter.Cancel();
  UpdateControllerBaseBindings(pad, true, false);
  Require(!g_camera_recenter.Active(),
          "holding Y after cancellation must not retrigger until released");
  pad.wButtons = 0;
  UpdateControllerBaseBindings(pad, true, false);
  pad.wButtons = XINPUT_GAMEPAD_Y;
  UpdateControllerBaseBindings(pad, true, false);
  Require(g_camera_recenter.Active(), "release and new Y edge must retrigger");
  UpdateControllerBaseBindings(pad, false, false);
  Require(!g_camera_recenter.Active(), "entering the frontend must cancel Y");
  g_xinput_menu_mode.store(false);
  UpdateControllerBaseBindings(pad, true, false);
  Require(!g_camera_recenter.Active(),
          "holding menu Y into gameplay must not carry a pending reset");
  pad.wButtons = 0;
  UpdateControllerBaseBindings(pad, true, false);
  for (int subframes : {1, 3}) {
    g_camera_recenter.Begin(CameraRecenterContext(false),
                            0.0, 0.8, kOrbitPi, 0.1, 0.42);
    int ticks = 0;
    while (g_camera_recenter.Active() && ++ticks < 20) {
      Require(AdvanceCameraRecenterSourceTick(true, false),
              "source reset must advance in both presentation modes");
      const double source_yaw = g_third_person_orbit_state.yaw;
      const double source_pitch = g_third_person_orbit_state.pitch;
      for (int pass = 0; pass < subframes; ++pass) {
        Require(!AdvanceCameraRecenterSourceTick(false, false) &&
                    g_third_person_orbit_state.yaw == source_yaw &&
                    g_third_person_orbit_state.pitch == source_pitch,
                "synthetic presentations must not advance reset or change source angles");
      }
    }
    Require(ticks == 7, "reset must take 420ms in 16.7 and 50 FPS modes");
  }
  // Exercise the real animated-head resolver, not only a supplied height.
  // root -> branch -> chest (three children) -> neck -> head, depth four.
  std::array<std::array<uint8_t, 0x100>, 7> skeleton{};
  std::array<uintptr_t, 7> joints{};
  for (size_t i = 0; i < joints.size(); ++i)
    joints[i] = reinterpret_cast<uintptr_t>(skeleton[i].data());
  const std::array<size_t, 7> parents{0, 0, 1, 2, 3, 2, 2};
  g_previous_snapshot = {};
  g_previous_snapshot.player = joints[0];
  g_previous_snapshot.camera_focus = {0, 1400, 0};
  g_previous_snapshot.camera_focus_valid = true;
  for (size_t i = 0; i < joints.size(); ++i) {
    NodeTransform transform;
    transform.parent = i == 0 ? 0 : joints[parents[i]];
    transform.world.values[10] = i == 4 ? 1600 : 1000;
    transform.bounds_radius = i == 4 ? 76 : 0;
    transform.bounds_valid = i == 4;
    g_previous_snapshot.nodes.emplace(joints[i], transform);
    std::memcpy(skeleton[i].data() + kMatrixOffset,
                &transform.world, sizeof(transform.world));
  }
  std::array<uint8_t, 0x280> camera_controller{};
  const std::array<int32_t, 3> focus{0, 1400, 0};
  std::memcpy(camera_controller.data() + kCameraControllerFocusOffset,
              focus.data(), sizeof(focus));
  g_third_person_orbit_state.controller = camera_controller.data();
  for (bool limited_pitch : {false, true}) {
    g_third_person_orbit_state.yaw = 0.0;
    g_third_person_orbit_state.pitch = 0.8;
    g_third_person_orbit_max_pitch_radians =
        limited_pitch ? 0.1 : 55.0 * kOrbitPi / 180.0;
    HandleControllerCameraRecenter(XINPUT_GAMEPAD_Y, 0, 0, false);
    for (int tick = 0; tick < 7; ++tick)
      Require(AdvanceCameraRecenterSourceTick(true, false),
              "live-head reset must advance through the production Y handler");
    Require(!g_camera_recenter.Active() &&
                (limited_pitch ?
                    std::abs(g_third_person_orbit_state.pitch - 0.1) < 1.0e-8 :
                    std::abs(focus[1] + 1400.0 *
                        std::sin(g_third_person_orbit_state.pitch) - 1696.0) < 1.0e-8),
            "Y must use the resolved head plus clearance while respecting configured pitch limits");
  }
  g_third_person_orbit_max_pitch_radians = 55.0 * kOrbitPi / 180.0;
  g_third_person_orbit_state.controller = nullptr;
  g_previous_snapshot = {};
  HandleControllerCameraRecenter(XINPUT_GAMEPAD_Y, 0, 0, false);
  HandleControllerCameraRecenter(0, 0.5, 0, false);
  Require(!g_camera_recenter.Active(), "manual stick must cancel immediately");
  HandleControllerCameraRecenter(XINPUT_GAMEPAD_Y, 0, 0, false);
  Require(g_camera_recenter.Active(), "camera must be ready for physical mouse cancellation");
  Require(!AdvanceCameraRecenterSourceTick(false, true) &&
              !g_camera_recenter.Active(),
          "physical mouse input must cancel even without a fresh controller sample");
  for (int scenario = 0; scenario < 6; ++scenario) {
    g_controller_selector = {};
    g_xinput_menu_mode.store(false);
    g_custom_camera_view_mode.store(0);
    g_scripted_camera_override_active.store(false);
    test_foreground = true;
    if (scenario == 0) g_controller_selector.row_open = true;
    if (scenario == 1) g_xinput_menu_mode.store(true);
    if (scenario == 2) g_scripted_camera_override_active.store(true);
    if (scenario == 3) test_foreground = false;
    if (scenario == 4) g_custom_camera_view_mode.store(1);
    HandleControllerCameraRecenter(XINPUT_GAMEPAD_Y, 0, 0, scenario == 5);
    Require(!g_camera_recenter.Active(),
            "production routing must reject menus, selectors, focus loss and other cameras");
  }
  g_dungeon_base = nullptr;
}

int32_t __cdecl NoNativeKeyPress(uint32_t) { return 0; }
std::array<std::array<uint8_t, 2>, 41> owned_items{};
void* __cdecl TestInventoryLookup(int32_t item) {
  return item >= 0 && static_cast<size_t>(item) < owned_items.size()
      ? owned_items[static_cast<size_t>(item)].data() : nullptr;
}

int ReportNativeRowException(EXCEPTION_POINTERS* exception) {
  std::cerr << "Native row exception RVA=0x" << std::hex
            << reinterpret_cast<uintptr_t>(exception->ExceptionRecord->ExceptionAddress) -
                   reinterpret_cast<uintptr_t>(g_dungeon_base)
            << " accessed=0x" << exception->ExceptionRecord->ExceptionInformation[1]
            << std::dec << " category=" << g_controller_selector.category
            << " slot=" << g_controller_selector.slot << '\n';
  return EXCEPTION_EXECUTE_HANDLER;
}

void CallNativeRowForTest(void(__cdecl* render)(void*)) {
  __try { render(nullptr); }
  __except (ReportNativeRowException(GetExceptionInformation())) {
    Require(false, "real selector producer must not access unresolved runtime state");
  }
}

void CheckNativeSelectorRows() {
  // Execute the ORIGINAL row producers, not hand-built slot structs. This
  // catches the melee quantity flag that the previous synthetic fixture
  // incorrectly treated as an availability flag. Only input/graphics leaves
  // are replaced; no DLL entry points or engine imports are executed.
  g_selector_text_set_font = &SetFont;
  g_selector_text_width = &TextWidth;
  g_selector_text_queue = &QueueText;
  g_original_inventory_slot_draw = &DrawSlot;
  for (auto& item : owned_items) item = {1, 3};
  struct TestHook { uintptr_t rva; void* detour; };
  const std::array<TestHook, 3> hooks = {{
      {kInventorySlotDrawRva, reinterpret_cast<void*>(&HookInventorySlotDraw)},
      {kKeyboardBindingKeyPressedRva, reinterpret_cast<void*>(&NoNativeKeyPress)},
      {kInventoryLookupRva, reinterpret_cast<void*>(&TestInventoryLookup)}}};
  for (const auto& hook : hooks) {
    Require(MH_CreateHook(g_dungeon_base + hook.rva, hook.detour, nullptr) == MH_OK &&
                MH_EnableHook(g_dungeon_base + hook.rva) == MH_OK,
            "real native selector leaves must be hooked safely for the test");
  }
  std::array<int32_t, 10> viewport{};
  viewport[8] = 960; viewport[9] = 600;
  const uintptr_t viewport_pointer = reinterpret_cast<uintptr_t>(viewport.data());
  SafeWrite(g_dungeon_base + kUiViewportPointerRva, &viewport_pointer, 4);
  const uintptr_t font_marker = 0x5678;
  SafeWrite(g_dungeon_base + kCurrentFontPointerRva, &font_marker, 4);
  g_centered_ui_context_compensation_x = 80;
  g_controller_selector = {};
  g_controller_selector.row_open = true;
  constexpr std::array<uintptr_t, 4> row_rvas = {
      0x76F30u, 0x773A0u, 0x777C0u, 0x77C00u};
  using AlignFn = void(__cdecl*)(int16_t*, int16_t*, int32_t, int32_t);
  const auto align = reinterpret_cast<AlignFn>(
      g_dungeon_base + ActiveDungeonCodeRva(0x76630u));
  for (uint32_t category = 1; category <= 4; ++category) {
    g_controller_selector.category = category;
    const auto render = reinterpret_cast<void(__cdecl*)(void*)>(
        g_dungeon_base + ActiveDungeonCodeRva(row_rvas[category - 1]));
    for (uint32_t selected = 0; selected < 8; ++selected) {
      g_controller_selector.slot = selected;
      drawn_slots.clear(); text_commands.clear();
      CallNativeRowForTest(render);
      Require(drawn_slots.size() == 8, "real retail row must draw eight slots");
      const auto& native_slot = drawn_slots[selected];
      if (native_slot.icon == 0xFFu || native_slot.blank) {
        Require(text_commands.empty(), "real empty native slot must have no name");
        continue;
      }
      Require(!text_commands.empty(),
              "every real owned icon, including quantity-less weapons, must have a name");
      if (category == 1) {
        Require(native_slot.show_quantity == 0,
                "real melee slots must test the original zero quantity flag");
      }
      int16_t ax = drawn_slots[0].x, ay = drawn_slots[0].y;
      int16_t bx = drawn_slots[4].x, by = drawn_slots[4].y;
      align(&ax, &ay, 2, 1); align(&bx, &by, 2, 1);
      const int32_t actual_ring_x = (ax + bx) / 2 + 16;
      const int32_t actual_ring_y = (ay + by) / 2 + 16;
      const auto& first = text_commands[0];
      for (const auto& text : text_commands) {
        Require(text.x + TextWidth(text.text.c_str()) / 2 +
                    g_centered_ui_context_compensation_x == actual_ring_x,
                "text's final widescreen center must match the actual native sprite ring");
      }
      const int32_t lines = static_cast<int32_t>(text_commands.size());
      Require(first.y + lines * 18 / 2 == actual_ring_y,
              "text's vertical center must match original native aligned cell bounds");
    }
  }
  for (const auto& hook : hooks) {
    MH_DisableHook(g_dungeon_base + hook.rva);
    MH_RemoveHook(g_dungeon_base + hook.rva);
  }
  g_centered_ui_context_compensation_x = 0;
}

void CheckOriginalNames(const std::filesystem::path& path) {
  HMODULE image = LoadLibraryExW(path.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
  Require(image != nullptr, "original DLL must map");
  g_dungeon_base = reinterpret_cast<uint8_t*>(image);
  g_dungeon_image_variant = IdentifyDungeonImage(g_dungeon_base);
  Require(g_dungeon_image_variant != DungeonImageVariant::kUnsupported,
          "original name table must use a verified compatibility profile");
  for (uint32_t category = 1; category <= 4; ++category) {
    for (uint32_t slot = 0; slot < 8; ++slot) {
      std::array<char, 128> name{};
      const auto item = deathtrap::selector_text::ItemForSlot(category, slot);
      const bool read = ReadControllerSelectorName(category, slot, &name);
      if (item == deathtrap::selector_text::kEmptyItem) {
        Require(!read, "empty ranged slot must not index a neighboring object");
      } else if (item == deathtrap::selector_text::kChalkItem) {
        Require(read && std::strcmp(name.data(),
            deathtrap::selector_text::ChalkName(ActiveGameTextLanguage())) == 0,
            "chalk must be localized separately from actual inventory IDs");
      } else {
        uintptr_t pointer = 0;
        SafeReadValue(g_dungeon_base + kInventoryNamesRva + item * 4u, &pointer);
        Require(read && std::strcmp(name.data(),
            reinterpret_cast<const char*>(pointer)) == 0,
            "every slot must preserve its exact original localized pickup name");
      }
    }
  }
  CheckNativeSelectorRows();
  std::cout << "Original radial item names: " << ActiveDungeonProfileName()
            << " passed\n";
  g_dungeon_base = nullptr; FreeLibrary(image);
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
  CheckRecenter();
  CheckRuntime();
  Require(MH_Initialize() == MH_OK, "native row test hooks must initialize");
  for (int index = 1; index < argc; ++index) CheckOriginalNames(argv[index]);
  MH_Uninitialize();
  std::cout << "Radial localized text, wrapping, Y recenter and ownership passed\n";
}
