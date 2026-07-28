#include <windows.h>

#include <dinput.h>

#include <cstdio>

int wmain(int argc, wchar_t** argv) {
  if (argc != 2) {
    return 2;
  }
  HMODULE proxy = LoadLibraryW(argv[1]);
  if (!proxy) {
    std::printf("LoadLibrary failed: %lu\n", GetLastError());
    return 3;
  }
  using CreateFn = HRESULT(WINAPI*)(HINSTANCE, DWORD, LPDIRECTINPUTA*,
                                    LPUNKNOWN);
  const auto create = reinterpret_cast<CreateFn>(
      GetProcAddress(proxy, "DirectInputCreateA"));
  if (!create) {
    std::printf("GetProcAddress failed: %lu\n", GetLastError());
    return 4;
  }
  LPDIRECTINPUTA direct_input = nullptr;
  const HRESULT result = create(GetModuleHandleW(nullptr), 0x0700,
                                &direct_input, nullptr);
  if (direct_input) {
    direct_input->Release();
  }
  FreeLibrary(proxy);
  std::printf("DirectInputCreateA=0x%08lX object=%s\n",
              static_cast<unsigned long>(result),
              direct_input ? "created" : "null");
  return SUCCEEDED(result) ? 0 : 5;
}
