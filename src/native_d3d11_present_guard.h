#pragma once

#include <dxgi.h>

// Tracks only the D3D11 surface state needed by the native Deathtrap renderer.
// No frame generation, overlay, NVIDIA API probing or shadow swapchain exists
// in this component.
void AttachNativeD3D11PresentGuard(IDXGISwapChain* swap_chain,
                                   IUnknown* creation_device);

// Returns false only for a transient black/corrupt synthetic native phase.
// The caller reports success without forwarding Present so the DirectDraw page
// chain remains coherent while the last valid endpoint stays visible.
bool AllowNativeD3D11Present(IDXGISwapChain* swap_chain);
