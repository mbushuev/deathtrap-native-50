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
SwapChainPresentFn g_present = nullptr;
SwapChainPresent1Fn g_present1 = nullptr;
DirectInputCreateDeviceAFn g_direct_input_create_device = nullptr;
DirectInputDeviceGetStateFn g_direct_input_device_get_state = nullptr;
DirectInputDeviceGetDataFn g_direct_input_device_get_data = nullptr;
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
  // Deathtrap uses the standard relative mouse state. Preserve the physical
  // mouse, then merge the bounded controller pointer state used by menus.
  // Exact-size checks also exclude keyboard and joystick devices if the
  // DirectInput implementation shares vtables.
  if (SUCCEEDED(result) && data &&
      (data_size == sizeof(DIMOUSESTATE) ||
       data_size == sizeof(DIMOUSESTATE2))) {
    auto* mouse = static_cast<DIMOUSESTATE*>(data);
    if (DeathtrapModernCameraConsumesMouse()) {
      SubmitDeathtrapPhysicalMouseDelta(mouse->lX, mouse->lY);
      // Camera-look owns only the physical axes during gameplay. Buttons and
      // wheel remain native, while frontend/menu samples bypass this branch.
      mouse->lX = 0;
      mouse->lY = 0;
    }
    mouse->lX += g_xinput_mouse_delta_x.exchange(0,
                                                 std::memory_order_acq_rel);
    mouse->lY += g_xinput_mouse_delta_y.exchange(0,
                                                 std::memory_order_acq_rel);
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
    return result;
  }

  DWORD written = *count;
  const bool peek = (flags & DIGDD_PEEK) != 0;
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

  int32_t delta_x = g_xinput_buffered_mouse_delta_x.load(
      std::memory_order_acquire);
  if (delta_x != 0 && append(DIMOFS_X, static_cast<DWORD>(delta_x)) && !peek) {
    g_xinput_buffered_mouse_delta_x.store(0, std::memory_order_release);
  }
  int32_t delta_y = g_xinput_buffered_mouse_delta_y.load(
      std::memory_order_acquire);
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
  return result;
}

void SubmitDeathtrapXInputMouseStateInternal(int32_t delta_x, int32_t delta_y,
                                             bool left_button,
                                             bool right_button) {
  // This is a relative state for one sample, not a FIFO. Replacing the pending
  // value avoids a huge cursor jump if a loading screen temporarily stops
  // polling the DirectInput mouse.
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
    void** vtable = *reinterpret_cast<void***>(*device);
    PatchVtableSlot(vtable, 9,
                    reinterpret_cast<void*>(&HookDirectInputDeviceGetState),
                    &g_direct_input_device_get_state);
    PatchVtableSlot(vtable, 10,
                    reinterpret_cast<void*>(&HookDirectInputDeviceGetData),
                    &g_direct_input_device_get_data);
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
  const HRESULT result =
      !allowed ? S_OK
               : (g_present ? g_present(swap_chain, sync_interval, flags)
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
  const HRESULT result =
      !allowed ? S_OK
               : (g_present1 ? g_present1(swap_chain, sync_interval, flags,
                                          parameters)
                             : E_FAIL);
  if (outer) {
    g_inside_present = false;
  }
  return result;
}

void AttachSwapChain(IDXGISwapChain* swap_chain, IUnknown* creation_device) {
  if (!swap_chain) {
    return;
  }
  void** vtable = *reinterpret_cast<void***>(swap_chain);
  PatchVtableSlot(vtable, 8, reinterpret_cast<void*>(&HookPresent),
                  &g_present);
  IDXGISwapChain1* swap_chain1 = nullptr;
  if (SUCCEEDED(swap_chain->QueryInterface(IID_PPV_ARGS(&swap_chain1)))) {
    void** vtable1 = *reinterpret_cast<void***>(swap_chain1);
    PatchVtableSlot(vtable1, 22, reinterpret_cast<void*>(&HookPresent1),
                    &g_present1);
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
  if (SUCCEEDED(result) && direct_input && *direct_input) {
    AttachDirectInput(*direct_input);
  }
  return result;
}
