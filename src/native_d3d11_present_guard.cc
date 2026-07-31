#include "native_d3d11_present_guard.h"

#include <windows.h>

#include <d3d11.h>
#include <d3d11_1.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "deathtrap_native_render_patch.h"

namespace {

using Microsoft::WRL::ComPtr;

constexpr uint32_t kColumns = 24;
constexpr uint32_t kRows = 14;
constexpr uint32_t kBlock = 4;

struct VisualSample {
  std::array<uint8_t, kColumns * kRows> luma{};
  double mean_luma = 0.0;
  uint32_t near_black_cells = 0;
  bool valid = false;
};

std::wstring ModuleDirectory() {
  wchar_t path[MAX_PATH] = {};
  HMODULE module = nullptr;
  GetModuleHandleExW(
      GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
          GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
      reinterpret_cast<LPCWSTR>(&ModuleDirectory), &module);
  const DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
  if (!length || length >= MAX_PATH) {
    return L".";
  }
  if (wchar_t* slash = wcsrchr(path, L'\\')) {
    *slash = L'\0';
  }
  return path;
}

void Log(const char* format, ...) {
  static const std::wstring ini =
      ModuleDirectory() + L"\\deathtrap_native.ini";
  static const bool enabled =
      GetPrivateProfileIntW(L"Diagnostics", L"DebugLog", 0,
                            ini.c_str()) != 0;
  if (!enabled) {
    return;
  }
  static std::mutex mutex;
  static HANDLE file = INVALID_HANDLE_VALUE;
  std::lock_guard<std::mutex> lock(mutex);
  if (file == INVALID_HANDLE_VALUE) {
    const std::wstring path =
        ModuleDirectory() + L"\\deathtrap_native_present.log";
    file = CreateFileW(path.c_str(), FILE_APPEND_DATA,
                       FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                       OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
      return;
    }
  }
  char message[768] = {};
  va_list args;
  va_start(args, format);
  const int length = vsnprintf(message, sizeof(message) - 3, format, args);
  va_end(args);
  if (length <= 0) {
    return;
  }
  const size_t used = std::min<size_t>(static_cast<size_t>(length),
                                       sizeof(message) - 3);
  message[used] = '\r';
  message[used + 1] = '\n';
  DWORD written = 0;
  WriteFile(file, message, static_cast<DWORD>(used + 2), &written, nullptr);
}

class Sampler {
 public:
  Sampler(ID3D11Device* device, ID3D11DeviceContext* context)
      : device_(device), context_(context) {}

  bool Capture(ID3D11Texture2D* source, VisualSample* visual) {
    if (!source || !visual) {
      return false;
    }
    D3D11_TEXTURE2D_DESC source_desc = {};
    source->GetDesc(&source_desc);
    if (!EnsureResources(source_desc)) {
      return false;
    }
    for (uint32_t row = 0; row < kRows; ++row) {
      for (uint32_t column = 0; column < kColumns; ++column) {
        const uint32_t center_x = static_cast<uint32_t>(
            (uint64_t(2 * column + 1) * source_desc.Width) /
            (2 * kColumns));
        const uint32_t center_y = static_cast<uint32_t>(
            (uint64_t(2 * row + 1) * source_desc.Height) /
            (2 * kRows));
        const uint32_t left = std::min(
            source_desc.Width - block_width_,
            center_x > block_width_ / 2 ? center_x - block_width_ / 2 : 0u);
        const uint32_t top = std::min(
            source_desc.Height - block_height_,
            center_y > block_height_ / 2 ? center_y - block_height_ / 2 : 0u);
        const D3D11_BOX box = {left, top, 0, left + block_width_,
                               top + block_height_, 1};
        context_->CopySubresourceRegion(
            sample_.Get(), 0, column * block_width_, row * block_height_, 0,
            source, 0, &box);
      }
    }
    context_->CopyResource(staging_.Get(), sample_.Get());
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(context_->Map(staging_.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
      return false;
    }
    *visual = {};
    uint64_t total_luma = 0;
    for (uint32_t cell_y = 0; cell_y < kRows; ++cell_y) {
      for (uint32_t cell_x = 0; cell_x < kColumns; ++cell_x) {
        uint32_t cell_luma = 0;
        for (uint32_t y = 0; y < block_height_; ++y) {
          const uint8_t* row = static_cast<const uint8_t*>(mapped.pData) +
              (cell_y * block_height_ + y) * mapped.RowPitch;
          for (uint32_t x = 0; x < block_width_; ++x) {
            const uint8_t* pixel =
                row + (cell_x * block_width_ + x) * 4;
            cell_luma += std::max({pixel[0], pixel[1], pixel[2]});
          }
        }
        const uint32_t pixels = block_width_ * block_height_;
        const uint8_t average =
            static_cast<uint8_t>(pixels ? cell_luma / pixels : 0u);
        const size_t cell = size_t(cell_y) * kColumns + cell_x;
        visual->luma[cell] = average;
        total_luma += average;
        if (average <= 8u) {
          ++visual->near_black_cells;
        }
      }
    }
    context_->Unmap(staging_.Get(), 0);
    visual->mean_luma = static_cast<double>(total_luma) /
                        static_cast<double>(visual->luma.size());
    visual->valid = true;
    return true;
  }

 private:
  bool EnsureResources(const D3D11_TEXTURE2D_DESC& source) {
    if (!source.Width || !source.Height || source.SampleDesc.Count != 1) {
      return false;
    }
    const uint32_t block_width = std::min(kBlock, source.Width);
    const uint32_t block_height = std::min(kBlock, source.Height);
    const uint32_t width = block_width * kColumns;
    const uint32_t height = block_height * kRows;
    if (sample_ && format_ == source.Format && sample_width_ == width &&
        sample_height_ == height) {
      return true;
    }
    sample_.Reset();
    staging_.Reset();
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = source.Format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    if (FAILED(device_->CreateTexture2D(&desc, nullptr, &sample_))) {
      return false;
    }
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(device_->CreateTexture2D(&desc, nullptr, &staging_))) {
      sample_.Reset();
      return false;
    }
    format_ = source.Format;
    block_width_ = block_width;
    block_height_ = block_height;
    sample_width_ = width;
    sample_height_ = height;
    return true;
  }

  ComPtr<ID3D11Device> device_;
  ComPtr<ID3D11DeviceContext> context_;
  ComPtr<ID3D11Texture2D> sample_;
  ComPtr<ID3D11Texture2D> staging_;
  DXGI_FORMAT format_ = DXGI_FORMAT_UNKNOWN;
  uint32_t block_width_ = 0;
  uint32_t block_height_ = 0;
  uint32_t sample_width_ = 0;
  uint32_t sample_height_ = 0;
};

struct State {
  ComPtr<IDXGISwapChain> swap_chain;
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  std::unique_ptr<Sampler> sampler;
  VisualSample last_exact;
  bool last_exact_valid = false;
  uint32_t width = 0;
  uint32_t height = 0;
  DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
  uint64_t rejected = 0;
  std::mutex mutex;
};

std::mutex g_states_mutex;
std::unordered_map<IDXGISwapChain*, std::shared_ptr<State>> g_states;

bool IsCorrupt(const VisualSample& exact, const VisualSample& candidate,
               uint32_t* newly_black, const char** reason) {
  if (!exact.valid || !candidate.valid) {
    return false;
  }
  uint32_t collapsed = 0;
  for (size_t i = 0; i < candidate.luma.size(); ++i) {
    if (candidate.luma[i] <= 8u && exact.luma[i] >= 36u) {
      ++collapsed;
    }
  }
  if (newly_black) {
    *newly_black = collapsed;
  }
  const uint32_t cells = static_cast<uint32_t>(candidate.luma.size());
  const bool whole_black =
      exact.mean_luma >= 18.0 &&
      (candidate.mean_luma <= 4.0 ||
       (candidate.near_black_cells * 100u >= cells * 94u &&
        exact.near_black_cells * 100u < cells * 75u));
  if (whole_black) {
    if (reason) {
      *reason = "whole_black";
    }
    return true;
  }
  const bool partial_black =
      collapsed >= 6u &&
      candidate.near_black_cells >= exact.near_black_cells + 5u &&
      candidate.mean_luma + 2.0 < exact.mean_luma;
  if (partial_black && reason) {
    *reason = "partial_black";
  }
  return partial_black;
}

void DrawSelectorDigit(ID3D11DeviceContext1* context,
                       ID3D11RenderTargetView* target, LONG center_x,
                       LONG center_y, LONG scale, uint32_t digit,
                       const float color[4]) {
  if (!context || !target || digit < 1u || digit > 8u) {
    return;
  }
  enum Segment : uint8_t {
    kTop = 1u << 0,
    kUpperRight = 1u << 1,
    kLowerRight = 1u << 2,
    kBottom = 1u << 3,
    kLowerLeft = 1u << 4,
    kUpperLeft = 1u << 5,
    kMiddle = 1u << 6,
  };
  constexpr std::array<uint8_t, 9> kSegments = {
      0u,
      kUpperRight | kLowerRight,
      kTop | kUpperRight | kMiddle | kLowerLeft | kBottom,
      kTop | kUpperRight | kMiddle | kLowerRight | kBottom,
      kUpperLeft | kMiddle | kUpperRight | kLowerRight,
      kTop | kUpperLeft | kMiddle | kLowerRight | kBottom,
      kTop | kUpperLeft | kMiddle | kLowerLeft | kLowerRight | kBottom,
      kTop | kUpperRight | kLowerRight,
      kTop | kUpperRight | kLowerRight | kBottom | kLowerLeft |
          kUpperLeft | kMiddle,
  };
  const LONG thickness = std::max<LONG>(1, scale);
  const LONG half_width = 2 * scale;
  const LONG half_height = 4 * scale;
  const LONG left = center_x - half_width;
  const LONG right = center_x + half_width;
  const LONG top = center_y - half_height;
  const LONG middle = center_y;
  const LONG bottom = center_y + half_height;
  const uint8_t mask = kSegments[digit];
  const auto draw = [&](uint8_t segment, const D3D11_RECT& rect) {
    if (mask & segment) {
      context->ClearView(target, color, &rect, 1);
    }
  };
  draw(kTop, {left, top, right + 1, top + thickness});
  draw(kMiddle,
       {left, middle - thickness / 2, right + 1,
        middle - thickness / 2 + thickness});
  draw(kBottom, {left, bottom - thickness, right + 1, bottom});
  draw(kUpperLeft, {left, top, left + thickness, middle + 1});
  draw(kUpperRight,
       {right - thickness + 1, top, right + 1, middle + 1});
  draw(kLowerLeft, {left, middle, left + thickness, bottom});
  draw(kLowerRight,
       {right - thickness + 1, middle, right + 1, bottom});
}

void DrawControllerSelector(State* state, ID3D11Texture2D* backbuffer) {
  if (!state || !backbuffer) {
    return;
  }
  const DeathtrapControllerSelectorStatus selector =
      GetDeathtrapControllerSelectorStatus();
  if (!selector.visible || selector.category < 1u ||
      selector.category > 4u || selector.slot >= 8u) {
    return;
  }
  ComPtr<ID3D11DeviceContext1> context1;
  if (FAILED(state->context.As(&context1))) {
    return;
  }
  ComPtr<ID3D11RenderTargetView> target;
  if (FAILED(state->device->CreateRenderTargetView(backbuffer, nullptr,
                                                   &target))) {
    return;
  }
  D3D11_TEXTURE2D_DESC desc = {};
  backbuffer->GetDesc(&desc);
  if (desc.Width < 160u || desc.Height < 120u) {
    return;
  }

  constexpr double kPi = 3.14159265358979323846;
  constexpr std::array<std::array<float, 4>, 4> kCategoryColors = {{
      {{0.95f, 0.45f, 0.08f, 1.0f}},
      {{0.95f, 0.80f, 0.10f, 1.0f}},
      {{0.10f, 0.70f, 0.95f, 1.0f}},
      {{0.80f, 0.25f, 0.90f, 1.0f}},
  }};
  const LONG center_x = static_cast<LONG>(desc.Width / 2u);
  const LONG center_y = static_cast<LONG>((desc.Height * 67u) / 100u);
  const LONG radius = static_cast<LONG>(
      std::clamp(std::min(desc.Width, desc.Height) / 15u, 38u, 72u));
  const LONG normal_half = std::clamp<LONG>(radius / 8, 5, 9);
  const auto& category_color = kCategoryColors[selector.category - 1u];
  for (uint32_t slot = 0; slot < 8u; ++slot) {
    const double angle = (static_cast<double>(slot) * kPi) / 4.0;
    const LONG x = center_x +
                   static_cast<LONG>(std::lround(std::sin(angle) * radius));
    const LONG y = center_y -
                   static_cast<LONG>(std::lround(std::cos(angle) * radius));
    const bool selected = slot == selector.slot;
    const LONG half = selected ? normal_half + 4 : normal_half;
    const D3D11_RECT rect = {x - half, y - half, x + half + 1,
                             y + half + 1};
    std::array<float, 4> color = category_color;
    if (!selected) {
      color[0] *= 0.28f;
      color[1] *= 0.28f;
      color[2] *= 0.28f;
    } else if (!selector.slot_available) {
      color = {0.95f, 0.05f, 0.04f, 1.0f};
    }
    context1->ClearView(target.Get(), color.data(), &rect, 1);
    const LONG digit_scale = selected ? 2 : 1;
    const float digit_color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    DrawSelectorDigit(context1.Get(), target.Get(), x, y, digit_scale,
                      slot + 1u, digit_color);
  }
  if (selector.confirmation_required) {
    const LONG half = normal_half;
    const D3D11_RECT center = {center_x - half, center_y - half,
                               center_x + half + 1, center_y + half + 1};
    const float confirmation[4] = {0.95f, 0.75f, 0.08f, 1.0f};
    context1->ClearView(target.Get(), confirmation, &center, 1);
  }
}

}  // namespace

void AttachNativeD3D11PresentGuard(IDXGISwapChain* swap_chain,
                                   IUnknown* creation_device) {
  if (!swap_chain) {
    return;
  }
  ComPtr<ID3D11Device> device;
  if (creation_device) {
    creation_device->QueryInterface(IID_PPV_ARGS(&device));
  }
  if (!device) {
    swap_chain->GetDevice(IID_PPV_ARGS(&device));
  }
  if (!device) {
    return;
  }
  std::lock_guard<std::mutex> lock(g_states_mutex);
  if (g_states.find(swap_chain) != g_states.end()) {
    return;
  }
  auto state = std::make_shared<State>();
  state->swap_chain = swap_chain;
  state->device = device;
  device->GetImmediateContext(&state->context);
  state->sampler =
      std::make_unique<Sampler>(device.Get(), state->context.Get());
  g_states.emplace(swap_chain, state);
  Log("native-only D3D11 swapchain attached sc=%p", swap_chain);
}

bool AllowNativeD3D11Present(IDXGISwapChain* swap_chain) {
  std::shared_ptr<State> state;
  {
    std::lock_guard<std::mutex> lock(g_states_mutex);
    const auto it = g_states.find(swap_chain);
    if (it == g_states.end()) {
      return true;
    }
    state = it->second;
  }
  std::lock_guard<std::mutex> lock(state->mutex);
  ComPtr<ID3D11Texture2D> backbuffer;
  ComPtr<IDXGISwapChain3> swap_chain3;
  UINT index = 0;
  if (SUCCEEDED(swap_chain->QueryInterface(IID_PPV_ARGS(&swap_chain3)))) {
    index = swap_chain3->GetCurrentBackBufferIndex();
  }
  if (FAILED(swap_chain->GetBuffer(index, IID_PPV_ARGS(&backbuffer)))) {
    return true;
  }
  D3D11_TEXTURE2D_DESC desc = {};
  backbuffer->GetDesc(&desc);
  if (state->width != desc.Width || state->height != desc.Height ||
      state->format != desc.Format) {
    state->width = desc.Width;
    state->height = desc.Height;
    state->format = desc.Format;
    state->last_exact_valid = false;
  }
  VisualSample sample;
  if (!state->sampler->Capture(backbuffer.Get(), &sample)) {
    return true;
  }
  const DeathtrapNativePresentationStage stage =
      GetDeathtrapNativePresentationStage();
  const uint64_t source_tick = GetDeathtrapNativePresentationTick();
  if (stage == DeathtrapNativePresentationStage::kMidpoint &&
      state->last_exact_valid) {
    uint32_t newly_black = 0;
    const char* reason = "none";
    if (IsCorrupt(state->last_exact, sample, &newly_black, &reason)) {
      ++state->rejected;
      Log("native phase rejected stage=midpoint tick=%llu reason=%s "
          "newly_black=%u total=%llu",
          static_cast<unsigned long long>(source_tick),
          reason, newly_black,
          static_cast<unsigned long long>(state->rejected));
      return false;
    }
  }
  if (stage == DeathtrapNativePresentationStage::kExact) {
    state->last_exact = sample;
    state->last_exact_valid = true;
  }
  // The controller selector is rendered by Dungeon.dll's native inventory
  // slot renderer. It supplies the real icon, number, quantity and highlight;
  // this presentation guard must not cover it with the old debug squares.
  return true;
}
