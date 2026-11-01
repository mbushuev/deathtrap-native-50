#include "core.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <algorithm>

namespace {
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void Put(const std::wstring& name, const std::string& data) {
  std::ofstream file(name, std::ios::binary); file << data; Require(file.good(), "Fixture write failed");
}
std::string Get(const std::wstring& name) {
  std::ifstream file(name, std::ios::binary); return std::string(std::istreambuf_iterator<char>(file), {});
}
bool Exists(const std::wstring& name) { return GetFileAttributesW(name.c_str()) != INVALID_FILE_ATTRIBUTES; }
}
int main() {
  try {
    auto payload = setup::Payload();
    wchar_t temporary[MAX_PATH]; GetTempPathW(MAX_PATH, temporary);
    std::wstring root = std::wstring(temporary) + L"native50-rollback-" + std::to_wstring(GetCurrentProcessId());
    Require(CreateDirectoryW(root.c_str(), nullptr) != 0, "Private fixture already exists");
    Require(CreateDirectoryW((root + L"\\ASYLUM").c_str(), nullptr) != 0, "Fixture directory failed");
    Put(root + L"\\DD_CD.EXE", "retail executable fixture"); Put(root + L"\\Dungeon.dll", "retail DLL fixture");
    Put(root + L"\\ASYLUM\\config.dat", "RESOLUTION 5RENDERING_PLATFORM 0\r\nD3D_ALLOW_MIPMAP 0\r\n");
    Put(root + L"\\ASYLUM\\keys.cfg", "custom controls"); Put(root + L"\\DINPUT.dll", "previous patch");
    Put(root + L"\\saved-game.dat", "save file sentinel");
    const auto configBefore = Get(root + L"\\ASYLUM\\config.dat");
    for (bool fail : {false, true}) {
      std::atomic<bool> cancel{false}; bool stopped = false, changed = false;
      try {
        setup::Install(setup::Inspect(root), payload, true, cancel, [&](int value, const std::wstring&) {
          if (value >= 50) { changed = true; if (fail) throw std::runtime_error("Injected I/O failure after commit began"); cancel.store(true); }
        });
      } catch (const std::exception& e) { stopped = true; if (!changed) std::cerr << e.what() << '\n'; }
      Require(stopped && changed, "Transaction did not exercise rollback after writes");
      Require(Get(root + L"\\DINPUT.dll") == "previous patch", "Previous DLL not restored");
      Require(Get(root + L"\\ASYLUM\\config.dat") == configBefore, "Retail config not restored");
      Require(Get(root + L"\\ASYLUM\\keys.cfg") == "custom controls", "Controls not restored");
      Require(Get(root + L"\\saved-game.dat") == "save file sentinel", "Save changed");
      Require(!Exists(root + L"\\native50-e3-model.n50p") && !Exists(root + L"\\Native50") && !Exists(root + L"\\back"), "Rollback left new files or directories");
    }
    for (const wchar_t* name : {L"DD_CD.EXE", L"Dungeon.dll", L"DINPUT.dll", L"saved-game.dat", L"ASYLUM\\config.dat", L"ASYLUM\\keys.cfg"})
      Require(DeleteFileW((root + L"\\" + name).c_str()) != 0, "Fixture cleanup failed");
    RemoveDirectoryW((root + L"\\ASYLUM").c_str()); Require(RemoveDirectoryW(root.c_str()) != 0, "Private fixture cleanup failed");
    std::cout << "PASS: cancellation and injected failure restore replaced files, remove new files, and preserve saves.\n"; return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
