#include "deathtrap_modern_mouse.h"

#include <windows.h>

#include <MinHook.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cwctype>
#include <cstring>
#include <mutex>
#include <string>

namespace {

constexpr DWORD kExpectedDungeonTimestamp = 0x35752434u;
constexpr DWORD kExpectedDungeonImageSize = 0x00367000u;

constexpr uintptr_t kInputUpdateRva = 0x0005DC40u;
constexpr uintptr_t kGameplayInputStageRva = 0x00040420u;
constexpr uintptr_t kActionBindingsRva = 0x001F4B30u;
constexpr uintptr_t kMouseSourcesRva = 0x000DF9D8u;
constexpr uintptr_t kMouseStateRva = 0x001F6B20u;
constexpr uintptr_t kActionStateRva = 0x001F6B40u;
constexpr uintptr_t kPlayerPointerRva = 0x0034F9D0u;

// The definition table stores zero-based public action IDs, but the runtime
// binding map reserves slot zero and returns public ID + 1 from its name
// lookup. These are therefore the runtime indices, not the printed IDs.
constexpr uint32_t kActionLeftSidestep = 7u;
constexpr uint32_t kActionRightSidestep = 8u;
constexpr uint32_t kActionTurnLeft = 9u;
constexpr uint32_t kActionTurnRight = 10u;
constexpr uint32_t kActionAttackRanged = 13u;
constexpr uint32_t kActionAttack1 = 14u;
constexpr uint32_t kActionParry = 18u;

constexpr uint32_t kMouseLeftButton = 0u;
constexpr uint32_t kMouseRightButton = 1u;
constexpr uint32_t kMouseHorizontalLeft = 2u;
constexpr uint32_t kMouseHorizontalRight = 3u;

constexpr uint32_t kBindingModeDown = 1u;
constexpr size_t kMouseSourceStride = 0x44u;
constexpr size_t kMaximumBindingsPerAction = 5u;

#pragma pack(push, 1)
struct NativeActionBinding {
  uint32_t source_count;
  std::array<uintptr_t, 5> sources;
  uint32_t mode;
};

struct NativeActionBindings {
  uint32_t binding_count;
  std::array<NativeActionBinding, kMaximumBindingsPerAction> bindings;
};

struct NativeActionState {
  uint32_t state;
  int32_t magnitude;
};
#pragma pack(pop)

static_assert(sizeof(NativeActionBinding) == 0x1Cu);
static_assert(sizeof(NativeActionBindings) == 0x90u);

using InputUpdateFn = void(__cdecl*)();
using GameplayInputStageFn = void(__cdecl*)();

enum class ControlMode : uint32_t {
  kClassic,
  kModern,
};

std::once_flag g_initialize_once;
std::atomic<bool> g_enabled{false};
std::atomic<bool> g_turn_enabled{false};
std::atomic<bool> g_attack_enabled{false};
std::atomic<bool> g_parry_enabled{false};
std::atomic<bool> g_repair_legacy_bindings{false};
std::atomic<bool> g_debug_log{false};
std::atomic<bool> g_hook_installed{false};
std::atomic<ControlMode> g_control_mode{ControlMode::kClassic};
std::atomic<bool> g_modern_wasd{false};
std::atomic<bool> g_invert_x{false};
std::atomic<bool> g_block_turn_during_combat{true};
std::atomic<int32_t> g_pending_mouse_dx{0};
uint8_t* g_dungeon_base = nullptr;
InputUpdateFn g_original_input_update = nullptr;
GameplayInputStageFn g_original_gameplay_input_stage = nullptr;
double g_sensitivity_degrees = 0.08;
double g_max_degrees_per_tick = 35.0;
double g_fractional_angle_units = 0.0;
int32_t g_jitter_threshold = 0;
uintptr_t g_left_key_source = 0;
uintptr_t g_right_key_source = 0;
uint64_t g_input_tick = 0;
int32_t g_previous_left_button = 0;
int32_t g_previous_right_button = 0;
std::mutex g_log_mutex;

std::wstring ModuleDirectory() {
  HMODULE module = nullptr;
  GetModuleHandleExW(
      GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
          GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
      reinterpret_cast<LPCWSTR>(&ModuleDirectory), &module);
  wchar_t path[MAX_PATH] = {};
  const DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
  if (!length || length >= MAX_PATH) {
    return L".";
  }
  std::wstring directory(path, length);
  const size_t slash = directory.find_last_of(L"\\/");
  return slash == std::wstring::npos ? L"." : directory.substr(0, slash);
}

bool Configured(const wchar_t* key, bool default_value) {
  const std::wstring path = ModuleDirectory() + L"\\deathtrap_native.ini";
  return GetPrivateProfileIntW(L"ModernMouse", key, default_value ? 1 : 0,
                               path.c_str()) != 0;
}

std::wstring ConfiguredString(const wchar_t* key, const wchar_t* value) {
  const std::wstring path = ModuleDirectory() + L"\\deathtrap_native.ini";
  wchar_t buffer[64] = {};
  GetPrivateProfileStringW(L"ModernMouse", key, value, buffer,
                           static_cast<DWORD>(std::size(buffer)), path.c_str());
  return buffer;
}

double ConfiguredDouble(const wchar_t* key, double default_value) {
  wchar_t fallback[32] = {};
  swprintf_s(fallback, L"%.6f", default_value);
  const std::wstring value = ConfiguredString(key, fallback);
  wchar_t* end = nullptr;
  const double parsed = wcstod(value.c_str(), &end);
  return end != value.c_str() && std::isfinite(parsed) ? parsed
                                                       : default_value;
}

bool ConfiguredDiagnostic(const wchar_t* key, bool default_value) {
  const std::wstring path = ModuleDirectory() + L"\\deathtrap_native.ini";
  return GetPrivateProfileIntW(L"Diagnostics", key, default_value ? 1 : 0,
                               path.c_str()) != 0;
}

void AppendInputLog(const char* format, ...) {
  if (!g_debug_log.load(std::memory_order_relaxed)) {
    return;
  }
  std::lock_guard<std::mutex> lock(g_log_mutex);
  FILE* file = nullptr;
  const std::wstring path = ModuleDirectory() + L"\\deathtrap_native_input.log";
  if (_wfopen_s(&file, path.c_str(), L"ab") != 0 || !file) {
    return;
  }
  va_list arguments;
  va_start(arguments, format);
  vfprintf(file, format, arguments);
  va_end(arguments);
  fclose(file);
}

bool IsExpectedDungeonImage(uint8_t* base) {
  if (!base) {
    return false;
  }
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) {
    return false;
  }
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(
      base + static_cast<uintptr_t>(dos->e_lfanew));
  return nt->Signature == IMAGE_NT_SIGNATURE &&
         nt->FileHeader.TimeDateStamp == kExpectedDungeonTimestamp &&
         nt->OptionalHeader.SizeOfImage == kExpectedDungeonImageSize;
}

NativeActionBindings* ActionBindings(uint32_t action) {
  auto* table = reinterpret_cast<NativeActionBindings*>(
      g_dungeon_base + kActionBindingsRva);
  return &table[action];
}

uintptr_t MouseSource(uint32_t source_index) {
  return reinterpret_cast<uintptr_t>(g_dungeon_base + kMouseSourcesRva +
                                     source_index * kMouseSourceStride);
}

bool BindingContainsSource(const NativeActionBinding& binding,
                           uintptr_t source) {
  const uint32_t count =
      binding.source_count > binding.sources.size()
          ? static_cast<uint32_t>(binding.sources.size())
          : binding.source_count;
  for (uint32_t index = 0; index < count; ++index) {
    if (binding.sources[index] == source) {
      return true;
    }
  }
  return false;
}

bool IsMouseSource(uintptr_t source) {
  for (uint32_t index = 0; index <= kMouseHorizontalRight; ++index) {
    if (source == MouseSource(index)) {
      return true;
    }
  }
  return false;
}

uintptr_t FindSingleKeyboardSource(uint32_t action) {
  const NativeActionBindings* action_bindings = ActionBindings(action);
  const uint32_t count = std::min<uint32_t>(
      action_bindings->binding_count,
      static_cast<uint32_t>(action_bindings->bindings.size()));
  for (uint32_t index = 0; index < count; ++index) {
    const NativeActionBinding& binding = action_bindings->bindings[index];
    if (binding.mode == kBindingModeDown && binding.source_count == 1u &&
        binding.sources[0] && !IsMouseSource(binding.sources[0])) {
      return binding.sources[0];
    }
  }
  return 0;
}

bool EnsureBinding(uint32_t action, uintptr_t source) {
  NativeActionBindings* action_bindings = ActionBindings(action);
  const uint32_t count =
      action_bindings->binding_count > action_bindings->bindings.size()
          ? static_cast<uint32_t>(action_bindings->bindings.size())
          : action_bindings->binding_count;
  for (uint32_t index = 0; index < count; ++index) {
    const NativeActionBinding& binding = action_bindings->bindings[index];
    if (binding.mode == kBindingModeDown && binding.source_count == 1u &&
        BindingContainsSource(binding, source)) {
      return true;
    }
  }
  if (count >= action_bindings->bindings.size()) {
    return false;
  }

  NativeActionBinding replacement{};
  replacement.source_count = 1u;
  replacement.sources[0] = source;
  replacement.mode = kBindingModeDown;

  // This table is writable engine state. Publish the completed binding before
  // increasing the count so the input evaluator never sees a partial entry.
  action_bindings->bindings[count] = replacement;
  MemoryBarrier();
  action_bindings->binding_count = count + 1u;
  return true;
}

bool RemoveBinding(uint32_t action, uintptr_t source) {
  NativeActionBindings* action_bindings = ActionBindings(action);
  uint32_t count =
      action_bindings->binding_count > action_bindings->bindings.size()
          ? static_cast<uint32_t>(action_bindings->bindings.size())
          : action_bindings->binding_count;
  bool removed = false;
  for (uint32_t index = 0; index < count;) {
    const NativeActionBinding& binding = action_bindings->bindings[index];
    if (binding.mode != kBindingModeDown || binding.source_count != 1u ||
        binding.sources[0] != source) {
      ++index;
      continue;
    }
    for (uint32_t move = index + 1u; move < count; ++move) {
      action_bindings->bindings[move - 1u] = action_bindings->bindings[move];
    }
    action_bindings->bindings[count - 1u] = {};
    --count;
    removed = true;
  }
  if (removed) {
    MemoryBarrier();
    action_bindings->binding_count = count;
  }
  return removed;
}

void RepairBindingsFromVersion016() {
  if (!g_repair_legacy_bindings.load(std::memory_order_relaxed)) {
    return;
  }
  const bool sidestep = RemoveBinding(
      kActionRightSidestep, MouseSource(kMouseHorizontalLeft));
  const bool reversed_turn =
      RemoveBinding(kActionTurnLeft, MouseSource(kMouseHorizontalRight));
  const bool ranged_attack =
      RemoveBinding(kActionAttackRanged, MouseSource(kMouseLeftButton));
  if (sidestep || reversed_turn || ranged_attack) {
    AppendInputLog(
        "legacy-repair sidestep=%u reversed-turn=%u ranged-attack=%u\r\n",
        sidestep ? 1u : 0u, reversed_turn ? 1u : 0u,
        ranged_attack ? 1u : 0u);
  }
}

void EnsureModernMouseBindings() {
  if (!g_enabled.load(std::memory_order_relaxed) || !g_dungeon_base) {
    return;
  }
  // 0.0.16 used definition-table IDs as runtime indices. The retail game
  // persists runtime mappings to keys.cfg, so remove those exact bad pairs
  // whenever the engine rebuilds its table.
  RepairBindingsFromVersion016();
  const bool modern =
      g_control_mode.load(std::memory_order_relaxed) == ControlMode::kModern;
  if (g_turn_enabled.load(std::memory_order_relaxed)) {
    if (modern) {
      RemoveBinding(kActionTurnLeft, MouseSource(kMouseHorizontalLeft));
      RemoveBinding(kActionTurnRight, MouseSource(kMouseHorizontalRight));
    } else {
      EnsureBinding(kActionTurnLeft, MouseSource(kMouseHorizontalLeft));
      EnsureBinding(kActionTurnRight, MouseSource(kMouseHorizontalRight));
    }
  }

  if (g_modern_wasd.load(std::memory_order_relaxed)) {
    if (!g_left_key_source) {
      g_left_key_source = FindSingleKeyboardSource(kActionTurnLeft);
      if (!g_left_key_source) {
        g_left_key_source = FindSingleKeyboardSource(kActionLeftSidestep);
      }
    }
    if (!g_right_key_source) {
      g_right_key_source = FindSingleKeyboardSource(kActionTurnRight);
      if (!g_right_key_source) {
        g_right_key_source = FindSingleKeyboardSource(kActionRightSidestep);
      }
    }
    if (g_left_key_source && g_right_key_source) {
      if (modern) {
        RemoveBinding(kActionTurnLeft, g_left_key_source);
        RemoveBinding(kActionTurnRight, g_right_key_source);
        EnsureBinding(kActionLeftSidestep, g_left_key_source);
        EnsureBinding(kActionRightSidestep, g_right_key_source);
      } else {
        RemoveBinding(kActionLeftSidestep, g_left_key_source);
        RemoveBinding(kActionRightSidestep, g_right_key_source);
        EnsureBinding(kActionTurnLeft, g_left_key_source);
        EnsureBinding(kActionTurnRight, g_right_key_source);
      }
    }
  }
  if (g_attack_enabled.load(std::memory_order_relaxed)) {
    EnsureBinding(kActionAttack1, MouseSource(kMouseLeftButton));
  }
  if (g_parry_enabled.load(std::memory_order_relaxed)) {
    EnsureBinding(kActionParry, MouseSource(kMouseRightButton));
  }
}

void AccumulateMouseDelta() {
  if (!g_dungeon_base ||
      g_control_mode.load(std::memory_order_relaxed) != ControlMode::kModern ||
      !g_turn_enabled.load(std::memory_order_relaxed)) {
    return;
  }
  const auto* mouse =
      reinterpret_cast<const int32_t*>(g_dungeon_base + kMouseStateRva);
  int32_t dx = mouse[0];
  if (std::abs(dx) <= g_jitter_threshold) {
    return;
  }
  dx = std::clamp(dx, -2048, 2048);
  int32_t pending = g_pending_mouse_dx.load(std::memory_order_relaxed);
  for (;;) {
    const int32_t next = std::clamp(pending + dx, -4096, 4096);
    if (g_pending_mouse_dx.compare_exchange_weak(
            pending, next, std::memory_order_release,
            std::memory_order_relaxed)) {
      break;
    }
  }
}

uint32_t* PlayerTransform() {
  if (!g_dungeon_base) {
    return nullptr;
  }
  const uintptr_t player = *reinterpret_cast<const uintptr_t*>(
      g_dungeon_base + kPlayerPointerRva);
  if (!player) {
    return nullptr;
  }
  const uintptr_t transform_holder =
      *reinterpret_cast<const uintptr_t*>(player + 0x10u);
  if (!transform_holder) {
    return nullptr;
  }
  return reinterpret_cast<uint32_t*>(
      *reinterpret_cast<const uintptr_t*>(transform_holder));
}

void ApplyModernHeading() {
  if (g_control_mode.load(std::memory_order_relaxed) != ControlMode::kModern ||
      !g_turn_enabled.load(std::memory_order_relaxed)) {
    g_pending_mouse_dx.store(0, std::memory_order_relaxed);
    return;
  }
  const int32_t dx =
      g_pending_mouse_dx.exchange(0, std::memory_order_acq_rel);
  if (!dx) {
    return;
  }
  if (g_block_turn_during_combat.load(std::memory_order_relaxed)) {
    const auto* mouse =
        reinterpret_cast<const int32_t*>(g_dungeon_base + kMouseStateRva);
    if (mouse[3] != 0 || mouse[7] != 0) {
      // Native turn actions are locked while an attack/parry pose owns the
      // player transform. Directly changing heading inside that pose can
      // deadlock the retail animation controller. Discard, never defer, the
      // delta so releasing the button cannot cause a delayed camera jump.
      AppendInputLog("modern-heading blocked combat dx=%ld lb=%ld rb=%ld\r\n",
                     static_cast<long>(dx), static_cast<long>(mouse[3]),
                     static_cast<long>(mouse[7]));
      g_fractional_angle_units = 0.0;
      return;
    }
  }
  uint32_t* const transform = PlayerTransform();
  if (!transform) {
    return;
  }

  constexpr double kAngleUnitsPerDegree = 1024.0 / 360.0;
  const double direction =
      g_invert_x.load(std::memory_order_relaxed) ? -1.0 : 1.0;
  const double requested_degrees = std::clamp(
      static_cast<double>(dx) * g_sensitivity_degrees * direction,
      -g_max_degrees_per_tick, g_max_degrees_per_tick);
  const double exact_units =
      requested_degrees * kAngleUnitsPerDegree + g_fractional_angle_units;
  const int32_t whole_units = static_cast<int32_t>(std::trunc(exact_units));
  g_fractional_angle_units = exact_units - whole_units;
  if (!whole_units) {
    return;
  }

  // Dungeon stores the canonical Y-axis heading as a 10-bit turn at +0x1C.
  // This runs after input collection and before the movement dispatcher, so
  // collision, attacks and animation observe the same authoritative heading.
  const uint32_t old_yaw = transform[0x1Cu / sizeof(uint32_t)] & 0x3FFu;
  const uint32_t new_yaw =
      static_cast<uint32_t>(static_cast<int32_t>(old_yaw) + whole_units) &
      0x3FFu;
  transform[0x1Cu / sizeof(uint32_t)] = new_yaw;
  AppendInputLog("modern-heading dx=%ld units=%ld yaw=%lu->%lu\r\n",
                 static_cast<long>(dx), static_cast<long>(whole_units),
                 static_cast<unsigned long>(old_yaw),
                 static_cast<unsigned long>(new_yaw));
}

void LogInputState() {
  if (!g_debug_log.load(std::memory_order_relaxed) || !g_dungeon_base) {
    return;
  }
  ++g_input_tick;
  const auto* mouse =
      reinterpret_cast<const int32_t*>(g_dungeon_base + kMouseStateRva);
  const auto* actions = reinterpret_cast<const NativeActionState*>(
      g_dungeon_base + kActionStateRva);
  const int32_t dx = mouse[0];
  const int32_t dy = mouse[1];
  const int32_t left_button = mouse[3];
  const int32_t right_button = mouse[7];
  const bool button_changed = left_button != g_previous_left_button ||
                              right_button != g_previous_right_button;
  g_previous_left_button = left_button;
  g_previous_right_button = right_button;
  if (!button_changed &&
      ((dx == 0 && dy == 0) || (g_input_tick & 7u) != 0u)) {
    return;
  }

  const NativeActionState& turn_left = actions[kActionTurnLeft];
  const NativeActionState& turn_right = actions[kActionTurnRight];
  const NativeActionState& attack = actions[kActionAttack1];
  const NativeActionState& parry = actions[kActionParry];
  AppendInputLog(
      "tick=%llu dx=%ld dy=%ld lb=%ld rb=%ld turnL=%lu/%ld "
      "turnR=%lu/%ld attack1=%lu/%ld parry=%lu/%ld\r\n",
      static_cast<unsigned long long>(g_input_tick), static_cast<long>(dx),
      static_cast<long>(dy), static_cast<long>(left_button),
      static_cast<long>(right_button),
      static_cast<unsigned long>(turn_left.state),
      static_cast<long>(turn_left.magnitude),
      static_cast<unsigned long>(turn_right.state),
      static_cast<long>(turn_right.magnitude),
      static_cast<unsigned long>(attack.state),
      static_cast<long>(attack.magnitude),
      static_cast<unsigned long>(parry.state),
      static_cast<long>(parry.magnitude));
}

void __cdecl HookInputUpdate() {
  if (g_original_input_update) {
    g_original_input_update();
  }
  // The game can rebuild the action map after redefining controls. Re-checking
  // here is cheap and makes the injected bindings self-healing.
  EnsureModernMouseBindings();
  AccumulateMouseDelta();
  LogInputState();
}

void __cdecl HookGameplayInputStage() {
  if (g_original_gameplay_input_stage) {
    g_original_gameplay_input_stage();
  }
  ApplyModernHeading();
}

void InitializeState() {
  const bool enabled = Configured(L"Enabled", true);
  g_enabled.store(enabled, std::memory_order_relaxed);
  g_turn_enabled.store(enabled && Configured(L"HorizontalTurn", true),
                       std::memory_order_relaxed);
  g_attack_enabled.store(enabled && Configured(L"LeftClickAttack", true),
                         std::memory_order_relaxed);
  g_parry_enabled.store(enabled && Configured(L"RightClickParry", true),
                        std::memory_order_relaxed);
  g_repair_legacy_bindings.store(
      enabled && Configured(L"RepairLegacyBindings", true),
      std::memory_order_relaxed);
  std::wstring control_mode = ConfiguredString(L"ControlMode", L"Classic");
  for (wchar_t& character : control_mode) {
    character = static_cast<wchar_t>(towlower(character));
  }
  g_control_mode.store(control_mode == L"modern" ? ControlMode::kModern
                                                  : ControlMode::kClassic,
                       std::memory_order_relaxed);
  g_modern_wasd.store(enabled && Configured(L"ModernWASD", true),
                      std::memory_order_relaxed);
  g_invert_x.store(Configured(L"InvertX", false),
                   std::memory_order_relaxed);
  g_block_turn_during_combat.store(
      Configured(L"BlockTurnDuringCombat", true), std::memory_order_relaxed);
  g_sensitivity_degrees =
      std::clamp(ConfiguredDouble(L"Sensitivity", 0.08), 0.001, 2.0);
  g_max_degrees_per_tick = std::clamp(
      ConfiguredDouble(L"MaxDegreesPerTick", 35.0), 1.0, 180.0);
  g_jitter_threshold = std::clamp(
      static_cast<int32_t>(ConfiguredDouble(L"JitterThreshold", 0.0)), 0, 32);
  g_debug_log.store(ConfiguredDiagnostic(L"DebugLog", false),
                    std::memory_order_relaxed);
  if (!enabled) {
    return;
  }

  HMODULE dungeon = GetModuleHandleW(L"Dungeon.dll");
  auto* base = reinterpret_cast<uint8_t*>(dungeon);
  if (IsExpectedDungeonImage(base)) {
    g_dungeon_base = base;
    AppendInputLog(
        "Deathtrap modern mouse 0.0.20 session enabled=%u mode=%s turn=%u "
        "wasd=%u attack=%u parry=%u repair=%u sensitivity=%.4f "
        "invert=%u combat-lock=%u jitter=%ld max-degrees=%.2f\r\n",
        enabled ? 1u : 0u,
        g_control_mode.load(std::memory_order_relaxed) == ControlMode::kModern
            ? "modern"
            : "classic",
        g_turn_enabled.load(std::memory_order_relaxed) ? 1u : 0u,
        g_modern_wasd.load(std::memory_order_relaxed) ? 1u : 0u,
        g_attack_enabled.load(std::memory_order_relaxed) ? 1u : 0u,
        g_parry_enabled.load(std::memory_order_relaxed) ? 1u : 0u,
        g_repair_legacy_bindings.load(std::memory_order_relaxed) ? 1u : 0u,
        g_sensitivity_degrees,
        g_invert_x.load(std::memory_order_relaxed) ? 1u : 0u,
        g_block_turn_during_combat.load(std::memory_order_relaxed) ? 1u : 0u,
        static_cast<long>(g_jitter_threshold), g_max_degrees_per_tick);
  } else {
    g_enabled.store(false, std::memory_order_relaxed);
  }
}

}  // namespace

void InitializeDeathtrapModernMouse() {
  std::call_once(g_initialize_once, &InitializeState);
}

bool InstallDeathtrapModernMouseHook() {
  InitializeDeathtrapModernMouse();
  if (!g_enabled.load(std::memory_order_relaxed)) {
    return false;
  }
  if (g_hook_installed.load(std::memory_order_acquire)) {
    return true;
  }

  void* const target = g_dungeon_base + kInputUpdateRva;
  const MH_STATUS created =
      MH_CreateHook(target, reinterpret_cast<void*>(&HookInputUpdate),
                    reinterpret_cast<void**>(&g_original_input_update));
  if (created != MH_OK && created != MH_ERROR_ALREADY_CREATED) {
    return false;
  }
  const MH_STATUS enabled = MH_EnableHook(target);
  if (enabled != MH_OK && enabled != MH_ERROR_ENABLED) {
    return false;
  }
  void* const gameplay_target = g_dungeon_base + kGameplayInputStageRva;
  const MH_STATUS gameplay_created = MH_CreateHook(
      gameplay_target, reinterpret_cast<void*>(&HookGameplayInputStage),
      reinterpret_cast<void**>(&g_original_gameplay_input_stage));
  if (gameplay_created != MH_OK &&
      gameplay_created != MH_ERROR_ALREADY_CREATED) {
    return false;
  }
  const MH_STATUS gameplay_enabled = MH_EnableHook(gameplay_target);
  if (gameplay_enabled != MH_OK && gameplay_enabled != MH_ERROR_ENABLED) {
    return false;
  }
  EnsureModernMouseBindings();
  g_hook_installed.store(true, std::memory_order_release);
  return true;
}
