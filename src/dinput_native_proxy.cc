#include <windows.h>

#include <d3d11.h>
#include <dinput.h>
#include <dxgi1_6.h>
#include <MinHook.h>

#include <atomic>
#include <cstdint>
#include <mutex>

#include "deathtrap_native_render_patch.h"
#include "deathtrap_modern_mouse.h"
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

HMODULE g_system_dinput = nullptr;
HMODULE g_dxgi = nullptr;
std::once_flag g_initialize_once;
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
thread_local bool g_inside_present = false;
thread_local bool g_suppress_page_restore = false;
std::atomic<uint64_t> g_suppressed_page_restores{0};

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

DWORD WINAPI InitializeThread(void*) {
  std::call_once(g_initialize_once, [] {
    LoadSystemDinput();
    InitializeDeathtrapNativeRenderPatch();
    InitializeDeathtrapModernMouse();
    InstallDxgiHooks();
    InstallDeathtrapNativeRenderHooks();
    InstallDeathtrapModernMouseHook();
  });
  return 0;
}

}  // namespace

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
  return target ? target(instance, version, direct_input, outer)
                : DIERR_GENERIC;
}
