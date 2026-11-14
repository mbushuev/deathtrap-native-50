// Retail writes a 320x200 indexed preview from this RGB capture. Keep that
// writer and give it a full frame at the logical size it already expects.
constexpr DungeonCodeRva kSaveThumbnailCaptureRva{0x97D10u};
constexpr DungeonCodeRva kSaveThumbnailConvertRva{0x97DB0u};
static_assert(sizeof(DDSURFACEDESC) == 0x6c);
using SaveThumbnailCaptureFn = int(__cdecl*)(uint8_t*);
// The native converter receives width first. Its caller reads this RGB buffer
// back as tightly packed rows at the logical render width.
using SaveThumbnailConvertFn = void(__cdecl*)(uint8_t*, int, int, DDSURFACEDESC);
SaveThumbnailCaptureFn g_original_save_thumbnail_capture = nullptr;
SaveThumbnailConvertFn g_original_save_thumbnail_convert = nullptr;
thread_local bool g_save_thumbnail_capture = false;

int __cdecl HookSaveThumbnailCapture(uint8_t* rgb) {
  const bool previous = g_save_thumbnail_capture;
  g_save_thumbnail_capture = true;
  int result = 0;
  __try {
    result = g_original_save_thumbnail_capture(rgb);
  } __finally {
    g_save_thumbnail_capture = previous;
  }
  return result;
}

bool ResizeSaveThumbnailFrame(uint8_t* output, int width, int height,
                              const DDSURFACEDESC& surface) {
  const auto& format = surface.ddpfPixelFormat;
  if (!output || !surface.lpSurface || width <= 0 || height <= 0 ||
      static_cast<int64_t>(width) * height > 0x240000 / 3 ||
      surface.dwWidth < static_cast<DWORD>(width) ||
      surface.dwHeight < static_cast<DWORD>(height) ||
      (surface.dwWidth == static_cast<DWORD>(width) &&
       surface.dwHeight == static_cast<DWORD>(height)) ||
      surface.dwWidth > 8192 || surface.dwHeight > 8192 ||
      format.dwRGBBitCount != 16 || format.dwRBitMask != 0xf800 ||
      format.dwGBitMask != 0x07e0 || format.dwBBitMask != 0x001f ||
      surface.lPitch < static_cast<LONG>(surface.dwWidth * 2u)) {
    return false;
  }
  const auto* pixels = static_cast<const uint8_t*>(surface.lpSurface);
  // The save menu displays the legacy 4:3 scene. Center it on widescreen
  // surfaces so the frame retains its proportions and the player's position.
  const DWORD source_width = std::min(
      surface.dwWidth, surface.dwHeight * static_cast<DWORD>(width) / height);
  const DWORD source_left = (surface.dwWidth - source_width) / 2u;
  for (int y = 0; y < height; ++y) {
    const size_t source_y = (2u * y + 1u) * surface.dwHeight / (2u * height);
    const auto* row = pixels + source_y * surface.lPitch;
    for (int x = 0; x < width; ++x) {
      const size_t source_x = source_left + (2u * x + 1u) * source_width / (2u * width);
      uint16_t pixel = 0;
      std::memcpy(&pixel, row + source_x * sizeof(pixel), sizeof(pixel));
      *output++ = static_cast<uint8_t>((pixel & 31u) << 3u);
      *output++ = static_cast<uint8_t>(((pixel >> 5u) & 63u) << 2u);
      *output++ = static_cast<uint8_t>(((pixel >> 11u) & 31u) << 3u);
    }
  }
  return true;
}

void __cdecl HookSaveThumbnailConvert(uint8_t* output, int width, int height,
                                      DDSURFACEDESC surface) {
  if (g_save_thumbnail_capture &&
      ResizeSaveThumbnailFrame(output, width, height, surface)) {
    AppendDeathtrapSupportLog("support_save thumbnail=%lux%lu logical=%dx%d",
                              surface.dwWidth, surface.dwHeight, width, height);
    return;
  }
  g_original_save_thumbnail_convert(output, width, height, surface);
}

bool InstallSaveThumbnailHooks() {
  if (g_original_save_thumbnail_capture) return true;
  if (!MatchesDungeonCode(kSaveThumbnailCaptureRva,
          std::array<uint8_t, 10>{0x83,0xEC,0x6C,0x8D,0x44,0x24,0,0x56,0x57,0x50}) ||
      !MatchesDungeonCode(kSaveThumbnailConvertRva,
          std::array<uint8_t, 6>{0xDB,0x44,0x24,8,0xDB,0x05})) return false;

  void* capture = g_dungeon_base + kSaveThumbnailCaptureRva;
  void* convert = g_dungeon_base + kSaveThumbnailConvertRva;
  if (MH_CreateHook(convert, reinterpret_cast<void*>(&HookSaveThumbnailConvert),
                    reinterpret_cast<void**>(&g_original_save_thumbnail_convert)) != MH_OK)
    return false;
  const bool capture_created = MH_CreateHook(
      capture, reinterpret_cast<void*>(&HookSaveThumbnailCapture),
      reinterpret_cast<void**>(&g_original_save_thumbnail_capture)) == MH_OK;
  if (capture_created && MH_EnableHook(convert) == MH_OK &&
      MH_EnableHook(capture) == MH_OK) return true;

  if (capture_created) MH_RemoveHook(capture);
  MH_RemoveHook(convert);
  g_original_save_thumbnail_capture = nullptr;
  g_original_save_thumbnail_convert = nullptr;
  return false;
}
