#include <windows.h>

#include <d3d11.h>
#include <dinput.h>
#include <dxgi1_6.h>
#include <MinHook.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <unordered_map>

#include "deathtrap_native_render_patch.h"
#include "native_d3d11_present_guard.h"

namespace {

using CreateDXGIFactoryFn = HRESULT(WINAPI*)(REFIID, void**);
using CreateDXGIFactory2Fn = HRESULT(WINAPI*)(UINT, REFIID, void**);
using FactoryCreateSwapChainFn = HRESULT(STDMETHODCALLTYPE*)(
    IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**);
using FactoryCreateSwapChainForHwndFn = HRESULT(STDMETHODCALLTYPE*)(
    IDXGIFactory2*, IUnknown*, HWND, const DXGI_SWAP_CHAIN_DESC1*,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*, IDXGISwapChain1**);
using FactoryCreateSwapChainForCoreWindowFn = HRESULT(STDMETHODCALLTYPE*)(
    IDXGIFactory2*, IUnknown*, IUnknown*, const DXGI_SWAP_CHAIN_DESC1*,
    IDXGIOutput*, IDXGISwapChain1**);
using FactoryCreateSwapChainForCompositionFn = HRESULT(STDMETHODCALLTYPE*)(
    IDXGIFactory2*, IUnknown*, const DXGI_SWAP_CHAIN_DESC1*, IDXGIOutput*,
    IDXGISwapChain1**);
using SwapChainPresentFn =
    HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using SwapChainPresent1Fn = HRESULT(STDMETHODCALLTYPE*)(
    IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
using DirectInputCreateDeviceAFn = HRESULT(STDMETHODCALLTYPE*)(
    IDirectInputA*, REFGUID, LPDIRECTINPUTDEVICEA*, LPUNKNOWN);
using DirectInputDeviceGetStateFn = HRESULT(STDMETHODCALLTYPE*)(
    IDirectInputDeviceA*, DWORD, LPVOID);
using DirectInputDeviceGetDataFn = HRESULT(STDMETHODCALLTYPE*)(
    IDirectInputDeviceA*, DWORD, LPDIDEVICEOBJECTDATA, LPDWORD, DWORD);
using DirectInputDeviceAcquireFn = HRESULT(STDMETHODCALLTYPE*)(
    IDirectInputDeviceA*);
using DirectInputDeviceUnacquireFn = HRESULT(STDMETHODCALLTYPE*)(
    IDirectInputDeviceA*);
using DirectInputDeviceSetDataFormatFn = HRESULT(STDMETHODCALLTYPE*)(
    IDirectInputDeviceA*, LPCDIDATAFORMAT);
using DirectInputDeviceSetCooperativeLevelFn = HRESULT(STDMETHODCALLTYPE*)(
    IDirectInputDeviceA*, HWND, DWORD);

HMODULE g_system_dinput = nullptr;
HMODULE g_dxgi = nullptr;
std::once_flag g_initialize_once;
std::atomic<bool> g_stop_frontend_input{false};
CreateDXGIFactoryFn g_create_factory = nullptr;
CreateDXGIFactoryFn g_create_factory1 = nullptr;
CreateDXGIFactory2Fn g_create_factory2 = nullptr;
FactoryCreateSwapChainFn g_factory_create_swap_chain = nullptr;
FactoryCreateSwapChainForHwndFn g_factory_create_swap_chain_for_hwnd = nullptr;
FactoryCreateSwapChainForCoreWindowFn
    g_factory_create_swap_chain_for_core_window = nullptr;
FactoryCreateSwapChainForCompositionFn
    g_factory_create_swap_chain_for_composition = nullptr;
DirectInputCreateDeviceAFn g_direct_input_create_device = nullptr;
DirectInputDeviceGetStateFn g_direct_input_device_get_state = nullptr;
DirectInputDeviceGetDataFn g_direct_input_device_get_data = nullptr;
DirectInputDeviceAcquireFn g_direct_input_device_acquire = nullptr;
DirectInputDeviceUnacquireFn g_direct_input_device_unacquire = nullptr;
DirectInputDeviceSetDataFormatFn g_direct_input_device_set_data_format =
    nullptr;
DirectInputDeviceSetCooperativeLevelFn
    g_direct_input_device_set_cooperative_level = nullptr;
std::mutex g_swap_chain_hooks_mutex;
std::unordered_map<void*, SwapChainPresentFn> g_swap_chain_present;
std::unordered_map<void*, SwapChainPresent1Fn> g_swap_chain_present1;
thread_local bool g_inside_present = false;
thread_local bool g_suppress_page_restore = false;
std::atomic<uint64_t> g_suppressed_page_restores{0};
std::atomic<int32_t> g_xinput_mouse_delta_x{0};
std::atomic<int32_t> g_xinput_mouse_delta_y{0};
std::atomic<uint8_t> g_xinput_mouse_buttons{0};
std::atomic<int32_t> g_xinput_buffered_mouse_delta_x{0};
std::atomic<int32_t> g_xinput_buffered_mouse_delta_y{0};
std::atomic<uint8_t> g_xinput_buffered_mouse_buttons{0};
std::atomic<uint8_t> g_xinput_buffered_mouse_buttons_delivered{0};
std::atomic<uint32_t> g_xinput_buffered_mouse_sequence{1};
std::atomic<bool> g_physical_operate_key_down{false};
std::atomic<uint64_t> g_last_physical_cursor_activity_ms{0};
std::atomic<uint64_t> g_last_controller_cursor_activity_ms{0};
std::atomic<IDirectInputDeviceA*> g_support_mouse_device{nullptr};
std::atomic<HWND> g_support_mouse_window{nullptr};
std::atomic<uint32_t> g_support_mouse_cooperative_flags{0};
std::atomic<uint64_t> g_support_mouse_state_calls{0};
std::atomic<uint64_t> g_support_mouse_data_calls{0};
std::atomic<uint64_t> g_support_mouse_failures{0};
std::atomic<HRESULT> g_support_mouse_last_failure{S_OK};
std::atomic<int64_t> g_support_mouse_delta_x{0};
std::atomic<int64_t> g_support_mouse_delta_y{0};
std::atomic<uint64_t> g_support_last_summary_ms{0};
std::atomic<int32_t> g_support_last_camera_owner{-1};
std::atomic<int32_t> g_support_last_clip_mismatch{-1};

extern "C" {
FARPROC g_target_DirectInputCreateA = nullptr;
FARPROC g_target_DirectInputCreateW = nullptr;
FARPROC g_target_DirectInputCreateEx = nullptr;
FARPROC g_target_DllCanUnloadNow = nullptr;
FARPROC g_target_DllGetClassObject = nullptr;
FARPROC g_target_DllRegisterServer = nullptr;
FARPROC g_target_DllUnregisterServer = nullptr;
}

bool LoadSystemDinput() {
  if (g_system_dinput) {
    return true;
  }
  wchar_t system_directory[MAX_PATH] = {};
  const UINT length = GetSystemDirectoryW(system_directory, MAX_PATH);
  if (!length || length >= MAX_PATH) {
    return false;
  }
  std::wstring path(system_directory);
  path += L"\\dinput.dll";
  g_system_dinput = LoadLibraryW(path.c_str());
  if (!g_system_dinput) {
    return false;
  }
#define RESOLVE(name) \
  g_target_##name = GetProcAddress(g_system_dinput, #name)
  RESOLVE(DirectInputCreateA);
  RESOLVE(DirectInputCreateW);
  RESOLVE(DirectInputCreateEx);
  RESOLVE(DllCanUnloadNow);
  RESOLVE(DllGetClassObject);
  RESOLVE(DllRegisterServer);
  RESOLVE(DllUnregisterServer);
#undef RESOLVE
  return g_target_DirectInputCreateA != nullptr;
}

void DiscardPendingControllerCursorAxes() {
  g_xinput_mouse_delta_x.store(0, std::memory_order_release);
  g_xinput_mouse_delta_y.store(0, std::memory_order_release);
  g_xinput_buffered_mouse_delta_x.store(0, std::memory_order_release);
  g_xinput_buffered_mouse_delta_y.store(0, std::memory_order_release);
}

void NotePhysicalCursorActivity(int32_t delta_x, int32_t delta_y) {
  if (delta_x == 0 && delta_y == 0) {
    return;
  }
  g_last_physical_cursor_activity_ms.store(GetTickCount64(),
                                            std::memory_order_release);
  // A real mouse movement wins this sample even if the frontend poll at the
  // top of the hook just queued a right-stick delta.
  DiscardPendingControllerCursorAxes();
}

bool ControllerCursorAxesOwnInput() {
  return g_last_controller_cursor_activity_ms.load(
             std::memory_order_acquire) >
         g_last_physical_cursor_activity_ms.load(
             std::memory_order_acquire);
}

bool IsCurrentProcessWindow(HWND window) {
  if (!window) {
    return false;
  }
  DWORD process_id = 0;
  GetWindowThreadProcessId(window, &process_id);
  return process_id == GetCurrentProcessId();
}

void AccumulateSupportMouseDelta(int32_t delta_x, int32_t delta_y) {
  g_support_mouse_delta_x.fetch_add(delta_x, std::memory_order_relaxed);
  g_support_mouse_delta_y.fetch_add(delta_y, std::memory_order_relaxed);
}

void MaybeLogMouseSupportSummary(bool camera_consumes) {
  const int32_t owner = camera_consumes ? 1 : 0;
  const int32_t previous_owner =
      g_support_last_camera_owner.exchange(owner, std::memory_order_acq_rel);
  if (previous_owner != owner) {
    AppendDeathtrapSupportLog("support_mouse_owner camera=%u",
                              camera_consumes ? 1u : 0u);
  }

  const uint64_t now_ms = GetTickCount64();
  uint64_t previous_ms =
      g_support_last_summary_ms.load(std::memory_order_relaxed);
  // Five seconds is enough to expose focus/capture/edge failures while
  // keeping an hour-long public session comfortably below the old debug-log
  // volume. Ownership transitions and acquire events are still immediate.
  if (previous_ms && now_ms - previous_ms < 5000u) {
    return;
  }
  if (!g_support_last_summary_ms.compare_exchange_strong(
          previous_ms, now_ms, std::memory_order_acq_rel,
          std::memory_order_relaxed)) {
    return;
  }

  HWND window = g_support_mouse_window.load(std::memory_order_acquire);
  if (!IsCurrentProcessWindow(window)) {
    const HWND foreground = GetForegroundWindow();
    window = IsCurrentProcessWindow(foreground) ? foreground : nullptr;
  }
  RECT window_rect = {};
  RECT client_rect = {};
  POINT client_origin = {};
  const bool have_window_rect = window && GetWindowRect(window, &window_rect);
  bool have_client_rect = window && GetClientRect(window, &client_rect);
  if (have_client_rect && !ClientToScreen(window, &client_origin)) {
    have_client_rect = false;
  }
  if (have_client_rect) {
    OffsetRect(&client_rect, client_origin.x, client_origin.y);
  }

  POINT cursor = {};
  const bool have_cursor = GetCursorPos(&cursor) != FALSE;
  CURSORINFO cursor_info = {};
  cursor_info.cbSize = sizeof(cursor_info);
  const bool cursor_visible =
      GetCursorInfo(&cursor_info) &&
      (cursor_info.flags & CURSOR_SHOWING) != 0u;
  RECT clip = {};
  const bool have_clip = GetClipCursor(&clip) != FALSE;
  const int virtual_left = GetSystemMetrics(SM_XVIRTUALSCREEN);
  const int virtual_top = GetSystemMetrics(SM_YVIRTUALSCREEN);
  const int virtual_right = virtual_left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
  const int virtual_bottom = virtual_top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
  const bool screen_right =
      have_cursor && cursor.x >= virtual_right - 2;
  const bool screen_bottom =
      have_cursor && cursor.y >= virtual_bottom - 2;
  const bool client_right = have_cursor && have_client_rect &&
      cursor.x >= client_rect.right - 2;
  const bool client_bottom = have_cursor && have_client_rect &&
      cursor.y >= client_rect.bottom - 2;
  const bool clip_right = have_cursor && have_clip &&
      cursor.x >= clip.right - 2;
  const bool clip_bottom = have_cursor && have_clip &&
      cursor.y >= clip.bottom - 2;
  const long client_width = have_client_rect
      ? client_rect.right - client_rect.left
      : 0;
  const long client_height = have_client_rect
      ? client_rect.bottom - client_rect.top
      : 0;
  const long clip_width = have_clip ? clip.right - clip.left : 0;
  const long clip_height = have_clip ? clip.bottom - clip.top : 0;
  const bool clip_client_mismatch = have_clip && have_client_rect &&
      (std::abs(clip_width - client_width) > 2 ||
       std::abs(clip_height - client_height) > 2);
  const int32_t mismatch_state = clip_client_mismatch ? 1 : 0;
  const int32_t previous_mismatch = g_support_last_clip_mismatch.exchange(
      mismatch_state, std::memory_order_acq_rel);
  if (clip_client_mismatch && previous_mismatch != mismatch_state) {
    AppendDeathtrapSupportLog(
        "support_mouse_warning type=clip_client_mismatch "
        "cursor=%ld,%ld clip=%ld,%ld,%ld,%ld "
        "client=%ld,%ld,%ld,%ld at_clip_edge=%u%u",
        have_cursor ? cursor.x : -1L, have_cursor ? cursor.y : -1L,
        clip.left, clip.top, clip.right, clip.bottom,
        client_rect.left, client_rect.top, client_rect.right,
        client_rect.bottom, clip_right ? 1u : 0u,
        clip_bottom ? 1u : 0u);
  }

  UINT window_dpi = 96;
  if (window) {
    if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
      using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
      const auto get_dpi_for_window = reinterpret_cast<GetDpiForWindowFn>(
          GetProcAddress(user32, "GetDpiForWindow"));
      if (get_dpi_for_window) {
        window_dpi = get_dpi_for_window(window);
      }
    }
  }

  AppendDeathtrapSupportLog(
      "support_mouse_summary state_calls=%llu data_calls=%llu failures=%llu "
      "last_failure=0x%08lx "
      "delta=%lld,%lld camera=%u controller_cursor=%u foreground=%u "
      "cursor=%ld,%ld visible=%u "
      "edge=screen_%u%u_client_%u%u_clip_%u%u "
      "clip_client_mismatch=%u "
      "window=%ld,%ld,%ld,%ld client=%ld,%ld,%ld,%ld "
      "clip=%ld,%ld,%ld,%ld dpi=%u coop=0x%08lx",
      static_cast<unsigned long long>(g_support_mouse_state_calls.exchange(
          0, std::memory_order_acq_rel)),
      static_cast<unsigned long long>(g_support_mouse_data_calls.exchange(
          0, std::memory_order_acq_rel)),
      static_cast<unsigned long long>(g_support_mouse_failures.exchange(
          0, std::memory_order_acq_rel)),
      static_cast<unsigned long>(
          g_support_mouse_last_failure.load(std::memory_order_acquire)),
      static_cast<long long>(g_support_mouse_delta_x.exchange(
          0, std::memory_order_acq_rel)),
      static_cast<long long>(g_support_mouse_delta_y.exchange(
          0, std::memory_order_acq_rel)),
      camera_consumes ? 1u : 0u,
      ControllerCursorAxesOwnInput() ? 1u : 0u,
      window && GetForegroundWindow() == window ? 1u : 0u,
      have_cursor ? cursor.x : -1L, have_cursor ? cursor.y : -1L,
      cursor_visible ? 1u : 0u, screen_right ? 1u : 0u,
      screen_bottom ? 1u : 0u, client_right ? 1u : 0u,
      client_bottom ? 1u : 0u, clip_right ? 1u : 0u,
      clip_bottom ? 1u : 0u, clip_client_mismatch ? 1u : 0u,
      have_window_rect ? window_rect.left : -1L,
      have_window_rect ? window_rect.top : -1L,
      have_window_rect ? window_rect.right : -1L,
      have_window_rect ? window_rect.bottom : -1L,
      have_client_rect ? client_rect.left : -1L,
      have_client_rect ? client_rect.top : -1L,
      have_client_rect ? client_rect.right : -1L,
      have_client_rect ? client_rect.bottom : -1L,
      have_clip ? clip.left : -1L, have_clip ? clip.top : -1L,
      have_clip ? clip.right : -1L, have_clip ? clip.bottom : -1L,
      window_dpi,
      static_cast<unsigned long>(
          g_support_mouse_cooperative_flags.load(std::memory_order_acquire)));
}

template <typename T>
bool PatchVtableSlot(void** vtable, size_t index, void* replacement,
                     T* original) {
  if (!vtable || !replacement || !original) {
    return false;
  }
  void** slot = &vtable[index];
  if (*slot == replacement) {
    return true;
  }
  DWORD old_protection = 0;
  if (!VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE,
                      &old_protection)) {
    return false;
  }
  void* previous = *slot;
  *slot = replacement;
  FlushInstructionCache(GetCurrentProcess(), slot, sizeof(void*));
  DWORD ignored = 0;
  VirtualProtect(slot, sizeof(void*), old_protection, &ignored);
  if (!*original) {
    *original = reinterpret_cast<T>(previous);
  }
  return true;
}

HRESULT STDMETHODCALLTYPE HookDirectInputDeviceGetState(
    IDirectInputDeviceA* device, DWORD data_size, LPVOID data) {
  PollDeathtrapFrontendXInput();
  const HRESULT result =
      g_direct_input_device_get_state
          ? g_direct_input_device_get_state(device, data_size, data)
          : DIERR_GENERIC;
  if (SUCCEEDED(result) && data && data_size == 256u) {
    auto* keyboard = static_cast<uint8_t*>(data);
    const bool operate_down = (keyboard[DIK_E] & 0x80u) != 0u;
    const bool was_down = g_physical_operate_key_down.exchange(
        operate_down, std::memory_order_acq_rel);
    if (operate_down && !was_down) {
      NotifyDeathtrapOperateInput();
    }
    if (DeathtrapImmersiveFirstPersonActive() &&
        DeathtrapImmersiveVectorLocomotionActive()) {
      const bool forward = (keyboard[DIK_W] & 0x80u) != 0u;
      const bool backward = (keyboard[DIK_S] & 0x80u) != 0u;
      const bool left = (keyboard[DIK_A] & 0x80u) != 0u;
      const bool right = (keyboard[DIK_D] & 0x80u) != 0u;
      const int32_t lateral = static_cast<int32_t>(right) -
          static_cast<int32_t>(left);
      const int32_t longitudinal = static_cast<int32_t>(forward) -
          static_cast<int32_t>(backward);
      SubmitDeathtrapImmersiveKeyboardMovement(lateral, longitudinal);
      keyboard[DIK_A] &= static_cast<uint8_t>(~0x80u);
      keyboard[DIK_D] &= static_cast<uint8_t>(~0x80u);
      // Dungeon's retail side-step states are mutually exclusive with W/S,
      // so they cannot represent a diagonal. Any purely lateral request uses
      // the ordinary forward state as its native root-motion driver; the three
      // verified player root-motion callsites rotate its local displacement
      // into the requested world direction before native collision runs.
      if (lateral != 0 && longitudinal == 0) {
        keyboard[DIK_W] |= 0x80u;
      }
      keyboard[DIK_J] &= static_cast<uint8_t>(~0x80u);
      keyboard[DIK_K] &= static_cast<uint8_t>(~0x80u);
    } else {
      SubmitDeathtrapImmersiveKeyboardMovement(0, 0);
    }
  }
  // Deathtrap uses the standard relative mouse state. Preserve the physical
  // mouse, then merge the bounded controller pointer state used by menus.
  // Exact-size checks also exclude keyboard and joystick devices if the
  // DirectInput implementation shares vtables.
  if (SUCCEEDED(result) && data &&
      (data_size == sizeof(DIMOUSESTATE) ||
       data_size == sizeof(DIMOUSESTATE2))) {
    g_support_mouse_state_calls.fetch_add(1, std::memory_order_relaxed);
    auto* mouse = static_cast<DIMOUSESTATE*>(data);
    AccumulateSupportMouseDelta(mouse->lX, mouse->lY);
    NotePhysicalCursorActivity(mouse->lX, mouse->lY);
    const bool camera_consumes = DeathtrapModernCameraConsumesMouse();
    if (camera_consumes) {
      SubmitDeathtrapPhysicalMouseDelta(mouse->lX, mouse->lY);
      // Camera-look owns only the physical axes during gameplay. Buttons and
      // wheel remain native, while frontend/menu samples bypass this branch.
      mouse->lX = 0;
      mouse->lY = 0;
    }
    const bool controller_cursor = ControllerCursorAxesOwnInput();
    const int32_t controller_x = g_xinput_mouse_delta_x.exchange(
        0, std::memory_order_acq_rel);
    const int32_t controller_y = g_xinput_mouse_delta_y.exchange(
        0, std::memory_order_acq_rel);
    if (controller_cursor) {
      mouse->lX += controller_x;
      mouse->lY += controller_y;
    }
    const uint8_t injected_buttons =
        g_xinput_mouse_buttons.load(std::memory_order_acquire);
    if (injected_buttons & 1u) {
      mouse->rgbButtons[0] |= 0x80u;
    }
    if (injected_buttons & 2u) {
      mouse->rgbButtons[1] |= 0x80u;
    }
    if (mouse->lZ != 0) {
      QueueDeathtrapWeaponWheelDelta(mouse->lZ);
    }
    MaybeLogMouseSupportSummary(camera_consumes);
  } else if (FAILED(result) &&
             device == g_support_mouse_device.load(std::memory_order_acquire)) {
    g_support_mouse_state_calls.fetch_add(1, std::memory_order_relaxed);
    g_support_mouse_failures.fetch_add(1, std::memory_order_relaxed);
    g_support_mouse_last_failure.store(result, std::memory_order_release);
    MaybeLogMouseSupportSummary(DeathtrapModernCameraConsumesMouse());
  }
  return result;
}

HRESULT STDMETHODCALLTYPE HookDirectInputDeviceGetData(
    IDirectInputDeviceA* device, DWORD object_size,
    LPDIDEVICEOBJECTDATA data, LPDWORD count, DWORD flags) {
  PollDeathtrapFrontendXInput();
  const DWORD capacity = count ? *count : 0u;
  const HRESULT result =
      g_direct_input_device_get_data
          ? g_direct_input_device_get_data(device, object_size, data, count,
                                           flags)
          : DIERR_GENERIC;
  if (FAILED(result) || !count || !data || object_size == 0u) {
    if (device ==
        g_support_mouse_device.load(std::memory_order_acquire)) {
      g_support_mouse_data_calls.fetch_add(1, std::memory_order_relaxed);
      if (FAILED(result)) {
        g_support_mouse_failures.fetch_add(1, std::memory_order_relaxed);
        g_support_mouse_last_failure.store(result, std::memory_order_release);
      }
      MaybeLogMouseSupportSummary(DeathtrapModernCameraConsumesMouse());
    }
    return result;
  }

  g_support_mouse_data_calls.fetch_add(1, std::memory_order_relaxed);

  DWORD written = *count;
  const bool peek = (flags & DIGDD_PEEK) != 0;
  int32_t observed_physical_x = 0;
  int32_t observed_physical_y = 0;
  if (object_size >= sizeof(DIDEVICEOBJECTDATA)) {
    for (DWORD index = 0; index < written; ++index) {
      const auto* source_bytes = reinterpret_cast<const uint8_t*>(data) +
                                 static_cast<size_t>(index) * object_size;
      DIDEVICEOBJECTDATA event = {};
      std::memcpy(&event, source_bytes, sizeof(event));
      if (event.dwOfs == DIMOFS_X) {
        observed_physical_x = std::clamp(
            observed_physical_x + static_cast<int32_t>(event.dwData),
            -8192, 8192);
      } else if (event.dwOfs == DIMOFS_Y) {
        observed_physical_y = std::clamp(
            observed_physical_y + static_cast<int32_t>(event.dwData),
            -8192, 8192);
      }
    }
    if (!peek) {
      AccumulateSupportMouseDelta(observed_physical_x, observed_physical_y);
      NotePhysicalCursorActivity(observed_physical_x, observed_physical_y);
    }
  }
  if (DeathtrapModernCameraConsumesMouse() &&
      object_size >= sizeof(DIDEVICEOBJECTDATA)) {
    // Some Deathtrap input paths use buffered DirectInput rather than
    // GetDeviceState.  Let buttons and the wheel pass through, but take
    // exclusive ownership of physical X/Y motion while modern camera-look is
    // active.  Without this second interception the same mouse movement also
    // reached the retail movement bindings and rotated/stepped Lara.
    DWORD kept = 0;
    int32_t physical_x = 0;
    int32_t physical_y = 0;
    for (DWORD index = 0; index < written; ++index) {
      auto* source_bytes = reinterpret_cast<uint8_t*>(data) +
                           static_cast<size_t>(index) * object_size;
      DIDEVICEOBJECTDATA event = {};
      std::memcpy(&event, source_bytes, sizeof(event));
      if (event.dwOfs == DIMOFS_X || event.dwOfs == DIMOFS_Y) {
        if (!peek) {
          const int32_t delta = static_cast<int32_t>(event.dwData);
          if (event.dwOfs == DIMOFS_X) {
            physical_x = std::clamp(physical_x + delta, -8192, 8192);
          } else {
            physical_y = std::clamp(physical_y + delta, -8192, 8192);
          }
        }
        continue;
      }
      if (kept != index) {
        auto* destination = reinterpret_cast<uint8_t*>(data) +
                            static_cast<size_t>(kept) * object_size;
        std::memmove(destination, source_bytes, object_size);
      }
      ++kept;
    }
    written = kept;
    if (!peek && (physical_x != 0 || physical_y != 0)) {
      SubmitDeathtrapPhysicalMouseDelta(physical_x, physical_y);
    }
  }
  auto append = [&](DWORD offset, DWORD value) {
    if (written >= capacity) {
      return false;
    }
    DIDEVICEOBJECTDATA event = {};
    event.dwOfs = offset;
    event.dwData = value;
    event.dwTimeStamp = GetTickCount();
    event.dwSequence =
        g_xinput_buffered_mouse_sequence.fetch_add(1,
                                                    std::memory_order_relaxed);
    auto* destination = reinterpret_cast<uint8_t*>(data) +
                        static_cast<size_t>(written) * object_size;
    std::memset(destination, 0, object_size);
    std::memcpy(destination, &event,
                std::min<size_t>(object_size, sizeof(event)));
    ++written;
    return true;
  };

  const bool controller_cursor = ControllerCursorAxesOwnInput();
  int32_t delta_x = controller_cursor
      ? g_xinput_buffered_mouse_delta_x.load(std::memory_order_acquire)
      : 0;
  if (delta_x != 0 && append(DIMOFS_X, static_cast<DWORD>(delta_x)) && !peek) {
    g_xinput_buffered_mouse_delta_x.store(0, std::memory_order_release);
  }
  int32_t delta_y = controller_cursor
      ? g_xinput_buffered_mouse_delta_y.load(std::memory_order_acquire)
      : 0;
  if (delta_y != 0 && append(DIMOFS_Y, static_cast<DWORD>(delta_y)) && !peek) {
    g_xinput_buffered_mouse_delta_y.store(0, std::memory_order_release);
  }
  const uint8_t desired_buttons =
      g_xinput_buffered_mouse_buttons.load(std::memory_order_acquire);
  const uint8_t delivered_buttons =
      g_xinput_buffered_mouse_buttons_delivered.load(
          std::memory_order_acquire);
  if ((desired_buttons ^ delivered_buttons) & 1u) {
    if (append(DIMOFS_BUTTON0, (desired_buttons & 1u) ? 0x80u : 0u) &&
        !peek) {
      g_xinput_buffered_mouse_buttons_delivered.store(
          static_cast<uint8_t>((delivered_buttons & ~1u) |
                               (desired_buttons & 1u)),
          std::memory_order_release);
    }
  }
  *count = written;
  MaybeLogMouseSupportSummary(DeathtrapModernCameraConsumesMouse());
  return result;
}

HRESULT STDMETHODCALLTYPE HookDirectInputDeviceAcquire(
    IDirectInputDeviceA* device) {
  const HRESULT result = g_direct_input_device_acquire
      ? g_direct_input_device_acquire(device)
      : DIERR_GENERIC;
  if (device == g_support_mouse_device.load(std::memory_order_acquire)) {
    AppendDeathtrapSupportLog("support_mouse_acquire result=0x%08lx",
                              static_cast<unsigned long>(result));
  }
  return result;
}

HRESULT STDMETHODCALLTYPE HookDirectInputDeviceUnacquire(
    IDirectInputDeviceA* device) {
  const HRESULT result = g_direct_input_device_unacquire
      ? g_direct_input_device_unacquire(device)
      : DIERR_GENERIC;
  if (device == g_support_mouse_device.load(std::memory_order_acquire)) {
    AppendDeathtrapSupportLog("support_mouse_unacquire result=0x%08lx",
                              static_cast<unsigned long>(result));
  }
  return result;
}

HRESULT STDMETHODCALLTYPE HookDirectInputDeviceSetDataFormat(
    IDirectInputDeviceA* device, LPCDIDATAFORMAT format) {
  const HRESULT result = g_direct_input_device_set_data_format
      ? g_direct_input_device_set_data_format(device, format)
      : DIERR_GENERIC;
  if (device == g_support_mouse_device.load(std::memory_order_acquire)) {
    AppendDeathtrapSupportLog(
        "support_mouse_data_format result=0x%08lx size=%lu objects=%lu "
        "flags=0x%08lx",
        static_cast<unsigned long>(result),
        format ? static_cast<unsigned long>(format->dwDataSize) : 0ul,
        format ? static_cast<unsigned long>(format->dwNumObjs) : 0ul,
        format ? static_cast<unsigned long>(format->dwFlags) : 0ul);
  }
  return result;
}

HRESULT STDMETHODCALLTYPE HookDirectInputDeviceSetCooperativeLevel(
    IDirectInputDeviceA* device, HWND window, DWORD flags) {
  const HRESULT result = g_direct_input_device_set_cooperative_level
      ? g_direct_input_device_set_cooperative_level(device, window, flags)
      : DIERR_GENERIC;
  if (device == g_support_mouse_device.load(std::memory_order_acquire)) {
    g_support_mouse_window.store(window, std::memory_order_release);
    g_support_mouse_cooperative_flags.store(flags, std::memory_order_release);
    AppendDeathtrapSupportLog(
        "support_mouse_cooperative result=0x%08lx flags=0x%08lx "
        "exclusive=%u foreground=%u window_valid=%u",
        static_cast<unsigned long>(result), static_cast<unsigned long>(flags),
        (flags & DISCL_EXCLUSIVE) != 0u ? 1u : 0u,
        (flags & DISCL_FOREGROUND) != 0u ? 1u : 0u,
        IsCurrentProcessWindow(window) ? 1u : 0u);
  }
  return result;
}

void SubmitDeathtrapXInputMouseStateInternal(int32_t delta_x, int32_t delta_y,
                                             bool left_button,
                                             bool right_button) {
  // This is a relative state for one sample, not a FIFO. Replacing the pending
  // value avoids a huge cursor jump if a loading screen temporarily stops
  // polling the DirectInput mouse.
  if (delta_x != 0 || delta_y != 0) {
    g_last_controller_cursor_activity_ms.store(GetTickCount64(),
                                                std::memory_order_release);
  }
  g_xinput_mouse_delta_x.store(delta_x, std::memory_order_release);
  g_xinput_mouse_delta_y.store(delta_y, std::memory_order_release);
  g_xinput_mouse_buttons.store(
      static_cast<uint8_t>((left_button ? 1u : 0u) |
                           (right_button ? 2u : 0u)),
      std::memory_order_release);
  g_xinput_buffered_mouse_delta_x.store(delta_x, std::memory_order_release);
  g_xinput_buffered_mouse_delta_y.store(delta_y, std::memory_order_release);
  g_xinput_buffered_mouse_buttons.store(
      static_cast<uint8_t>((left_button ? 1u : 0u) |
                           (right_button ? 2u : 0u)),
      std::memory_order_release);
}

HRESULT STDMETHODCALLTYPE HookDirectInputCreateDeviceA(
    IDirectInputA* direct_input, REFGUID device_guid,
    LPDIRECTINPUTDEVICEA* device, LPUNKNOWN outer) {
  const HRESULT result =
      g_direct_input_create_device
          ? g_direct_input_create_device(direct_input, device_guid, device,
                                         outer)
          : DIERR_GENERIC;
  if (SUCCEEDED(result) && device && *device &&
      IsEqualGUID(device_guid, GUID_SysMouse)) {
    g_support_mouse_device.store(*device, std::memory_order_release);
    void** vtable = *reinterpret_cast<void***>(*device);
    const bool acquire_hooked = PatchVtableSlot(
        vtable, 7, reinterpret_cast<void*>(&HookDirectInputDeviceAcquire),
        &g_direct_input_device_acquire);
    const bool unacquire_hooked = PatchVtableSlot(
        vtable, 8, reinterpret_cast<void*>(&HookDirectInputDeviceUnacquire),
        &g_direct_input_device_unacquire);
    PatchVtableSlot(vtable, 9,
                    reinterpret_cast<void*>(&HookDirectInputDeviceGetState),
                    &g_direct_input_device_get_state);
    PatchVtableSlot(vtable, 10,
                    reinterpret_cast<void*>(&HookDirectInputDeviceGetData),
                    &g_direct_input_device_get_data);
    const bool data_format_hooked = PatchVtableSlot(
        vtable, 11,
        reinterpret_cast<void*>(&HookDirectInputDeviceSetDataFormat),
        &g_direct_input_device_set_data_format);
    const bool cooperative_hooked = PatchVtableSlot(
        vtable, 13,
        reinterpret_cast<void*>(&HookDirectInputDeviceSetCooperativeLevel),
        &g_direct_input_device_set_cooperative_level);
    AppendDeathtrapSupportLog(
        "support_mouse_device result=0x%08lx hooks=%u%u%u%u%u%u",
        static_cast<unsigned long>(result), acquire_hooked ? 1u : 0u,
        unacquire_hooked ? 1u : 0u,
        g_direct_input_device_get_state ? 1u : 0u,
        g_direct_input_device_get_data ? 1u : 0u,
        data_format_hooked ? 1u : 0u, cooperative_hooked ? 1u : 0u);
  }
  return result;
}

void AttachDirectInput(IDirectInputA* direct_input) {
  if (!direct_input) {
    return;
  }
  void** vtable = *reinterpret_cast<void***>(direct_input);
  PatchVtableSlot(vtable, 3,
                  reinterpret_cast<void*>(&HookDirectInputCreateDeviceA),
                  &g_direct_input_create_device);
}

void AttachSwapChain(IDXGISwapChain* swap_chain, IUnknown* creation_device);

SwapChainPresentFn OriginalPresent(IDXGISwapChain* swap_chain) {
  std::lock_guard<std::mutex> lock(g_swap_chain_hooks_mutex);
  const auto found = g_swap_chain_present.find(swap_chain);
  return found != g_swap_chain_present.end() ? found->second : nullptr;
}

SwapChainPresent1Fn OriginalPresent1(IDXGISwapChain1* swap_chain) {
  std::lock_guard<std::mutex> lock(g_swap_chain_hooks_mutex);
  const auto found = g_swap_chain_present1.find(swap_chain);
  return found != g_swap_chain_present1.end() ? found->second : nullptr;
}

HRESULT STDMETHODCALLTYPE HookPresent(IDXGISwapChain* swap_chain,
                                      UINT sync_interval, UINT flags) {
  if (g_suppress_page_restore) {
    g_suppressed_page_restores.fetch_add(1, std::memory_order_relaxed);
    return S_OK;
  }
  const bool outer = !g_inside_present;
  bool allowed = true;
  if (outer) {
    g_inside_present = true;
    allowed = AllowNativeD3D11Present(swap_chain);
  }
  const SwapChainPresentFn original = OriginalPresent(swap_chain);
  const HRESULT result =
      !allowed ? S_OK
               : (original ? original(swap_chain, sync_interval, flags)
                           : E_FAIL);
  if (outer) {
    g_inside_present = false;
  }
  return result;
}

HRESULT STDMETHODCALLTYPE HookPresent1(
    IDXGISwapChain1* swap_chain, UINT sync_interval, UINT flags,
    const DXGI_PRESENT_PARAMETERS* parameters) {
  if (g_suppress_page_restore) {
    g_suppressed_page_restores.fetch_add(1, std::memory_order_relaxed);
    return S_OK;
  }
  const bool outer = !g_inside_present;
  bool allowed = true;
  if (outer) {
    g_inside_present = true;
    allowed = AllowNativeD3D11Present(swap_chain);
  }
  const SwapChainPresent1Fn original = OriginalPresent1(swap_chain);
  const HRESULT result =
      !allowed ? S_OK
               : (original ? original(swap_chain, sync_interval, flags,
                                      parameters)
                           : E_FAIL);
  if (outer) {
    g_inside_present = false;
  }
  return result;
}

size_t SwapChainVtableSize(IDXGISwapChain* swap_chain) {
  IDXGISwapChain4* swap_chain4 = nullptr;
  if (SUCCEEDED(swap_chain->QueryInterface(IID_PPV_ARGS(&swap_chain4)))) {
    swap_chain4->Release();
    return 41u;
  }
  IDXGISwapChain3* swap_chain3 = nullptr;
  if (SUCCEEDED(swap_chain->QueryInterface(IID_PPV_ARGS(&swap_chain3)))) {
    swap_chain3->Release();
    return 40u;
  }
  IDXGISwapChain2* swap_chain2 = nullptr;
  if (SUCCEEDED(swap_chain->QueryInterface(IID_PPV_ARGS(&swap_chain2)))) {
    swap_chain2->Release();
    return 36u;
  }
  IDXGISwapChain1* swap_chain1 = nullptr;
  if (SUCCEEDED(swap_chain->QueryInterface(IID_PPV_ARGS(&swap_chain1)))) {
    swap_chain1->Release();
    return 29u;
  }
  return 18u;
}

bool CloneAndPatchSwapChainVtable(void* interface_pointer,
                                  size_t vtable_size, bool patch_present,
                                  bool patch_present1) {
  if (!interface_pointer || vtable_size < 18u ||
      (patch_present1 && vtable_size <= 22u)) {
    return false;
  }
  std::lock_guard<std::mutex> lock(g_swap_chain_hooks_mutex);
  auto*** const object = reinterpret_cast<void***>(interface_pointer);
  void** const current = *object;
  if (!current) {
    return false;
  }

  const bool present_already_patched =
      !patch_present || current[8] == reinterpret_cast<void*>(&HookPresent);
  const bool present1_already_patched =
      !patch_present1 ||
      current[22] == reinterpret_cast<void*>(&HookPresent1);
  if (present_already_patched && present1_already_patched) {
    return true;
  }

  const size_t bytes = vtable_size * sizeof(void*);
  auto** const clone = static_cast<void**>(VirtualAlloc(
      nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
  if (!clone) {
    return false;
  }
  std::memcpy(clone, current, bytes);

  if (patch_present && !present_already_patched) {
    const auto original = reinterpret_cast<SwapChainPresentFn>(current[8]);
    if (!original || original == &HookPresent) {
      VirtualFree(clone, 0, MEM_RELEASE);
      return false;
    }
    g_swap_chain_present[interface_pointer] = original;
    clone[8] = reinterpret_cast<void*>(&HookPresent);
  }
  if (patch_present1 && !present1_already_patched) {
    const auto original = reinterpret_cast<SwapChainPresent1Fn>(current[22]);
    if (!original || original == &HookPresent1) {
      VirtualFree(clone, 0, MEM_RELEASE);
      return false;
    }
    g_swap_chain_present1[interface_pointer] = original;
    clone[22] = reinterpret_cast<void*>(&HookPresent1);
  }

  // DXGI implementations share their class vtables. Steam's overlay patches
  // that shared table as each swap chain is created. Patching it in place made
  // Steam save our hook as its original while we saved Steam's hook as ours,
  // producing an immediate Present -> Present recursion on the next chain.
  // Give this COM instance an immutable private table instead, so both hook
  // layers retain one stable downstream target.
  InterlockedExchangePointer(reinterpret_cast<PVOID volatile*>(object), clone);
  return true;
}

void AttachSwapChain(IDXGISwapChain* swap_chain, IUnknown* creation_device) {
  if (!swap_chain) {
    return;
  }
  IDXGISwapChain1* swap_chain1 = nullptr;
  const bool has_swap_chain1 =
      SUCCEEDED(swap_chain->QueryInterface(IID_PPV_ARGS(&swap_chain1)));
  const size_t vtable_size = SwapChainVtableSize(swap_chain);
  if (has_swap_chain1 && swap_chain1 == swap_chain) {
    CloneAndPatchSwapChainVtable(swap_chain, vtable_size, true, true);
  } else {
    CloneAndPatchSwapChainVtable(swap_chain, vtable_size, true, false);
    if (has_swap_chain1) {
      CloneAndPatchSwapChainVtable(swap_chain1, vtable_size, false, true);
    }
  }
  if (has_swap_chain1) {
    swap_chain1->Release();
  }
  AttachNativeD3D11PresentGuard(swap_chain, creation_device);
}

HRESULT STDMETHODCALLTYPE HookCreateSwapChain(
    IDXGIFactory* factory, IUnknown* device, DXGI_SWAP_CHAIN_DESC* desc,
    IDXGISwapChain** swap_chain) {
  const HRESULT result =
      g_factory_create_swap_chain
          ? g_factory_create_swap_chain(factory, device, desc, swap_chain)
          : E_FAIL;
  if (SUCCEEDED(result) && swap_chain && *swap_chain) {
    AttachSwapChain(*swap_chain, device);
  }
  return result;
}

HRESULT STDMETHODCALLTYPE HookCreateSwapChainForHwnd(
    IDXGIFactory2* factory, IUnknown* device, HWND window,
    const DXGI_SWAP_CHAIN_DESC1* desc,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreen_desc,
    IDXGIOutput* output, IDXGISwapChain1** swap_chain) {
  const HRESULT result =
      g_factory_create_swap_chain_for_hwnd
          ? g_factory_create_swap_chain_for_hwnd(
                factory, device, window, desc, fullscreen_desc, output,
                swap_chain)
          : E_FAIL;
  if (SUCCEEDED(result) && swap_chain && *swap_chain) {
    AttachSwapChain(*swap_chain, device);
  }
  return result;
}

HRESULT STDMETHODCALLTYPE HookCreateSwapChainForCoreWindow(
    IDXGIFactory2* factory, IUnknown* device, IUnknown* window,
    const DXGI_SWAP_CHAIN_DESC1* desc, IDXGIOutput* output,
    IDXGISwapChain1** swap_chain) {
  const HRESULT result =
      g_factory_create_swap_chain_for_core_window
          ? g_factory_create_swap_chain_for_core_window(
                factory, device, window, desc, output, swap_chain)
          : E_FAIL;
  if (SUCCEEDED(result) && swap_chain && *swap_chain) {
    AttachSwapChain(*swap_chain, device);
  }
  return result;
}

HRESULT STDMETHODCALLTYPE HookCreateSwapChainForComposition(
    IDXGIFactory2* factory, IUnknown* device,
    const DXGI_SWAP_CHAIN_DESC1* desc, IDXGIOutput* output,
    IDXGISwapChain1** swap_chain) {
  const HRESULT result =
      g_factory_create_swap_chain_for_composition
          ? g_factory_create_swap_chain_for_composition(
                factory, device, desc, output, swap_chain)
          : E_FAIL;
  if (SUCCEEDED(result) && swap_chain && *swap_chain) {
    AttachSwapChain(*swap_chain, device);
  }
  return result;
}

void PatchFactory(IUnknown* unknown) {
  if (!unknown) {
    return;
  }
  IDXGIFactory* factory = nullptr;
  if (SUCCEEDED(unknown->QueryInterface(IID_PPV_ARGS(&factory)))) {
    PatchVtableSlot(*reinterpret_cast<void***>(factory), 10,
                    reinterpret_cast<void*>(&HookCreateSwapChain),
                    &g_factory_create_swap_chain);
    factory->Release();
  }
  IDXGIFactory2* factory2 = nullptr;
  if (SUCCEEDED(unknown->QueryInterface(IID_PPV_ARGS(&factory2)))) {
    void** vtable = *reinterpret_cast<void***>(factory2);
    PatchVtableSlot(vtable, 15,
                    reinterpret_cast<void*>(&HookCreateSwapChainForHwnd),
                    &g_factory_create_swap_chain_for_hwnd);
    PatchVtableSlot(
        vtable, 16,
        reinterpret_cast<void*>(&HookCreateSwapChainForCoreWindow),
        &g_factory_create_swap_chain_for_core_window);
    PatchVtableSlot(
        vtable, 24,
        reinterpret_cast<void*>(&HookCreateSwapChainForComposition),
        &g_factory_create_swap_chain_for_composition);
    factory2->Release();
  }
}

HRESULT WINAPI HookCreateDXGIFactory(REFIID iid, void** factory) {
  const HRESULT result =
      g_create_factory ? g_create_factory(iid, factory) : E_FAIL;
  if (SUCCEEDED(result) && factory && *factory) {
    PatchFactory(static_cast<IUnknown*>(*factory));
  }
  return result;
}

HRESULT WINAPI HookCreateDXGIFactory1(REFIID iid, void** factory) {
  const HRESULT result =
      g_create_factory1 ? g_create_factory1(iid, factory) : E_FAIL;
  if (SUCCEEDED(result) && factory && *factory) {
    PatchFactory(static_cast<IUnknown*>(*factory));
  }
  return result;
}

HRESULT WINAPI HookCreateDXGIFactory2(UINT flags, REFIID iid, void** factory) {
  const HRESULT result =
      g_create_factory2 ? g_create_factory2(flags, iid, factory) : E_FAIL;
  if (SUCCEEDED(result) && factory && *factory) {
    PatchFactory(static_cast<IUnknown*>(*factory));
  }
  return result;
}

template <typename T>
bool HookDxgiExport(const char* name, void* replacement, T* original) {
  FARPROC target = GetProcAddress(g_dxgi, name);
  if (!target) {
    return false;
  }
  const MH_STATUS created =
      MH_CreateHook(reinterpret_cast<void*>(target), replacement,
                    reinterpret_cast<void**>(original));
  if (created != MH_OK && created != MH_ERROR_ALREADY_CREATED) {
    return false;
  }
  const MH_STATUS enabled = MH_EnableHook(reinterpret_cast<void*>(target));
  return enabled == MH_OK || enabled == MH_ERROR_ENABLED;
}

bool InstallDxgiHooks() {
  g_dxgi = LoadLibraryW(L"dxgi.dll");
  if (!g_dxgi) {
    return false;
  }
  const MH_STATUS initialized = MH_Initialize();
  if (initialized != MH_OK && initialized != MH_ERROR_ALREADY_INITIALIZED) {
    return false;
  }
  bool installed = false;
  installed |= HookDxgiExport(
      "CreateDXGIFactory", reinterpret_cast<void*>(&HookCreateDXGIFactory),
      &g_create_factory);
  installed |= HookDxgiExport(
      "CreateDXGIFactory1", reinterpret_cast<void*>(&HookCreateDXGIFactory1),
      &g_create_factory1);
  installed |= HookDxgiExport(
      "CreateDXGIFactory2", reinterpret_cast<void*>(&HookCreateDXGIFactory2),
      &g_create_factory2);
  return installed;
}

DWORD WINAPI FrontendInputThread(void*) {
  while (!g_stop_frontend_input.load(std::memory_order_acquire)) {
    PollDeathtrapFrontendXInput();
    Sleep(8);
  }
  return 0;
}

DWORD WINAPI InitializeThread(void*) {
  std::call_once(g_initialize_once, [] {
    LoadSystemDinput();
    InitializeDeathtrapNativeRenderPatch();
    InstallDxgiHooks();
    InstallDeathtrapNativeRenderHooks();
    HANDLE frontend_thread = CreateThread(
        nullptr, 0, &FrontendInputThread, nullptr, 0, nullptr);
    if (frontend_thread) {
      CloseHandle(frontend_thread);
    }
  });
  return 0;
}

}  // namespace

void SubmitDeathtrapXInputMouseState(int32_t delta_x, int32_t delta_y,
                                     bool left_button, bool right_button) {
  SubmitDeathtrapXInputMouseStateInternal(delta_x, delta_y, left_button,
                                          right_button);
}

void SetDeathtrapNativePageRestorePresentSuppressed(bool suppressed) {
  g_suppress_page_restore = suppressed;
}

uint64_t GetDeathtrapNativeSuppressedPresentCount() {
  return g_suppressed_page_restores.load(std::memory_order_relaxed);
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(instance);
    HANDLE thread = CreateThread(nullptr, 0, &InitializeThread, nullptr, 0,
                                 nullptr);
    if (thread) {
      CloseHandle(thread);
    }
  } else if (reason == DLL_PROCESS_DETACH) {
    g_stop_frontend_input.store(true, std::memory_order_release);
  }
  return TRUE;
}

#if !defined(_M_IX86)
#error Deathtrap's legacy dinput proxy must be built for x86.
#endif

#define DINPUT_JUMP_STUB(name)                         \
  extern "C" __declspec(naked) void Proxy_##name() { \
    __asm { jmp dword ptr[g_target_##name] }           \
  }

DINPUT_JUMP_STUB(DirectInputCreateW)
DINPUT_JUMP_STUB(DirectInputCreateEx)
DINPUT_JUMP_STUB(DllCanUnloadNow)
DINPUT_JUMP_STUB(DllGetClassObject)
DINPUT_JUMP_STUB(DllRegisterServer)
DINPUT_JUMP_STUB(DllUnregisterServer)

#undef DINPUT_JUMP_STUB

extern "C" HRESULT WINAPI Proxy_DirectInputCreateA(
    HINSTANCE instance, DWORD version, LPDIRECTINPUTA* direct_input,
    LPUNKNOWN outer) {
  InitializeThread(nullptr);
  using DirectInputCreateAFn = HRESULT(WINAPI*)(
      HINSTANCE, DWORD, LPDIRECTINPUTA*, LPUNKNOWN);
  const auto target = reinterpret_cast<DirectInputCreateAFn>(
      g_target_DirectInputCreateA);
  const HRESULT result =
      target ? target(instance, version, direct_input, outer) : DIERR_GENERIC;
  AppendDeathtrapSupportLog(
      "support_directinput_create version=0x%08lx system_dinput=%u "
      "result=0x%08lx object=%u steam=%u",
      static_cast<unsigned long>(version), target ? 1u : 0u,
      static_cast<unsigned long>(result),
      SUCCEEDED(result) && direct_input && *direct_input ? 1u : 0u,
      GetModuleHandleW(L"gameoverlayrenderer.dll") ? 1u : 0u);
  if (SUCCEEDED(result) && direct_input && *direct_input) {
    AttachDirectInput(*direct_input);
  }
  return result;
}
