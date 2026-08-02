#pragma once

#include <cstdint>

// One process session writes all render/input/present diagnostics to one
// timestamped file under the game's logs subdirectory.
const wchar_t* GetDeathtrapSessionLogPath();

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

struct DeathtrapControllerSelectorStatus {
  bool visible = false;
  bool slot_available = false;
  bool confirmation_required = false;
  uint32_t category = 0;
  uint32_t slot = 0;
};

enum class DeathtrapNativePresentationStage : uint8_t {
  kNone = 0,
  kMidpoint = 1,
  kExact = 2,
};

// Initializes the opt-in Deathtrap Dungeon transform-interpolation experiment.
// It never advances the gameplay scheduler, Miles/audio timer, AI, physics,
// animation tick or input tick. The optional Steam music fix changes only
// Redbook track enumeration/routing before original MP3 playback.
void InitializeDeathtrapNativeRenderPatch();

// DirectInput observation is deliberately separated from game-state mutation.
// The proxy only queues relative wheel motion; the native scheduler consumes
// it on the next real gameplay tick through Dungeon.dll's own weapon-selection
// path.
void QueueDeathtrapWeaponWheelDelta(int32_t delta);

// Adds controller-originated relative mouse motion to the next DirectInput
// mouse sample. Buttons are level states and are merged with the real mouse.
// This is used for the retail menu pointer because legacy DirectInput does not
// reliably receive SendInput mouse events on current Windows versions.
void SubmitDeathtrapXInputMouseState(int32_t delta_x, int32_t delta_y,
                                     bool left_button, bool right_button);

// Routes physical relative DirectInput mouse motion to the modern third-
// person camera. The proxy calls this only when the render patch explicitly
// owns mouse-look, so menus and the retail first-person mode still receive the
// original mouse stream unchanged.
void SubmitDeathtrapPhysicalMouseDelta(int32_t delta_x, int32_t delta_y);
bool DeathtrapModernCameraConsumesMouse();

// True only while the overlay-owned body-visible first-person camera is the
// active gameplay view. The DirectInput proxy uses this to reinterpret A/D as
// the already-installed J/K side-step actions without changing keys.cfg.
bool DeathtrapImmersiveFirstPersonActive();

// Marks an explicit operate/use input from either the controller bridge or
// the physical DirectInput keyboard. The camera uses this narrow signal to
// allow only authored lever/switch reveal shots through the retail rig.
void NotifyDeathtrapOperateInput();

// Polls only the startup/movie/menu controller path. DirectInput calls this
// immediately before the retail frontend consumes mouse data, so controller
// input is available before Dungeon.dll's gameplay render scheduler starts.
// Gameplay and the native inventory selector remain on the engine scheduler
// thread and are deliberately not executed from this path.
void PollDeathtrapFrontendXInput();

// Used only while the native midpoint restores the legacy DirectDraw page
// orientation. The restore Flip must rotate the game's front/back surfaces,
// but must not become another visible DXGI presentation.
void SetDeathtrapNativePageRestorePresentSuppressed(bool suppressed);
uint64_t GetDeathtrapNativeSuppressedPresentCount();

// Identifies the native render pass currently reaching the dgVoodoo DXGI
// Present hook. This is thread-local because both the renderer and Present
// execute synchronously on the game's render thread.
DeathtrapNativePresentationStage GetDeathtrapNativePresentationStage();
uint64_t GetDeathtrapNativePresentationTick();

// Read-only presentation state for the controller's four-category selector.
// The D3D11 layer uses this to draw an eight-direction marker after the game
// has rendered its original inventory row.
DeathtrapControllerSelectorStatus GetDeathtrapControllerSelectorStatus();

// Installs the native render detours after MH_Initialize.
bool InstallDeathtrapNativeRenderHooks();

DeathtrapNativeRenderPatchStatus GetDeathtrapNativeRenderPatchStatus();
const char* DeathtrapNativeRenderPatchStateName(
    DeathtrapNativeRenderPatchState state);
