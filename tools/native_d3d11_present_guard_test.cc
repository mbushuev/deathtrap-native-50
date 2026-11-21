#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <wrl/implements.h>

#include <cstdio>
#include <cstdlib>

#include "deathtrap_native_render_patch.h"
#include "native_d3d11_present_guard.h"

namespace {

using Microsoft::WRL::ComPtr;
DeathtrapNativePresentationStage stage = DeathtrapNativePresentationStage::kExact;
unsigned destroyed_chains = 0;

struct LifetimeObserver final : Microsoft::WRL::RuntimeClass<
    Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IUnknown> {
  ~LifetimeObserver() { ++destroyed_chains; }
};

void Require(bool condition, const char* message) {
  if (!condition) {
    std::fprintf(stderr, "%s after %u swap chain releases\n", message, destroyed_chains);
    std::exit(EXIT_FAILURE);
  }
}

void CheckSwapChain(HWND window) {
  const GUID observer_key = {0x6bf04a48, 0x637e, 0x49f6,
                             {0xab, 0x01, 0xff, 0x90, 0xbc, 0xe1, 0x01, 0x99}};
  DXGI_SWAP_CHAIN_DESC desc{};
  desc.BufferDesc.Width = 128;
  desc.BufferDesc.Height = 128;
  desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  desc.BufferCount = 2;
  desc.OutputWindow = window;
  desc.Windowed = TRUE;
  ComPtr<IDXGISwapChain> swap_chain;
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  Require(SUCCEEDED(D3D11CreateDeviceAndSwapChain(
              nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
              D3D11_SDK_VERSION, &desc, &swap_chain, &device, nullptr, &context)),
          "Could not create WARP swap chain");
  auto observer = Microsoft::WRL::Make<LifetimeObserver>();
  Require(observer && SUCCEEDED(swap_chain->SetPrivateDataInterface(observer_key, observer.Get())),
          "Could not register lifetime observer");
  observer.Reset();
  AttachNativeD3D11PresentGuard(swap_chain.Get(), device.Get());
  AttachNativeD3D11PresentGuard(swap_chain.Get(), device.Get());

  ComPtr<ID3D11Texture2D> backbuffer;
  ComPtr<ID3D11RenderTargetView> view;
  Require(SUCCEEDED(swap_chain->GetBuffer(0, IID_PPV_ARGS(&backbuffer))) &&
              SUCCEEDED(device->CreateRenderTargetView(backbuffer.Get(), nullptr, &view)),
          "Could not create backbuffer view");
  const float bright[] = {1, 1, 1, 1};
  const float black[] = {0, 0, 0, 1};
  context->ClearRenderTargetView(view.Get(), bright);
  stage = DeathtrapNativePresentationStage::kExact;
  Require(AllowNativeD3D11Present(swap_chain.Get()), "Exact frame rejected");
  context->ClearRenderTargetView(view.Get(), black);
  stage = DeathtrapNativePresentationStage::kMidpoint;
  Require(!AllowNativeD3D11Present(swap_chain.Get()), "Black midpoint accepted");
}

}  // namespace

const wchar_t* GetDeathtrapSessionLogPath() { return L"NUL"; }
bool DeathtrapExtendedLogsEnabled() { return false; }
void AppendDeathtrapSupportLog(const char*, ...) {}
deathtrap::timing::Stamp BeginDeathtrapDiagnosticTiming() { return {}; }
void EndDeathtrapDiagnosticTiming(deathtrap::timing::Part, deathtrap::timing::Stamp) {}
DeathtrapNativePresentationStage GetDeathtrapNativePresentationStage() { return stage; }
uint64_t GetDeathtrapNativePresentationTick() { return 1; }
DeathtrapControllerSelectorStatus GetDeathtrapControllerSelectorStatus() { return {}; }

int main() {
  const HWND window = CreateWindowExW(0, L"STATIC", L"Swap chain lifetime test",
      WS_OVERLAPPED, 0, 0, 128, 128, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
  Require(window != nullptr, "Could not create test window");
  for (unsigned cycle = 1; cycle <= 24; ++cycle) {
    CheckSwapChain(window);
    Require(destroyed_chains == cycle, "Swap chain remained alive");
  }
  DestroyWindow(window);
  std::puts("PASS: 24 swap chains released and black midpoints rejected");
}
