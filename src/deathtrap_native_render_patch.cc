#include "deathtrap_native_render_patch.h"

#include <windows.h>

#include <Xinput.h>

#include <MinHook.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <functional>
#include <limits>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

constexpr uint32_t kExpectedTimestamp = 0x35752434u;
constexpr uint32_t kExpectedImageSize = 0x00367000u;
constexpr uintptr_t kRateConsumerSignatureRva = 0x0007F015u;
constexpr uintptr_t kRenderPresentWaitRva = 0x00080600u;
constexpr uintptr_t kRendererRva = 0x0001B320u;
constexpr uintptr_t kSceneCacheUpdateRva = 0x00039160u;
constexpr uintptr_t kCameraCacheUpdateRva = 0x00003860u;
constexpr uintptr_t kBackendFlipPointerRva = 0x000FFA50u;
constexpr uintptr_t kUiFrameStampRva = 0x000FB96Cu;
constexpr uintptr_t kUiRenderStateRva = 0x0022B5B0u;
constexpr uintptr_t kUiSelectorModeRva = 0x001086FCu;
constexpr uintptr_t kUiMessageStateRva = 0x001D41C8u;
constexpr uintptr_t kUiMessageEntriesRva = 0x001D4360u;
constexpr uintptr_t kUiMessageLifetimeImmediateRva = 0x0008F6AFu;
constexpr uintptr_t kPstMessageStateRva = 0x001F0A90u;
constexpr uintptr_t kPstMessageLifetimeImmediateRva = 0x000781C8u;
constexpr uintptr_t kUiCountdownStateRva = 0x001D89F0u;
constexpr uintptr_t kUiOwnerPointerRva = 0x0034F9D0u;
constexpr uintptr_t kEngineFrameCounterRva = 0x001D24DCu;
constexpr uintptr_t kActiveCloseCombatWeaponRva = 0x001D8A68u;
constexpr uintptr_t kInventoryLookupRva = 0x0007BD30u;
constexpr uintptr_t kSelectCloseCombatWeaponRva = 0x00090610u;
constexpr uintptr_t kSelectRangedWeaponRva = 0x00090740u;
constexpr uintptr_t kSelectSpellRva = 0x0007BAF0u;
constexpr uintptr_t kUseConsumableRva = 0x0007B9C0u;
constexpr uintptr_t kUseChalkRva = 0x000458B0u;
constexpr uintptr_t kInventorySlotDrawRva = 0x000772A0u;
constexpr uintptr_t kGameRootPointerRva = 0x00235EA4u;
constexpr size_t kMovementStageProbeCount = 95u;
constexpr size_t kMovementCallbackProbeCount = 64u;
constexpr std::array<uintptr_t, 5> kMovementDynamicCallbackRvas = {
    0x000810CFu, 0x00090427u, 0x00082770u, 0x00082780u,
    0x000827D0u};
constexpr std::array<uintptr_t, 3> kMovementResolverCallbackRvas = {
    0x00082770u, 0x00082780u, 0x000827D0u};
constexpr uintptr_t kMovementVtableCallbackRva = 0x0005778Au;
constexpr size_t kUiRenderStateSize = 0x40u;
constexpr size_t kUiMessageStateSize = 0x20u;
constexpr size_t kUiMessageEntrySize = 0x50u;
constexpr size_t kUiMessageEntryCount = 3u;
constexpr size_t kUiMessageEntriesSize =
    kUiMessageEntrySize * kUiMessageEntryCount;
constexpr size_t kPstMessageStateSize = 0x620u;
constexpr size_t kUiCountdownStateSize = 0x20u;
constexpr uint32_t kOriginalUiMessageLifetimeTicks = 50u;
constexpr uint32_t kOriginalPstMessageLifetimeTicks = 27u;
constexpr uint32_t kOriginalGameplayRate = 16u;
constexpr uint32_t kOriginalPeriodMilliseconds = 60u;
constexpr double kMatrixFixedScale = 16384.0;
constexpr size_t kMatrixOffset = 0x9Cu;
constexpr size_t kParentOffset = 0x2Cu;
constexpr size_t kChildOffset = 0x30u;
constexpr size_t kSiblingOffset = 0x34u;
constexpr size_t kLocalMatrixOffset = 0xD0u;
constexpr size_t kSceneRootOffset = 0x1Cu;
constexpr size_t kContextCameraOwnerOffset = 0x28u;
constexpr size_t kContextSceneOwnerOffset = 0x2Cu;
constexpr size_t kCameraNodeOffset = 0x10u;
constexpr size_t kMaximumSceneNodes = 8192u;
constexpr size_t kContextPrimaryCallbackOffset = 0x50u;
constexpr size_t kContextSecondaryCallbackOffset = 0x54u;
constexpr double kMaximumNodeTranslation = 8.0;
constexpr double kMaximumNodeRotationDegrees = 100.0;
constexpr double kMaximumScaleRatio = 1.25;
constexpr double kMaximumBasisDot = 0.025;

constexpr std::array<uint8_t, 16> kRateConsumerSignature = {
    0x8B, 0x83, 0x08, 0x08, 0x00, 0x00, 0x85, 0xC0,
    0x7E, 0x09, 0x50, 0xE8, 0x6B, 0x13, 0xFD, 0xFF};

struct Matrix3x4 {
  std::array<int32_t, 12> values{};
};

struct NodeTransform {
  Matrix3x4 world;
  Matrix3x4 local;
  uintptr_t parent = 0;
};

struct SceneSnapshot {
  uintptr_t root = 0;
  uintptr_t camera = 0;
  bool camera_in_scene_tree = false;
  uintptr_t player = 0;
  uintptr_t player_object = 0;
  std::array<int32_t, 3> player_position{};
  std::array<int32_t, 3> player_cached_position{};
  std::array<int32_t, 3> player_bounds_a{};
  std::array<int32_t, 3> player_bounds_b{};
  // Collision projection written by Dungeon.dll+0x54D00, or by a qualified
  // large correction from Dungeon.dll+0x68390, while producing this exact
  // simulation endpoint. It is captured at the original callsite and
  // consumed by the next render snapshot only; it is never written back to
  // gameplay state.
  std::array<int32_t, 3> player_contact_projection{};
  // Render-only correction for an exact endpoint that lies behind the last
  // confirmed persistent-contact plane. Unlike the resolver projection
  // above, this is inferred prospectively from an already confirmed A-B-A
  // contact manifold. It is never written to simulation-owned state.
  std::array<int32_t, 3> player_contact_manifold_projection{};
  uint64_t player_contact_projection_sequence = 0;
  uint8_t player_contact_count = 0;
  bool player_position_valid = false;
  bool player_cached_position_valid = false;
  bool player_bounds_a_valid = false;
  bool player_bounds_b_valid = false;
  bool player_contact_count_valid = false;
  bool player_contact_projection_valid = false;
  bool player_contact_manifold_projection_valid = false;
  std::unordered_map<uintptr_t, NodeTransform> nodes;
};

struct Vec3 {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

struct Quaternion {
  double w = 1.0;
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

struct RigidTransform {
  std::array<Vec3, 3> rows{};
  Vec3 translation;
  Vec3 scale{1.0, 1.0, 1.0};
  Quaternion rotation;
};

struct InterpolationStats {
  uint64_t applied = 0;
  uint64_t hierarchical = 0;
  uint64_t world_fallback = 0;
  uint64_t exact = 0;
  uint64_t invalid = 0;
  uint64_t parent_mismatch = 0;
  uint64_t contact_reversal = 0;
  uint64_t temporal_pose_guard = 0;
  uint64_t player_root_found = 0;
  uint64_t player_contact_settle = 0;
  uint64_t player_raw_contact = 0;
  uint64_t player_exact_subtree_nodes = 0;
  uint64_t player_bounds_guided_nodes = 0;
  uint64_t player_contact_projection_nodes = 0;
  uint64_t player_contact_projection_events = 0;
  uint64_t player_contact_bridge_nodes = 0;
  uint64_t player_contact_bridge_events = 0;
  uint64_t player_contact_manifold_midpoint_nodes = 0;
  uint64_t player_contact_manifold_events = 0;
  uint64_t player_contact_normal_flip_rejections = 0;
  uint64_t player_root_coherence_nodes = 0;
  std::array<int32_t, 3> player_root_coherence_offset{};
  double player_contact_manifold_depth = 0.0;
  uint32_t player_bounds_axis_mask = 0;
  uint64_t player_directional_release = 0;
  double max_translation = 0.0;
  double max_rotation_degrees = 0.0;
};

struct ActivePresentationTrace {
  uint64_t tick = 0;
  uintptr_t player = 0;
  const SceneSnapshot* exact_scene = nullptr;
  std::array<int32_t, 3> exact_projection{};
  std::array<int32_t, 3> midpoint_seen{};
  std::array<int32_t, 3> exact_before_projection{};
  std::array<int32_t, 3> exact_seen{};
  uint64_t exact_projection_nodes = 0;
  uint32_t midpoint_calls = 0;
  uint32_t exact_calls = 0;
  DeathtrapNativePresentationStage stage =
      DeathtrapNativePresentationStage::kNone;
  bool exact_projection_valid = false;
  bool midpoint_seen_valid = false;
  bool exact_before_projection_valid = false;
  bool exact_seen_valid = false;
};

struct PresentationTraceSample {
  uint64_t tick = 0;
  std::array<int32_t, 3> previous{};
  std::array<int32_t, 3> current{};
  std::array<int32_t, 3> midpoint_seen{};
  std::array<int32_t, 3> exact_before_projection{};
  std::array<int32_t, 3> exact_seen{};
  std::array<int32_t, 3> projection{};
  std::array<int32_t, 3> root_coherence{};
  uint64_t exact_projection_nodes = 0;
  uint64_t root_coherence_nodes = 0;
  uint32_t midpoint_calls = 0;
  uint32_t exact_calls = 0;
  uint32_t flags = 0;
};

struct UiRenderStateSnapshot {
  std::array<uint8_t, kUiRenderStateSize> control{};
  std::array<uint8_t, kUiMessageStateSize> messages{};
  // Dungeon.dll+0x8F4C0 decrements the lifetime at +0x4C of every active
  // 0x50-byte message entry whenever the renderer is invoked. Preserve the
  // complete three-entry queue around synthetic render passes so only the
  // exact simulation render is allowed to age on-screen text.
  std::array<uint8_t, kUiMessageEntriesSize> message_entries{};
  // Level-script (PST) notifications use a separate six-entry ring at
  // 0x101F0A90. Its per-entry lifetime at +0x100 was previously aged by all
  // three renderer calls, making messages such as "You need the silver key"
  // disappear roughly three times faster than retail.
  std::array<uint8_t, kPstMessageStateSize> pst_messages{};
  std::array<uint8_t, kUiCountdownStateSize> countdowns{};
  int32_t frame_stamp = 0;
  uint8_t selector_mode = 0;
  bool control_valid = false;
  bool messages_valid = false;
  bool message_entries_valid = false;
  bool pst_messages_valid = false;
  bool countdowns_valid = false;
  bool frame_stamp_valid = false;
  bool selector_mode_valid = false;
};

struct PlayerMutableStateRollbackStats {
  uint32_t changed_mask = 0;
  uint32_t restored_mask = 0;
  uint64_t changed_fields = 0;
  int64_t maximum_delta = 0;
};

struct TransitionStats {
  size_t matched = 0;
  size_t entered = 0;
  size_t departed = 0;
  double overlap = 0.0;
  double camera_translation = 0.0;
  double camera_rotation_degrees = 0.0;
  uintptr_t primary_callback = 0;
  uintptr_t secondary_callback = 0;
  bool camera_valid = false;
  bool camera_in_scene_tree = false;
  bool suppress_midpoint = false;
  const char* reason = "none";
};

std::once_flag g_patch_once;
std::atomic<DeathtrapNativeRenderPatchState> g_state{
    DeathtrapNativeRenderPatchState::kNotAttempted};
std::atomic<uint32_t> g_subframes{0};
std::atomic<uint64_t> g_source_ticks{0};
std::atomic<uint64_t> g_interpolated_frames{0};
std::atomic<uint64_t> g_interpolated_nodes{0};
std::atomic<bool> g_render_hook_installed{false};
std::atomic<bool> g_movement_stage_probes_installed{false};
std::atomic<bool> g_movement_callback_probe_installed{false};
std::atomic<int32_t> g_pending_weapon_wheel_detents{0};
std::atomic<uint64_t> g_last_weapon_wheel_event_ms{0};
std::atomic<uint32_t> g_controller_selector_overlay{0};
std::mutex g_contact_projection_mutex;
std::array<int64_t, 3> g_pending_contact_projection{};
uint64_t g_pending_contact_projection_sequence = 0;
uint64_t g_contact_projection_captures = 0;
uint64_t g_contact_projection_consumes = 0;
uint64_t g_contact_projection_54d00_captures = 0;
uint64_t g_contact_projection_68390_captures = 0;
uint64_t g_contact_projection_68390_small_rejections = 0;
uint8_t* g_dungeon_base = nullptr;
SceneSnapshot g_previous_snapshot;
SceneSnapshot g_older_snapshot;
LARGE_INTEGER g_qpc_frequency{};
bool g_debug_log = false;
bool g_weapon_wheel_enabled = false;
bool g_weapon_wheel_invert = false;
bool g_xinput_enabled = false;
bool g_xinput_base_bindings = false;
uint32_t g_xinput_controller_index = 0;
uint32_t g_xinput_selector_hold_ms = 225;
int32_t g_xinput_left_deadzone = XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE;
int32_t g_xinput_right_deadzone = XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE;
int32_t g_xinput_trigger_threshold = XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
int32_t g_xinput_first_person_pixels = 12;
int32_t g_xinput_menu_mouse_pixels = 6;
double g_xinput_right_stick_curve = 1.35;
bool g_xinput_invert_right_y = false;
uint32_t g_ui_message_lifetime_percent = 300u;
uint32_t g_ui_message_lifetime_ticks = 150u;
uint32_t g_pst_message_lifetime_ticks = 81u;
bool g_ui_message_lifetime_patched = false;
bool g_pst_message_lifetime_patched = false;
int32_t g_xinput_selector_radius = 104;
int32_t g_xinput_selector_center_y = 316;
double g_xinput_movement_threshold = 0.14;
double g_xinput_run_threshold = 0.50;
double g_xinput_run_release_threshold = 0.30;
bool g_xinput_run_active = false;
uint64_t g_xinput_chalk_actions_asserted = 0;
uint64_t g_weapon_wheel_switches = 0;
uint64_t g_weapon_wheel_rejections = 0;
uint64_t g_suppressed_midpoints = 0;
uint64_t g_last_transition_log_tick = 0;
uint64_t g_ui_state_restores = 0;
uint64_t g_ui_state_changes = 0;
uint64_t g_ui_logic_rollbacks = 0;
uint64_t g_player_state_rollback_events = 0;
uint64_t g_player_state_rollback_fields = 0;
uint64_t g_player_state_restore_failures = 0;
uint64_t g_last_player_state_rollback_log_tick = 0;
int64_t g_player_state_rollback_maximum_delta = 0;
uint64_t g_player_contact_settles = 0;
uint64_t g_player_raw_contact_detections = 0;
uint64_t g_player_matrix_contact_candidates = 0;
uint64_t g_player_raw_contact_candidates = 0;
uint32_t g_player_contact_exact_cooldown = 0;
uint32_t g_player_contact_hit_window = 0;
uint32_t g_player_contact_hits_in_window = 0;
uint32_t g_player_persistent_contact_cooldown = 0;
Vec3 g_player_contact_outward_normal{};
Vec3 g_player_contact_anchor{};
bool g_player_contact_normal_valid = false;
bool g_player_contact_anchor_valid = false;
uint32_t g_player_contact_release_streak = 0;
uint32_t g_player_contact_quiet_ticks = 0;
double g_player_contact_release_alignment = 0.0;
double g_player_contact_outward_distance = 0.0;
double g_player_contact_correction_length = 0.0;
uint64_t g_player_contact_directional_releases = 0;
uint64_t g_player_contact_manifold_events = 0;
uint64_t g_player_contact_manifold_midpoint_nodes = 0;
uint64_t g_player_contact_manifold_endpoint_nodes = 0;
uint64_t g_player_contact_normal_flip_rejections = 0;
double g_player_contact_manifold_max_depth = 0.0;
uint64_t g_last_player_probe_log_tick = 0;
uint64_t g_last_bounds_apply_log_tick = 0;
using AxisReturnBins = std::array<std::array<uint64_t, 4>, 3>;
AxisReturnBins g_matrix_axis_return_bins{};
AxisReturnBins g_raw_axis_return_bins{};
uint32_t g_transition_guard_cooldown = 0;
uint64_t g_page_restore_flips = 0;
uint64_t g_ui_owner_ticks = 0;
uint64_t g_ui_object_ticks = 0;
uint64_t g_ui_gate_open_ticks = 0;
uint64_t g_ui_gate_closed_ticks = 0;
int64_t g_last_ui_frame_delta = 0;

using RenderPresentWaitFn = void(__cdecl*)(void* context, int wait);
using RendererFn = void(__cdecl*)(void* context);
using InventorySlotDrawFn = void(__cdecl*)(void* slot);
using RenderCacheUpdateFn = void(__cdecl*)(void* owner);
using BackendFlipFn = void(__cdecl*)();

RenderPresentWaitFn g_original_render_present_wait = nullptr;
RendererFn g_renderer = nullptr;
RendererFn g_original_renderer = nullptr;
InventorySlotDrawFn g_original_inventory_slot_draw = nullptr;
RenderCacheUpdateFn g_scene_cache_update = nullptr;
RenderCacheUpdateFn g_camera_cache_update = nullptr;
thread_local ActivePresentationTrace g_active_presentation_trace;
std::vector<PresentationTraceSample> g_presentation_trace_buffer;

bool SafeRead(const void* source, void* destination, size_t size) {
  __try {
    std::memcpy(destination, source, size);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

bool SafeWrite(void* destination, const void* source, size_t size) {
  __try {
    std::memcpy(destination, source, size);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

template <typename T>
bool SafeReadValue(const void* address, T* value) {
  return SafeRead(address, value, sizeof(*value));
}

UiRenderStateSnapshot CaptureUiRenderState() {
  UiRenderStateSnapshot snapshot;
  if (!g_dungeon_base) {
    return snapshot;
  }
  snapshot.control_valid =
      SafeRead(g_dungeon_base + kUiRenderStateRva,
               snapshot.control.data(), snapshot.control.size());
  snapshot.messages_valid =
      SafeRead(g_dungeon_base + kUiMessageStateRva,
               snapshot.messages.data(), snapshot.messages.size());
  snapshot.message_entries_valid =
      SafeRead(g_dungeon_base + kUiMessageEntriesRva,
               snapshot.message_entries.data(),
               snapshot.message_entries.size());
  snapshot.pst_messages_valid =
      SafeRead(g_dungeon_base + kPstMessageStateRva,
               snapshot.pst_messages.data(), snapshot.pst_messages.size());
  snapshot.countdowns_valid =
      SafeRead(g_dungeon_base + kUiCountdownStateRva,
               snapshot.countdowns.data(), snapshot.countdowns.size());
  snapshot.frame_stamp_valid = SafeReadValue(
      g_dungeon_base + kUiFrameStampRva, &snapshot.frame_stamp);
  snapshot.selector_mode_valid = SafeReadValue(
      g_dungeon_base + kUiSelectorModeRva, &snapshot.selector_mode);
  return snapshot;
}

bool RestoreUiRenderState(const UiRenderStateSnapshot& snapshot) {
  bool restored = false;
  if (snapshot.control_valid) {
    restored |= SafeWrite(g_dungeon_base + kUiRenderStateRva,
                          snapshot.control.data(), snapshot.control.size());
  }
  if (snapshot.messages_valid) {
    restored |= SafeWrite(g_dungeon_base + kUiMessageStateRva,
                          snapshot.messages.data(), snapshot.messages.size());
  }
  if (snapshot.message_entries_valid) {
    restored |= SafeWrite(g_dungeon_base + kUiMessageEntriesRva,
                          snapshot.message_entries.data(),
                          snapshot.message_entries.size());
  }
  if (snapshot.pst_messages_valid) {
    restored |= SafeWrite(g_dungeon_base + kPstMessageStateRva,
                          snapshot.pst_messages.data(),
                          snapshot.pst_messages.size());
  }
  if (snapshot.countdowns_valid) {
    restored |= SafeWrite(g_dungeon_base + kUiCountdownStateRva,
                          snapshot.countdowns.data(),
                          snapshot.countdowns.size());
  }
  if (snapshot.frame_stamp_valid) {
    restored |= SafeWrite(g_dungeon_base + kUiFrameStampRva,
                          &snapshot.frame_stamp, sizeof(snapshot.frame_stamp));
  }
  if (snapshot.selector_mode_valid) {
    restored |= SafeWrite(g_dungeon_base + kUiSelectorModeRva,
                          &snapshot.selector_mode,
                          sizeof(snapshot.selector_mode));
  }
  return restored;
}

bool UiRenderStateChanged(const UiRenderStateSnapshot& before,
                          const UiRenderStateSnapshot& after) {
  return (before.control_valid && after.control_valid &&
          before.control != after.control) ||
         (before.messages_valid && after.messages_valid &&
          before.messages != after.messages) ||
         (before.message_entries_valid && after.message_entries_valid &&
          before.message_entries != after.message_entries) ||
         (before.pst_messages_valid && after.pst_messages_valid &&
          before.pst_messages != after.pst_messages) ||
         (before.countdowns_valid && after.countdowns_valid &&
          before.countdowns != after.countdowns) ||
         (before.frame_stamp_valid && after.frame_stamp_valid &&
          before.frame_stamp != after.frame_stamp) ||
         (before.selector_mode_valid && after.selector_mode_valid &&
          before.selector_mode != after.selector_mode);
}

bool UiTransientLogicChanged(const UiRenderStateSnapshot& before,
                             const UiRenderStateSnapshot& after) {
  return (before.messages_valid && after.messages_valid &&
          before.messages != after.messages) ||
         (before.message_entries_valid && after.message_entries_valid &&
          before.message_entries != after.message_entries) ||
         (before.pst_messages_valid && after.pst_messages_valid &&
          before.pst_messages != after.pst_messages) ||
         (before.countdowns_valid && after.countdowns_valid &&
          before.countdowns != after.countdowns) ||
         (before.selector_mode_valid && after.selector_mode_valid &&
          before.selector_mode != after.selector_mode);
}

void SampleUiEligibility() {
  uintptr_t owner = 0;
  uintptr_t ui_context = 0;
  uintptr_t ui_object = 0;
  int32_t engine_frame = 0;
  int32_t frame_stamp = 0;
  if (!SafeReadValue(g_dungeon_base + kUiOwnerPointerRva, &owner) || !owner) {
    return;
  }
  ++g_ui_owner_ticks;
  SafeReadValue(reinterpret_cast<const void*>(owner + 0x2Cu), &ui_context);
  if (ui_context) {
    SafeReadValue(reinterpret_cast<const void*>(ui_context + 0x1030u),
                  &ui_object);
  }
  if (ui_object) {
    ++g_ui_object_ticks;
  }
  if (!SafeReadValue(g_dungeon_base + kEngineFrameCounterRva,
                     &engine_frame) ||
      !SafeReadValue(g_dungeon_base + kUiFrameStampRva, &frame_stamp)) {
    return;
  }
  g_last_ui_frame_delta = std::llabs(
      static_cast<long long>(frame_stamp) -
      static_cast<long long>(engine_frame));
  if (g_last_ui_frame_delta >= 2) {
    ++g_ui_gate_open_ticks;
  } else {
    ++g_ui_gate_closed_ticks;
  }
}

std::wstring ModuleDirectory() {
  HMODULE self = nullptr;
  if (!GetModuleHandleExW(
          GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
              GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
          reinterpret_cast<LPCWSTR>(&InitializeDeathtrapNativeRenderPatch),
          &self)) {
    return L".";
  }
  wchar_t path[MAX_PATH] = {};
  const DWORD length = GetModuleFileNameW(self, path, MAX_PATH);
  if (!length || length >= MAX_PATH) {
    return L".";
  }
  wchar_t* slash = wcsrchr(path, L'\\');
  if (slash) {
    *slash = L'\0';
  }
  return path;
}

std::wstring ConfigurationPath() {
  return ModuleDirectory() + L"\\deathtrap_native.ini";
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
  if (nt->Signature != IMAGE_NT_SIGNATURE ||
      nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
      nt->FileHeader.TimeDateStamp != kExpectedTimestamp ||
      nt->OptionalHeader.SizeOfImage != kExpectedImageSize) {
    return false;
  }
  return std::memcmp(base + kRateConsumerSignatureRva,
                     kRateConsumerSignature.data(),
                     kRateConsumerSignature.size()) == 0;
}

uint32_t ConfiguredSubframes() {
  const std::wstring ini = ConfigurationPath();
  if (GetPrivateProfileIntW(L"NativeRender", L"Enabled", 0,
                            ini.c_str()) == 0) {
    return 0;
  }
  // Integer x3 renders two clock-isolated phases plus the exact native frame.
  // Integer x2 remains selectable as a conservative fallback.
  const int configured = GetPrivateProfileIntW(
      L"NativeRender", L"Subframes", 3, ini.c_str());
  if (configured >= 3) {
    return 3u;
  }
  return configured == 2 ? 2u : 0u;
}

bool ConfiguredDebugLog() {
  const std::wstring ini = ConfigurationPath();
  return GetPrivateProfileIntW(L"Diagnostics", L"DebugLog", 0,
                               ini.c_str()) != 0;
}

bool ConfiguredWeaponWheelEnabled() {
  const std::wstring ini = ConfigurationPath();
  return GetPrivateProfileIntW(L"WeaponWheel", L"Enabled", 1,
                               ini.c_str()) != 0;
}

bool ConfiguredWeaponWheelInvert() {
  const std::wstring ini = ConfigurationPath();
  return GetPrivateProfileIntW(L"WeaponWheel", L"Invert", 0,
                               ini.c_str()) != 0;
}

int ConfiguredInteger(const wchar_t* section, const wchar_t* key,
                      int default_value) {
  const std::wstring ini = ConfigurationPath();
  return GetPrivateProfileIntW(section, key, default_value, ini.c_str());
}

bool PatchMessageLifetimeImmediate(uintptr_t rva, uint32_t expected,
                                   uint32_t ticks) {
  if (!g_dungeon_base || ticks == 0u) {
    return false;
  }
  uint32_t original = 0;
  uint8_t* immediate = g_dungeon_base + rva;
  if (!SafeReadValue(immediate, &original) || original != expected) {
    return false;
  }
  DWORD old_protection = 0;
  if (!VirtualProtect(immediate, sizeof(ticks), PAGE_EXECUTE_READWRITE,
                      &old_protection)) {
    return false;
  }
  const bool written = SafeWrite(immediate, &ticks, sizeof(ticks));
  FlushInstructionCache(GetCurrentProcess(), immediate, sizeof(ticks));
  DWORD ignored = 0;
  VirtualProtect(immediate, sizeof(ticks), old_protection, &ignored);
  uint32_t verified = 0;
  return written && SafeReadValue(immediate, &verified) && verified == ticks;
}

bool PatchUiMessageLifetime(uint32_t ticks) {
  return PatchMessageLifetimeImmediate(kUiMessageLifetimeImmediateRva,
                                       kOriginalUiMessageLifetimeTicks,
                                       ticks);
}

bool PatchPstMessageLifetime(uint32_t ticks) {
  return PatchMessageLifetimeImmediate(kPstMessageLifetimeImmediateRva,
                                       kOriginalPstMessageLifetimeTicks,
                                       ticks);
}

void AppendNativeLog(const char* format, ...) {
  if (!g_debug_log) {
    return;
  }
  // Periodic resolver telemetry is intentionally dense. A 1 KiB buffer
  // caused vsnprintf's truncation terminator to hide the counters at the end
  // of each line.
  char line[4096] = {};
  va_list args;
  va_start(args, format);
  const int length = std::vsnprintf(line, sizeof(line) - 2, format, args);
  va_end(args);
  if (length <= 0) {
    return;
  }
  const size_t used = std::min<size_t>(static_cast<size_t>(length),
                                       sizeof(line) - 2);
  line[used] = '\r';
  line[used + 1] = '\n';
  const std::wstring path =
      ModuleDirectory() + L"\\deathtrap_native_render.log";
  HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return;
  }
  DWORD written = 0;
  WriteFile(file, line, static_cast<DWORD>(used + 2), &written, nullptr);
  CloseHandle(file);
}

void AppendNativeLogBlock(const std::string& block) {
  if (!g_debug_log || block.empty()) {
    return;
  }
  const std::wstring path =
      ModuleDirectory() + L"\\deathtrap_native_render.log";
  HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return;
  }
  DWORD written = 0;
  WriteFile(file, block.data(), static_cast<DWORD>(block.size()), &written,
            nullptr);
  CloseHandle(file);
}

bool DeathtrapGameplayReady(bool require_selector_closed) {
  if (!g_dungeon_base) {
    return false;
  }

  uint8_t selector_mode = 0;
  uintptr_t player = 0;
  uintptr_t game_root = 0;
  uintptr_t gameplay_context = 0;
  uintptr_t gameplay_object = 0;
  uintptr_t root_vtable = 0;
  if (!SafeReadValue(g_dungeon_base + kUiSelectorModeRva, &selector_mode) ||
      (require_selector_closed && selector_mode != 0) ||
      !SafeReadValue(g_dungeon_base + kUiOwnerPointerRva, &player) ||
      !player ||
      !SafeReadValue(g_dungeon_base + kGameRootPointerRva, &game_root) ||
      !game_root ||
      !SafeReadValue(reinterpret_cast<const void*>(game_root), &root_vtable) ||
      root_vtable == reinterpret_cast<uintptr_t>(g_dungeon_base) +
                         0x00083F20u ||
      root_vtable == reinterpret_cast<uintptr_t>(g_dungeon_base) +
                         0x0004EBF0u ||
      !SafeReadValue(reinterpret_cast<const void*>(game_root + 0x2Cu),
                     &gameplay_context) ||
      !gameplay_context ||
      !SafeReadValue(
          reinterpret_cast<const void*>(gameplay_context + 0x1030u),
          &gameplay_object) ||
      !gameplay_object) {
    return false;
  }
  return true;
}

bool WeaponWheelGameplayReady() {
  return g_weapon_wheel_enabled && DeathtrapGameplayReady(true);
}

bool NativeWeaponAvailable(int32_t weapon_id) {
  if (weapon_id == 0 || weapon_id == 7) {
    return true;
  }
  if (weapon_id < 1 || weapon_id > 6) {
    return false;
  }
  using InventoryLookupFn = void*(__cdecl*)(int32_t);
  void* item = nullptr;
  __try {
    item = reinterpret_cast<InventoryLookupFn>(
        g_dungeon_base + kInventoryLookupRva)(weapon_id);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    item = nullptr;
  }
  return item != nullptr;
}

bool SelectNativeWeapon(int32_t action_code) {
  using SelectWeaponFn = void(__cdecl*)(int32_t, int32_t);
  __try {
    reinterpret_cast<SelectWeaponFn>(
        g_dungeon_base + kSelectCloseCombatWeaponRva)(action_code, 1);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

void ConsumePendingWeaponWheel() {
  int32_t queued =
      g_pending_weapon_wheel_detents.load(std::memory_order_acquire);
  if (queued == 0) {
    return;
  }

  // Never carry a wheel action out of a menu or loading screen. DirectInput
  // observes the wheel independently, so stale menu scrolling must not equip
  // a weapon after gameplay resumes.
  const uint64_t age_ms =
      GetTickCount64() -
      g_last_weapon_wheel_event_ms.load(std::memory_order_relaxed);
  if (age_ms > 500u || !WeaponWheelGameplayReady()) {
    g_pending_weapon_wheel_detents.store(0, std::memory_order_release);
    ++g_weapon_wheel_rejections;
    return;
  }

  queued = g_pending_weapon_wheel_detents.exchange(
      0, std::memory_order_acq_rel);
  int32_t current = -1;
  if (!SafeReadValue(g_dungeon_base + kActiveCloseCombatWeaponRva,
                     &current) ||
      current < 0 || current > 7) {
    ++g_weapon_wheel_rejections;
    return;
  }

  // The retail close-combat selector maps internal IDs to non-contiguous
  // action codes. ID 0 and the special ID 7 are always present; IDs 1..6 use
  // the same inventory-presence test as the original selector.
  constexpr std::array<int32_t, 8> kActionCodeByWeaponId = {
      1, 2, 4, 5, 6, 7, 8, 0};
  int direction = queued > 0 ? -1 : 1;
  if (g_weapon_wheel_invert) {
    direction = -direction;
  }
  for (int distance = 1; distance <= 8; ++distance) {
    const int32_t candidate =
        (current + direction * distance + 64) % 8;
    if (!NativeWeaponAvailable(candidate)) {
      continue;
    }
    if (SelectNativeWeapon(kActionCodeByWeaponId[candidate])) {
      ++g_weapon_wheel_switches;
      AppendNativeLog(
          "weapon_wheel current=%d selected=%d action=%d direction=%d "
          "queued=%d switches=%llu rejections=%llu",
          current, candidate, kActionCodeByWeaponId[candidate], direction,
          queued, static_cast<unsigned long long>(g_weapon_wheel_switches),
          static_cast<unsigned long long>(g_weapon_wheel_rejections));
    } else {
      ++g_weapon_wheel_rejections;
    }
    return;
  }
  ++g_weapon_wheel_rejections;
}

using DynamicXInputGetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
using DynamicXInputSetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_VIBRATION*);

struct ControllerSelectorState {
  bool direction_down = false;
  bool row_open = false;
  bool cancelled = false;
  bool selection_confirmed = false;
  uint32_t category = 0;
  uint32_t slot = 0;
  uint64_t pressed_ms = 0;
  std::array<uint32_t, 4> remembered_slot{};
};

enum class InjectedKey : size_t {
  kW,
  kS,
  kA,
  kD,
  kJ,
  kK,
  kShift,
  kSpace,
  kE,
  kQ,
  kC,
  kTab,
  kEscape,
  kUp,
  kDown,
  kLeft,
  kRight,
  kEnter,
  kCount,
};

HMODULE g_xinput_module = nullptr;
DynamicXInputGetStateFn g_xinput_get_state = nullptr;
DynamicXInputSetStateFn g_xinput_set_state = nullptr;
ControllerSelectorState g_controller_selector;
std::array<bool, static_cast<size_t>(InjectedKey::kCount)>
    g_injected_keys{};
bool g_injected_mouse_left = false;
bool g_injected_mouse_right = false;
bool g_xinput_was_connected = false;
WORD g_previous_xinput_buttons = 0;
bool g_xinput_first_person_toggled = false;
std::atomic<bool> g_xinput_menu_mode{true};
bool g_xinput_previous_native_gameplay = false;
bool g_xinput_vibration_enabled = true;
uint32_t g_xinput_vibration_strength_percent = 70u;
uint32_t g_xinput_attack_vibration_ms = 85u;
uint32_t g_xinput_block_vibration_ms = 55u;
BYTE g_previous_xinput_left_trigger = 0;
BYTE g_previous_xinput_right_trigger = 0;
WORD g_applied_vibration_left = 0;
WORD g_applied_vibration_right = 0;
uint64_t g_attack_vibration_until_ms = 0;
uint64_t g_block_vibration_until_ms = 0;
std::atomic_flag g_xinput_poll_guard = ATOMIC_FLAG_INIT;

class ScopedXInputPoll {
 public:
  explicit ScopedXInputPoll(std::atomic_flag& flag) : flag_(flag) {
    acquired_ = !flag_.test_and_set(std::memory_order_acquire);
  }
  ~ScopedXInputPoll() {
    if (acquired_) {
      flag_.clear(std::memory_order_release);
    }
  }
  explicit operator bool() const { return acquired_; }

 private:
  std::atomic_flag& flag_;
  bool acquired_ = false;
};

constexpr std::array<WORD, static_cast<size_t>(InjectedKey::kCount)>
    kInjectedVirtualKeys = {L'W', L'S', L'A', L'D', L'J', L'K', VK_LSHIFT,
                            VK_SPACE, L'E', L'Q', L'C', VK_TAB, VK_ESCAPE,
                            VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, VK_RETURN};

bool IsGameForeground() {
  const HWND foreground = GetForegroundWindow();
  if (!foreground) {
    return false;
  }
  DWORD process_id = 0;
  GetWindowThreadProcessId(foreground, &process_id);
  return process_id == GetCurrentProcessId();
}

void InjectVirtualKey(InjectedKey key, bool down) {
  const size_t index = static_cast<size_t>(key);
  if (g_injected_keys[index] == down) {
    return;
  }
  INPUT input = {};
  input.type = INPUT_KEYBOARD;
  input.ki.wVk = kInjectedVirtualKeys[index];
  input.ki.dwFlags = down ? 0u : KEYEVENTF_KEYUP;
  if (key == InjectedKey::kUp || key == InjectedKey::kDown ||
      key == InjectedKey::kLeft || key == InjectedKey::kRight) {
    input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
  }
  if (SendInput(1, &input, sizeof(input)) == 1) {
    g_injected_keys[index] = down;
  }
}

void InjectMouseLeft(bool down) {
  if (g_injected_mouse_left == down) {
    return;
  }
  INPUT input = {};
  input.type = INPUT_MOUSE;
  input.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
  if (SendInput(1, &input, sizeof(input)) == 1) {
    g_injected_mouse_left = down;
  }
}

void InjectMouseRight(bool down) {
  if (g_injected_mouse_right == down) {
    return;
  }
  INPUT input = {};
  input.type = INPUT_MOUSE;
  input.mi.dwFlags = down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
  if (SendInput(1, &input, sizeof(input)) == 1) {
    g_injected_mouse_right = down;
  }
}

void InjectRelativeMouseMove(double normalized_x, double normalized_y,
                             int32_t pixels_per_tick) {
  const LONG movement_x = static_cast<LONG>(std::lround(
      normalized_x * static_cast<double>(pixels_per_tick)));
  const double y_sign = g_xinput_invert_right_y ? 1.0 : -1.0;
  const LONG movement_y = static_cast<LONG>(std::lround(
      normalized_y * y_sign * static_cast<double>(pixels_per_tick)));
  if (movement_x == 0 && movement_y == 0) {
    return;
  }
  INPUT input = {};
  input.type = INPUT_MOUSE;
  input.mi.dx = movement_x;
  input.mi.dy = movement_y;
  input.mi.dwFlags = MOUSEEVENTF_MOVE;
  SendInput(1, &input, sizeof(input));
}

double CurvedStick(double value, double exponent) {
  if (value == 0.0) {
    return 0.0;
  }
  return std::copysign(std::pow(std::abs(value), exponent), value);
}

WORD VibrationMotorValue(uint32_t percent, double channel_scale) {
  const double scaled = std::clamp(
      static_cast<double>(percent) * channel_scale, 0.0, 100.0);
  return static_cast<WORD>(std::lround(
      scaled * static_cast<double>(std::numeric_limits<WORD>::max()) / 100.0));
}

void ApplyControllerVibration(WORD left_motor, WORD right_motor) {
  if (left_motor == g_applied_vibration_left &&
      right_motor == g_applied_vibration_right) {
    return;
  }
  if (!g_xinput_set_state) {
    g_applied_vibration_left = 0;
    g_applied_vibration_right = 0;
    return;
  }
  XINPUT_VIBRATION vibration = {};
  vibration.wLeftMotorSpeed = left_motor;
  vibration.wRightMotorSpeed = right_motor;
  if (g_xinput_set_state(g_xinput_controller_index, &vibration) ==
      ERROR_SUCCESS) {
    g_applied_vibration_left = left_motor;
    g_applied_vibration_right = right_motor;
  }
}

void StopControllerVibration() {
  g_attack_vibration_until_ms = 0;
  g_block_vibration_until_ms = 0;
  g_previous_xinput_left_trigger = 0;
  g_previous_xinput_right_trigger = 0;
  ApplyControllerVibration(0, 0);
}

void UpdateControllerVibration(const XINPUT_GAMEPAD& pad, bool gameplay,
                               bool selector_captures_controls) {
  if (!g_xinput_vibration_enabled || !gameplay ||
      selector_captures_controls || !g_xinput_set_state) {
    StopControllerVibration();
    return;
  }

  const uint64_t now = GetTickCount64();
  const bool attack_pressed =
      pad.bRightTrigger >= g_xinput_trigger_threshold &&
      g_previous_xinput_right_trigger < g_xinput_trigger_threshold;
  const bool block_pressed =
      pad.bLeftTrigger >= g_xinput_trigger_threshold &&
      g_previous_xinput_left_trigger < g_xinput_trigger_threshold;
  if (attack_pressed) {
    g_attack_vibration_until_ms = now + g_xinput_attack_vibration_ms;
  }
  if (block_pressed) {
    g_block_vibration_until_ms = now + g_xinput_block_vibration_ms;
  }
  g_previous_xinput_left_trigger = pad.bLeftTrigger;
  g_previous_xinput_right_trigger = pad.bRightTrigger;

  WORD left_motor = 0;
  WORD right_motor = 0;
  if (now < g_attack_vibration_until_ms) {
    // A short high-frequency pulse confirms the native attack action without
    // pretending to know whether the weapon hit an enemy.
    left_motor = VibrationMotorValue(g_xinput_vibration_strength_percent,
                                     0.32);
    right_motor = VibrationMotorValue(g_xinput_vibration_strength_percent,
                                      0.82);
  }
  if (now < g_block_vibration_until_ms) {
    // Blocking is deliberately lower-frequency and lighter than attacking.
    left_motor = std::max(
        left_motor,
        VibrationMotorValue(g_xinput_vibration_strength_percent, 0.50));
    right_motor = std::max(
        right_motor,
        VibrationMotorValue(g_xinput_vibration_strength_percent, 0.14));
  }
  ApplyControllerVibration(left_motor, right_motor);
}

void ReleaseInjectedControllerInput() {
  for (size_t i = 0; i < g_injected_keys.size(); ++i) {
    InjectVirtualKey(static_cast<InjectedKey>(i), false);
  }
  InjectMouseLeft(false);
  InjectMouseRight(false);
  SubmitDeathtrapXInputMouseState(0, 0, false, false);
  g_xinput_first_person_toggled = false;
  g_xinput_run_active = false;
  g_xinput_menu_mode.store(true, std::memory_order_release);
  g_xinput_previous_native_gameplay = false;
  g_previous_xinput_buttons = 0;
  StopControllerVibration();
}

bool LoadXInputRuntime() {
  if (g_xinput_get_state) {
    return true;
  }
  constexpr std::array<const wchar_t*, 3> kCandidates = {
      L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"};
  for (const wchar_t* candidate : kCandidates) {
    HMODULE module = LoadLibraryW(candidate);
    if (!module) {
      continue;
    }
    auto function = reinterpret_cast<DynamicXInputGetStateFn>(
        GetProcAddress(module, "XInputGetState"));
    if (function) {
      g_xinput_module = module;
      g_xinput_get_state = function;
      g_xinput_set_state = reinterpret_cast<DynamicXInputSetStateFn>(
          GetProcAddress(module, "XInputSetState"));
      return true;
    }
    FreeLibrary(module);
  }
  return false;
}

void PublishControllerSelector(bool visible, uint32_t category,
                               uint32_t slot, bool available,
                               bool confirmation_required) {
  uint32_t packed = 0;
  if (visible) {
    packed |= 1u;
  }
  if (available) {
    packed |= 2u;
  }
  if (confirmation_required) {
    packed |= 4u;
  }
  packed |= (category & 0xFu) << 8u;
  packed |= (slot & 0xFu) << 16u;
  g_controller_selector_overlay.store(packed, std::memory_order_release);
}

void SetNativeSelectorMode(uint8_t mode) {
  if (!g_dungeon_base) {
    return;
  }
  SafeWrite(g_dungeon_base + kUiSelectorModeRva, &mode, sizeof(mode));
}

#pragma pack(push, 1)
struct NativeInventorySlotDrawState {
  int16_t x = 0;
  int16_t y = 0;
  uint8_t icon = 0xFFu;
  uint8_t selected = 0;
  uint8_t available = 0;
  uint8_t slot = 0;
  uint8_t blank = 0;
  uint8_t reserved = 0;
  uint16_t quantity = 0;
};
#pragma pack(pop)
static_assert(sizeof(NativeInventorySlotDrawState) == 12u);

void __cdecl HookInventorySlotDraw(void* raw_slot) {
  if (!g_original_inventory_slot_draw || !raw_slot) {
    return;
  }
  if (!g_controller_selector.row_open ||
      g_controller_selector.cancelled) {
    g_original_inventory_slot_draw(raw_slot);
    return;
  }

  NativeInventorySlotDrawState slot = {};
  std::memcpy(&slot, raw_slot, sizeof(slot));
  if (slot.slot >= 8u) {
    g_original_inventory_slot_draw(raw_slot);
    return;
  }

  // The retail selector renderer uses a centered 640x480-style coordinate
  // space and 32-pixel cells. Reposition its own complete slot draw (frame,
  // icon, highlight, slot number and quantity) into a radial layout. No game
  // textures are copied, replaced or reimplemented here.
  constexpr double kPi = 3.14159265358979323846;
  const double angle = static_cast<double>(slot.slot) * kPi / 4.0;
  slot.x = static_cast<int16_t>(std::lround(
      std::sin(angle) * static_cast<double>(g_xinput_selector_radius) - 16.0));
  // Native selector Y grows upward from the bottom edge (unlike screen/D3D
  // coordinates), so positive cosine places stick-up at the top of the ring.
  slot.y = static_cast<int16_t>(std::lround(
      static_cast<double>(g_xinput_selector_center_y) +
      std::cos(angle) * static_cast<double>(g_xinput_selector_radius) - 16.0));
  slot.selected = slot.slot == g_controller_selector.slot ? 1u : 0u;
  g_original_inventory_slot_draw(&slot);
}

bool NativeInventoryItemAvailable(int32_t item_id) {
  using InventoryLookupFn = void*(__cdecl*)(int32_t);
  void* item = nullptr;
  __try {
    item = reinterpret_cast<InventoryLookupFn>(
        g_dungeon_base + kInventoryLookupRva)(item_id);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    item = nullptr;
  }
  return item != nullptr;
}

bool ControllerSlotAvailable(uint32_t category, uint32_t slot) {
  if (slot >= 8u) {
    return false;
  }
  switch (category) {
    case 1:
      return NativeWeaponAvailable(static_cast<int32_t>(slot));
    case 2:
      // The retail F2 selector exposes six ranged inventory objects, leaves
      // slot 7 empty and draws a dedicated chalk entry in slot 8.
      return (slot < 6u &&
              NativeInventoryItemAvailable(8 + static_cast<int32_t>(slot))) ||
             slot == 7u;
    case 3:
      return NativeInventoryItemAvailable(0x0E +
                                          static_cast<int32_t>(slot));
    case 4:
      return NativeInventoryItemAvailable(0x16 +
                                          static_cast<int32_t>(slot));
    default:
      return false;
  }
}

bool UseNativeChalk() {
  uintptr_t game_root = 0;
  uintptr_t gameplay_owner = 0;
  if (!SafeReadValue(g_dungeon_base + kGameRootPointerRva, &game_root) ||
      !game_root ||
      !SafeReadValue(reinterpret_cast<const void*>(game_root + 0x114u),
                     &gameplay_owner) ||
      !gameplay_owner) {
    AppendNativeLog("xinput chalk F2+8 owner unavailable");
    return false;
  }
  __try {
    reinterpret_cast<void(__cdecl*)(void*)>(
        g_dungeon_base + kUseChalkRva)(
        reinterpret_cast<void*>(gameplay_owner));
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    AppendNativeLog("xinput chalk F2+8 native call failed owner=%08llX",
                    static_cast<unsigned long long>(gameplay_owner));
    return false;
  }
  ++g_xinput_chalk_actions_asserted;
  AppendNativeLog("xinput chalk F2+8 native call owner=%08llX total=%llu",
                  static_cast<unsigned long long>(gameplay_owner),
                  static_cast<unsigned long long>(
                      g_xinput_chalk_actions_asserted));
  return true;
}

uint32_t CurrentControllerSlot(uint32_t category) {
  int32_t active = -1;
  if (category == 1 || category == 2) {
    SafeReadValue(g_dungeon_base + kActiveCloseCombatWeaponRva, &active);
    if (category == 1 && active >= 0 && active <= 7) {
      return static_cast<uint32_t>(active);
    }
    if (category == 2 && active >= 8 && active <= 13) {
      return static_cast<uint32_t>(active - 8);
    }
  } else if (category == 3) {
    SafeReadValue(g_dungeon_base + 0x001D8A6Cu, &active);
    if (active >= 0x0E && active <= 0x15) {
      return static_cast<uint32_t>(active - 0x0E);
    }
  }
  return g_controller_selector.remembered_slot[category - 1u] & 7u;
}

bool CommitControllerSlot(uint32_t category, uint32_t slot) {
  if (!ControllerSlotAvailable(category, slot)) {
    return false;
  }
  __try {
    switch (category) {
      case 1: {
        constexpr std::array<int32_t, 8> kActions = {1, 2, 4, 5,
                                                     6, 7, 8, 0};
        reinterpret_cast<void(__cdecl*)(int32_t, int32_t)>(
            g_dungeon_base + kSelectCloseCombatWeaponRva)(kActions[slot], 1);
        break;
      }
      case 2: {
        if (slot == 7u) {
          // Match the retail F2+8 selector exactly. Its eighth entry does not
          // select a ranged inventory ID and does not use ACTION_CHALK_CROSS;
          // it invokes the dedicated chalk routine with the gameplay owner.
          if (!UseNativeChalk()) {
            return false;
          }
          break;
        }
        constexpr std::array<int32_t, 6> kActions = {
            0x10, 0x0E, 0x0F, 0x11, 0x0C, 0x0D};
        if (slot >= kActions.size()) {
          return false;
        }
        reinterpret_cast<void(__cdecl*)(int32_t, int32_t)>(
            g_dungeon_base + kSelectRangedWeaponRva)(kActions[slot], 1);
        break;
      }
      case 3:
        reinterpret_cast<void(__cdecl*)(int32_t)>(
            g_dungeon_base + kSelectSpellRva)(0x0E +
                                              static_cast<int32_t>(slot));
        break;
      case 4:
        reinterpret_cast<void(__cdecl*)(int32_t)>(
            g_dungeon_base + kUseConsumableRva)(0x16 +
                                                static_cast<int32_t>(slot));
        break;
      default:
        return false;
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
  g_controller_selector.remembered_slot[category - 1u] = slot;
  AppendNativeLog("xinput selector commit category=%u slot=%u", category,
                  slot + 1u);
  return true;
}

uint32_t NextAvailableControllerSlot(uint32_t category, uint32_t current) {
  for (uint32_t distance = 1; distance <= 8u; ++distance) {
    const uint32_t candidate = (current + distance) & 7u;
    if (ControllerSlotAvailable(category, candidate)) {
      return candidate;
    }
  }
  return current & 7u;
}

uint32_t DpadCategory(WORD buttons) {
  if (buttons & XINPUT_GAMEPAD_DPAD_UP) {
    return 1;
  }
  if (buttons & XINPUT_GAMEPAD_DPAD_RIGHT) {
    return 2;
  }
  if (buttons & XINPUT_GAMEPAD_DPAD_DOWN) {
    return 3;
  }
  if (buttons & XINPUT_GAMEPAD_DPAD_LEFT) {
    return 4;
  }
  return 0;
}

uint32_t RightStickSlot(const XINPUT_GAMEPAD& pad, uint32_t fallback) {
  const double x = static_cast<double>(pad.sThumbRX);
  const double y = static_cast<double>(pad.sThumbRY);
  if (x * x + y * y <
      static_cast<double>(g_xinput_right_deadzone) *
          static_cast<double>(g_xinput_right_deadzone)) {
    return fallback;
  }
  constexpr double kPi = 3.14159265358979323846;
  double angle = std::atan2(x, y);
  if (angle < 0.0) {
    angle += 2.0 * kPi;
  }
  return static_cast<uint32_t>(
             std::floor(angle / (kPi / 4.0) + 0.5)) &
         7u;
}

void CloseControllerSelector() {
  if (g_controller_selector.row_open) {
    SetNativeSelectorMode(0);
  }
  PublishControllerSelector(false, 0, 0, false, false);
  g_controller_selector = {};
}

void UpdateControllerSelector(const XINPUT_GAMEPAD& pad, bool gameplay) {
  const uint32_t category = gameplay ? DpadCategory(pad.wButtons) : 0u;
  const uint64_t now = GetTickCount64();
  if (!g_controller_selector.direction_down && category != 0u) {
    uint8_t native_mode = 0;
    if (!SafeReadValue(g_dungeon_base + kUiSelectorModeRva, &native_mode)) {
      return;
    }
    if (native_mode != 0) {
      // A keyboard-opened F1-F4 row remains latched after its key is released.
      // D-pad input explicitly takes ownership so the controller selector is
      // never permanently blocked by the old row.
      SetNativeSelectorMode(0);
      AppendNativeLog("xinput selector takeover native_mode=%u category=%u",
                      native_mode, category);
    }
    g_controller_selector.direction_down = true;
    g_controller_selector.category = category;
    g_controller_selector.slot = CurrentControllerSlot(category);
    g_controller_selector.pressed_ms = now;
    return;
  }
  if (!g_controller_selector.direction_down) {
    return;
  }
  if (category != 0u && category != g_controller_selector.category) {
    CloseControllerSelector();
    return;
  }
  if (category == 0u) {
    const uint64_t duration = now - g_controller_selector.pressed_ms;
    if (!g_controller_selector.row_open &&
        !g_controller_selector.cancelled && duration < g_xinput_selector_hold_ms &&
        g_controller_selector.category != 4u) {
      const uint32_t next = NextAvailableControllerSlot(
          g_controller_selector.category, g_controller_selector.slot);
      CommitControllerSlot(g_controller_selector.category, next);
    } else if (g_controller_selector.row_open &&
               !g_controller_selector.cancelled &&
               g_controller_selector.category != 2u &&
               g_controller_selector.category != 4u) {
      CommitControllerSlot(g_controller_selector.category,
                           g_controller_selector.slot);
    }
    CloseControllerSelector();
    return;
  }

  if (!g_controller_selector.row_open && !g_controller_selector.cancelled &&
      now - g_controller_selector.pressed_ms >= g_xinput_selector_hold_ms) {
    g_controller_selector.row_open = true;
    SetNativeSelectorMode(static_cast<uint8_t>(g_controller_selector.category));
    AppendNativeLog("xinput selector open category=%u",
                    g_controller_selector.category);
  }
  if (!g_controller_selector.row_open || g_controller_selector.cancelled) {
    return;
  }
  g_controller_selector.slot =
      RightStickSlot(pad, g_controller_selector.slot);
  const bool available = ControllerSlotAvailable(
      g_controller_selector.category, g_controller_selector.slot);
  PublishControllerSelector(true, g_controller_selector.category,
                            g_controller_selector.slot, available,
                            g_controller_selector.category == 2u ||
                                g_controller_selector.category == 4u);
  if (pad.wButtons & XINPUT_GAMEPAD_B) {
    g_controller_selector.cancelled = true;
    SetNativeSelectorMode(0);
    PublishControllerSelector(false, 0, 0, false, false);
    AppendNativeLog("xinput selector cancel category=%u",
                    g_controller_selector.category);
  } else if ((g_controller_selector.category == 2u ||
              g_controller_selector.category == 4u) &&
             available && !g_controller_selector.selection_confirmed &&
             (pad.wButtons & XINPUT_GAMEPAD_A)) {
    g_controller_selector.selection_confirmed = CommitControllerSlot(
        g_controller_selector.category, g_controller_selector.slot);
    g_controller_selector.cancelled = true;
    SetNativeSelectorMode(0);
    PublishControllerSelector(false, 0, 0, false, false);
  }
}

double NormalizedStick(SHORT value, int32_t deadzone) {
  const int32_t signed_value = static_cast<int32_t>(value);
  const int32_t magnitude = std::abs(signed_value);
  if (magnitude <= deadzone) {
    return 0.0;
  }
  const double normalized = static_cast<double>(magnitude - deadzone) /
                            static_cast<double>(32767 - deadzone);
  return signed_value < 0 ? -normalized : normalized;
}

void UpdateControllerBaseBindings(const XINPUT_GAMEPAD& pad, bool gameplay,
                                  bool selector_captures_controls) {
  UpdateControllerVibration(pad, gameplay, selector_captures_controls);
  if (!g_xinput_base_bindings) {
    ReleaseInjectedControllerInput();
    return;
  }
  const double left_x = NormalizedStick(pad.sThumbLX, g_xinput_left_deadzone);
  const double left_y = NormalizedStick(pad.sThumbLY, g_xinput_left_deadzone);
  const double right_x =
      NormalizedStick(pad.sThumbRX, g_xinput_right_deadzone);
  const double right_y =
      NormalizedStick(pad.sThumbRY, g_xinput_right_deadzone);
  const WORD buttons = pad.wButtons;
  const WORD pressed = buttons & ~g_previous_xinput_buttons;
  if (gameplay) {
    const bool strafe_modifier =
        (buttons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0;
    InjectVirtualKey(InjectedKey::kW, left_y > g_xinput_movement_threshold);
    InjectVirtualKey(InjectedKey::kS, left_y < -g_xinput_movement_threshold);
    // Default to the original predictable tank turn on the movement stick.
    // Holding LB changes only the horizontal axis to the retail side-step
    // actions, preserving forward/diagonal movement without Ctrl+W clashes.
    InjectVirtualKey(InjectedKey::kA,
                     !strafe_modifier &&
                         left_x < -g_xinput_movement_threshold);
    InjectVirtualKey(InjectedKey::kD,
                     !strafe_modifier &&
                         left_x > g_xinput_movement_threshold);
    InjectVirtualKey(InjectedKey::kJ,
                     strafe_modifier &&
                         left_x < -g_xinput_movement_threshold);
    InjectVirtualKey(InjectedKey::kK,
                     strafe_modifier &&
                         left_x > g_xinput_movement_threshold);
    const double forward_magnitude = std::abs(left_y);
    if (forward_magnitude >= g_xinput_run_threshold) {
      g_xinput_run_active = true;
    } else if (forward_magnitude <= g_xinput_run_release_threshold) {
      g_xinput_run_active = false;
    }
    InjectVirtualKey(InjectedKey::kShift, g_xinput_run_active);
    InjectVirtualKey(InjectedKey::kSpace,
                     !selector_captures_controls &&
                         (buttons & XINPUT_GAMEPAD_A));
    InjectVirtualKey(InjectedKey::kE, buttons & XINPUT_GAMEPAD_X);
    InjectVirtualKey(InjectedKey::kQ, buttons & XINPUT_GAMEPAD_RIGHT_SHOULDER);
    // Chalk is dispatched only by the radial selector through the exact
    // retail F2+8 routine. Never synthesize the unrelated C binding here.
    InjectVirtualKey(InjectedKey::kC, false);
    if (!selector_captures_controls &&
        (pressed & XINPUT_GAMEPAD_RIGHT_THUMB)) {
      g_xinput_first_person_toggled = !g_xinput_first_person_toggled;
    }
    InjectVirtualKey(InjectedKey::kTab,
                     !selector_captures_controls &&
                         g_xinput_first_person_toggled);
    InjectMouseLeft(!selector_captures_controls &&
                    pad.bRightTrigger >= g_xinput_trigger_threshold);
    InjectMouseRight(!selector_captures_controls &&
                     pad.bLeftTrigger >= g_xinput_trigger_threshold);
    SubmitDeathtrapXInputMouseState(0, 0, false, false);
    InjectVirtualKey(InjectedKey::kUp, false);
    InjectVirtualKey(InjectedKey::kDown, false);
    InjectVirtualKey(InjectedKey::kLeft, false);
    InjectVirtualKey(InjectedKey::kRight, false);
    InjectVirtualKey(InjectedKey::kEnter, false);

    if (!selector_captures_controls && g_xinput_first_person_toggled) {
      InjectRelativeMouseMove(
          CurvedStick(right_x, g_xinput_right_stick_curve),
          CurvedStick(right_y, g_xinput_right_stick_curve),
          g_xinput_first_person_pixels);
    }
  } else {
    g_xinput_first_person_toggled = false;
    g_xinput_run_active = false;
    InjectVirtualKey(InjectedKey::kW, false);
    InjectVirtualKey(InjectedKey::kS, false);
    InjectVirtualKey(InjectedKey::kA, false);
    InjectVirtualKey(InjectedKey::kD, false);
    InjectVirtualKey(InjectedKey::kJ, false);
    InjectVirtualKey(InjectedKey::kK, false);
    InjectVirtualKey(InjectedKey::kShift, false);
    // A is simultaneously the native mouse click and the keyboard confirm /
    // movie-skip key. X remains an alternate skip key for the retail screens
    // that bind Space but don't expose a clickable target.
    InjectVirtualKey(InjectedKey::kSpace,
                     (buttons & (XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_X)) != 0);
    InjectVirtualKey(InjectedKey::kE, false);
    InjectVirtualKey(InjectedKey::kQ, false);
    InjectVirtualKey(InjectedKey::kC, false);
    InjectVirtualKey(InjectedKey::kTab, false);
    InjectMouseLeft(false);
    InjectMouseRight(false);
    InjectVirtualKey(InjectedKey::kUp,
                     left_y > 0.35 || (buttons & XINPUT_GAMEPAD_DPAD_UP));
    InjectVirtualKey(InjectedKey::kDown,
                     left_y < -0.35 || (buttons & XINPUT_GAMEPAD_DPAD_DOWN));
    InjectVirtualKey(InjectedKey::kLeft,
                     left_x < -0.35 || (buttons & XINPUT_GAMEPAD_DPAD_LEFT));
    InjectVirtualKey(InjectedKey::kRight,
                     left_x > 0.35 || (buttons & XINPUT_GAMEPAD_DPAD_RIGHT));
    InjectVirtualKey(InjectedKey::kEnter,
                     (buttons & XINPUT_GAMEPAD_A) != 0);
    const int32_t menu_mouse_x = static_cast<int32_t>(std::lround(
        right_x * static_cast<double>(g_xinput_menu_mouse_pixels)));
    const double menu_y_sign = g_xinput_invert_right_y ? 1.0 : -1.0;
    const int32_t menu_mouse_y = static_cast<int32_t>(std::lround(
        right_y * menu_y_sign *
        static_cast<double>(g_xinput_menu_mouse_pixels)));
    SubmitDeathtrapXInputMouseState(
        menu_mouse_x, menu_mouse_y,
        (buttons & XINPUT_GAMEPAD_A) != 0, false);
  }
  InjectVirtualKey(InjectedKey::kEscape,
                   (buttons & XINPUT_GAMEPAD_START) ||
                       (!gameplay && (buttons & XINPUT_GAMEPAD_B)));
  g_previous_xinput_buttons = buttons;
}

void UpdateDeathtrapXInput() {
  if (!g_xinput_enabled || !LoadXInputRuntime()) {
    return;
  }
  ScopedXInputPoll poll(g_xinput_poll_guard);
  if (!poll) {
    return;
  }
  XINPUT_STATE state = {};
  const bool connected =
      g_xinput_get_state(g_xinput_controller_index, &state) == ERROR_SUCCESS;
  if (!connected || !IsGameForeground()) {
    if (g_xinput_was_connected) {
      ReleaseInjectedControllerInput();
      CloseControllerSelector();
    }
    g_xinput_was_connected = connected;
    return;
  }
  if (!g_xinput_was_connected) {
    AppendNativeLog("xinput controller connected index=%u",
                    g_xinput_controller_index);
  }
  g_xinput_was_connected = true;
  const bool native_gameplay = DeathtrapGameplayReady(false);
  const WORD newly_pressed =
      state.Gamepad.wButtons & ~g_previous_xinput_buttons;
  if (!native_gameplay) {
    g_xinput_menu_mode.store(true, std::memory_order_release);
  } else if (!g_xinput_previous_native_gameplay) {
    // Loading/main-menu -> gameplay is an unambiguous automatic transition.
    g_xinput_menu_mode.store(false, std::memory_order_release);
  }
  if (native_gameplay && (newly_pressed & XINPUT_GAMEPAD_START)) {
    // The pause/options menus retain the live player pointer, so the native
    // gameplay test alone cannot identify them. Start is the authoritative
    // transition used by the retail game and by this controller bridge.
    const bool menu_mode =
        !g_xinput_menu_mode.load(std::memory_order_acquire);
    g_xinput_menu_mode.store(menu_mode, std::memory_order_release);
    AppendNativeLog("xinput menu mode=%u", menu_mode ? 1u : 0u);
  }
  g_xinput_previous_native_gameplay = native_gameplay;
  const bool gameplay =
      native_gameplay && !g_xinput_menu_mode.load(std::memory_order_acquire);
  // Frontend DirectInput polling owns movies and menus. The scheduler owns
  // only gameplay so engine actions and the native selector never run from a
  // foreign input thread.
  if (!gameplay) {
    return;
  }
  UpdateControllerSelector(state.Gamepad, gameplay);
  const bool selector_captures_controls =
      g_controller_selector.direction_down &&
      (g_controller_selector.row_open ||
       g_controller_selector.category == 2u ||
       g_controller_selector.category == 4u);
  UpdateControllerBaseBindings(state.Gamepad, gameplay,
                               selector_captures_controls);
}

void PollFrontendXInputInternal() {
  if (!g_xinput_enabled || !LoadXInputRuntime()) {
    return;
  }
  ScopedXInputPoll poll(g_xinput_poll_guard);
  if (!poll) {
    return;
  }
  XINPUT_STATE state = {};
  const bool connected =
      g_xinput_get_state(g_xinput_controller_index, &state) == ERROR_SUCCESS;
  if (!connected || !IsGameForeground()) {
    SubmitDeathtrapXInputMouseState(0, 0, false, false);
    return;
  }

  const bool native_gameplay = DeathtrapGameplayReady(false);
  bool menu_mode = g_xinput_menu_mode.load(std::memory_order_acquire);
  if (!native_gameplay) {
    menu_mode = true;
    g_xinput_menu_mode.store(true, std::memory_order_release);
  }
  if (native_gameplay && !menu_mode) {
    return;
  }

  // The frontend consumes the same retail keyboard/mouse actions as a real
  // user: right stick is the pointer, A is both click/confirm and the movie
  // skip key, B/Start is Escape, and the left stick/D-pad provide keyboard
  // navigation for screens that don't expose a mouse target.
  UpdateControllerBaseBindings(state.Gamepad, false, false);
}

// The old V26 experiment detoured complete engine functions. Those functions
// are shared by loading, UI, animation and gameplay paths, so observing them
// globally changed timing and could deadlock startup. This diagnostic instead
// rewrites only the concrete E8 call instructions in the single gameplay main
// loop at 0x10057CD0. The original functions and every other caller remain
// untouched. Probe wrappers only update fixed-size memory counters; file I/O is
// performed later by the already established render hook.
struct MovementStageSample {
  std::array<int32_t, 3> root{};
  std::array<int32_t, 3> cache{};
  std::array<int32_t, 3> bounds_a{};
  std::array<int32_t, 3> bounds_b{};
  bool root_valid = false;
  bool cache_valid = false;
  bool bounds_a_valid = false;
  bool bounds_b_valid = false;
};

struct MovementStageProbeStats {
  uint64_t calls = 0;
  uint64_t root_changes = 0;
  uint64_t cache_changes = 0;
  uint64_t bounds_a_changes = 0;
  uint64_t bounds_b_changes = 0;
  uint64_t root_reversals = 0;
  std::array<uint64_t, 3> root_absolute_delta{};
  std::array<uint32_t, 3> root_maximum_delta{};
  std::array<int32_t, 3> previous_root_delta{};
  std::array<int32_t, 3> last_root_before{};
  std::array<int32_t, 3> last_root_after{};
  bool previous_root_delta_valid = false;
};

std::array<MovementStageProbeStats, kMovementStageProbeCount>
    g_movement_stage_probe_stats{};

struct MovementCallbackProbeStats {
  uintptr_t target = 0;
  uintptr_t entry = 0;
  MovementStageProbeStats motion;
};

std::array<MovementCallbackProbeStats, kMovementCallbackProbeCount>
    g_movement_callback_probe_stats{};

bool CaptureMovementStageSample(MovementStageSample* sample) {
  if (!sample || !g_dungeon_base) {
    return false;
  }
  *sample = {};
  uintptr_t player = 0;
  uintptr_t render_link = 0;
  uintptr_t render_node = 0;
  if (!SafeReadValue(g_dungeon_base + kUiOwnerPointerRva, &player) ||
      !player) {
    return false;
  }
  if (SafeReadValue(reinterpret_cast<const void*>(player + 0x10u),
                    &render_link) &&
      render_link &&
      SafeReadValue(reinterpret_cast<const void*>(render_link),
                    &render_node) &&
      render_node) {
    sample->root_valid = SafeRead(
        reinterpret_cast<const void*>(render_node), sample->root.data(),
        sizeof(sample->root));
  }
  sample->cache_valid = SafeRead(
      reinterpret_cast<const void*>(player + 0x70u), sample->cache.data(),
      sizeof(sample->cache));
  sample->bounds_a_valid = SafeRead(
      reinterpret_cast<const void*>(player + 0xA4u), sample->bounds_a.data(),
      sizeof(sample->bounds_a));
  sample->bounds_b_valid = SafeRead(
      reinterpret_cast<const void*>(player + 0xC4u), sample->bounds_b.data(),
      sizeof(sample->bounds_b));
  return sample->root_valid || sample->cache_valid ||
         sample->bounds_a_valid || sample->bounds_b_valid;
}

void RecordMovementProbeStats(MovementStageProbeStats& stats,
                              const MovementStageSample& before,
                              const MovementStageSample& after) {
  ++stats.calls;
  if (before.cache_valid && after.cache_valid &&
      before.cache != after.cache) {
    ++stats.cache_changes;
  }
  if (before.bounds_a_valid && after.bounds_a_valid &&
      before.bounds_a != after.bounds_a) {
    ++stats.bounds_a_changes;
  }
  if (before.bounds_b_valid && after.bounds_b_valid &&
      before.bounds_b != after.bounds_b) {
    ++stats.bounds_b_changes;
  }
  if (!before.root_valid || !after.root_valid ||
      before.root == after.root) {
    return;
  }

  ++stats.root_changes;
  std::array<int32_t, 3> delta{};
  int64_t direction_dot = 0;
  for (size_t axis = 0; axis < delta.size(); ++axis) {
    const int64_t wide_delta =
        static_cast<int64_t>(after.root[axis]) - before.root[axis];
    delta[axis] = static_cast<int32_t>(std::clamp<int64_t>(
        wide_delta, std::numeric_limits<int32_t>::min(),
        std::numeric_limits<int32_t>::max()));
    const uint32_t magnitude = static_cast<uint32_t>(
        std::min<int64_t>(std::llabs(wide_delta),
                          std::numeric_limits<uint32_t>::max()));
    stats.root_absolute_delta[axis] += magnitude;
    stats.root_maximum_delta[axis] =
        std::max(stats.root_maximum_delta[axis], magnitude);
    if (stats.previous_root_delta_valid) {
      direction_dot += static_cast<int64_t>(delta[axis]) *
                       stats.previous_root_delta[axis];
    }
  }
  if (stats.previous_root_delta_valid && direction_dot < 0) {
    ++stats.root_reversals;
  }
  stats.previous_root_delta = delta;
  stats.previous_root_delta_valid = true;
  stats.last_root_before = before.root;
  stats.last_root_after = after.root;
}

void RecordMovementStageProbe(size_t index,
                              const MovementStageSample& before,
                              const MovementStageSample& after) {
  if (index >= g_movement_stage_probe_stats.size()) {
    return;
  }
  RecordMovementProbeStats(g_movement_stage_probe_stats[index], before,
                           after);
}

MovementCallbackProbeStats* FindMovementCallbackProbeStats(
    uintptr_t target, uintptr_t entry) {
  MovementCallbackProbeStats* empty = nullptr;
  for (MovementCallbackProbeStats& stats : g_movement_callback_probe_stats) {
    if (stats.target == target) {
      stats.entry = entry;
      return &stats;
    }
    if (!stats.target && !empty) {
      empty = &stats;
    }
  }
  if (empty) {
    empty->target = target;
    empty->entry = entry;
  }
  return empty;
}

void RecordPendingContactProjection(const MovementStageSample& before,
                                    const MovementStageSample& after,
                                    uintptr_t target) {
  if (!before.root_valid || !after.root_valid || before.root == after.root) {
    return;
  }
  std::array<int64_t, 3> delta{};
  bool nonzero = false;
  for (size_t axis = 0; axis < delta.size(); ++axis) {
    delta[axis] = static_cast<int64_t>(after.root[axis]) - before.root[axis];
    // A genuine 0x54D00 projection observed in V36 was only a few dozen
    // fixed-point units.  Reject corrupt/stale pointers without imposing a
    // gameplay-scale threshold on valid collision response.
    if (std::llabs(delta[axis]) > 4096ll) {
      return;
    }
    nonzero = nonzero || delta[axis] != 0;
  }
  if (!nonzero) {
    return;
  }

  const uintptr_t dungeon_base = reinterpret_cast<uintptr_t>(g_dungeon_base);
  const bool resolver_projection =
      dungeon_base && target == dungeon_base + 0x00068390u;
  if (resolver_projection) {
    // V39 validation showed that 0x68390 also performs frequent 1-3 unit
    // bookkeeping adjustments. Promoting those to authoritative collision
    // projections incorrectly clears the proven contact fallback. Ordinary
    // 0x7E530 source motion was measured at up to 14 units per horizontal
    // axis, while the genuine sparse 0x68390 corrections were 52-90 units.
    // Only accept a resolver delta that exceeds that measured ordinary-step
    // envelope. 0x54D00 remains authoritative at any non-zero magnitude.
    const int64_t horizontal_magnitude =
        std::max(std::llabs(delta[0]), std::llabs(delta[2]));
    if (horizontal_magnitude <= 14ll) {
      ++g_contact_projection_68390_small_rejections;
      return;
    }
  }

  std::lock_guard<std::mutex> lock(g_contact_projection_mutex);
  for (size_t axis = 0; axis < delta.size(); ++axis) {
    g_pending_contact_projection[axis] = std::clamp<int64_t>(
        g_pending_contact_projection[axis] + delta[axis], -4096ll, 4096ll);
  }
  ++g_pending_contact_projection_sequence;
  ++g_contact_projection_captures;
  if (resolver_projection) {
    ++g_contact_projection_68390_captures;
  } else {
    ++g_contact_projection_54d00_captures;
  }
}

void ConsumePendingContactProjection(SceneSnapshot* snapshot) {
  if (!snapshot) {
    return;
  }
  std::lock_guard<std::mutex> lock(g_contact_projection_mutex);
  bool nonzero = false;
  for (size_t axis = 0; axis < g_pending_contact_projection.size(); ++axis) {
    snapshot->player_contact_projection[axis] = static_cast<int32_t>(
        g_pending_contact_projection[axis]);
    nonzero = nonzero || g_pending_contact_projection[axis] != 0;
    g_pending_contact_projection[axis] = 0;
  }
  snapshot->player_contact_projection_sequence =
      g_pending_contact_projection_sequence;
  snapshot->player_contact_projection_valid =
      nonzero && snapshot->player != 0 && snapshot->player_position_valid;
  if (snapshot->player_contact_projection_valid) {
    ++g_contact_projection_consumes;
  }
}

uintptr_t __cdecl DispatchMovementCallbackProbe(uintptr_t target,
                                                 uintptr_t entry) {
  using Fn = uintptr_t(__cdecl*)(uintptr_t);
  const bool contact_projection_callback =
      g_dungeon_base &&
      (target == reinterpret_cast<uintptr_t>(g_dungeon_base) + 0x00054D00u ||
       target == reinterpret_cast<uintptr_t>(g_dungeon_base) + 0x00068390u);
  // In production only the measured collision callbacks need sampling. The
  // broad statistics path remains available when DebugLog=1, but has no
  // per-callback coordinate-read cost in normal play.
  if (!contact_projection_callback && !g_debug_log) {
    return reinterpret_cast<Fn>(target)(entry);
  }
  MovementStageSample before;
  MovementStageSample after;
  CaptureMovementStageSample(&before);
  const uintptr_t result = reinterpret_cast<Fn>(target)(entry);
  CaptureMovementStageSample(&after);
  if (contact_projection_callback) {
    RecordPendingContactProjection(before, after, target);
  }
  if (g_debug_log) {
    MovementCallbackProbeStats* const stats =
        FindMovementCallbackProbeStats(target, entry);
    if (stats) {
      RecordMovementProbeStats(stats->motion, before, after);
    }
  }
  return result;
}

#if defined(_M_IX86)
// At 0x100810CE the engine has already pushed the dispatcher entry and put
// its dynamic callback target in EAX. The original five bytes are
//   call eax; add esp, 4
// so this thunk duplicates those exact cdecl semantics and returns with the
// original argument removed. No callback is globally detoured.
__declspec(naked) uintptr_t MovementCallbackProbeThunk() {
  __asm {
    mov ecx, dword ptr [esp + 4]
    push ecx
    push eax
    call DispatchMovementCallbackProbe
    add esp, 8
    ret 4
  }
}

// At 0x10057789 the object update has pushed its controller argument and EDI
// points at the object vtable holder. The original six bytes are
//   call dword ptr [edi+18h]; add esp, 4
// This callsite-specific thunk preserves EDI and the exact cdecl cleanup
// while routing the dynamically selected target through the same read-only
// statistics collector.
__declspec(naked) uintptr_t MovementVtableCallbackProbeThunk() {
  __asm {
    mov eax, dword ptr [edi + 0x18]
    mov ecx, dword ptr [esp + 4]
    push ecx
    push eax
    call DispatchMovementCallbackProbe
    add esp, 8
    ret 4
  }
}
#endif

template <size_t Index, uintptr_t TargetRva>
uintptr_t __cdecl HookMovementStageNoArg() {
  MovementStageSample before;
  MovementStageSample after;
  CaptureMovementStageSample(&before);
  using Fn = uintptr_t(__cdecl*)();
  const uintptr_t result =
      reinterpret_cast<Fn>(g_dungeon_base + TargetRva)();
  CaptureMovementStageSample(&after);
  RecordMovementStageProbe(Index, before, after);
  return result;
}

template <size_t Index, uintptr_t TargetRva>
uintptr_t __cdecl HookMovementStageOneArg(uintptr_t argument) {
  MovementStageSample before;
  MovementStageSample after;
  CaptureMovementStageSample(&before);
  using Fn = uintptr_t(__cdecl*)(uintptr_t);
  const uintptr_t result =
      reinterpret_cast<Fn>(g_dungeon_base + TargetRva)(argument);
  CaptureMovementStageSample(&after);
  RecordMovementStageProbe(Index, before, after);
  return result;
}

template <size_t Index, uintptr_t TargetRva>
uintptr_t __cdecl HookMovementStageTwoArgs(uintptr_t first,
                                           uintptr_t second) {
  MovementStageSample before;
  MovementStageSample after;
  CaptureMovementStageSample(&before);
  using Fn = uintptr_t(__cdecl*)(uintptr_t, uintptr_t);
  const uintptr_t result =
      reinterpret_cast<Fn>(g_dungeon_base + TargetRva)(first, second);
  CaptureMovementStageSample(&after);
  RecordMovementStageProbe(Index, before, after);
  return result;
}

template <size_t Index, uintptr_t TargetRva>
uintptr_t __cdecl HookMovementStageThreeArgs(uintptr_t first,
                                             uintptr_t second,
                                             uintptr_t third) {
  MovementStageSample before;
  MovementStageSample after;
  CaptureMovementStageSample(&before);
  using Fn = uintptr_t(__cdecl*)(uintptr_t, uintptr_t, uintptr_t);
  const uintptr_t result = reinterpret_cast<Fn>(
      g_dungeon_base + TargetRva)(first, second, third);
  CaptureMovementStageSample(&after);
  RecordMovementStageProbe(Index, before, after);
  return result;
}

struct MovementStageCallsite {
  uintptr_t callsite_rva;
  uintptr_t target_rva;
  const char* name;
  void* wrapper;
};

#define NOARG_STAGE(Index, Callsite, Target, Name)                         \
  {Callsite, Target, Name,                                                \
   reinterpret_cast<void*>(&HookMovementStageNoArg<Index, Target>)}
#define ONEARG_STAGE(Index, Callsite, Target, Name)                        \
  {Callsite, Target, Name,                                                \
   reinterpret_cast<void*>(&HookMovementStageOneArg<Index, Target>)}
#define TWOARG_STAGE(Index, Callsite, Target, Name)                        \
  {Callsite, Target, Name,                                                \
   reinterpret_cast<void*>(&HookMovementStageTwoArgs<Index, Target>)}
#define THREEARG_STAGE(Index, Callsite, Target, Name)                      \
  {Callsite, Target, Name,                                                \
   reinterpret_cast<void*>(&HookMovementStageThreeArgs<Index, Target>)}

const std::array<MovementStageCallsite, kMovementStageProbeCount>
    kMovementStageCallsites = {{
        NOARG_STAGE(0, 0x00057CF1u, 0x00007CA0u, "pre_07CA0"),
        NOARG_STAGE(1, 0x00057CF6u, 0x00040420u, "input_40420"),
        NOARG_STAGE(2, 0x00057CFBu, 0x000810A0u, "stage_810A0"),
        NOARG_STAGE(3, 0x00057D0Au, 0x00003D00u, "stage_03D00"),
        NOARG_STAGE(4, 0x00057D0Fu, 0x00059190u, "events_59190"),
        NOARG_STAGE(5, 0x00057D14u, 0x00093FF0u, "stage_93FF0"),
        NOARG_STAGE(6, 0x00057D19u, 0x00027130u, "stage_27130"),
        TWOARG_STAGE(7, 0x00057D22u, 0x00047D80u, "stage_47D80"),
        NOARG_STAGE(8, 0x00057D2Au, 0x000820C0u, "timers_820C0"),
        NOARG_STAGE(9, 0x00057D2Fu, 0x0001BA40u, "stage_1BA40"),
        NOARG_STAGE(10, 0x00057D34u, 0x0001F860u, "stage_1F860"),
        NOARG_STAGE(11, 0x00057D39u, 0x00049FE0u, "pre_render_49FE0"),
        NOARG_STAGE(12, 0x00057D4Du, 0x00049FE0u, "post_render_49FE0"),
        NOARG_STAGE(13, 0x00057D52u, 0x0001B9D0u, "stage_1B9D0"),
        NOARG_STAGE(14, 0x00057D57u, 0x0004DA90u, "effects_4DA90"),
        NOARG_STAGE(15, 0x00057D5Cu, 0x00096580u, "stage_96580"),
        ONEARG_STAGE(16, 0x00057D68u, 0x0001C2D0u, "player_1C2D0"),
        NOARG_STAGE(17, 0x00057D70u, 0x0009D8D0u, "stage_9D8D0"),
        NOARG_STAGE(18, 0x00057D75u, 0x000079C0u, "stage_079C0"),
        NOARG_STAGE(19, 0x00057D7Au, 0x0007D2A0u, "stage_7D2A0"),
        NOARG_STAGE(20, 0x00057D7Fu, 0x000237A0u, "stage_237A0"),
        NOARG_STAGE(21, 0x00057D84u, 0x00046080u, "stage_46080"),
        NOARG_STAGE(22, 0x00057D89u, 0x00040520u, "ui_40520"),
        ONEARG_STAGE(23, 0x00057D9Du, 0x0009A840u, "stage_9A840"),
        NOARG_STAGE(24, 0x00057DA5u, 0x0009A850u, "stage_9A850"),
        NOARG_STAGE(25, 0x00057DAAu, 0x00087A70u, "stage_87A70"),
        ONEARG_STAGE(26, 0x00057DB0u, 0x0000C3A0u, "stage_0C3A0"),
        NOARG_STAGE(27, 0x00057DBFu, 0x0007BB60u, "stage_7BB60"),
        ONEARG_STAGE(28, 0x00057DD3u, 0x0006C540u, "stage_6C540"),
        ONEARG_STAGE(29, 0x00057DDCu, 0x0001CCC0u, "cache_1CCC0"),
        ONEARG_STAGE(30, 0x00057DE5u, 0x0001DAB0u, "stage_1DAB0"),
        NOARG_STAGE(31, 0x00057DF3u, 0x00095890u, "stage_95890"),
        NOARG_STAGE(32, 0x00057DF8u, 0x00092BD0u, "stage_92BD0"),
        NOARG_STAGE(33, 0x00057DFDu, 0x0004B660u, "stage_4B660"),
        NOARG_STAGE(34, 0x00057E02u, 0x000878D0u, "stage_878D0"),
        NOARG_STAGE(35, 0x00057E07u, 0x00045770u, "stage_45770"),
        NOARG_STAGE(36, 0x00057E0Cu, 0x0009B290u, "stage_9B290"),
        NOARG_STAGE(37, 0x00057E11u, 0x000986C0u, "stage_986C0"),
        NOARG_STAGE(38, 0x00057E16u, 0x0004D5A0u, "stage_4D5A0"),
        NOARG_STAGE(39, 0x00057E1Bu, 0x00007CF0u, "tail_07CF0"),
        NOARG_STAGE(40, 0x0009001Cu, 0x000403F0u, "player_403F0"),
        ONEARG_STAGE(41, 0x0009002Au, 0x00069890u, "player_69890"),
        NOARG_STAGE(42, 0x0009003Fu, 0x00087000u, "player_87000"),
        NOARG_STAGE(43, 0x00090058u, 0x0002F920u, "player_2F920_a"),
        NOARG_STAGE(44, 0x00090061u, 0x0002F930u, "player_2F930_a"),
        NOARG_STAGE(45, 0x0009006Au, 0x0002F920u, "player_2F920_b"),
        NOARG_STAGE(46, 0x00090073u, 0x0002F930u, "player_2F930_b"),
        ONEARG_STAGE(47, 0x00090082u, 0x000904C0u, "player_904C0"),
        NOARG_STAGE(48, 0x0009008Au, 0x000860E0u, "player_860E0"),
        TWOARG_STAGE(49, 0x000900AFu, 0x00082840u, "player_82840_a"),
        ONEARG_STAGE(50, 0x000900D4u, 0x0008FDE0u, "player_8FDE0"),
        ONEARG_STAGE(51, 0x000900DDu, 0x0008FD20u, "player_8FD20"),
        ONEARG_STAGE(52, 0x000900E6u, 0x0008FC10u, "player_8FC10"),
        ONEARG_STAGE(53, 0x000900F4u, 0x000451C0u, "player_451C0"),
        ONEARG_STAGE(54, 0x000900FDu, 0x00053DD0u, "player_53DD0"),
        ONEARG_STAGE(55, 0x00090106u, 0x00083690u, "player_83690"),
        TWOARG_STAGE(56, 0x00090114u, 0x00082840u, "player_82840_b"),
        ONEARG_STAGE(57, 0x00090137u, 0x00082C70u, "player_82C70_a"),
        ONEARG_STAGE(58, 0x0009014Au, 0x00082C90u, "player_82C90"),
        TWOARG_STAGE(59, 0x00090160u, 0x00082840u, "player_82840_c"),
        ONEARG_STAGE(60, 0x0009016Du, 0x00083340u, "player_83340_a"),
        THREEARG_STAGE(61, 0x000901D4u, 0x0001D210u, "player_1D210"),
        TWOARG_STAGE(62, 0x000901F5u, 0x00082840u, "player_82840_d"),
        TWOARG_STAGE(63, 0x00090207u, 0x00082840u, "player_82840_e"),
        TWOARG_STAGE(64, 0x00090216u, 0x0001C480u, "player_1C480_a"),
        TWOARG_STAGE(65, 0x00090232u, 0x00082840u, "player_82840_f"),
        TWOARG_STAGE(66, 0x00090244u, 0x00082840u, "player_82840_g"),
        TWOARG_STAGE(67, 0x00090253u, 0x0001C480u, "player_1C480_b"),
        TWOARG_STAGE(68, 0x00090266u, 0x00082840u, "player_82840_h"),
        TWOARG_STAGE(69, 0x00090278u, 0x00082840u, "player_82840_i"),
        TWOARG_STAGE(70, 0x00090287u, 0x0001C480u, "player_1C480_c"),
        TWOARG_STAGE(71, 0x0009029Au, 0x00082840u, "player_82840_j"),
        TWOARG_STAGE(72, 0x000902ACu, 0x00082840u, "player_82840_k"),
        TWOARG_STAGE(73, 0x000902BBu, 0x0001C480u, "player_1C480_d"),
        TWOARG_STAGE(74, 0x000902CEu, 0x00082840u, "player_82840_l"),
        TWOARG_STAGE(75, 0x000902EFu, 0x00083440u, "player_83440"),
        TWOARG_STAGE(76, 0x00090302u, 0x00082840u, "player_82840_m"),
        ONEARG_STAGE(77, 0x00090313u, 0x00083340u, "player_83340_b"),
        ONEARG_STAGE(78, 0x00090333u, 0x000836D0u, "player_836D0"),
        THREEARG_STAGE(79, 0x00090367u, 0x0001CEC0u, "player_1CEC0_a"),
        THREEARG_STAGE(80, 0x0009039Du, 0x0001EC20u, "player_1EC20"),
        ONEARG_STAGE(81, 0x000903A6u, 0x0004E570u, "player_4E570"),
        THREEARG_STAGE(82, 0x000903F8u, 0x0001CEC0u, "player_1CEC0_b"),
        ONEARG_STAGE(83, 0x0009042Du, 0x00082C70u, "player_82C70_b"),
        TWOARG_STAGE(84, 0x00090455u, 0x00090610u, "player_90610"),
        TWOARG_STAGE(85, 0x0009046Fu, 0x00090740u, "player_90740"),
        ONEARG_STAGE(86, 0x0008275Du, 0x00057760u, "resolver_57760"),
        ONEARG_STAGE(87, 0x00082786u, 0x000833D0u, "resolver_833D0"),
        TWOARG_STAGE(88, 0x000827ADu, 0x000444A0u, "resolver_444A0"),
        THREEARG_STAGE(89, 0x000827BDu, 0x00044470u, "resolver_44470"),
        ONEARG_STAGE(90, 0x0005776Bu, 0x00057610u, "contact_57610"),
        THREEARG_STAGE(91, 0x00057778u, 0x00053CF0u, "contact_53CF0"),
        ONEARG_STAGE(92, 0x00057781u, 0x000528C0u, "contact_528C0"),
        ONEARG_STAGE(93, 0x00057791u, 0x00052990u, "contact_52990"),
        ONEARG_STAGE(94, 0x000577A0u, 0x00053E80u, "contact_53E80"),
    }};

#undef NOARG_STAGE
#undef ONEARG_STAGE
#undef TWOARG_STAGE
#undef THREEARG_STAGE

bool PatchMovementStageCallsite(const MovementStageCallsite& stage) {
  uint8_t* const callsite = g_dungeon_base + stage.callsite_rva;
  uint8_t opcode = 0;
  int32_t old_relative = 0;
  if (!SafeRead(callsite, &opcode, sizeof(opcode)) || opcode != 0xE8u ||
      !SafeRead(callsite + 1u, &old_relative, sizeof(old_relative))) {
    AppendNativeLog("movement_callsite_invalid name=%s callsite=%08llX",
                    stage.name,
                    static_cast<unsigned long long>(stage.callsite_rva));
    return false;
  }
  uint8_t* const old_target = callsite + 5u + old_relative;
  if (old_target != g_dungeon_base + stage.target_rva) {
    AppendNativeLog(
        "movement_callsite_target_mismatch name=%s callsite=%08llX "
        "actual_rva=%08llX expected_rva=%08llX",
        stage.name,
        static_cast<unsigned long long>(stage.callsite_rva),
        static_cast<unsigned long long>(old_target - g_dungeon_base),
        static_cast<unsigned long long>(stage.target_rva));
    return false;
  }
  const intptr_t wide_relative =
      reinterpret_cast<uint8_t*>(stage.wrapper) - (callsite + 5u);
  if (wide_relative < std::numeric_limits<int32_t>::min() ||
      wide_relative > std::numeric_limits<int32_t>::max()) {
    AppendNativeLog("movement_callsite_out_of_range name=%s", stage.name);
    return false;
  }
  const int32_t new_relative = static_cast<int32_t>(wide_relative);
  DWORD old_protection = 0;
  if (!VirtualProtect(callsite + 1u, sizeof(new_relative),
                      PAGE_EXECUTE_READWRITE, &old_protection)) {
    AppendNativeLog("movement_callsite_protect_failed name=%s", stage.name);
    return false;
  }
  const bool wrote = SafeWrite(callsite + 1u, &new_relative,
                               sizeof(new_relative));
  FlushInstructionCache(GetCurrentProcess(), callsite, 5u);
  DWORD ignored = 0;
  VirtualProtect(callsite + 1u, sizeof(new_relative), old_protection,
                 &ignored);
  return wrote;
}

bool InstallMovementStageCallsiteProbes() {
  if (!g_debug_log ||
      g_movement_stage_probes_installed.load(std::memory_order_acquire)) {
    return true;
  }
  size_t installed = 0;
  for (const MovementStageCallsite& stage : kMovementStageCallsites) {
    if (PatchMovementStageCallsite(stage)) {
      ++installed;
    }
  }
  const bool complete = installed == kMovementStageCallsites.size();
  g_movement_stage_probes_installed.store(complete,
                                           std::memory_order_release);
  AppendNativeLog("movement_callsite_probe_install installed=%zu requested=%zu",
                  installed, kMovementStageCallsites.size());
  return complete;
}

bool InstallMovementDispatcherCallbackProbe() {
  if (g_movement_callback_probe_installed.load(std::memory_order_acquire)) {
    return true;
  }
#if !defined(_M_IX86)
  return false;
#else
  constexpr std::array<uint8_t, 5> expected = {
      0xFFu, 0xD0u, 0x83u, 0xC4u, 0x04u};
  size_t installed = 0;
  for (const uintptr_t callback_rva : kMovementDynamicCallbackRvas) {
      const bool resolver_capture_callsite =
          std::find(kMovementResolverCallbackRvas.begin(),
                    kMovementResolverCallbackRvas.end(), callback_rva) !=
          kMovementResolverCallbackRvas.end();
      if (!g_debug_log && !resolver_capture_callsite) {
        continue;
      }
      uint8_t* const callsite = g_dungeon_base + callback_rva;
      std::array<uint8_t, 5> actual{};
      if (!SafeRead(callsite, actual.data(), actual.size()) ||
          actual != expected) {
        AppendNativeLog(
            "movement_callback_probe_invalid callsite=%08llX "
            "bytes=%02X%02X%02X%02X%02X",
            static_cast<unsigned long long>(callback_rva), actual[0],
            actual[1], actual[2], actual[3], actual[4]);
        continue;
      }
      const intptr_t wide_relative =
          reinterpret_cast<uint8_t*>(&MovementCallbackProbeThunk) -
          (callsite + 5u);
      if (wide_relative < std::numeric_limits<int32_t>::min() ||
          wide_relative > std::numeric_limits<int32_t>::max()) {
        AppendNativeLog(
            "movement_callback_probe_out_of_range callsite=%08llX",
            static_cast<unsigned long long>(callback_rva));
        continue;
      }
      std::array<uint8_t, 5> replacement{};
      replacement[0] = 0xE8u;
      const int32_t relative = static_cast<int32_t>(wide_relative);
      std::memcpy(replacement.data() + 1u, &relative, sizeof(relative));
      DWORD old_protection = 0;
      if (!VirtualProtect(callsite, replacement.size(),
                          PAGE_EXECUTE_READWRITE, &old_protection)) {
        AppendNativeLog(
            "movement_callback_probe_protect_failed callsite=%08llX",
            static_cast<unsigned long long>(callback_rva));
        continue;
      }
      const bool wrote =
          SafeWrite(callsite, replacement.data(), replacement.size());
      FlushInstructionCache(GetCurrentProcess(), callsite,
                            replacement.size());
      DWORD ignored = 0;
      VirtualProtect(callsite, replacement.size(), old_protection, &ignored);
      installed += wrote ? 1u : 0u;
  }
  constexpr std::array<uint8_t, 6> vtable_expected = {
      0xFFu, 0x57u, 0x18u, 0x83u, 0xC4u, 0x04u};
  uint8_t* const vtable_callsite =
      g_dungeon_base + kMovementVtableCallbackRva;
  std::array<uint8_t, 6> vtable_actual{};
  if (!SafeRead(vtable_callsite, vtable_actual.data(), vtable_actual.size()) ||
      vtable_actual != vtable_expected) {
    AppendNativeLog(
        "movement_callback_probe_invalid callsite=%08llX "
        "bytes=%02X%02X%02X%02X%02X%02X",
        static_cast<unsigned long long>(kMovementVtableCallbackRva),
        vtable_actual[0], vtable_actual[1], vtable_actual[2],
        vtable_actual[3], vtable_actual[4], vtable_actual[5]);
  } else {
    const intptr_t wide_relative =
        reinterpret_cast<uint8_t*>(&MovementVtableCallbackProbeThunk) -
        (vtable_callsite + 5u);
    if (wide_relative < std::numeric_limits<int32_t>::min() ||
        wide_relative > std::numeric_limits<int32_t>::max()) {
      AppendNativeLog(
          "movement_callback_probe_out_of_range callsite=%08llX",
          static_cast<unsigned long long>(kMovementVtableCallbackRva));
    } else {
      std::array<uint8_t, 6> replacement{};
      replacement[0] = 0xE8u;
      const int32_t relative = static_cast<int32_t>(wide_relative);
      std::memcpy(replacement.data() + 1u, &relative, sizeof(relative));
      replacement[5] = 0x90u;
      DWORD old_protection = 0;
      if (!VirtualProtect(vtable_callsite, replacement.size(),
                          PAGE_EXECUTE_READWRITE, &old_protection)) {
        AppendNativeLog(
            "movement_callback_probe_protect_failed callsite=%08llX",
            static_cast<unsigned long long>(kMovementVtableCallbackRva));
      } else {
        const bool wrote = SafeWrite(vtable_callsite, replacement.data(),
                                     replacement.size());
        FlushInstructionCache(GetCurrentProcess(), vtable_callsite,
                              replacement.size());
        DWORD ignored = 0;
        VirtualProtect(vtable_callsite, replacement.size(), old_protection,
                       &ignored);
        installed += wrote ? 1u : 0u;
      }
    }
  }
  const size_t requested =
      (g_debug_log ? kMovementDynamicCallbackRvas.size()
                   : kMovementResolverCallbackRvas.size()) +
      1u;
  const bool complete = installed == requested;
  g_movement_callback_probe_installed.store(complete,
                                             std::memory_order_release);
  AppendNativeLog(
      "movement_callback_probe_install installed=%zu requested=%zu",
      installed, requested);
  return complete;
#endif
}

void FlushMovementStageProbeStats(uint64_t source_tick) {
  for (size_t index = 0; index < g_movement_stage_probe_stats.size();
       ++index) {
    MovementStageProbeStats& stats = g_movement_stage_probe_stats[index];
    if (!stats.root_changes && !stats.cache_changes &&
        !stats.bounds_a_changes && !stats.bounds_b_changes) {
      stats = {};
      continue;
    }
    const MovementStageCallsite& stage = kMovementStageCallsites[index];
    AppendNativeLog(
        "movement_callsite tick=%llu index=%zu name=%s calls=%llu "
        "root=%llu cache=%llu bounds_a=%llu bounds_b=%llu reversals=%llu "
        "abs_root=%llu/%llu/%llu max_root=%u/%u/%u "
        "last_root=%d/%d/%d->%d/%d/%d",
        static_cast<unsigned long long>(source_tick), index, stage.name,
        static_cast<unsigned long long>(stats.calls),
        static_cast<unsigned long long>(stats.root_changes),
        static_cast<unsigned long long>(stats.cache_changes),
        static_cast<unsigned long long>(stats.bounds_a_changes),
        static_cast<unsigned long long>(stats.bounds_b_changes),
        static_cast<unsigned long long>(stats.root_reversals),
        static_cast<unsigned long long>(stats.root_absolute_delta[0]),
        static_cast<unsigned long long>(stats.root_absolute_delta[1]),
        static_cast<unsigned long long>(stats.root_absolute_delta[2]),
        stats.root_maximum_delta[0], stats.root_maximum_delta[1],
        stats.root_maximum_delta[2], stats.last_root_before[0],
        stats.last_root_before[1], stats.last_root_before[2],
        stats.last_root_after[0], stats.last_root_after[1],
        stats.last_root_after[2]);
    stats = {};
  }
}

void FlushMovementCallbackProbeStats(uint64_t source_tick) {
  const uintptr_t base = reinterpret_cast<uintptr_t>(g_dungeon_base);
  for (size_t index = 0; index < g_movement_callback_probe_stats.size();
       ++index) {
    MovementCallbackProbeStats& callback =
        g_movement_callback_probe_stats[index];
    MovementStageProbeStats& stats = callback.motion;
    if (!callback.target || !stats.calls) {
      continue;
    }
    const uintptr_t target_rva =
        callback.target >= base &&
                callback.target < base + kExpectedImageSize
            ? callback.target - base
            : callback.target;
    uint32_t entry_state = 0xFFFFFFFFu;
    SafeReadValue(reinterpret_cast<const void*>(callback.entry + 0x14u),
                  &entry_state);
    AppendNativeLog(
        "movement_callback tick=%llu slot=%zu target=%08llX entry=%08llX "
        "state=%u calls=%llu root=%llu cache=%llu bounds_a=%llu "
        "bounds_b=%llu reversals=%llu abs_root=%llu/%llu/%llu "
        "max_root=%u/%u/%u last_root=%d/%d/%d->%d/%d/%d",
        static_cast<unsigned long long>(source_tick), index,
        static_cast<unsigned long long>(target_rva),
        static_cast<unsigned long long>(callback.entry), entry_state,
        static_cast<unsigned long long>(stats.calls),
        static_cast<unsigned long long>(stats.root_changes),
        static_cast<unsigned long long>(stats.cache_changes),
        static_cast<unsigned long long>(stats.bounds_a_changes),
        static_cast<unsigned long long>(stats.bounds_b_changes),
        static_cast<unsigned long long>(stats.root_reversals),
        static_cast<unsigned long long>(stats.root_absolute_delta[0]),
        static_cast<unsigned long long>(stats.root_absolute_delta[1]),
        static_cast<unsigned long long>(stats.root_absolute_delta[2]),
        stats.root_maximum_delta[0], stats.root_maximum_delta[1],
        stats.root_maximum_delta[2], stats.last_root_before[0],
        stats.last_root_before[1], stats.last_root_before[2],
        stats.last_root_after[0], stats.last_root_after[1],
        stats.last_root_after[2]);
    stats = {};
  }
}

double Dot(const Vec3& a, const Vec3& b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 Scale(const Vec3& value, double factor) {
  return {value.x * factor, value.y * factor, value.z * factor};
}

Vec3 Subtract(const Vec3& a, const Vec3& b) {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 Cross(const Vec3& a, const Vec3& b) {
  return {a.y * b.z - a.z * b.y,
          a.z * b.x - a.x * b.z,
          a.x * b.y - a.y * b.x};
}

double Length(const Vec3& value) {
  return std::sqrt(Dot(value, value));
}

bool Normalize(Vec3* value) {
  const double length = Length(*value);
  if (!std::isfinite(length) || length < 1.0e-6) {
    return false;
  }
  *value = Scale(*value, 1.0 / length);
  return true;
}

Quaternion NormalizeQuaternion(Quaternion value) {
  const double length = std::sqrt(value.w * value.w + value.x * value.x +
                                  value.y * value.y + value.z * value.z);
  if (!std::isfinite(length) || length < 1.0e-8) {
    return {};
  }
  value.w /= length;
  value.x /= length;
  value.y /= length;
  value.z /= length;
  return value;
}

Quaternion QuaternionFromRows(const std::array<Vec3, 3>& rows) {
  const double m00 = rows[0].x;
  const double m01 = rows[0].y;
  const double m02 = rows[0].z;
  const double m10 = rows[1].x;
  const double m11 = rows[1].y;
  const double m12 = rows[1].z;
  const double m20 = rows[2].x;
  const double m21 = rows[2].y;
  const double m22 = rows[2].z;
  Quaternion q;
  const double trace = m00 + m11 + m22;
  if (trace > 0.0) {
    const double s = std::sqrt(trace + 1.0) * 2.0;
    q.w = 0.25 * s;
    q.x = (m21 - m12) / s;
    q.y = (m02 - m20) / s;
    q.z = (m10 - m01) / s;
  } else if (m00 > m11 && m00 > m22) {
    const double s = std::sqrt(1.0 + m00 - m11 - m22) * 2.0;
    q.w = (m21 - m12) / s;
    q.x = 0.25 * s;
    q.y = (m01 + m10) / s;
    q.z = (m02 + m20) / s;
  } else if (m11 > m22) {
    const double s = std::sqrt(1.0 + m11 - m00 - m22) * 2.0;
    q.w = (m02 - m20) / s;
    q.x = (m01 + m10) / s;
    q.y = 0.25 * s;
    q.z = (m12 + m21) / s;
  } else {
    const double s = std::sqrt(1.0 + m22 - m00 - m11) * 2.0;
    q.w = (m10 - m01) / s;
    q.x = (m02 + m20) / s;
    q.y = (m12 + m21) / s;
    q.z = 0.25 * s;
  }
  return NormalizeQuaternion(q);
}

std::array<Vec3, 3> RowsFromQuaternion(const Quaternion& input) {
  const Quaternion q = NormalizeQuaternion(input);
  const double xx = q.x * q.x;
  const double yy = q.y * q.y;
  const double zz = q.z * q.z;
  const double xy = q.x * q.y;
  const double xz = q.x * q.z;
  const double yz = q.y * q.z;
  const double wx = q.w * q.x;
  const double wy = q.w * q.y;
  const double wz = q.w * q.z;
  return {{{1.0 - 2.0 * (yy + zz), 2.0 * (xy - wz),
            2.0 * (xz + wy)},
           {2.0 * (xy + wz), 1.0 - 2.0 * (xx + zz),
            2.0 * (yz - wx)},
           {2.0 * (xz - wy), 2.0 * (yz + wx),
            1.0 - 2.0 * (xx + yy)}}};
}

bool DecodeRigid(const Matrix3x4& matrix, RigidTransform* transform) {
  std::array<Vec3, 3> original{};
  for (size_t row = 0; row < 3; ++row) {
    original[row] = {
        matrix.values[row * 3 + 0] / kMatrixFixedScale,
        matrix.values[row * 3 + 1] / kMatrixFixedScale,
        matrix.values[row * 3 + 2] / kMatrixFixedScale};
  }
  const double sx = Length(original[0]);
  const double sy = Length(original[1]);
  const double sz = Length(original[2]);
  if (!std::isfinite(sx) || !std::isfinite(sy) || !std::isfinite(sz) ||
      sx < 0.01 || sy < 0.01 || sz < 0.01 ||
      sx > 100.0 || sy > 100.0 || sz > 100.0) {
    return false;
  }

  const Vec3 normalized0 = Scale(original[0], 1.0 / sx);
  const Vec3 normalized1 = Scale(original[1], 1.0 / sy);
  const Vec3 normalized2 = Scale(original[2], 1.0 / sz);
  if (std::abs(Dot(normalized0, normalized1)) > kMaximumBasisDot ||
      std::abs(Dot(normalized0, normalized2)) > kMaximumBasisDot ||
      std::abs(Dot(normalized1, normalized2)) > kMaximumBasisDot) {
    // Projection, shear and transitional visibility matrices are not rigid
    // object poses. Gram-Schmidt would make them look valid while changing
    // their actual meaning, so leave those nodes at the exact endpoint.
    return false;
  }

  Vec3 row0 = original[0];
  if (!Normalize(&row0)) {
    return false;
  }
  Vec3 row1 = Subtract(original[1], Scale(row0, Dot(original[1], row0)));
  if (!Normalize(&row1)) {
    return false;
  }
  Vec3 row2 = Cross(row0, row1);
  if (!Normalize(&row2) || Dot(row2, original[2]) < 0.0) {
    return false;
  }

  transform->rows = {row0, row1, row2};
  transform->scale = {sx, sy, sz};
  transform->translation = {
      matrix.values[9] / kMatrixFixedScale,
      matrix.values[10] / kMatrixFixedScale,
      matrix.values[11] / kMatrixFixedScale};
  transform->rotation = QuaternionFromRows(transform->rows);
  return std::isfinite(transform->translation.x) &&
         std::isfinite(transform->translation.y) &&
         std::isfinite(transform->translation.z);
}

double QuaternionAngularDistanceDegrees(Quaternion a, Quaternion b) {
  a = NormalizeQuaternion(a);
  b = NormalizeQuaternion(b);
  double dot = std::abs(a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z);
  dot = std::clamp(dot, 0.0, 1.0);
  return 2.0 * std::acos(dot) * 180.0 / 3.14159265358979323846;
}

bool HasPlayerContactCorrection(const Matrix3x4& older,
                                const Matrix3x4& previous,
                                const Matrix3x4& current) {
  RigidTransform a;
  RigidTransform b;
  RigidTransform c;
  if (!DecodeRigid(older, &a) || !DecodeRigid(previous, &b) ||
      !DecodeRigid(current, &c)) {
    return false;
  }

  // Collision resolution moves the player root forward and immediately back
  // by only a few Q14 units. Do not expose an extra halfway pose on that
  // reverse leg. The exact native endpoints are left completely untouched.
  const Vec3 incoming = Subtract(b.translation, a.translation);
  const Vec3 outgoing = Subtract(c.translation, b.translation);
  const double incoming_length = Length(incoming);
  const double outgoing_length = Length(outgoing);
  const double maximum_leg = std::max(incoming_length, outgoing_length);
  if (incoming_length >= (1.0 / kMatrixFixedScale) &&
      outgoing_length >= (1.0 / kMatrixFixedScale) &&
      maximum_leg <= 0.25) {
    const double direction = Dot(incoming, outgoing) /
                             (incoming_length * outgoing_length);
    const double round_trip = Length(Subtract(c.translation, a.translation));
    if (std::isfinite(direction) && direction < -0.15 &&
        round_trip < 1.10 * maximum_leg) {
      return true;
    }
  }
  return false;
}

bool HasPlayerRawContactCorrection(
    const std::array<int32_t, 3>& older,
    const std::array<int32_t, 3>& previous,
    const std::array<int32_t, 3>& current) {
  const Vec3 a{static_cast<double>(older[0]),
               static_cast<double>(older[1]),
               static_cast<double>(older[2])};
  const Vec3 b{static_cast<double>(previous[0]),
               static_cast<double>(previous[1]),
               static_cast<double>(previous[2])};
  const Vec3 c{static_cast<double>(current[0]),
               static_cast<double>(current[1]),
               static_cast<double>(current[2])};
  const Vec3 incoming = Subtract(b, a);
  const Vec3 outgoing = Subtract(c, b);
  const double incoming_length = Length(incoming);
  const double outgoing_length = Length(outgoing);
  const double maximum_leg = std::max(incoming_length, outgoing_length);
  if (incoming_length < 1.0 || outgoing_length < 1.0 ||
      !std::isfinite(maximum_leg)) {
    return false;
  }

  // The gameplay coordinate is corrected before the render cache is built.
  // At a blocked wall it therefore forms a very tight A -> B -> A pattern,
  // even when the derived world matrix has already hidden part of the kick.
  // Restrict this to a strong reversal returning close to A so ordinary
  // forward motion and animation changes remain fully interpolated.
  const double direction =
      Dot(incoming, outgoing) / (incoming_length * outgoing_length);
  const double round_trip = Length(Subtract(c, a));
  return std::isfinite(direction) && direction < -0.35 &&
         round_trip < 0.45 * maximum_leg;
}

void RecordAxisReturnBins(const Vec3& older, const Vec3& previous,
                          const Vec3& current, double minimum_leg,
                          double maximum_leg, AxisReturnBins* bins) {
  const std::array<double, 3> a = {older.x, older.y, older.z};
  const std::array<double, 3> b = {previous.x, previous.y, previous.z};
  const std::array<double, 3> c = {current.x, current.y, current.z};
  for (size_t axis = 0; axis < a.size(); ++axis) {
    const double incoming = b[axis] - a[axis];
    const double outgoing = c[axis] - b[axis];
    const double largest = std::max(std::abs(incoming), std::abs(outgoing));
    if (std::abs(incoming) < minimum_leg ||
        std::abs(outgoing) < minimum_leg || largest > maximum_leg ||
        incoming * outgoing >= 0.0) {
      continue;
    }
    const double return_ratio = std::abs(c[axis] - a[axis]) / largest;
    if (!std::isfinite(return_ratio) || return_ratio >= 1.0) {
      continue;
    }
    const size_t bin = return_ratio < 0.25 ? 0u
                       : return_ratio < 0.50 ? 1u
                       : return_ratio < 0.75 ? 2u
                                             : 3u;
    ++(*bins)[axis][bin];
  }
}

void RecordPlayerAxisTelemetry(const SceneSnapshot& older,
                               const SceneSnapshot& previous,
                               const SceneSnapshot& current,
                               uintptr_t player_render_node) {
  const auto older_player = older.nodes.find(player_render_node);
  const auto previous_player = previous.nodes.find(player_render_node);
  const auto current_player = current.nodes.find(player_render_node);
  if (older_player != older.nodes.end() &&
      previous_player != previous.nodes.end() &&
      current_player != current.nodes.end()) {
    RigidTransform a;
    RigidTransform b;
    RigidTransform c;
    if (DecodeRigid(older_player->second.world, &a) &&
        DecodeRigid(previous_player->second.world, &b) &&
        DecodeRigid(current_player->second.world, &c)) {
      RecordAxisReturnBins(a.translation, b.translation, c.translation,
                           1.0 / kMatrixFixedScale, 0.25,
                           &g_matrix_axis_return_bins);
    }
  }
  if (older.player_position_valid && previous.player_position_valid &&
      current.player_position_valid) {
    const Vec3 a{static_cast<double>(older.player_position[0]),
                 static_cast<double>(older.player_position[1]),
                 static_cast<double>(older.player_position[2])};
    const Vec3 b{static_cast<double>(previous.player_position[0]),
                 static_cast<double>(previous.player_position[1]),
                 static_cast<double>(previous.player_position[2])};
    const Vec3 c{static_cast<double>(current.player_position[0]),
                 static_cast<double>(current.player_position[1]),
                 static_cast<double>(current.player_position[2])};
    RecordAxisReturnBins(a, b, c, 1.0,
                         std::numeric_limits<double>::max(),
                         &g_raw_axis_return_bins);
  }
}

uintptr_t ResolvePlayerRenderNode(const SceneSnapshot& scene) {
  uintptr_t player = 0;
  uintptr_t render_link = 0;
  uintptr_t render_node = 0;
  if (!SafeReadValue(g_dungeon_base + kUiOwnerPointerRva, &player) ||
      !player ||
      !SafeReadValue(reinterpret_cast<const void*>(player + 0x10u),
                     &render_link) ||
      !render_link ||
      !SafeReadValue(reinterpret_cast<const void*>(render_link),
                     &render_node) ||
      !render_node || scene.nodes.find(render_node) == scene.nodes.end()) {
    return 0;
  }
  return render_node;
}

bool ReadPlayerObject(uintptr_t* player) {
  if (!player || !g_dungeon_base) {
    return false;
  }
  uintptr_t value = 0;
  if (!SafeReadValue(g_dungeon_base + kUiOwnerPointerRva, &value) || !value) {
    return false;
  }
  *player = value;
  return true;
}

void LogPlayerProbeTriplet(uint64_t source_tick,
                           const SceneSnapshot& older,
                           const SceneSnapshot& previous,
                           const SceneSnapshot& current) {
  if (!g_debug_log || source_tick - g_last_player_probe_log_tick < 6u ||
      !older.player_position_valid || !previous.player_position_valid ||
      !current.player_position_valid) {
    return;
  }
  g_last_player_probe_log_tick = source_tick;
  auto value = [](const std::array<int32_t, 3>& position, size_t axis) {
    return static_cast<long long>(position[axis]);
  };
  AppendNativeLog(
      "player_probe tick=%llu object=%08llX contacts=%u/%u/%u "
      "node_x=%lld/%lld/%lld node_y=%lld/%lld/%lld node_z=%lld/%lld/%lld "
      "cache_x=%lld/%lld/%lld cache_y=%lld/%lld/%lld "
      "cache_z=%lld/%lld/%lld bounds_a_x=%lld/%lld/%lld "
      "bounds_a_y=%lld/%lld/%lld bounds_a_z=%lld/%lld/%lld "
      "bounds_b_x=%lld/%lld/%lld bounds_b_y=%lld/%lld/%lld "
      "bounds_b_z=%lld/%lld/%lld valid_cache=%u/%u/%u "
      "valid_a=%u/%u/%u valid_b=%u/%u/%u",
      static_cast<unsigned long long>(source_tick),
      static_cast<unsigned long long>(current.player_object),
      older.player_contact_count, previous.player_contact_count,
      current.player_contact_count, value(older.player_position, 0),
      value(previous.player_position, 0), value(current.player_position, 0),
      value(older.player_position, 1), value(previous.player_position, 1),
      value(current.player_position, 1), value(older.player_position, 2),
      value(previous.player_position, 2), value(current.player_position, 2),
      value(older.player_cached_position, 0),
      value(previous.player_cached_position, 0),
      value(current.player_cached_position, 0),
      value(older.player_cached_position, 1),
      value(previous.player_cached_position, 1),
      value(current.player_cached_position, 1),
      value(older.player_cached_position, 2),
      value(previous.player_cached_position, 2),
      value(current.player_cached_position, 2), value(older.player_bounds_a, 0),
      value(previous.player_bounds_a, 0), value(current.player_bounds_a, 0),
      value(older.player_bounds_a, 1), value(previous.player_bounds_a, 1),
      value(current.player_bounds_a, 1), value(older.player_bounds_a, 2),
      value(previous.player_bounds_a, 2), value(current.player_bounds_a, 2),
      value(older.player_bounds_b, 0), value(previous.player_bounds_b, 0),
      value(current.player_bounds_b, 0), value(older.player_bounds_b, 1),
      value(previous.player_bounds_b, 1), value(current.player_bounds_b, 1),
      value(older.player_bounds_b, 2), value(previous.player_bounds_b, 2),
      value(current.player_bounds_b, 2),
      older.player_cached_position_valid ? 1u : 0u,
      previous.player_cached_position_valid ? 1u : 0u,
      current.player_cached_position_valid ? 1u : 0u,
      older.player_bounds_a_valid ? 1u : 0u,
      previous.player_bounds_a_valid ? 1u : 0u,
      current.player_bounds_a_valid ? 1u : 0u,
      older.player_bounds_b_valid ? 1u : 0u,
      previous.player_bounds_b_valid ? 1u : 0u,
      current.player_bounds_b_valid ? 1u : 0u);
}

uint32_t PlayerBoundsConsensusAxes(const SceneSnapshot& older,
                                   const SceneSnapshot& previous,
                                   const SceneSnapshot& current,
                                   Vec3* midpoint_offset) {
  if (midpoint_offset) {
    *midpoint_offset = {};
  }
  if (!older.player_position_valid || !previous.player_position_valid ||
      !current.player_position_valid || !older.player_bounds_a_valid ||
      !previous.player_bounds_a_valid || !current.player_bounds_a_valid ||
      !older.player_bounds_b_valid || !previous.player_bounds_b_valid ||
      !current.player_bounds_b_valid || !older.player_object ||
      older.player_object != previous.player_object ||
      older.player_object != current.player_object) {
    return 0u;
  }

  constexpr int64_t kMinimumMotion = 2;
  constexpr int64_t kMaximumMotion = 4096;
  auto agrees = [](int64_t reference, int64_t value) {
    return std::abs(value) >= kMinimumMotion &&
           std::abs(value) <= kMaximumMotion &&
           ((reference > 0 && value > 0) ||
            (reference < 0 && value < 0));
  };

  uint32_t mask = 0u;
  std::array<int64_t, 3> root_deltas{};
  for (size_t axis = 0; axis < 3u; ++axis) {
    const int64_t root_delta =
        static_cast<int64_t>(current.player_position[axis]) -
        static_cast<int64_t>(previous.player_position[axis]);
    root_deltas[axis] = root_delta;
    if (std::abs(root_delta) < kMinimumMotion ||
        std::abs(root_delta) > kMaximumMotion) {
      continue;
    }
    const int64_t previous_a =
        static_cast<int64_t>(previous.player_bounds_a[axis]) -
        static_cast<int64_t>(older.player_bounds_a[axis]);
    const int64_t current_a =
        static_cast<int64_t>(current.player_bounds_a[axis]) -
        static_cast<int64_t>(previous.player_bounds_a[axis]);
    const int64_t previous_b =
        static_cast<int64_t>(previous.player_bounds_b[axis]) -
        static_cast<int64_t>(older.player_bounds_b[axis]);
    const int64_t current_b =
        static_cast<int64_t>(current.player_bounds_b[axis]) -
        static_cast<int64_t>(previous.player_bounds_b[axis]);
    const double current_centroid_delta =
        0.5 * static_cast<double>(current_a + current_b);
    const double previous_centroid_delta =
        0.5 * static_cast<double>(previous_a + previous_b);
    const double magnitude_ratio =
        std::abs(current_centroid_delta) /
        static_cast<double>(std::abs(root_delta));
    const double temporal_ratio =
        std::abs(previous_centroid_delta) /
        std::max(std::abs(current_centroid_delta), 1.0);
    if (agrees(root_delta, previous_a) && agrees(root_delta, current_a) &&
        agrees(root_delta, previous_b) && agrees(root_delta, current_b) &&
        std::isfinite(magnitude_ratio) && magnitude_ratio >= 0.50 &&
        magnitude_ratio <= 1.50 && std::isfinite(temporal_ratio) &&
        temporal_ratio >= 0.50 && temporal_ratio <= 2.00) {
      mask |= 1u << axis;
    }
  }

  // Never create a hybrid diagonal pose where one meaningful horizontal axis
  // is halfway while the other is left at the exact endpoint. This occurred
  // during detach and angled wall motion (for example root X/Z 64/91 with only
  // X accepted) and visibly bent the path for one synthetic frame.
  const double accepted_horizontal = std::max(
      (mask & 0x1u) ? static_cast<double>(std::abs(root_deltas[0])) : 0.0,
      (mask & 0x4u) ? static_cast<double>(std::abs(root_deltas[2])) : 0.0);
  if (accepted_horizontal > 0.0) {
    constexpr double kMaximumRejectedAxisFraction = 0.35;
    if (((mask & 0x1u) == 0u &&
         std::abs(root_deltas[0]) >
             kMaximumRejectedAxisFraction * accepted_horizontal) ||
        ((mask & 0x4u) == 0u &&
         std::abs(root_deltas[2]) >
             kMaximumRejectedAxisFraction * accepted_horizontal)) {
      mask = 0u;
    }
  }

  if (midpoint_offset && mask) {
    midpoint_offset->x = (mask & 0x1u)
                             ? -0.5 * root_deltas[0] / kMatrixFixedScale
                             : 0.0;
    midpoint_offset->y = (mask & 0x2u)
                             ? -0.5 * root_deltas[1] / kMatrixFixedScale
                             : 0.0;
    midpoint_offset->z = (mask & 0x4u)
                             ? -0.5 * root_deltas[2] / kMatrixFixedScale
                             : 0.0;
  }
  return mask;
}

void LogBoundsApplication(uint64_t source_tick,
                          const SceneSnapshot& previous,
                          const SceneSnapshot& current,
                          uint32_t axis_mask) {
  if (!g_debug_log || !axis_mask ||
      source_tick - g_last_bounds_apply_log_tick < 6u) {
    return;
  }
  g_last_bounds_apply_log_tick = source_tick;
  AppendNativeLog(
      "bounds_apply tick=%llu axes=%u root_delta=%lld/%lld/%lld "
      "bounds_a_delta=%lld/%lld/%lld bounds_b_delta=%lld/%lld/%lld "
      "exact_cooldown=%u persistent=%u",
      static_cast<unsigned long long>(source_tick), axis_mask,
      static_cast<long long>(current.player_position[0]) -
          static_cast<long long>(previous.player_position[0]),
      static_cast<long long>(current.player_position[1]) -
          static_cast<long long>(previous.player_position[1]),
      static_cast<long long>(current.player_position[2]) -
          static_cast<long long>(previous.player_position[2]),
      static_cast<long long>(current.player_bounds_a[0]) -
          static_cast<long long>(previous.player_bounds_a[0]),
      static_cast<long long>(current.player_bounds_a[1]) -
          static_cast<long long>(previous.player_bounds_a[1]),
      static_cast<long long>(current.player_bounds_a[2]) -
          static_cast<long long>(previous.player_bounds_a[2]),
      static_cast<long long>(current.player_bounds_b[0]) -
          static_cast<long long>(previous.player_bounds_b[0]),
      static_cast<long long>(current.player_bounds_b[1]) -
          static_cast<long long>(previous.player_bounds_b[1]),
      static_cast<long long>(current.player_bounds_b[2]) -
          static_cast<long long>(previous.player_bounds_b[2]),
      g_player_contact_exact_cooldown,
      g_player_persistent_contact_cooldown);
}

Quaternion Slerp(Quaternion a, Quaternion b, double amount,
                 double* angle_degrees) {
  a = NormalizeQuaternion(a);
  b = NormalizeQuaternion(b);
  double dot = a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
  if (dot < 0.0) {
    dot = -dot;
    b.w = -b.w;
    b.x = -b.x;
    b.y = -b.y;
    b.z = -b.z;
  }
  dot = std::clamp(dot, -1.0, 1.0);
  if (angle_degrees) {
    *angle_degrees = 2.0 * std::acos(dot) * 180.0 / 3.14159265358979323846;
  }
  if (dot > 0.9995) {
    return NormalizeQuaternion({a.w + amount * (b.w - a.w),
                                a.x + amount * (b.x - a.x),
                                a.y + amount * (b.y - a.y),
                                a.z + amount * (b.z - a.z)});
  }
  const double theta = std::acos(dot);
  const double sin_theta = std::sin(theta);
  const double from_weight = std::sin((1.0 - amount) * theta) / sin_theta;
  const double to_weight = std::sin(amount * theta) / sin_theta;
  return NormalizeQuaternion({a.w * from_weight + b.w * to_weight,
                              a.x * from_weight + b.x * to_weight,
                              a.y * from_weight + b.y * to_weight,
                              a.z * from_weight + b.z * to_weight});
}

int32_t ToFixed(double value) {
  const double fixed = std::round(value * kMatrixFixedScale);
  return static_cast<int32_t>(std::clamp(
      fixed, static_cast<double>(std::numeric_limits<int32_t>::min()),
      static_cast<double>(std::numeric_limits<int32_t>::max())));
}

bool InterpolateRigid(const Matrix3x4& previous, const Matrix3x4& current,
                      double phase, Matrix3x4* interpolated,
                      double* translation_distance,
                      double* rotation_degrees) {
  RigidTransform from;
  RigidTransform to;
  if (!DecodeRigid(previous, &from) || !DecodeRigid(current, &to)) {
    return false;
  }
  const Vec3 translation_delta = Subtract(to.translation, from.translation);
  const double distance = Length(translation_delta);
  if (!std::isfinite(distance) || distance > kMaximumNodeTranslation) {
    return false;
  }
  double angle = 0.0;
  phase = std::clamp(phase, 0.0, 1.0);
  const Quaternion rotation = Slerp(from.rotation, to.rotation, phase, &angle);
  if (!std::isfinite(angle) || angle > kMaximumNodeRotationDegrees) {
    return false;
  }
  const auto scale_ratio = [](double a, double b) {
    const double smaller = std::min(std::abs(a), std::abs(b));
    const double larger = std::max(std::abs(a), std::abs(b));
    return smaller > 1.0e-6 ? larger / smaller
                            : std::numeric_limits<double>::infinity();
  };
  if (scale_ratio(from.scale.x, to.scale.x) > kMaximumScaleRatio ||
      scale_ratio(from.scale.y, to.scale.y) > kMaximumScaleRatio ||
      scale_ratio(from.scale.z, to.scale.z) > kMaximumScaleRatio) {
    // Scale is also used for visibility and abrupt state changes in Asylum.
    // An in-between scale creates black wedges or a partially erased UI
    // object.
    return false;
  }
  const auto rows = RowsFromQuaternion(rotation);
  const Vec3 scale{from.scale.x + (to.scale.x - from.scale.x) * phase,
                   from.scale.y + (to.scale.y - from.scale.y) * phase,
                   from.scale.z + (to.scale.z - from.scale.z) * phase};
  for (size_t row = 0; row < 3; ++row) {
    const double row_scale = row == 0 ? scale.x : (row == 1 ? scale.y : scale.z);
    interpolated->values[row * 3 + 0] = ToFixed(rows[row].x * row_scale);
    interpolated->values[row * 3 + 1] = ToFixed(rows[row].y * row_scale);
    interpolated->values[row * 3 + 2] = ToFixed(rows[row].z * row_scale);
  }
  interpolated->values[9] = ToFixed(
      from.translation.x + (to.translation.x - from.translation.x) * phase);
  interpolated->values[10] = ToFixed(
      from.translation.y + (to.translation.y - from.translation.y) * phase);
  interpolated->values[11] = ToFixed(
      from.translation.z + (to.translation.z - from.translation.z) * phase);
  if (translation_distance) {
    *translation_distance = distance;
  }
  if (rotation_degrees) {
    *rotation_degrees = angle;
  }
  return true;
}

Matrix3x4 MultiplyAffine(const Matrix3x4& local,
                         const Matrix3x4& parent) {
  Matrix3x4 world{};
  for (size_t row = 0; row < 3; ++row) {
    for (size_t column = 0; column < 3; ++column) {
      int64_t sum = 0;
      for (size_t k = 0; k < 3; ++k) {
        sum += static_cast<int64_t>(local.values[row * 3 + k]) *
               static_cast<int64_t>(parent.values[k * 3 + column]);
      }
      world.values[row * 3 + column] = static_cast<int32_t>(
          sum >= 0 ? (sum + 8192) >> 14 : -(((-sum) + 8192) >> 14));
    }
  }
  for (size_t column = 0; column < 3; ++column) {
    int64_t sum = 0;
    for (size_t k = 0; k < 3; ++k) {
      sum += static_cast<int64_t>(local.values[9 + k]) *
             static_cast<int64_t>(parent.values[k * 3 + column]);
    }
    const int64_t rotated =
        sum >= 0 ? (sum + 8192) >> 14 : -(((-sum) + 8192) >> 14);
    world.values[9 + column] = static_cast<int32_t>(
        std::clamp<int64_t>(rotated + parent.values[9 + column],
                            std::numeric_limits<int32_t>::min(),
                            std::numeric_limits<int32_t>::max()));
  }
  return world;
}

void CaptureNode(uintptr_t node, SceneSnapshot* snapshot,
                 std::unordered_set<uintptr_t>* visited) {
  while (node && snapshot->nodes.size() < kMaximumSceneNodes) {
    if (!visited->insert(node).second) {
      return;
    }

    NodeTransform transform{};
    if (!SafeRead(reinterpret_cast<const void*>(node + kMatrixOffset),
                  &transform.world, sizeof(transform.world)) ||
        !SafeRead(reinterpret_cast<const void*>(node + kLocalMatrixOffset),
                  &transform.local, sizeof(transform.local)) ||
        !SafeReadValue(reinterpret_cast<const void*>(node + kParentOffset),
                       &transform.parent)) {
      return;
    }
    snapshot->nodes.emplace(node, transform);

    uintptr_t child = 0;
    uintptr_t sibling = 0;
    if (!SafeReadValue(reinterpret_cast<const void*>(node + kChildOffset),
                       &child) ||
        !SafeReadValue(reinterpret_cast<const void*>(node + kSiblingOffset),
                       &sibling)) {
      return;
    }
    if (child) {
      CaptureNode(child, snapshot, visited);
    }
    node = sibling;
  }
}

SceneSnapshot CaptureScene(void* context) {
  SceneSnapshot snapshot;
  snapshot.nodes.reserve(1024);
  std::unordered_set<uintptr_t> visited;
  visited.reserve(1024);

  const uintptr_t context_address = reinterpret_cast<uintptr_t>(context);
  uintptr_t scene_owner = 0;
  uintptr_t root = 0;
  if (SafeReadValue(
          reinterpret_cast<const void*>(context_address +
                                        kContextSceneOwnerOffset),
          &scene_owner) &&
      scene_owner &&
      SafeReadValue(reinterpret_cast<const void*>(scene_owner +
                                                  kSceneRootOffset),
                    &root) &&
      root) {
    snapshot.root = root;
    CaptureNode(root, &snapshot, &visited);
  }

  // The camera transform is supplied separately to the scene traversal in
  // Dungeon.dll!0x10039100, so include it even if it is not in the root tree.
  uintptr_t camera_owner = 0;
  uintptr_t camera_node = 0;
  if (SafeReadValue(
          reinterpret_cast<const void*>(context_address +
                                        kContextCameraOwnerOffset),
          &camera_owner) &&
      camera_owner &&
      SafeReadValue(reinterpret_cast<const void*>(camera_owner +
                                                  kCameraNodeOffset),
                    &camera_node) &&
      camera_node) {
    snapshot.camera = camera_node;
    const auto existing = snapshot.nodes.find(camera_node);
    snapshot.camera_in_scene_tree = existing != snapshot.nodes.end();
    if (existing == snapshot.nodes.end()) {
      visited.insert(camera_node);
      NodeTransform transform{};
      if (SafeRead(reinterpret_cast<const void*>(camera_node + kMatrixOffset),
                   &transform.world, sizeof(transform.world)) &&
          SafeRead(
              reinterpret_cast<const void*>(camera_node + kLocalMatrixOffset),
              &transform.local, sizeof(transform.local)) &&
          SafeReadValue(
              reinterpret_cast<const void*>(camera_node + kParentOffset),
              &transform.parent)) {
        snapshot.nodes.emplace(camera_node, transform);
      }
    }
  }

  snapshot.player = ResolvePlayerRenderNode(snapshot);
  if (snapshot.player) {
    snapshot.player_position_valid = SafeRead(
        reinterpret_cast<const void*>(snapshot.player),
        snapshot.player_position.data(),
        sizeof(snapshot.player_position));
  }
  if (ReadPlayerObject(&snapshot.player_object)) {
    snapshot.player_cached_position_valid = SafeRead(
        reinterpret_cast<const void*>(snapshot.player_object + 0x70u),
        snapshot.player_cached_position.data(),
        sizeof(snapshot.player_cached_position));
    snapshot.player_bounds_a_valid = SafeRead(
        reinterpret_cast<const void*>(snapshot.player_object + 0xA4u),
        snapshot.player_bounds_a.data(), sizeof(snapshot.player_bounds_a));
    snapshot.player_bounds_b_valid = SafeRead(
        reinterpret_cast<const void*>(snapshot.player_object + 0xC4u),
        snapshot.player_bounds_b.data(), sizeof(snapshot.player_bounds_b));
    snapshot.player_contact_count_valid = SafeRead(
        reinterpret_cast<const void*>(snapshot.player_object + 0x44u),
        &snapshot.player_contact_count, sizeof(snapshot.player_contact_count));
  }
  return snapshot;
}

bool MeasureRigidDelta(const Matrix3x4& previous, const Matrix3x4& current,
                       double* translation_distance,
                       double* rotation_degrees) {
  RigidTransform from;
  RigidTransform to;
  if (!DecodeRigid(previous, &from) || !DecodeRigid(current, &to)) {
    return false;
  }
  const double distance = Length(Subtract(to.translation, from.translation));
  Quaternion a = NormalizeQuaternion(from.rotation);
  Quaternion b = NormalizeQuaternion(to.rotation);
  double dot = std::abs(a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z);
  dot = std::clamp(dot, 0.0, 1.0);
  const double angle =
      2.0 * std::acos(dot) * 180.0 / 3.14159265358979323846;
  if (!std::isfinite(distance) || !std::isfinite(angle)) {
    return false;
  }
  *translation_distance = distance;
  *rotation_degrees = angle;
  return true;
}

TransitionStats AnalyzeTransition(void* context,
                                  const SceneSnapshot& previous,
                                  const SceneSnapshot& current) {
  TransitionStats stats;
  stats.camera_in_scene_tree = current.camera_in_scene_tree;
  for (const auto& entry : current.nodes) {
    if (previous.nodes.find(entry.first) != previous.nodes.end()) {
      ++stats.matched;
    }
  }
  stats.entered = current.nodes.size() - stats.matched;
  stats.departed = previous.nodes.size() - stats.matched;
  const size_t population = std::max(previous.nodes.size(), current.nodes.size());
  stats.overlap = population
                      ? static_cast<double>(stats.matched) /
                            static_cast<double>(population)
                      : 0.0;

  if (current.camera && current.camera == previous.camera) {
    const auto previous_camera = previous.nodes.find(previous.camera);
    const auto current_camera = current.nodes.find(current.camera);
    if (previous_camera != previous.nodes.end() &&
        current_camera != current.nodes.end()) {
      stats.camera_valid = MeasureRigidDelta(
          previous_camera->second.world, current_camera->second.world,
          &stats.camera_translation, &stats.camera_rotation_degrees);
    }
  }

  const uintptr_t context_address = reinterpret_cast<uintptr_t>(context);
  SafeReadValue(reinterpret_cast<const void*>(
                    context_address + kContextPrimaryCallbackOffset),
                &stats.primary_callback);
  SafeReadValue(reinterpret_cast<const void*>(
                    context_address + kContextSecondaryCallbackOffset),
                &stats.secondary_callback);

  // Suppress only transitions whose scene membership is demonstrably unsafe.
  // Camera rotation by itself is not a cut signal in Deathtrap: the camera
  // quaternion may cross an equivalent representation during an ordinary
  // turn, and small frustum membership changes are normal. Rejecting those
  // valid midpoints caused long runs of held frames and visible tripling.
  // Completed D3D11 midpoints still pass through the independent pixel-level
  // black-region guard, so actual corrupt raster output remains hidden.
  if (stats.overlap < 0.90) {
    stats.suppress_midpoint = true;
    stats.reason = "visibility_cut";
  } else if (stats.overlap < 0.960 &&
             stats.entered + stats.departed >= 24u) {
    // The captured camera node is static in this engine path. Moderate
    // culling-set churn is therefore the more reliable signal for a fast view
    // transition. Normal actor animation changes at most a few nodes.
    stats.suppress_midpoint = true;
    stats.reason = "visibility_churn";
  }
  return stats;
}

uintptr_t DungeonRva(uintptr_t address) {
  const uintptr_t base = reinterpret_cast<uintptr_t>(g_dungeon_base);
  return address >= base && address < base + kExpectedImageSize
             ? address - base
             : address;
}

InterpolationStats ApplyInterpolatedScene(const SceneSnapshot* older,
                                           const SceneSnapshot& previous,
                                           SceneSnapshot& current,
                                           double phase,
                                           bool update_temporal_state) {
  struct MidpointNode {
    Matrix3x4 local;
    Matrix3x4 world;
    bool local_interpolated = false;
    bool world_interpolated = false;
    bool ready = false;
  };

  InterpolationStats stats;
  const uintptr_t player_render_node = current.player;
  const bool authoritative_contact_projection =
      current.player_contact_projection_valid && player_render_node != 0;
  if (authoritative_contact_projection) {
    stats.player_contact_projection_events = 1u;
  }
  stats.player_root_found = player_render_node ? 1u : 0u;
  bool player_contact_correction = false;
  bool player_raw_contact_correction = false;
  RigidTransform previous_player_transform;
  RigidTransform current_player_transform;
  RigidTransform older_player_transform;
  bool player_motion_valid = false;
  bool player_contact_bridge_valid = false;
  Vec3 player_contact_bridge_offset{};
  phase = std::clamp(phase, 0.0, 1.0);
  if (older && player_render_node &&
      older->player == player_render_node &&
      previous.player == player_render_node) {
    if (update_temporal_state) {
      RecordPlayerAxisTelemetry(*older, previous, current,
                                player_render_node);
    }
    const auto older_player = older->nodes.find(player_render_node);
    const auto previous_player = previous.nodes.find(player_render_node);
    const auto current_player = current.nodes.find(player_render_node);
    if (older_player != older->nodes.end() &&
        previous_player != previous.nodes.end() &&
        current_player != current.nodes.end() &&
        older_player->second.parent == current_player->second.parent &&
        previous_player->second.parent == current_player->second.parent) {
      player_motion_valid =
          DecodeRigid(older_player->second.world, &older_player_transform) &&
          DecodeRigid(previous_player->second.world,
                      &previous_player_transform) &&
          DecodeRigid(current_player->second.world, &current_player_transform);
      player_contact_correction = HasPlayerContactCorrection(
          older_player->second.world, previous_player->second.world,
          current_player->second.world);
    }
    if (older->player_position_valid && previous.player_position_valid &&
        current.player_position_valid) {
      player_raw_contact_correction = HasPlayerRawContactCorrection(
          older->player_position, previous.player_position,
          current.player_position);
    }
  }
  if (update_temporal_state && player_contact_correction) {
    ++g_player_matrix_contact_candidates;
  }
  if (update_temporal_state && player_raw_contact_correction) {
    ++g_player_raw_contact_candidates;
  }
  // The world-matrix detector deliberately has a wide envelope and catches
  // animation-root sway as well as collision response. V41 proved that using
  // it alone was far too eager (63 visual reconciliations versus 8 tight raw
  // A-B-A returns in the same run). Require both independently captured root
  // representations to agree before changing contact presentation cadence.
  const bool player_confirmed_contact_correction =
      player_contact_correction && player_raw_contact_correction;
  if (player_confirmed_contact_correction) {
    if (player_motion_valid) {
      // A -> B -> C ~= A means B was the rejected collision pose. Preserve
      // B-to-C bone animation, but translate the complete synthetic actor so
      // its root lies between the two accepted endpoints A and C. The
      // correction decreases toward C as the presentation phase advances.
      player_contact_bridge_offset = Scale(
          Subtract(older_player_transform.translation,
                   previous_player_transform.translation),
          1.0 - phase);
      player_contact_bridge_valid = true;
      stats.player_contact_bridge_events = 1u;
      if (update_temporal_state) {
        Vec3 correction = Subtract(current_player_transform.translation,
                                   previous_player_transform.translation);
        const double correction_length = Length(correction);
        if (Normalize(&correction)) {
          // A whole-vector A -> B -> A correction ends with B -> C pointing
          // away from the rejected wall penetration. Keep that direction so
          // a later genuine detach can end the exact-current fallback.
          const double normal_alignment = g_player_contact_normal_valid
              ? Dot(correction, g_player_contact_outward_normal)
              : 1.0;
          const bool isolated_opposite_flip =
              g_player_contact_normal_valid && g_player_contact_hit_window &&
              std::isfinite(normal_alignment) && normal_alignment < -0.25;
          if (isolated_opposite_flip) {
            // The wall trace occasionally contains one reversed A-B-A cycle
            // between dozens of consistently oriented cycles (v46 tick 270,
            // then normal orientation restored at tick 276). Letting that one
            // sample replace the established plane moves the actor to the
            // opposite side for several presentations. Keep the prior
            // measured manifold; a real detach is handled independently by
            // sustained outward motion and a later contact starts from
            // cleared state.
            ++stats.player_contact_normal_flip_rejections;
            ++g_player_contact_normal_flip_rejections;
          } else {
            g_player_contact_outward_normal = correction;
            g_player_contact_anchor = current_player_transform.translation;
            g_player_contact_normal_valid = true;
            g_player_contact_anchor_valid = true;
            g_player_contact_correction_length = correction_length;
          }
        }
      }
    }
    if (update_temporal_state) {
      g_player_contact_release_streak = 0u;
      g_player_contact_quiet_ticks = 0u;
      g_player_contact_release_alignment = 0.0;
      g_player_contact_outward_distance = 0.0;
      if (!g_player_contact_hit_window) {
        g_player_contact_hits_in_window = 0u;
      }
      ++g_player_contact_hits_in_window;
      g_player_contact_hit_window = 12u;
      if (g_player_contact_hits_in_window >= 2u) {
        g_player_persistent_contact_cooldown = 18u;
      }
      // A single confirmed A-B-A event is handled by the accepted-endpoint
      // bridge above. Exact-current cadence is reserved for repeated contact;
      // holding an isolated correction for eight endpoints caused visible
      // sticking and multiple silhouettes at pipes and angled walls.
      g_player_contact_exact_cooldown =
          g_player_contact_hits_in_window >= 2u ? 1u : 0u;
      ++stats.contact_reversal;
      ++stats.temporal_pose_guard;
      ++stats.player_contact_settle;
      ++stats.player_raw_contact;
    }
  } else if (update_temporal_state) {
    bool directional_release = false;
    if (g_player_contact_quiet_ticks <
        std::numeric_limits<uint32_t>::max()) {
      ++g_player_contact_quiet_ticks;
    }
    if (g_player_contact_normal_valid && g_player_contact_anchor_valid &&
        player_motion_valid &&
        g_player_persistent_contact_cooldown) {
      const Vec3 motion = Subtract(current_player_transform.translation,
                                   previous_player_transform.translation);
      const double motion_length = Length(motion);
      const Vec3 from_contact = Subtract(
          current_player_transform.translation, g_player_contact_anchor);
      g_player_contact_outward_distance =
          Dot(from_contact, g_player_contact_outward_normal);
      if (std::isfinite(motion_length) &&
          motion_length >= 2.0 / kMatrixFixedScale) {
        g_player_contact_release_alignment =
            Dot(motion, g_player_contact_outward_normal) / motion_length;
        if (std::isfinite(g_player_contact_release_alignment) &&
            g_player_contact_release_alignment > 0.10) {
          ++g_player_contact_release_streak;
        } else {
          g_player_contact_release_streak = 0u;
        }
      } else {
        g_player_contact_release_alignment = 0.0;
        g_player_contact_release_streak = 0u;
      }
      const double required_outward_distance = std::max(
          64.0 / kMatrixFixedScale,
          4.0 * g_player_contact_correction_length);
      if (g_player_contact_quiet_ticks >= 12u &&
          g_player_contact_release_streak >= 6u &&
          std::isfinite(g_player_contact_outward_distance) &&
          g_player_contact_outward_distance >= required_outward_distance) {
        // Collision response itself also contains one or two outward steps.
        // Only a sustained, spatially meaningful departure beyond the last
        // correction endpoint is a real detach.
        directional_release = true;
        const bool was_persistent =
            g_player_persistent_contact_cooldown != 0u;
        g_player_contact_exact_cooldown = 0u;
        g_player_persistent_contact_cooldown = 0u;
        g_player_contact_hit_window = 0u;
        g_player_contact_hits_in_window = 0u;
        g_player_contact_normal_valid = false;
        g_player_contact_anchor_valid = false;
        ++stats.player_directional_release;
        ++g_player_contact_directional_releases;
        if (g_debug_log) {
          AppendNativeLog(
              "contact_release quiet=%u streak=%u alignment=%.3f "
              "outward=%.6f required=%.6f correction=%.6f persistent=%u",
              g_player_contact_quiet_ticks,
              g_player_contact_release_streak,
              g_player_contact_release_alignment,
              g_player_contact_outward_distance,
              required_outward_distance,
              g_player_contact_correction_length,
              was_persistent ? 1u : 0u);
        }
      }
    }
    if (!directional_release && g_player_contact_exact_cooldown) {
      --g_player_contact_exact_cooldown;
    }
  }
  if (update_temporal_state) {
    if (g_player_contact_hit_window) {
      --g_player_contact_hit_window;
      if (!g_player_contact_hit_window) {
        g_player_contact_hits_in_window = 0u;
      }
    }
    if (g_player_persistent_contact_cooldown &&
        !player_confirmed_contact_correction) {
      --g_player_persistent_contact_cooldown;
    }
    if (!g_player_contact_exact_cooldown &&
        !g_player_persistent_contact_cooldown &&
        !g_player_contact_hit_window) {
      g_player_contact_normal_valid = false;
      g_player_contact_anchor_valid = false;
      g_player_contact_release_streak = 0u;
      g_player_contact_quiet_ticks = 0u;
      g_player_contact_release_alignment = 0.0;
      g_player_contact_outward_distance = 0.0;
      g_player_contact_correction_length = 0.0;
    }
  }
  if (update_temporal_state && authoritative_contact_projection) {
    // The measured collision projection supersedes the old A-B-A fallback
    // for this transition.  Keeping the fallback cooldown would still force
    // the complete actor to exact-current and throw away the smooth tangent
    // component that we can now reconstruct exactly.
    g_player_contact_exact_cooldown = 0u;
    g_player_contact_hit_window = 0u;
    g_player_contact_hits_in_window = 0u;
    g_player_persistent_contact_cooldown = 0u;
    g_player_contact_normal_valid = false;
    g_player_contact_anchor_valid = false;
    g_player_contact_release_streak = 0u;
    g_player_contact_quiet_ticks = 0u;
    g_player_contact_release_alignment = 0.0;
    g_player_contact_outward_distance = 0.0;
    g_player_contact_correction_length = 0.0;
  }

  // Once two independently observed A-B-A collision returns have established
  // a persistent contact, reject only the component of a later render
  // endpoint that crosses the accepted contact plane. The game still receives
  // and resolves its original B endpoint; this correction exists solely for
  // midpoint and exact presentation. Tangential motion and motion away from
  // the surface are intentionally untouched.
  if (update_temporal_state && !authoritative_contact_projection &&
      player_render_node &&
      player_motion_valid && g_player_persistent_contact_cooldown &&
      g_player_contact_normal_valid && g_player_contact_anchor_valid) {
    const Vec3 from_contact = Subtract(
        current_player_transform.translation, g_player_contact_anchor);
    const double signed_distance =
        Dot(from_contact, g_player_contact_outward_normal);
    const double minimum_depth = 0.5 / kMatrixFixedScale;
    const double maximum_depth = 256.0 / kMatrixFixedScale;
    if (std::isfinite(signed_distance) && signed_distance < -minimum_depth &&
        -signed_distance <= maximum_depth) {
      const double depth = -signed_distance;
      const Vec3 projection =
          Scale(g_player_contact_outward_normal, depth);
      current.player_contact_manifold_projection = {
          ToFixed(projection.x), ToFixed(projection.y),
          ToFixed(projection.z)};
      current.player_contact_manifold_projection_valid = true;
      stats.player_contact_manifold_events = 1u;
      stats.player_contact_manifold_depth = depth;
      ++g_player_contact_manifold_events;
      g_player_contact_manifold_max_depth =
          std::max(g_player_contact_manifold_max_depth, depth);
    }
  }

  auto is_player_subtree_node = [&](uintptr_t address) {
    for (size_t depth = 0; address && depth < 128u; ++depth) {
      if (address == player_render_node) {
        return true;
      }
      const auto node = current.nodes.find(address);
      if (node == current.nodes.end()) {
        break;
      }
      address = node->second.parent;
    }
    return false;
  };

  std::unordered_map<uintptr_t, MidpointNode> midpoint_nodes;
  midpoint_nodes.reserve(current.nodes.size());
  for (const auto& entry : current.nodes) {
    MidpointNode midpoint;
    midpoint.local = entry.second.local;
    midpoint.world = entry.second.world;
    const auto previous_it = previous.nodes.find(entry.first);
    if (previous_it == previous.nodes.end()) {
      ++stats.exact;
    } else if (previous_it->second.parent != entry.second.parent) {
      ++stats.parent_mismatch;
      ++stats.exact;
    } else {
      double translation = 0.0;
      double rotation = 0.0;
      if (InterpolateRigid(previous_it->second.local, entry.second.local,
                           phase, &midpoint.local, &translation, &rotation)) {
        midpoint.local_interpolated = true;
        stats.max_translation = std::max(stats.max_translation, translation);
        stats.max_rotation_degrees =
            std::max(stats.max_rotation_degrees, rotation);
      } else {
        ++stats.invalid;
        ++stats.exact;
      }
    }
    midpoint_nodes.emplace(entry.first, midpoint);
  }

  std::unordered_set<uintptr_t> visiting;
  visiting.reserve(current.nodes.size());
  std::function<bool(uintptr_t)> build_world = [&](uintptr_t address) {
    auto midpoint_it = midpoint_nodes.find(address);
    const auto current_it = current.nodes.find(address);
    if (midpoint_it == midpoint_nodes.end() || current_it == current.nodes.end()) {
      return false;
    }
    MidpointNode& midpoint = midpoint_it->second;
    if (midpoint.ready) {
      return midpoint.world_interpolated;
    }
    if (!visiting.insert(address).second) {
      midpoint.world = current_it->second.world;
      midpoint.ready = true;
      midpoint.world_interpolated = false;
      ++stats.exact;
      return false;
    }

    const uintptr_t parent = current_it->second.parent;
    if (!midpoint.local_interpolated) {
      midpoint.world = current_it->second.world;
    } else if (!parent) {
      midpoint.world = midpoint.local;
      midpoint.world_interpolated = true;
      ++stats.hierarchical;
    } else {
      auto parent_midpoint = midpoint_nodes.find(parent);
      if (parent_midpoint != midpoint_nodes.end() && build_world(parent)) {
        midpoint.world = MultiplyAffine(midpoint.local,
                                        parent_midpoint->second.world);
        midpoint.world_interpolated = true;
        ++stats.hierarchical;
      } else if (parent_midpoint == midpoint_nodes.end()) {
        const auto previous_it = previous.nodes.find(address);
        double translation = 0.0;
        double rotation = 0.0;
        if (previous_it != previous.nodes.end() &&
            InterpolateRigid(previous_it->second.world,
                             current_it->second.world, phase,
                             &midpoint.world, &translation, &rotation)) {
          midpoint.world_interpolated = true;
          ++stats.world_fallback;
          stats.max_translation =
              std::max(stats.max_translation, translation);
          stats.max_rotation_degrees =
              std::max(stats.max_rotation_degrees, rotation);
        } else {
          midpoint.world = current_it->second.world;
        }
      } else {
        midpoint.world = current_it->second.world;
      }
    }
    visiting.erase(address);
    midpoint.ready = true;
    return midpoint.world_interpolated;
  };

  for (const auto& entry : current.nodes) {
    build_world(entry.first);
  }

  // A player's animated parent chain can contain a model-space pivot that is
  // refreshed on a different cadence from the scene-node world matrix. When
  // the hierarchy is rebuilt at an intermediate phase, that stale pivot can
  // move the complete actor far away from the direct interpolation of its two
  // real render roots. V48's
  // raster-entry trace exposed errors over 100 fixed-point units while the
  // adjacent exact roots differed by fewer than 15. Keep bone rotations and
  // relative animation hierarchical, but make the complete actor's root
  // translation agree with a direct rigid world-space interpolation.
  if (player_render_node) {
    const auto previous_player = previous.nodes.find(player_render_node);
    const auto current_player = current.nodes.find(player_render_node);
    auto midpoint_player = midpoint_nodes.find(player_render_node);
    if (previous_player != previous.nodes.end() &&
        current_player != current.nodes.end() &&
        midpoint_player != midpoint_nodes.end() &&
        midpoint_player->second.world_interpolated &&
        previous_player->second.parent == current_player->second.parent) {
      Matrix3x4 desired_world{};
      double ignored_translation = 0.0;
      double ignored_rotation = 0.0;
      if (InterpolateRigid(previous_player->second.world,
                           current_player->second.world, phase,
                           &desired_world, &ignored_translation,
                           &ignored_rotation)) {
        std::array<int32_t, 3> coherence{};
        for (size_t axis = 0; axis < coherence.size(); ++axis) {
          const int64_t delta =
              static_cast<int64_t>(desired_world.values[9u + axis]) -
              midpoint_player->second.world.values[9u + axis];
          coherence[axis] = static_cast<int32_t>(std::clamp<int64_t>(
              delta, std::numeric_limits<int32_t>::min(),
              std::numeric_limits<int32_t>::max()));
        }
        stats.player_root_coherence_offset = coherence;
        if (coherence != std::array<int32_t, 3>{}) {
          for (auto& entry : midpoint_nodes) {
            MidpointNode& midpoint = entry.second;
            if (!midpoint.world_interpolated ||
                !is_player_subtree_node(entry.first)) {
              continue;
            }
            for (size_t axis = 0; axis < coherence.size(); ++axis) {
              const int64_t shifted =
                  static_cast<int64_t>(midpoint.world.values[9u + axis]) +
                  coherence[axis];
              midpoint.world.values[9u + axis] = static_cast<int32_t>(
                  std::clamp<int64_t>(
                      shifted, std::numeric_limits<int32_t>::min(),
                      std::numeric_limits<int32_t>::max()));
            }
            ++stats.player_root_coherence_nodes;
          }
        }
      }
    }
  }
  if (player_contact_bridge_valid && !authoritative_contact_projection) {
    const std::array<double, 3> offset = {
        player_contact_bridge_offset.x,
        player_contact_bridge_offset.y,
        player_contact_bridge_offset.z};
    for (auto& entry : midpoint_nodes) {
      MidpointNode& midpoint = entry.second;
      if (!midpoint.world_interpolated ||
          !is_player_subtree_node(entry.first)) {
        continue;
      }
      for (size_t axis = 0; axis < offset.size(); ++axis) {
        const int64_t shifted =
            static_cast<int64_t>(midpoint.world.values[9u + axis]) +
            static_cast<int64_t>(ToFixed(offset[axis]));
        midpoint.world.values[9u + axis] = static_cast<int32_t>(
            std::clamp<int64_t>(shifted,
                                std::numeric_limits<int32_t>::min(),
                                std::numeric_limits<int32_t>::max()));
      }
      ++stats.player_contact_bridge_nodes;
    }
  }
  if (current.player_contact_manifold_projection_valid) {
    // The uncorrected phase contains phase*C of the rejected inward endpoint,
    // so applying phase times the full endpoint projection keeps every
    // synthetic sample on the same contact plane without changing tangent
    // motion.
    std::array<int32_t, 3> phase_projection{};
    for (size_t axis = 0; axis < phase_projection.size(); ++axis) {
      phase_projection[axis] = static_cast<int32_t>(std::llround(
          phase * static_cast<double>(
                      current.player_contact_manifold_projection[axis])));
    }
    for (auto& entry : midpoint_nodes) {
      MidpointNode& midpoint = entry.second;
      if (!midpoint.world_interpolated ||
          !is_player_subtree_node(entry.first)) {
        continue;
      }
      for (size_t axis = 0; axis < phase_projection.size(); ++axis) {
        const int64_t shifted =
            static_cast<int64_t>(midpoint.world.values[9u + axis]) +
            phase_projection[axis];
        midpoint.world.values[9u + axis] = static_cast<int32_t>(
            std::clamp<int64_t>(shifted,
                                std::numeric_limits<int32_t>::min(),
                                std::numeric_limits<int32_t>::max()));
      }
      ++stats.player_contact_manifold_midpoint_nodes;
    }
    g_player_contact_manifold_midpoint_nodes +=
        stats.player_contact_manifold_midpoint_nodes;
  }
  if (authoritative_contact_projection) {
    // The exact endpoint already contains the full resolver correction C.
    // A naive phase contains only phase*C, so add the missing (1-phase)*C to
    // the complete actor subtree. Never carry C into later exact endpoints:
    // doing that preserved the rejected pose and produced multi-frame ghosts.
    std::array<int32_t, 3> missing_projection{};
    for (size_t axis = 0; axis < missing_projection.size(); ++axis) {
      missing_projection[axis] = static_cast<int32_t>(std::llround(
          (1.0 - phase) * static_cast<double>(
                              current.player_contact_projection[axis])));
    }
    for (auto& entry : midpoint_nodes) {
      MidpointNode& midpoint = entry.second;
      if (!midpoint.world_interpolated ||
          !is_player_subtree_node(entry.first)) {
        continue;
      }
      for (size_t axis = 0; axis < missing_projection.size(); ++axis) {
        const int64_t shifted =
            static_cast<int64_t>(midpoint.world.values[9u + axis]) +
            missing_projection[axis];
        midpoint.world.values[9u + axis] = static_cast<int32_t>(
            std::clamp<int64_t>(shifted,
                                std::numeric_limits<int32_t>::min(),
                                std::numeric_limits<int32_t>::max()));
      }
      ++stats.player_contact_projection_nodes;
    }
  }
  for (const auto& entry : midpoint_nodes) {
    if (!entry.second.world_interpolated) {
      continue;
    }
    if (SafeWrite(reinterpret_cast<void*>(entry.first + kMatrixOffset),
                  &entry.second.world, sizeof(entry.second.world))) {
      ++stats.applied;
    }
  }
  return stats;
}

void RestoreScene(const SceneSnapshot& current) {
  for (const auto& entry : current.nodes) {
    SafeWrite(reinterpret_cast<void*>(entry.first + kMatrixOffset),
              &entry.second.world, sizeof(entry.second.world));
  }
}

uint64_t ApplyPlayerEndpointOffset(
    const SceneSnapshot& scene, const std::array<int32_t, 3>& offset) {
  if (!scene.player) {
    return 0;
  }
  uint64_t applied = 0;
  for (const auto& entry : scene.nodes) {
    uintptr_t address = entry.first;
    bool player_subtree = false;
    for (size_t depth = 0; address && depth < 128u; ++depth) {
      if (address == scene.player) {
        player_subtree = true;
        break;
      }
      const auto node = scene.nodes.find(address);
      if (node == scene.nodes.end()) {
        break;
      }
      address = node->second.parent;
    }
    if (!player_subtree) {
      continue;
    }
    Matrix3x4 projected = entry.second.world;
    for (size_t axis = 0; axis < offset.size(); ++axis) {
      const int64_t shifted =
          static_cast<int64_t>(projected.values[9u + axis]) + offset[axis];
      projected.values[9u + axis] = static_cast<int32_t>(
          std::clamp<int64_t>(shifted,
                              std::numeric_limits<int32_t>::min(),
                              std::numeric_limits<int32_t>::max()));
    }
    if (SafeWrite(reinterpret_cast<void*>(entry.first + kMatrixOffset),
                  &projected, sizeof(projected))) {
      ++applied;
    }
  }
  return applied;
}

void RestorePlayerEndpoint(const SceneSnapshot& scene) {
  if (!scene.player) {
    return;
  }
  for (const auto& entry : scene.nodes) {
    uintptr_t address = entry.first;
    bool player_subtree = false;
    for (size_t depth = 0; address && depth < 128u; ++depth) {
      if (address == scene.player) {
        player_subtree = true;
        break;
      }
      const auto node = scene.nodes.find(address);
      if (node == scene.nodes.end()) {
        break;
      }
      address = node->second.parent;
    }
    if (player_subtree) {
      SafeWrite(reinterpret_cast<void*>(entry.first + kMatrixOffset),
                &entry.second.world, sizeof(entry.second.world));
    }
  }
}

bool ReadLiveNodeTranslation(uintptr_t node,
                             std::array<int32_t, 3>* translation) {
  if (!node || !translation) {
    return false;
  }
  Matrix3x4 world{};
  if (!SafeRead(reinterpret_cast<const void*>(node + kMatrixOffset),
                &world, sizeof(world))) {
    return false;
  }
  *translation = {world.values[9], world.values[10], world.values[11]};
  return true;
}

bool SnapshotPlayerTranslation(const SceneSnapshot& scene,
                               std::array<int32_t, 3>* translation) {
  if (!scene.player || !translation) {
    return false;
  }
  const auto player = scene.nodes.find(scene.player);
  if (player == scene.nodes.end()) {
    return false;
  }
  *translation = {player->second.world.values[9],
                  player->second.world.values[10],
                  player->second.world.values[11]};
  return true;
}

void FlushPresentationTraceBuffer() {
  if (!g_debug_log || g_presentation_trace_buffer.empty()) {
    g_presentation_trace_buffer.clear();
    return;
  }
  std::string block;
  block.reserve(g_presentation_trace_buffer.size() * 240u);
  for (const PresentationTraceSample& sample :
       g_presentation_trace_buffer) {
    char line[512] = {};
    const int length = std::snprintf(
        line, sizeof(line),
        "presentation_pair tick=%llu calls=%u/%u flags=%02X "
        "prev=%d/%d/%d current=%d/%d/%d midpoint_seen=%d/%d/%d "
        "exact_pre=%d/%d/%d exact_seen=%d/%d/%d projection=%d/%d/%d "
        "endpoint_nodes=%llu root_coherence=%d/%d/%d "
        "root_nodes=%llu\r\n",
        static_cast<unsigned long long>(sample.tick), sample.midpoint_calls,
        sample.exact_calls, sample.flags, sample.previous[0],
        sample.previous[1], sample.previous[2], sample.current[0],
        sample.current[1], sample.current[2], sample.midpoint_seen[0],
        sample.midpoint_seen[1], sample.midpoint_seen[2],
        sample.exact_before_projection[0],
        sample.exact_before_projection[1],
        sample.exact_before_projection[2], sample.exact_seen[0],
        sample.exact_seen[1], sample.exact_seen[2], sample.projection[0],
        sample.projection[1], sample.projection[2],
        static_cast<unsigned long long>(sample.exact_projection_nodes),
        sample.root_coherence[0], sample.root_coherence[1],
        sample.root_coherence[2],
        static_cast<unsigned long long>(sample.root_coherence_nodes));
    if (length > 0) {
      block.append(line, std::min<size_t>(static_cast<size_t>(length),
                                          sizeof(line) - 1u));
    }
  }
  AppendNativeLogBlock(block);
  g_presentation_trace_buffer.clear();
}

void QueuePresentationTrace(uint64_t source_tick,
                            const SceneSnapshot& previous,
                            const SceneSnapshot& current,
                            const InterpolationStats& interpolation) {
  const bool contact_active =
      interpolation.contact_reversal ||
      current.player_contact_manifold_projection_valid ||
      current.player_contact_projection_valid ||
      g_player_contact_hit_window || g_player_persistent_contact_cooldown;
  if (!g_debug_log || !contact_active || !current.player) {
    if (g_debug_log && source_tick % 120u == 0u) {
      FlushPresentationTraceBuffer();
    }
    return;
  }

  PresentationTraceSample sample;
  sample.tick = source_tick;
  SnapshotPlayerTranslation(previous, &sample.previous);
  SnapshotPlayerTranslation(current, &sample.current);
  sample.midpoint_seen = g_active_presentation_trace.midpoint_seen;
  sample.exact_before_projection =
      g_active_presentation_trace.exact_before_projection;
  sample.exact_seen = g_active_presentation_trace.exact_seen;
  sample.projection =
      current.player_contact_manifold_projection_valid
          ? current.player_contact_manifold_projection
          : std::array<int32_t, 3>{};
  sample.exact_projection_nodes =
      g_active_presentation_trace.exact_projection_nodes;
  sample.root_coherence = interpolation.player_root_coherence_offset;
  sample.root_coherence_nodes = interpolation.player_root_coherence_nodes;
  sample.midpoint_calls = g_active_presentation_trace.midpoint_calls;
  sample.exact_calls = g_active_presentation_trace.exact_calls;
  sample.flags =
      (g_active_presentation_trace.midpoint_seen_valid ? 1u : 0u) |
      (g_active_presentation_trace.exact_before_projection_valid ? 2u : 0u) |
      (g_active_presentation_trace.exact_seen_valid ? 4u : 0u) |
      (current.player_contact_manifold_projection_valid ? 8u : 0u) |
      (interpolation.contact_reversal ? 16u : 0u);
  g_presentation_trace_buffer.push_back(sample);
  if (g_presentation_trace_buffer.size() >= 16u ||
      source_tick % 120u == 0u) {
    FlushPresentationTraceBuffer();
  }
}

void __cdecl HookRenderer(void* context) {
  ActivePresentationTrace& trace = g_active_presentation_trace;
  if (!g_original_renderer) {
    return;
  }

  if (trace.stage == DeathtrapNativePresentationStage::kMidpoint) {
    ++trace.midpoint_calls;
    trace.midpoint_seen_valid =
        ReadLiveNodeTranslation(trace.player, &trace.midpoint_seen);
    g_original_renderer(context);
    return;
  }

  if (trace.stage == DeathtrapNativePresentationStage::kExact) {
    ++trace.exact_calls;
    trace.exact_before_projection_valid = ReadLiveNodeTranslation(
        trace.player, &trace.exact_before_projection);
    uint64_t projected_nodes = 0;
    if (trace.exact_projection_valid && trace.exact_scene) {
      // Apply the one-sided contact projection at the last possible point,
      // after the original render/present wrapper has refreshed every scene
      // cache. Earlier versions wrote the same matrices before that wrapper;
      // its cache update could silently replace them before rasterization.
      projected_nodes = ApplyPlayerEndpointOffset(
          *trace.exact_scene, trace.exact_projection);
      trace.exact_projection_nodes += projected_nodes;
    }
    trace.exact_seen_valid =
        ReadLiveNodeTranslation(trace.player, &trace.exact_seen);
    g_original_renderer(context);
    if (projected_nodes && trace.exact_scene) {
      RestorePlayerEndpoint(*trace.exact_scene);
    }
    return;
  }

  g_original_renderer(context);
}

PlayerMutableStateRollbackStats RestorePlayerMutableState(
    const SceneSnapshot& exact) {
  PlayerMutableStateRollbackStats stats;

  const auto restore_vector = [&stats](uintptr_t address,
                                       const std::array<int32_t, 3>& expected,
                                       bool valid, uint32_t bit) {
    if (!address || !valid) {
      return;
    }
    std::array<int32_t, 3> live{};
    if (!SafeRead(reinterpret_cast<const void*>(address), live.data(),
                  sizeof(live)) ||
        live == expected) {
      return;
    }
    stats.changed_mask |= bit;
    ++stats.changed_fields;
    for (size_t axis = 0; axis < live.size(); ++axis) {
      const int64_t delta =
          static_cast<int64_t>(live[axis]) - expected[axis];
      stats.maximum_delta =
          std::max<int64_t>(stats.maximum_delta, std::llabs(delta));
    }
    if (SafeWrite(reinterpret_cast<void*>(address), expected.data(),
                  sizeof(expected))) {
      stats.restored_mask |= bit;
    }
  };

  // The synthetic renderer must be a transaction.  Exact gameplay owns all
  // of these fields; the midpoint is allowed to consume them, never to leave
  // mutations behind for the next collision or animation update.
  restore_vector(exact.player, exact.player_position,
                 exact.player_position_valid, 1u << 0u);
  restore_vector(exact.player_object + 0x70u,
                 exact.player_cached_position,
                 exact.player_cached_position_valid, 1u << 1u);
  restore_vector(exact.player_object + 0xA4u, exact.player_bounds_a,
                 exact.player_bounds_a_valid, 1u << 2u);
  restore_vector(exact.player_object + 0xC4u, exact.player_bounds_b,
                 exact.player_bounds_b_valid, 1u << 3u);

  if (exact.player_object && exact.player_contact_count_valid) {
    uint8_t live_contact_count = 0;
    const uintptr_t address = exact.player_object + 0x44u;
    if (SafeRead(reinterpret_cast<const void*>(address), &live_contact_count,
                 sizeof(live_contact_count)) &&
        live_contact_count != exact.player_contact_count) {
      stats.changed_mask |= 1u << 4u;
      ++stats.changed_fields;
      stats.maximum_delta = std::max<int64_t>(
          stats.maximum_delta,
          std::llabs(static_cast<int64_t>(live_contact_count) -
                     exact.player_contact_count));
      if (SafeWrite(reinterpret_cast<void*>(address),
                    &exact.player_contact_count,
                    sizeof(exact.player_contact_count))) {
        stats.restored_mask |= 1u << 4u;
      }
    }
  }
  return stats;
}

void AdvanceSceneHistory(SceneSnapshot&& current) {
  g_older_snapshot = std::move(g_previous_snapshot);
  g_previous_snapshot = std::move(current);
}

void ResetSceneHistory() {
  FlushPresentationTraceBuffer();
  g_active_presentation_trace = {};
  g_older_snapshot = {};
  g_previous_snapshot = {};
  g_player_contact_exact_cooldown = 0u;
  g_player_contact_hit_window = 0u;
  g_player_contact_hits_in_window = 0u;
  g_player_persistent_contact_cooldown = 0u;
  g_player_contact_outward_normal = {};
  g_player_contact_anchor = {};
  g_player_contact_normal_valid = false;
  g_player_contact_anchor_valid = false;
  g_player_contact_release_streak = 0u;
  g_player_contact_quiet_ticks = 0u;
  g_player_contact_release_alignment = 0.0;
  g_player_contact_outward_distance = 0.0;
  g_player_contact_correction_length = 0.0;
  g_matrix_axis_return_bins = {};
  g_raw_axis_return_bins = {};
  {
    std::lock_guard<std::mutex> lock(g_contact_projection_mutex);
    g_pending_contact_projection = {};
  }
}

void WaitUntil(const LARGE_INTEGER& target) {
  if (!g_qpc_frequency.QuadPart) {
    return;
  }
  for (;;) {
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    const int64_t remaining = target.QuadPart - now.QuadPart;
    if (remaining <= 0) {
      return;
    }
    const double remaining_ms =
        static_cast<double>(remaining) * 1000.0 /
        static_cast<double>(g_qpc_frequency.QuadPart);
    if (remaining_ms > 2.0) {
      Sleep(1);
    } else {
      SwitchToThread();
    }
  }
}

bool RefreshCurrentRenderCaches(void* context) {
  const uintptr_t context_address = reinterpret_cast<uintptr_t>(context);
  uintptr_t scene_owner = 0;
  uintptr_t camera_owner = 0;
  if (!g_scene_cache_update || !g_camera_cache_update ||
      !SafeReadValue(reinterpret_cast<const void*>(
                         context_address + kContextSceneOwnerOffset),
                     &scene_owner) ||
      !scene_owner ||
      !SafeReadValue(reinterpret_cast<const void*>(
                         context_address + kContextCameraOwnerOffset),
                     &camera_owner) ||
      !camera_owner) {
    return false;
  }

  // Dungeon.dll!0x1001B280 normally performs these two calls at the start of
  // the renderer. Execute them once, at the same engine-frame value, before
  // capturing the current state. Their owner stamps then make both the
  // midpoint and exact renderer calls skip duplicate updates for this tick.
  __try {
    g_scene_cache_update(reinterpret_cast<void*>(scene_owner));
    g_camera_cache_update(reinterpret_cast<void*>(camera_owner));
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
  return true;
}

bool RenderMidpointWithoutAdvancingEngineClock(void* context) {
  const uintptr_t context_address = reinterpret_cast<uintptr_t>(context);
  uintptr_t backend_flip_address = 0;
  uint8_t context_flags = 0;
  if (!SafeReadValue(g_dungeon_base + kBackendFlipPointerRva,
                     &backend_flip_address) ||
      !backend_flip_address ||
      !SafeReadValue(reinterpret_cast<const void*>(context_address),
                     &context_flags)) {
    return false;
  }

  // Bit 2 permits an internal present from the renderer. The normal gameplay
  // context does not need it, but mask it defensively so the only midpoint
  // presentation is the direct backend Flip below.
  const uint8_t midpoint_flags = context_flags & ~uint8_t{0x04u};
  const bool prepared = SafeWrite(reinterpret_cast<void*>(context_address),
                                  &midpoint_flags, sizeof(midpoint_flags));

  bool rendered = false;
  __try {
    if (prepared) {
      g_renderer(context);
      reinterpret_cast<BackendFlipFn>(backend_flip_address)();

      // A legacy DirectDraw Flip rotates the game's front/back surface chain.
      // Leaving the midpoint page at the front makes the exact renderer use a
      // different historical page than the original engine expects. Deathtrap
      // keeps transient UI and partially redrawn sprite regions on those
      // pages, which manifests as disappearing selectors, actor tripling and
      // black tiles. Rotate the DirectDraw chain back immediately, while the
      // DXGI hook suppresses only this restore presentation. The display keeps
      // showing the midpoint until the exact endpoint is due, but the game
      // renders that endpoint into the same page it would have used natively.
      SetDeathtrapNativePageRestorePresentSuppressed(true);
      __try {
        reinterpret_cast<BackendFlipFn>(backend_flip_address)();
        ++g_page_restore_flips;
      } __finally {
        SetDeathtrapNativePageRestorePresentSuppressed(false);
      }
      rendered = true;
    }
  } __finally {
    SafeWrite(reinterpret_cast<void*>(context_address),
              &context_flags, sizeof(context_flags));
  }
  return rendered;
}

struct InterpolatedPassResult {
  InterpolationStats interpolation;
  PlayerMutableStateRollbackStats player_rollback;
  bool rendered = false;
};

InterpolatedPassResult RenderInterpolatedPass(
    void* context, const SceneSnapshot* older,
    const SceneSnapshot& previous, SceneSnapshot& current,
    double phase, bool update_temporal_state, uint64_t source_tick) {
  InterpolatedPassResult result;
  const UiRenderStateSnapshot ui_before = CaptureUiRenderState();
  result.interpolation = ApplyInterpolatedScene(
      older, previous, current, phase, update_temporal_state);

  g_active_presentation_trace.stage =
      DeathtrapNativePresentationStage::kMidpoint;
  result.rendered = RenderMidpointWithoutAdvancingEngineClock(context);
  g_active_presentation_trace.stage = DeathtrapNativePresentationStage::kNone;

  const UiRenderStateSnapshot ui_after = CaptureUiRenderState();
  if (UiRenderStateChanged(ui_before, ui_after)) {
    ++g_ui_state_changes;
  }
  if (UiTransientLogicChanged(ui_before, ui_after)) {
    ++g_ui_logic_rollbacks;
  }
  if (RestoreUiRenderState(ui_before)) {
    ++g_ui_state_restores;
  }

  result.player_rollback = RestorePlayerMutableState(current);
  if (result.player_rollback.changed_mask) {
    ++g_player_state_rollback_events;
    g_player_state_rollback_fields += result.player_rollback.changed_fields;
    g_player_state_rollback_maximum_delta = std::max(
        g_player_state_rollback_maximum_delta,
        result.player_rollback.maximum_delta);
    if (result.player_rollback.restored_mask !=
        result.player_rollback.changed_mask) {
      ++g_player_state_restore_failures;
    }
    if (g_debug_log &&
        source_tick - g_last_player_state_rollback_log_tick >= 30u) {
      AppendNativeLog(
          "player_state_rollback tick=%llu phase=%.3f changed_mask=%02X "
          "restored_mask=%02X fields=%llu max_delta=%lld events_total=%llu "
          "restore_failures=%llu",
          static_cast<unsigned long long>(source_tick), phase,
          result.player_rollback.changed_mask,
          result.player_rollback.restored_mask,
          static_cast<unsigned long long>(
              result.player_rollback.changed_fields),
          static_cast<long long>(result.player_rollback.maximum_delta),
          static_cast<unsigned long long>(g_player_state_rollback_events),
          static_cast<unsigned long long>(g_player_state_restore_failures));
      g_last_player_state_rollback_log_tick = source_tick;
    }
  }

  // Every synthetic pass is a transaction. The next phase starts from the
  // same exact-current matrices and mutable state as the first one.
  RestoreScene(current);
  if (result.rendered) {
    g_interpolated_frames.fetch_add(1, std::memory_order_relaxed);
    g_interpolated_nodes.fetch_add(result.interpolation.applied,
                                   std::memory_order_relaxed);
  }
  return result;
}

void CallOriginalRenderPresentWait(void* context, int wait) {
  g_original_render_present_wait(context, wait);
}

void __cdecl HookRenderPresentWait(void* context, int wait) {
  if ((GetAsyncKeyState(VK_F11) & 1) != 0) {
    const uint32_t previous =
        g_subframes.load(std::memory_order_relaxed);
    const uint32_t toggled = previous ? 0u : ConfiguredSubframes();
    g_subframes.store(toggled, std::memory_order_relaxed);
    ResetSceneHistory();
    AppendNativeLog("runtime native interpolation=%s passes=%u via F11",
                    toggled ? "ENABLED" : "DISABLED",
                    toggled ? toggled - 1u : 0u);
  }
  // Consume input only once at the real scheduler boundary. Synthetic render
  // phases never poll DirectInput and never feed another weapon action back
  // into the simulation.
  UpdateDeathtrapXInput();
  ConsumePendingWeaponWheel();
  const uint32_t subframes = g_subframes.load(std::memory_order_relaxed);
  if ((subframes != 2u && subframes != 3u) || wait <= 0 || !g_renderer) {
    CallOriginalRenderPresentWait(context, wait);
    return;
  }

  // Refresh the exact current transform state before taking the snapshot.
  // Without this, +0x9C still contains the preceding frame and the output
  // sequence moves backward for the midpoint, then jumps forward on exact.
  if (!RefreshCurrentRenderCaches(context)) {
    CallOriginalRenderPresentWait(context, wait);
    ResetSceneHistory();
    return;
  }

  SceneSnapshot current = CaptureScene(context);
  ConsumePendingContactProjection(&current);
  const uint64_t source_tick =
      g_source_ticks.fetch_add(1, std::memory_order_relaxed) + 1;
  SampleUiEligibility();
  if (current.nodes.empty() || g_previous_snapshot.nodes.empty() ||
      current.root == 0 || current.root != g_previous_snapshot.root) {
    CallOriginalRenderPresentWait(context, wait);
    g_older_snapshot = {};
    g_previous_snapshot = std::move(current);
    return;
  }

  TransitionStats transition =
      AnalyzeTransition(context, g_previous_snapshot, current);
  if (transition.suppress_midpoint) {
    g_transition_guard_cooldown = 1u;
  } else if (g_transition_guard_cooldown) {
    transition.suppress_midpoint = true;
    transition.reason = "post_transition";
    --g_transition_guard_cooldown;
  }
  if (transition.suppress_midpoint) {
    ++g_suppressed_midpoints;
    if (g_debug_log &&
        source_tick - g_last_transition_log_tick >= 30u) {
      AppendNativeLog(
          "transition_guard tick=%llu reason=%s matched=%zu entered=%zu "
          "departed=%zu overlap=%.4f camera_move=%.3f camera_rotation_deg=%.2f "
          "camera_tree=%u "
          "callback50_rva=%08llX callback54_rva=%08llX suppressed_total=%llu",
          static_cast<unsigned long long>(source_tick), transition.reason,
          transition.matched, transition.entered, transition.departed,
          transition.overlap, transition.camera_translation,
          transition.camera_rotation_degrees,
          transition.camera_in_scene_tree ? 1u : 0u,
          static_cast<unsigned long long>(
              DungeonRva(transition.primary_callback)),
          static_cast<unsigned long long>(
              DungeonRva(transition.secondary_callback)),
          static_cast<unsigned long long>(g_suppressed_midpoints));
      g_last_transition_log_tick = source_tick;
    }
    CallOriginalRenderPresentWait(context, wait);
    AdvanceSceneHistory(std::move(current));
    return;
  }

  LARGE_INTEGER start{};
  QueryPerformanceCounter(&start);
  const int64_t phase_ticks =
      (g_qpc_frequency.QuadPart * kOriginalPeriodMilliseconds) /
      (1000ll * static_cast<int64_t>(subframes));
  const double first_phase = 1.0 / static_cast<double>(subframes);

  const SceneSnapshot* older =
      g_older_snapshot.root == current.root && !g_older_snapshot.nodes.empty()
          ? &g_older_snapshot
          : nullptr;
  g_active_presentation_trace = {};
  g_active_presentation_trace.tick = source_tick;
  g_active_presentation_trace.player = current.player;
  const InterpolatedPassResult first_pass = RenderInterpolatedPass(
      context, older, g_previous_snapshot, current, first_phase,
      true, source_tick);
  const InterpolationStats& interpolation = first_pass.interpolation;
  const PlayerMutableStateRollbackStats& player_rollback =
      first_pass.player_rollback;
  if (older && interpolation.contact_reversal) {
    LogPlayerProbeTriplet(source_tick, *older, g_previous_snapshot, current);
  }
  if (interpolation.player_bounds_axis_mask) {
    LogBoundsApplication(source_tick, g_previous_snapshot, current,
                         interpolation.player_bounds_axis_mask);
  }
  if (!first_pass.rendered) {
    CallOriginalRenderPresentWait(context, wait);
    AdvanceSceneHistory(std::move(current));
    return;
  }
  g_player_contact_settles += interpolation.player_contact_settle;
  g_player_raw_contact_detections += interpolation.player_raw_contact;
  if (g_debug_log && source_tick % 120u == 0u) {
    AppendNativeLog(
        "tick=%llu captured=%zu applied=%llu hierarchy=%llu world_fallback=%llu "
        "exact=%llu invalid=%llu parent_mismatch=%llu contact_reversal=%llu "
        "temporal_pose_guard=%llu player_root_found=%llu "
        "player_contact_settle=%llu player_raw_contact=%llu "
        "player_exact_subtree_nodes=%llu contact_settle_total=%llu "
        "raw_contact_total=%llu matrix_candidates=%llu raw_candidates=%llu "
        "contact_cooldown=%u contact_hits=%u "
        "contact_hit_window=%u persistent_contact=%u "
        "directional_release=%llu directional_release_total=%llu "
        "release_streak=%u release_quiet=%u release_alignment=%.3f "
        "release_outward=%.6f correction_length=%.6f normal_valid=%u "
        "bounds_nodes=%llu bounds_axes=%u projection=%d/%d/%d "
        "projection_seq=%llu projection_events=%llu projection_nodes=%llu "
        "projection_captures=%llu projection_consumes=%llu "
        "projection_54d00=%llu projection_68390=%llu "
        "projection_68390_small_rejected=%llu bridge_events=%llu "
        "bridge_nodes=%llu manifold=%d/%d/%d manifold_event=%llu "
        "manifold_events_total=%llu manifold_midpoint_nodes=%llu "
        "manifold_endpoint_nodes=%llu manifold_depth=%.6f "
        "manifold_max_depth=%.6f normal_flip_rejected=%llu "
        "normal_flip_rejected_total=%llu "
        "max_move=%.3f "
        "max_rotation_deg=%.2f overlap=%.4f entered=%zu departed=%zu "
        "camera_move=%.3f camera_rotation_deg=%.2f camera_tree=%u "
        "callback50_rva=%08llX "
        "callback54_rva=%08llX suppressed_total=%llu ui_changed=%llu "
        "ui_restored=%llu ui_logic_rollbacks=%llu page_restores=%llu "
        "player_rollback_mask=%02X player_restore_mask=%02X "
        "player_rollback_events=%llu player_rollback_fields=%llu "
        "player_restore_failures=%llu player_rollback_max_delta=%lld "
        "suppressed_dxgi=%llu "
        "ui_owner_ticks=%llu ui_object_ticks=%llu ui_gate_open=%llu "
        "ui_gate_closed=%llu ui_delta=%lld",
        static_cast<unsigned long long>(source_tick), current.nodes.size(),
        static_cast<unsigned long long>(interpolation.applied),
        static_cast<unsigned long long>(interpolation.hierarchical),
        static_cast<unsigned long long>(interpolation.world_fallback),
        static_cast<unsigned long long>(interpolation.exact),
        static_cast<unsigned long long>(interpolation.invalid),
        static_cast<unsigned long long>(interpolation.parent_mismatch),
        static_cast<unsigned long long>(interpolation.contact_reversal),
        static_cast<unsigned long long>(interpolation.temporal_pose_guard),
        static_cast<unsigned long long>(interpolation.player_root_found),
        static_cast<unsigned long long>(interpolation.player_contact_settle),
        static_cast<unsigned long long>(interpolation.player_raw_contact),
        static_cast<unsigned long long>(
            interpolation.player_exact_subtree_nodes),
        static_cast<unsigned long long>(g_player_contact_settles),
        static_cast<unsigned long long>(g_player_raw_contact_detections),
        static_cast<unsigned long long>(g_player_matrix_contact_candidates),
        static_cast<unsigned long long>(g_player_raw_contact_candidates),
        g_player_contact_exact_cooldown,
        g_player_contact_hits_in_window,
        g_player_contact_hit_window,
        g_player_persistent_contact_cooldown,
        static_cast<unsigned long long>(
            interpolation.player_directional_release),
        static_cast<unsigned long long>(g_player_contact_directional_releases),
        g_player_contact_release_streak,
        g_player_contact_quiet_ticks,
        g_player_contact_release_alignment,
        g_player_contact_outward_distance,
        g_player_contact_correction_length,
        g_player_contact_normal_valid ? 1u : 0u,
        static_cast<unsigned long long>(
            interpolation.player_bounds_guided_nodes),
        interpolation.player_bounds_axis_mask,
        current.player_contact_projection[0],
        current.player_contact_projection[1],
        current.player_contact_projection[2],
        static_cast<unsigned long long>(
            current.player_contact_projection_sequence),
        static_cast<unsigned long long>(
            interpolation.player_contact_projection_events),
        static_cast<unsigned long long>(
            interpolation.player_contact_projection_nodes),
        static_cast<unsigned long long>(g_contact_projection_captures),
        static_cast<unsigned long long>(g_contact_projection_consumes),
        static_cast<unsigned long long>(g_contact_projection_54d00_captures),
        static_cast<unsigned long long>(g_contact_projection_68390_captures),
        static_cast<unsigned long long>(
            g_contact_projection_68390_small_rejections),
        static_cast<unsigned long long>(
            interpolation.player_contact_bridge_events),
        static_cast<unsigned long long>(
            interpolation.player_contact_bridge_nodes),
        current.player_contact_manifold_projection[0],
        current.player_contact_manifold_projection[1],
        current.player_contact_manifold_projection[2],
        static_cast<unsigned long long>(
            interpolation.player_contact_manifold_events),
        static_cast<unsigned long long>(g_player_contact_manifold_events),
        static_cast<unsigned long long>(
            g_player_contact_manifold_midpoint_nodes),
        static_cast<unsigned long long>(
            g_player_contact_manifold_endpoint_nodes),
        interpolation.player_contact_manifold_depth,
        g_player_contact_manifold_max_depth,
        static_cast<unsigned long long>(
            interpolation.player_contact_normal_flip_rejections),
        static_cast<unsigned long long>(
            g_player_contact_normal_flip_rejections),
        interpolation.max_translation, interpolation.max_rotation_degrees,
        transition.overlap, transition.entered, transition.departed,
        transition.camera_translation, transition.camera_rotation_degrees,
        transition.camera_in_scene_tree ? 1u : 0u,
        static_cast<unsigned long long>(
            DungeonRva(transition.primary_callback)),
        static_cast<unsigned long long>(
            DungeonRva(transition.secondary_callback)),
        static_cast<unsigned long long>(g_suppressed_midpoints),
        static_cast<unsigned long long>(g_ui_state_changes),
        static_cast<unsigned long long>(g_ui_state_restores),
        static_cast<unsigned long long>(g_ui_logic_rollbacks),
        static_cast<unsigned long long>(g_page_restore_flips),
        player_rollback.changed_mask, player_rollback.restored_mask,
        static_cast<unsigned long long>(g_player_state_rollback_events),
        static_cast<unsigned long long>(g_player_state_rollback_fields),
        static_cast<unsigned long long>(g_player_state_restore_failures),
        static_cast<long long>(g_player_state_rollback_maximum_delta),
        static_cast<unsigned long long>(
            GetDeathtrapNativeSuppressedPresentCount()),
        static_cast<unsigned long long>(g_ui_owner_ticks),
        static_cast<unsigned long long>(g_ui_object_ticks),
        static_cast<unsigned long long>(g_ui_gate_open_ticks),
        static_cast<unsigned long long>(g_ui_gate_closed_ticks),
        static_cast<long long>(g_last_ui_frame_delta));
    AppendNativeLog(
        "axis_return_bins tick=%llu bins=<.25/.50/.75/1.0 "
        "matrix_x=%llu/%llu/%llu/%llu matrix_y=%llu/%llu/%llu/%llu "
        "matrix_z=%llu/%llu/%llu/%llu raw_x=%llu/%llu/%llu/%llu "
        "raw_y=%llu/%llu/%llu/%llu raw_z=%llu/%llu/%llu/%llu",
        static_cast<unsigned long long>(source_tick),
        static_cast<unsigned long long>(g_matrix_axis_return_bins[0][0]),
        static_cast<unsigned long long>(g_matrix_axis_return_bins[0][1]),
        static_cast<unsigned long long>(g_matrix_axis_return_bins[0][2]),
        static_cast<unsigned long long>(g_matrix_axis_return_bins[0][3]),
        static_cast<unsigned long long>(g_matrix_axis_return_bins[1][0]),
        static_cast<unsigned long long>(g_matrix_axis_return_bins[1][1]),
        static_cast<unsigned long long>(g_matrix_axis_return_bins[1][2]),
        static_cast<unsigned long long>(g_matrix_axis_return_bins[1][3]),
        static_cast<unsigned long long>(g_matrix_axis_return_bins[2][0]),
        static_cast<unsigned long long>(g_matrix_axis_return_bins[2][1]),
        static_cast<unsigned long long>(g_matrix_axis_return_bins[2][2]),
        static_cast<unsigned long long>(g_matrix_axis_return_bins[2][3]),
        static_cast<unsigned long long>(g_raw_axis_return_bins[0][0]),
        static_cast<unsigned long long>(g_raw_axis_return_bins[0][1]),
        static_cast<unsigned long long>(g_raw_axis_return_bins[0][2]),
        static_cast<unsigned long long>(g_raw_axis_return_bins[0][3]),
        static_cast<unsigned long long>(g_raw_axis_return_bins[1][0]),
        static_cast<unsigned long long>(g_raw_axis_return_bins[1][1]),
        static_cast<unsigned long long>(g_raw_axis_return_bins[1][2]),
        static_cast<unsigned long long>(g_raw_axis_return_bins[1][3]),
        static_cast<unsigned long long>(g_raw_axis_return_bins[2][0]),
        static_cast<unsigned long long>(g_raw_axis_return_bins[2][1]),
        static_cast<unsigned long long>(g_raw_axis_return_bins[2][2]),
        static_cast<unsigned long long>(g_raw_axis_return_bins[2][3]));
    FlushMovementStageProbeStats(source_tick);
    FlushMovementCallbackProbeStats(source_tick);
    g_matrix_axis_return_bins = {};
    g_raw_axis_return_bins = {};
  }

  LARGE_INTEGER target{};
  target.QuadPart = start.QuadPart + phase_ticks;
  WaitUntil(target);

  if (subframes == 3u) {
    // Temporal contact state was advanced by the first pass. The second pass
    // reuses its resolved projection and performs only another render
    // transaction at 2/3, so collision cooldowns still advance exactly once
    // per real source tick.
    const InterpolatedPassResult second_pass = RenderInterpolatedPass(
        context, older, g_previous_snapshot, current, 2.0 / 3.0,
        false, source_tick);
    if (!second_pass.rendered && g_debug_log) {
      AppendNativeLog("interpolated phase failed tick=%llu phase=0.667",
                      static_cast<unsigned long long>(source_tick));
    }
    target.QuadPart = start.QuadPart + phase_ticks * 2ll;
    WaitUntil(target);
  }

  // The original routine performs the exact current-state render, advances
  // the engine frame counter once, runs its normal housekeeping, and waits
  // the remaining final fraction of the native six-count scheduler period.
  RestoreScene(current);
  g_active_presentation_trace.stage = DeathtrapNativePresentationStage::kExact;
  g_active_presentation_trace.exact_scene = &current;
  if (current.player_contact_manifold_projection_valid) {
    g_active_presentation_trace.exact_projection =
        current.player_contact_manifold_projection;
    g_active_presentation_trace.exact_projection_valid = true;
  }
  CallOriginalRenderPresentWait(context, wait);
  g_active_presentation_trace.stage = DeathtrapNativePresentationStage::kNone;
  g_active_presentation_trace.exact_scene = nullptr;
  g_player_contact_manifold_endpoint_nodes +=
      g_active_presentation_trace.exact_projection_nodes;
  QueuePresentationTrace(source_tick, g_previous_snapshot, current,
                         interpolation);
  AdvanceSceneHistory(std::move(current));
}

void InitializePatchState() {
  g_debug_log = ConfiguredDebugLog();
  g_weapon_wheel_enabled = ConfiguredWeaponWheelEnabled();
  g_weapon_wheel_invert = ConfiguredWeaponWheelInvert();
  g_xinput_enabled =
      ConfiguredInteger(L"XInput", L"Enabled", 1) != 0;
  g_xinput_base_bindings =
      ConfiguredInteger(L"XInput", L"BaseBindings", 1) != 0;
  g_xinput_controller_index = static_cast<uint32_t>(std::clamp(
      ConfiguredInteger(L"XInput", L"Controller", 0), 0, 3));
  g_xinput_selector_hold_ms = static_cast<uint32_t>(std::clamp(
      ConfiguredInteger(L"XInput", L"SelectorHoldMs", 225), 150, 600));
  g_xinput_left_deadzone = std::clamp(
      ConfiguredInteger(L"XInput", L"LeftStickDeadzone",
                        XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE),
      0, 30000);
  g_xinput_right_deadzone = std::clamp(
      ConfiguredInteger(L"XInput", L"RightStickDeadzone",
                        XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE),
      0, 30000);
  g_xinput_trigger_threshold = std::clamp(
      ConfiguredInteger(L"XInput", L"TriggerThreshold",
                        XINPUT_GAMEPAD_TRIGGER_THRESHOLD),
      0, 255);
  g_xinput_first_person_pixels = std::clamp(
      ConfiguredInteger(L"XInput", L"RightStickPixelsPerTick", 12), 1, 80);
  g_xinput_menu_mouse_pixels = std::clamp(
      ConfiguredInteger(L"XInput", L"MenuRightStickPixelsPerTick", 6), 1, 40);
  g_xinput_right_stick_curve = static_cast<double>(std::clamp(
      ConfiguredInteger(L"XInput", L"RightStickResponseCurvePercent", 135),
      100, 250)) / 100.0;
  g_xinput_invert_right_y =
      ConfiguredInteger(L"XInput", L"InvertRightY", 0) != 0;
  g_xinput_vibration_enabled =
      ConfiguredInteger(L"XInput", L"VibrationEnabled", 1) != 0;
  g_xinput_vibration_strength_percent = static_cast<uint32_t>(std::clamp(
      ConfiguredInteger(L"XInput", L"VibrationStrengthPercent", 70),
      0, 100));
  g_xinput_attack_vibration_ms = static_cast<uint32_t>(std::clamp(
      ConfiguredInteger(L"XInput", L"AttackVibrationMs", 85), 20, 250));
  g_xinput_block_vibration_ms = static_cast<uint32_t>(std::clamp(
      ConfiguredInteger(L"XInput", L"BlockVibrationMs", 55), 20, 250));
  g_ui_message_lifetime_percent = static_cast<uint32_t>(std::clamp(
      ConfiguredInteger(L"Text", L"MessageLifetimePercent", 300),
      100, 1000));
  g_ui_message_lifetime_ticks = static_cast<uint32_t>(
      (kOriginalUiMessageLifetimeTicks * g_ui_message_lifetime_percent + 50u) /
      100u);
  g_pst_message_lifetime_ticks = static_cast<uint32_t>(
      (kOriginalPstMessageLifetimeTicks * g_ui_message_lifetime_percent +
       50u) /
      100u);
  g_xinput_selector_radius = std::clamp(
      ConfiguredInteger(L"XInput", L"SelectorRadius", 104), 64, 160);
  g_xinput_selector_center_y = std::clamp(
      ConfiguredInteger(L"XInput", L"SelectorCenterY", 316), 192, 400);
  g_xinput_movement_threshold = static_cast<double>(std::clamp(
      ConfiguredInteger(L"XInput", L"MovementThresholdPercent", 14),
      10, 60)) / 100.0;
  g_xinput_run_threshold = static_cast<double>(std::clamp(
      ConfiguredInteger(L"XInput", L"RunThresholdPercent", 50),
      40, 95)) / 100.0;
  g_xinput_run_release_threshold = static_cast<double>(std::clamp(
      ConfiguredInteger(L"XInput", L"RunReleaseThresholdPercent", 30),
      20, 80)) / 100.0;
  if (g_xinput_run_release_threshold >= g_xinput_run_threshold) {
    g_xinput_run_release_threshold =
        std::max(0.20, g_xinput_run_threshold - 0.12);
  }
  if (g_xinput_enabled) {
    LoadXInputRuntime();
  }
  const uint32_t subframes = ConfiguredSubframes();
  g_subframes.store(subframes, std::memory_order_relaxed);
  if (!subframes) {
    g_state.store(DeathtrapNativeRenderPatchState::kDisabled,
                  std::memory_order_release);
    return;
  }

  HMODULE dungeon = GetModuleHandleW(L"Dungeon.dll");
  if (!dungeon) {
    g_state.store(DeathtrapNativeRenderPatchState::kDungeonModuleMissing,
                  std::memory_order_release);
    return;
  }
  g_dungeon_base = reinterpret_cast<uint8_t*>(dungeon);
  if (!IsExpectedDungeonImage(g_dungeon_base)) {
    g_state.store(DeathtrapNativeRenderPatchState::kUnsupportedDungeonDll,
                  std::memory_order_release);
    return;
  }

  g_ui_message_lifetime_patched =
      PatchUiMessageLifetime(g_ui_message_lifetime_ticks);
  g_pst_message_lifetime_patched =
      PatchPstMessageLifetime(g_pst_message_lifetime_ticks);

  QueryPerformanceFrequency(&g_qpc_frequency);
  g_renderer = reinterpret_cast<RendererFn>(g_dungeon_base + kRendererRva);
  g_scene_cache_update = reinterpret_cast<RenderCacheUpdateFn>(
      g_dungeon_base + kSceneCacheUpdateRva);
  g_camera_cache_update = reinterpret_cast<RenderCacheUpdateFn>(
      g_dungeon_base + kCameraCacheUpdateRva);
  AppendNativeLog(
      "Deathtrap native render overlay 0.0.39 XInput vibration, "
      "transactional PST text lifetime and tuned controller response "
      "integer x3 presentation "
      "session: "
      "unchanged v31 "
      "stable-cadence "
      "page-coherent "
      "hierarchical rigid phase interpolation at 1/3 and 2/3 with "
      "transactional UI logic rollback on every synthetic pass, "
      "dual world-matrix plus tight raw-root contact consensus, accepted "
      "phase-scaled A-to-C contact bridge, one-sided persistent contact "
      "manifold on synthetic and exact presentation with opposing one-sample normal "
      "outliers rejected; exact contact projection is applied at the final "
      "renderer entry after cache refresh and actual midpoint/exact player "
      "roots are traced in buffered presentation_pair records; measured "
      "player render-root translation is reconciled to direct world-space "
      "midpoint while bone animation remains hierarchical; "
      "camera identity is retained when the camera already belongs to the "
      "scene tree for diagnostics, while normal camera turns and small "
      "frustum-membership changes keep their synthetic midpoint cadence; "
      "the D3D11 present layer independently "
      "rejects whole-black and newly-black midpoint regions against the last "
      "exact endpoint; "
      "0x10054D00/qualified 0x10068390 "
      "projection remains midpoint-only; every visual correction is "
      "render-only and exact simulation state is restored; "
      "debug-only broad main-loop probes plus always-on lightweight local "
      "resolver and [edi+0x18] contact capture, transactional synthetic-pass "
      "player-state rollback, "
      "F11 native A/B and global clock untouched; DirectInput wheel events "
      "are observation-only and commit through Dungeon.dll+0x90610 once per "
      "real gameplay tick (enabled=%u invert=%u); XInput controller=%u "
      "base_bindings=%u hold_ms=%u deadzones=%d/%d radial=%d center_y=%d "
      "vibration=%u/%u%%/%u/%ums available=%u "
      "message_lifetime=%u%% ui=%u_ticks/%u pst=%u_ticks/%u",
      g_weapon_wheel_enabled ? 1u : 0u,
      g_weapon_wheel_invert ? 1u : 0u,
      g_xinput_controller_index,
      g_xinput_base_bindings ? 1u : 0u,
      g_xinput_selector_hold_ms,
      g_xinput_left_deadzone,
      g_xinput_right_deadzone,
      g_xinput_selector_radius,
      g_xinput_selector_center_y,
      g_xinput_vibration_enabled ? 1u : 0u,
      g_xinput_vibration_strength_percent,
      g_xinput_attack_vibration_ms,
      g_xinput_block_vibration_ms,
      g_xinput_set_state ? 1u : 0u,
      g_ui_message_lifetime_percent,
      g_ui_message_lifetime_ticks,
      g_ui_message_lifetime_patched ? 1u : 0u,
      g_pst_message_lifetime_ticks,
      g_pst_message_lifetime_patched ? 1u : 0u);
  g_state.store(DeathtrapNativeRenderPatchState::kActive,
                std::memory_order_release);
}

}  // namespace

void QueueDeathtrapWeaponWheelDelta(int32_t delta) {
  if (!g_weapon_wheel_enabled || delta == 0) {
    return;
  }
  // DirectInput reports relative wheel motion in WHEEL_DELTA units. Preserve
  // the sign and a small bounded number of detents without ever delaying the
  // input callback or touching engine state from the input thread.
  int32_t detents = delta / WHEEL_DELTA;
  if (detents == 0) {
    detents = delta > 0 ? 1 : -1;
  }
  int32_t observed =
      g_pending_weapon_wheel_detents.load(std::memory_order_relaxed);
  for (;;) {
    const int32_t desired = std::clamp(observed + detents, -8, 8);
    if (g_pending_weapon_wheel_detents.compare_exchange_weak(
            observed, desired, std::memory_order_release,
            std::memory_order_relaxed)) {
      break;
    }
  }
  g_last_weapon_wheel_event_ms.store(GetTickCount64(),
                                     std::memory_order_relaxed);
}

void PollDeathtrapFrontendXInput() {
  PollFrontendXInputInternal();
}

DeathtrapNativePresentationStage GetDeathtrapNativePresentationStage() {
  return g_active_presentation_trace.stage;
}

DeathtrapControllerSelectorStatus GetDeathtrapControllerSelectorStatus() {
  const uint32_t packed =
      g_controller_selector_overlay.load(std::memory_order_acquire);
  DeathtrapControllerSelectorStatus status;
  status.visible = (packed & 1u) != 0;
  status.slot_available = (packed & 2u) != 0;
  status.confirmation_required = (packed & 4u) != 0;
  status.category = (packed >> 8u) & 0xFu;
  status.slot = (packed >> 16u) & 0xFu;
  return status;
}

void InitializeDeathtrapNativeRenderPatch() {
  std::call_once(g_patch_once, &InitializePatchState);
}

bool InstallDeathtrapNativeRenderHooks() {
  if (g_render_hook_installed.load(std::memory_order_acquire)) {
    return true;
  }
  if (g_state.load(std::memory_order_acquire) !=
      DeathtrapNativeRenderPatchState::kActive ||
      !g_dungeon_base || !IsExpectedDungeonImage(g_dungeon_base)) {
    return false;
  }
  void* const inventory_slot_target =
      g_dungeon_base + kInventorySlotDrawRva;
  const MH_STATUS create_inventory_slot = MH_CreateHook(
      inventory_slot_target, reinterpret_cast<void*>(&HookInventorySlotDraw),
      reinterpret_cast<void**>(&g_original_inventory_slot_draw));
  if (create_inventory_slot != MH_OK &&
      create_inventory_slot != MH_ERROR_ALREADY_CREATED) {
    return false;
  }
  const MH_STATUS enable_inventory_slot = MH_EnableHook(inventory_slot_target);
  if (enable_inventory_slot != MH_OK &&
      enable_inventory_slot != MH_ERROR_ENABLED) {
    return false;
  }

  void* const renderer_target = g_dungeon_base + kRendererRva;
  const MH_STATUS create_renderer = MH_CreateHook(
      renderer_target, reinterpret_cast<void*>(&HookRenderer),
      reinterpret_cast<void**>(&g_original_renderer));
  if (create_renderer != MH_OK &&
      create_renderer != MH_ERROR_ALREADY_CREATED) {
    MH_DisableHook(inventory_slot_target);
    return false;
  }
  const MH_STATUS enable_renderer = MH_EnableHook(renderer_target);
  if (enable_renderer != MH_OK && enable_renderer != MH_ERROR_ENABLED) {
    MH_DisableHook(inventory_slot_target);
    return false;
  }

  void* const scheduler_target = g_dungeon_base + kRenderPresentWaitRva;
  const MH_STATUS create = MH_CreateHook(
      scheduler_target, reinterpret_cast<void*>(&HookRenderPresentWait),
      reinterpret_cast<void**>(&g_original_render_present_wait));
  if (create != MH_OK && create != MH_ERROR_ALREADY_CREATED) {
    MH_DisableHook(renderer_target);
    MH_DisableHook(inventory_slot_target);
    return false;
  }
  const MH_STATUS enable = MH_EnableHook(scheduler_target);
  if (enable != MH_OK && enable != MH_ERROR_ENABLED) {
    MH_DisableHook(renderer_target);
    MH_DisableHook(inventory_slot_target);
    return false;
  }

  // Diagnostic-only and non-fatal. Unlike the reverted V26 experiment, this
  // patches only the direct calls made by the one gameplay main loop and does
  // not detour any shared engine function globally.
  InstallMovementStageCallsiteProbes();
  InstallMovementDispatcherCallbackProbe();
  g_render_hook_installed.store(true, std::memory_order_release);
  return true;
}

DeathtrapNativeRenderPatchStatus GetDeathtrapNativeRenderPatchStatus() {
  DeathtrapNativeRenderPatchStatus status;
  status.state = g_state.load(std::memory_order_acquire);
  status.subframes = g_subframes.load(std::memory_order_relaxed);
  status.original_rate = kOriginalGameplayRate;
  status.requested_rate = kOriginalGameplayRate * status.subframes;
  status.effective_rate =
      status.subframes >= 3u ? 50u
                             : (status.subframes == 2u
                                    ? 33u
                                    : kOriginalGameplayRate);
  status.last_scheduler_request = kOriginalGameplayRate;
  status.forced_scheduler_requests = 0;
  status.scheduler_hook_installed =
      g_render_hook_installed.load(std::memory_order_relaxed);
  status.source_ticks = g_source_ticks.load(std::memory_order_relaxed);
  status.interpolated_frames =
      g_interpolated_frames.load(std::memory_order_relaxed);
  status.interpolated_nodes =
      g_interpolated_nodes.load(std::memory_order_relaxed);
  return status;
}

const char* DeathtrapNativeRenderPatchStateName(
    DeathtrapNativeRenderPatchState state) {
  switch (state) {
    case DeathtrapNativeRenderPatchState::kDisabled:
      return "OFF";
    case DeathtrapNativeRenderPatchState::kActive:
      return "ACTIVE";
    case DeathtrapNativeRenderPatchState::kDungeonModuleMissing:
      return "DUNGEON.DLL NOT FOUND";
    case DeathtrapNativeRenderPatchState::kUnsupportedDungeonDll:
      return "UNSUPPORTED DUNGEON.DLL";
    case DeathtrapNativeRenderPatchState::kUnexpectedOriginalValue:
      return "ORIGINAL RATE ALREADY MODIFIED";
    case DeathtrapNativeRenderPatchState::kWriteFailed:
      return "MEMORY PATCH FAILED";
    default:
      return "NOT ATTEMPTED";
  }
}
