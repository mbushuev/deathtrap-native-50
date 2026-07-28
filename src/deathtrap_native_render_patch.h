#pragma once

#include <cstdint>

enum class DeathtrapNativeRenderPatchState : uint32_t {
  kNotAttempted,
  kDisabled,
  kActive,
  kDungeonModuleMissing,
  kUnsupportedDungeonDll,
  kUnexpectedOriginalValue,
  kWriteFailed,
};

struct DeathtrapNativeRenderPatchStatus {
  DeathtrapNativeRenderPatchState state =
      DeathtrapNativeRenderPatchState::kNotAttempted;
  uint32_t original_rate = 0;
  uint32_t requested_rate = 0;
  uint32_t effective_rate = 0;
  uint32_t last_scheduler_request = 0;
  uint32_t forced_scheduler_requests = 0;
  bool scheduler_hook_installed = false;
  uint64_t source_ticks = 0;
  uint64_t interpolated_frames = 0;
  uint64_t interpolated_nodes = 0;
  uint32_t subframes = 0;
};

enum class DeathtrapNativePresentationStage : uint8_t {
  kNone = 0,
  kMidpoint = 1,
  kExact = 2,
};

// Initializes the opt-in Deathtrap Dungeon transform-interpolation experiment.
// It never changes the gameplay scheduler, Miles timer, Redbook audio, AI,
// physics, animation tick or input tick.
void InitializeDeathtrapNativeRenderPatch();

// Used only while the native midpoint restores the legacy DirectDraw page
// orientation. The restore Flip must rotate the game's front/back surfaces,
// but must not become another visible DXGI presentation.
void SetDeathtrapNativePageRestorePresentSuppressed(bool suppressed);
uint64_t GetDeathtrapNativeSuppressedPresentCount();

// Identifies the native render pass currently reaching the dgVoodoo DXGI
// Present hook. This is thread-local because both the renderer and Present
// execute synchronously on the game's render thread.
DeathtrapNativePresentationStage GetDeathtrapNativePresentationStage();

// Installs the native render detours after MH_Initialize.
bool InstallDeathtrapNativeRenderHooks();

DeathtrapNativeRenderPatchStatus GetDeathtrapNativeRenderPatchStatus();
const char* DeathtrapNativeRenderPatchStateName(
    DeathtrapNativeRenderPatchState state);
