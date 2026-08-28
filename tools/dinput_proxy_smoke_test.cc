#include <windows.h>

#include <dinput.h>

#include <cstdio>
#include <string>
#include <vector>

bool VerifyAutomaticSupportLog(const wchar_t* proxy_path) {
  std::wstring directory = proxy_path ? proxy_path : L"";
  const size_t slash = directory.find_last_of(L"\\/");
  directory = slash == std::wstring::npos ? L"." : directory.substr(0, slash);
  wchar_t pattern[128] = {};
  swprintf_s(pattern, L"deathtrap-native-*-pid%lu.log",
             static_cast<unsigned long>(GetCurrentProcessId()));
  const std::wstring search = directory + L"\\logs\\" + pattern;
  WIN32_FIND_DATAW found = {};
  HANDLE find = FindFirstFileW(search.c_str(), &found);
  if (find == INVALID_HANDLE_VALUE) {
    return false;
  }
  FindClose(find);
  const std::wstring path = directory + L"\\logs\\" + found.cFileName;
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return false;
  }
  const DWORD size = GetFileSize(file, nullptr);
  if (size == INVALID_FILE_SIZE || size > 1024u * 1024u) {
    CloseHandle(file);
    return false;
  }
  std::vector<char> bytes(static_cast<size_t>(size) + 1u, '\0');
  DWORD read = 0;
  const bool loaded = ReadFile(file, bytes.data(), size, &read, nullptr) &&
      read == size;
  CloseHandle(file);
  if (!loaded) {
    return false;
  }
  const std::string log(bytes.data(), read);
  return log.find("support_start version=") != std::string::npos &&
      log.find("support_directinput_create") != std::string::npos &&
      log.find("support_mouse_device") != std::string::npos;
}

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
  HRESULT mouse_result = DIERR_NOTINITIALIZED;
  HRESULT state_result = DIERR_NOTINITIALIZED;
  if (direct_input) {
    LPDIRECTINPUTDEVICEA mouse = nullptr;
    mouse_result = direct_input->CreateDevice(GUID_SysMouse, &mouse, nullptr);
    if (mouse) {
      DIMOUSESTATE state{};
      state_result = mouse->GetDeviceState(sizeof(state), &state);
      mouse->Release();
    }
    direct_input->Release();
  }
  FreeLibrary(proxy);
  const bool support_log = VerifyAutomaticSupportLog(argv[1]);
  std::printf("DirectInputCreateA=0x%08lX object=%s mouse=0x%08lX "
              "state_probe=0x%08lX support_log=%s\n",
              static_cast<unsigned long>(result),
              direct_input ? "created" : "null",
              static_cast<unsigned long>(mouse_result),
              static_cast<unsigned long>(state_result),
              support_log ? "verified" : "missing");
  return SUCCEEDED(result) && SUCCEEDED(mouse_result) && support_log ? 0 : 5;
}
