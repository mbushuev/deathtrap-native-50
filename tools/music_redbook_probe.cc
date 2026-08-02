#include <windows.h>

#include <cstdint>
#include <cstdio>

namespace {

using RedbookOpenFn = void*(__stdcall*)(uint32_t drive);
using RedbookCloseFn = int32_t(__stdcall*)(void* handle);
using RedbookTracksFn = uint32_t(__stdcall*)(void* handle);
using RedbookTrackInfoFn = int32_t(__stdcall*)(
    void* handle, uint32_t track, uint32_t* start, uint32_t* end);

template <typename T>
T Resolve(HMODULE module, const char* name) {
  return reinterpret_cast<T>(GetProcAddress(module, name));
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  if (argc != 2) {
    fwprintf(stderr, L"usage: music_redbook_probe <MSS32.DLL>\n");
    return 2;
  }
  HMODULE module = LoadLibraryW(argv[1]);
  if (!module) {
    fwprintf(stderr, L"LoadLibrary failed: %lu\n", GetLastError());
    return 1;
  }
  const auto open = Resolve<RedbookOpenFn>(module, "_AIL_redbook_open@4");
  const auto close = Resolve<RedbookCloseFn>(module, "_AIL_redbook_close@4");
  const auto tracks = Resolve<RedbookTracksFn>(module, "_AIL_redbook_tracks@4");
  const auto track_info = Resolve<RedbookTrackInfoFn>(
      module, "_AIL_redbook_track_info@16");
  if (!open || !close || !tracks || !track_info) {
    std::fputs("required Redbook exports are missing\n", stderr);
    FreeLibrary(module);
    return 1;
  }

  void* const handle = open(0u);
  std::printf("handle=%p tracks=%u\n", handle,
              handle ? tracks(handle) : 0u);
  if (handle) {
    for (uint32_t track = 2u; track <= 16u; ++track) {
      uint32_t start = 0;
      uint32_t end = 0;
      const int32_t result = track_info(handle, track, &start, &end);
      std::printf("track=%u result=%d start=%u end=%u\n", track, result,
                  start, end);
    }
    close(handle);
  }
  FreeLibrary(module);
  return 0;
}
