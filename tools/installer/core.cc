#include "core.h"
#include "cabinet.h"
#include <wincrypt.h>
#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>

namespace setup {
namespace {
struct Handle {
  HANDLE h = INVALID_HANDLE_VALUE;
  explicit Handle(HANDLE value) : h(value) {}
  ~Handle() { if (h != INVALID_HANDLE_VALUE) CloseHandle(h); }
  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;
};
[[noreturn]] void Fail(const std::wstring& message) { throw std::runtime_error(Utf8(message)); }
void Check(bool ok, const std::wstring& message) {
  if (!ok) Fail(message + L" (Windows error " + std::to_wstring(GetLastError()) + L").");
}
std::wstring Fs(const std::wstring& path) {
  return path.size() >= 3 && path[1] == L':' ? L"\\\\?\\" + path : path;
}
bool Exists(const std::wstring& path) { return GetFileAttributesW(Fs(path).c_str()) != INVALID_FILE_ATTRIBUTES; }
std::wstring Parent(const std::wstring& path) { return path.substr(0, path.find_last_of(L"\\/")); }
std::wstring Full(const std::wstring& path) {
  wchar_t buffer[32768];
  DWORD n = GetFullPathNameW(path.c_str(), 32768, buffer, nullptr);
  if (!n || n >= 32768) Fail(L"Invalid game directory.");
  std::wstring result(buffer);
  while (result.size() > 3 && result.back() == L'\\') result.pop_back();
  if (result.size() > 32000 || result.size() < 4 || result[1] != L':')
    Fail(L"Choose a local game folder with a drive letter.");
  return result;
}
void NoLinks(const std::wstring& path) {
  for (std::wstring current = path; current.size() > 3; current = Parent(current)) {
    DWORD a = GetFileAttributesW(Fs(current).c_str());
    if (a == INVALID_FILE_ATTRIBUTES) {
      DWORD error = GetLastError();
      if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
        Fail(L"Cannot inspect file permissions: " + current);
    } else if (a & FILE_ATTRIBUTE_REPARSE_POINT) Fail(L"Choose a folder without symbolic links or junctions: " + current);
  }
}
std::wstring Path(const std::wstring& root, const std::wstring& relative) {
  if (relative.empty() || relative.front() == L'/' || relative.front() == L'\\' ||
      relative.find(L':') != std::wstring::npos || relative.find(L'\\') != std::wstring::npos)
    Fail(L"Invalid installer file path.");
  std::wistringstream parts(relative); std::wstring part;
  while (std::getline(parts, part, L'/')) {
    if (part.empty() || part == L"." || part == L".." || part.back() == L'.' || part.back() == L' ')
      Fail(L"Unsafe installer file path.");
  }
  std::wstring path = root + L"\\" + relative;
  std::replace(path.begin(), path.end(), L'/', L'\\'); NoLinks(path); return path;
}
Bytes Read(const std::wstring& path) {
  NoLinks(path);
  Handle f(CreateFileW(Fs(path).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr));
  Check(f.h != INVALID_HANDLE_VALUE, L"Cannot read " + path);
  LARGE_INTEGER size{}; Check(GetFileSizeEx(f.h, &size) != 0, L"Cannot measure file");
  if (size.QuadPart < 0 || size.QuadPart > 64 * 1024 * 1024) Fail(L"Unexpectedly large installer input: " + path);
  Bytes data(static_cast<size_t>(size.QuadPart)); DWORD read = 0;
  Check(ReadFile(f.h, data.data(), static_cast<DWORD>(data.size()), &read, nullptr) && read == data.size(), L"Cannot read file");
  return data;
}
std::string Hash(const unsigned char* data, size_t size) {
  HCRYPTPROV provider = 0; HCRYPTHASH hash = 0;
  Check(CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) != 0, L"Cannot initialize SHA-256");
  if (!CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash)) { CryptReleaseContext(provider, 0); Fail(L"Cannot create SHA-256"); }
  BYTE digest[32]; DWORD length = sizeof(digest);
  bool ok = CryptHashData(hash, data, static_cast<DWORD>(size), 0) && CryptGetHashParam(hash, HP_HASHVAL, digest, &length, 0);
  CryptDestroyHash(hash); CryptReleaseContext(provider, 0); Check(ok, L"Cannot hash installer data");
  const char* hex = "0123456789abcdef"; std::string result;
  for (BYTE b : digest) { result += hex[b >> 4]; result += hex[b & 15]; }
  return result;
}
std::string Hash(const Bytes& data) { return Hash(data.data(), data.size()); }
std::string Text(const Bytes& data) { return std::string(data.begin(), data.end()); }
Bytes Data(const std::string& text) { return Bytes(text.begin(), text.end()); }
void MakeDir(const std::wstring& path, std::vector<std::wstring>& made) {
  NoLinks(path);
  if (Exists(path)) {
    Check((GetFileAttributesW(Fs(path).c_str()) & FILE_ATTRIBUTE_DIRECTORY) != 0, L"A required directory is occupied by a file"); return;
  }
  MakeDir(Parent(path), made);
  Check(CreateDirectoryW(Fs(path).c_str(), nullptr) != 0, L"Cannot create directory " + path); made.push_back(path);
}
void WriteNew(const std::wstring& path, const Bytes& data, bool* owned = nullptr) {
  NoLinks(path);
  Handle f(CreateFileW(Fs(path).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
  Check(f.h != INVALID_HANDLE_VALUE, L"Cannot create file " + path);
  if (owned) *owned = true;
  DWORD written = 0;
  Check(WriteFile(f.h, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) && written == data.size(), L"Cannot write file " + path);
  Check(FlushFileBuffers(f.h) != 0, L"Cannot flush file " + path);
}
std::string Trim(std::string value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  return first == std::string::npos ? "" : value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
}
std::string Lower(std::string value) { for (char& c : value) if (c >= 'A' && c <= 'Z') c += 'a' - 'A'; return value; }
std::string Crlf(const std::string& text) {
  std::string out; for (char c : text) { if (c == '\r') continue; if (c == '\n') out += '\r'; out += c; }
  if (out.size() < 2 || out.substr(out.size() - 2) != "\r\n") out += "\r\n";
  return out;
}
std::string NativeIni(std::string defaults, const std::string& old, bool keep) {
  std::map<std::string, std::string> values;
  std::istringstream previous(old); std::string line, section;
  if (keep) while (std::getline(previous, line)) {
    const std::string clean = Trim(line);
    if (!clean.empty() && clean.front() == '[') section = Lower(clean);
    else if (!clean.empty() && clean[0] != '#' && clean[0] != ';') {
      auto split = clean.find('=');
      if (split != std::string::npos) values[section + Lower(Trim(clean.substr(0, split)))] = Trim(clean.substr(split + 1));
    }
  }
  DEVMODEW mode{}; mode.dmSize = sizeof(mode);
  Check(EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode) && mode.dmPelsWidth >= 640 && mode.dmPelsHeight >= 480, L"Cannot read the primary monitor resolution");
  if (!keep || old.empty()) {
    values["[display]windowwidth"] = std::to_string(mode.dmPelsWidth);
    values["[display]windowheight"] = std::to_string(mode.dmPelsHeight);
  }
  std::istringstream input(defaults); std::string out; section.clear();
  while (std::getline(input, line)) {
    line = Trim(line);
    if (!line.empty() && line.front() == '[') section = Lower(line);
    else if (!line.empty() && line[0] != '#' && line[0] != ';') {
      auto split = line.find('=');
      if (split != std::string::npos) {
        auto found = values.find(section + Lower(Trim(line.substr(0, split))));
        if (found != values.end()) line = line.substr(0, split + 1) + found->second;
      }
    }
    out += line + "\r\n";
  }
  return out;
}
std::string RetailConfig(std::string text) {
  const std::pair<const char*, const char*> settings[] = {{"RENDERING_PLATFORM", "13"}, {"D3D_ALLOW_MIPMAP", "1"},
    {"D3D_ALLOW_PALETTISED", "0"}, {"D3D_TYPE1_SHADOWS", "1"}};
  const std::string newline = text.find("\r\n") == std::string::npos ? "\n" : "\r\n";
  for (const auto& setting : settings) {
    std::string key(setting.first); size_t at = 0;
    while ((at = text.find(key, at)) != std::string::npos) {
      if (at && text[at-1] != '\r' && text[at-1] != '\n' && text[at-1] != ' ' && text[at-1] != '\t') { text.insert(at, newline); at += newline.size(); }
      at += key.size();
    }
    bool terminal = !text.empty() && text.back() == '\n', inserted = false;
    std::istringstream input(text); std::vector<std::string> lines; std::string line;
    while (std::getline(input, line)) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      const auto clean = Trim(line);
      if (clean == key || (clean.size() > key.size() && clean.compare(0, key.size(), key) == 0 &&
          (clean[key.size()] == ' ' || clean[key.size()] == '\t'))) {
        if (!inserted) lines.push_back(key + " " + setting.second); inserted = true;
      } else lines.push_back(line);
    }
    if (!inserted) lines.push_back(key + " " + setting.second);
    text.clear(); for (size_t i = 0; i < lines.size(); ++i) { if (i) text += newline; text += lines[i]; }
    if (terminal || text.empty()) text += newline;
  }
  return text;
}
const std::set<std::wstring> allowed = {L"DINPUT.dll", L"native50-e3-model.n50p", L"deathtrap_native.ini", L"ASYLUM/keys.cfg",
  L"DDraw.dll", L"dxwrapper.dll", L"dxwrapper.ini", L"D3D9.dll", L"D3DImm.dll", L"dgVoodoo.conf",
  L"Native50/README.txt", L"Native50/CHANGELOG.txt", L"Native50/LICENSE.txt", L"Native50/THIRD_PARTY_NOTICES.txt",
  L"Native50/licenses/dxwrapper.txt", L"Native50/source/dxwrapper/README.md", L"Native50/source/dxwrapper/deathtrap-dxwrapper.patch",
  L"Native50/source/dxwrapper/deathtrap-page-restore-present.patch"};
constexpr const char* musicOriginal = "cc7e8d381b21049175ff25f2f628347718df7c8070661dfb31ec4c71fc47ab85";
constexpr const char* musicFixed = "43c879a24ff746674a959b85c0223c73f92ccbd4bb7e81d81100beec232fb410";
}
std::wstring Wide(const std::string& text) {
  int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
  std::wstring out(n, L'\0'); if (n) MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), &out[0], n); return out;
}
std::string Utf8(const std::wstring& text) {
  int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
  std::string out(n, '\0'); if (n) WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), &out[0], n, nullptr, nullptr); return out;
}
std::vector<File> Payload() {
  HMODULE module = GetModuleHandleW(nullptr); HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(101), RT_RCDATA);
  if (!resource) Fail(L"Installer payload is missing.");
  const auto* packed = static_cast<const unsigned char*>(LockResource(LoadResource(module, resource)));
  const auto unpacked = UnpackPayloadResource(packed, SizeofResource(module, resource));
  const auto* bytes = unpacked.data();
  const size_t size = unpacked.size();
  if (!bytes || size < 44 || std::string(reinterpret_cast<const char*>(bytes), 8) != "N50SET01") Fail(L"Invalid installer payload.");
  const auto digest = Hash(bytes + 40, size - 40); const char* hex = "0123456789abcdef"; std::string expected;
  for (size_t i = 8; i < 40; ++i) { expected += hex[bytes[i] >> 4]; expected += hex[bytes[i] & 15]; }
  if (digest != expected) Fail(L"Installer payload failed SHA-256 verification. Download it again.");
  size_t pos = 40;
  auto number = [&](size_t count) {
    if (pos > size || count > size - pos) Fail(L"Truncated installer payload.");
    unsigned value = 0; for (size_t i = 0; i < count; ++i) value |= unsigned(bytes[pos++]) << (i * 8); return value;
  };
  unsigned count = number(4); if (count != allowed.size()) Fail(L"Unexpected installer file count.");
  std::vector<File> files; std::set<std::wstring> names;
  for (unsigned i = 0; i < count; ++i) {
    unsigned length = number(2), dataSize = number(4);
    if (!length || length > 200 || dataSize > 64 * 1024 * 1024 || size - pos < 32ull + length + dataSize) Fail(L"Invalid installer file size.");
    Bytes fileHash(bytes + pos, bytes + pos + 32); pos += 32;
    std::wstring name = Wide(std::string(reinterpret_cast<const char*>(bytes + pos), length)); pos += length;
    if (!allowed.count(name) || !names.insert(name).second) Fail(L"Unexpected or duplicate installer file.");
    File file{name, Bytes(bytes + pos, bytes + pos + dataSize)}; pos += dataSize;
    expected.clear(); for (auto b : fileHash) { expected += hex[b >> 4]; expected += hex[b & 15]; }
    if (Hash(file.data) != expected) Fail(L"Corrupt installer file: " + name);
    files.push_back(std::move(file));
  }
  if (pos != size) Fail(L"Unexpected trailing installer data.");
  return files;
}
Inspection Inspect(const std::wstring& directory) {
  Inspection result; result.directory = Full(directory); NoLinks(result.directory);
  for (const auto* relative : {L"Dungeon.dll", L"DD_CD.EXE", L"ASYLUM/keys.cfg", L"ASYLUM/config.dat"}) {
    auto path = Path(result.directory, relative); DWORD a = GetFileAttributesW(Fs(path).c_str());
    if (a == INVALID_FILE_ATTRIBUTES || (a & FILE_ATTRIBUTE_DIRECTORY)) Fail(L"Select the installed game folder containing DD_CD.EXE. Missing: " + std::wstring(relative));
  }
  const auto dungeon = Hash(Read(Path(result.directory, L"Dungeon.dll"))), exe = Hash(Read(Path(result.directory, L"DD_CD.EXE")));
  const std::set<std::string> dungeons = {"95fe9ce0fff387f00704548f152e4340815213fcb3833dbe1b5c42871e7d2e56", "ca58e0af445b1eb66b7b7653d71371d81e4b03dd5bdb2f82ae01c0a2e499c0fa",
    "83f7e98973a497358eb8e51ea844a803f9afe3e255a3c680e12058e0679d1d3b", "1fbf70ab094c8e767fef5ede13ff1ff0d94d5e887645e5f9a401f84c1c228747"};
  result.known = dungeons.count(dungeon) && (exe == "0c644a00e62652e046c5dad2960f0f6c8c1998f4ca065780fbd7811d9908bf1f" || exe == "4cd498274d32392ea11864616c44241be3624748b3d16e88d6df9330a8c9497f");
  auto miles = Path(result.directory, L"MSS32.DLL");
  result.musicFix = Exists(miles) && Hash(Read(miles)) == musicOriginal;
  if (result.musicFix) for (const auto* relative : {L"WIN32.dll", L"MUSIC/track02.ogg"}) {
    auto path = Path(result.directory, relative);
    if (!Exists(path) || (GetFileAttributesW(Fs(path).c_str()) & FILE_ATTRIBUTE_DIRECTORY)) Fail(L"The GOG soundtrack is incomplete. Reinstall the game: " + std::wstring(relative));
  }
  return result;
}
std::wstring Install(const Inspection& game, const std::vector<File>& payload, bool keepSettings,
                     std::atomic<bool>& cancel, const Progress& progress) {
  auto checkCancel = [&] { if (cancel.load()) Fail(L"Installation cancelled. Previous files have been restored."); };
  // Hold the original game binaries read-only for the whole transaction.
  std::vector<std::unique_ptr<Handle>> guards;
  for (auto* name : {L"DD_CD.EXE", L"Dungeon.dll"}) {
    auto handle = std::make_unique<Handle>(CreateFileW(Fs(Path(game.directory, name)).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr));
    Check(handle->h != INVALID_HANDLE_VALUE, L"Close the game before installing"); guards.push_back(std::move(handle));
  }
  const auto current = Inspect(game.directory);
  if (current.musicFix != game.musicFix || current.known != game.known) Fail(L"The game files changed. Select the folder again.");
  struct Change { File file; Bytes before; bool existed = false, installed = false; };
  std::vector<Change> changes;
  for (const auto& file : payload) changes.push_back({file});
  changes.push_back({{L"ASYLUM/config.dat", Data(RetailConfig(Text(Read(Path(game.directory, L"ASYLUM/config.dat")))))}});
  if (game.musicFix) {
    auto bytes = Read(Path(game.directory, L"MSS32.DLL"));
    if (bytes.size() < 0x24188 + 9 || std::string(bytes.begin() + 0x24188, bytes.begin() + 0x24188 + 9) != "WINMM.dll") Fail(L"Unexpected GOG music import.");
    bytes[0x24188 + 3] = '3'; bytes[0x24188 + 4] = '2';
    if (Hash(bytes) != musicFixed) Fail(L"GOG music repair failed verification.");
    changes.push_back({{L"MSS32.DLL", std::move(bytes)}});
  }
  for (auto& change : changes) {
    checkCancel(); auto target = Path(game.directory, change.file.name); DWORD a = GetFileAttributesW(Fs(target).c_str());
    change.existed = a != INVALID_FILE_ATTRIBUTES;
    if (change.existed) {
      if (a & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_READONLY)) Fail(L"A file cannot be replaced: " + change.file.name);
      Handle lock(CreateFileW(Fs(target).c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr));
      Check(lock.h != INVALID_HANDLE_VALUE, L"Close the game or other programs using " + change.file.name);
    }
    if (change.existed) change.before = Read(target);
    if (change.file.name == L"deathtrap_native.ini") change.file.data = Data(NativeIni(Text(change.file.data), Text(change.before), keepSettings));
    if (change.file.name == L"ASYLUM/keys.cfg") change.file.data = Data(Crlf(Text(change.file.data)));
  }
  SYSTEMTIME now{}; GetLocalTime(&now); wchar_t stamp[80];
  swprintf_s(stamp, L"%04u%02u%02u-%02u%02u%02u-%lu-%lu", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, GetCurrentProcessId(), GetTickCount());
  auto staging = Path(game.directory, std::wstring(L".native50-setup-") + stamp);
  auto backup = Path(game.directory, std::wstring(L"back/native50-setup-") + stamp);
  std::vector<std::wstring> made, stagedFiles, backupFiles; bool committed = false;
  try {
    // Never adopt a pre-existing transaction directory, even on a name collision.
    MakeDir(Parent(backup), made);
    Check(CreateDirectoryW(Fs(staging).c_str(), nullptr) != 0, L"Cannot create a private staging directory"); made.push_back(staging);
    Check(CreateDirectoryW(Fs(backup).c_str(), nullptr) != 0, L"Cannot create a unique rollback directory"); made.push_back(backup);
    std::string receipt = "Native 50 " NATIVE50_VERSION " rollback files (paths relative to the game folder).\r\n"
      "Close the game. Copy files from this folder back to their matching game paths.\r\n"
      "Remove the files listed as NEW if returning to the previous installation.\r\n\r\n";
    for (size_t i = 0; i < changes.size(); ++i) {
      checkCancel(); auto& c = changes[i]; progress(static_cast<int>(i * 45 / changes.size()), L"Preparing: " + c.file.name);
      auto incoming = Path(staging, c.file.name); MakeDir(Parent(incoming), made); stagedFiles.push_back(incoming); WriteNew(incoming, c.file.data);
      if (Hash(Read(incoming)) != Hash(c.file.data)) Fail(L"A staged file failed verification.");
      if (c.existed) {
        auto saved = Path(backup, c.file.name); MakeDir(Parent(saved), made); backupFiles.push_back(saved); WriteNew(saved, c.before);
        if (Hash(Read(saved)) != Hash(c.before)) Fail(L"A rollback file failed verification.");
      }
      receipt += (c.existed ? "SAVED " : "NEW   ") + Utf8(c.file.name) + "\r\n";
    }
    auto receiptPath = Path(backup, L"RESTORE.txt"); backupFiles.push_back(receiptPath); WriteNew(receiptPath, Data(receipt));
    for (size_t i = 0; i < changes.size(); ++i) {
      checkCancel(); auto& c = changes[i]; auto target = Path(game.directory, c.file.name);
      // Do not overwrite a file changed since the preflight snapshot.
      if (Exists(target) != c.existed || (c.existed && Hash(Read(target)) != Hash(c.before))) Fail(L"A destination file changed during setup: " + c.file.name);
      MakeDir(Parent(target), made);
      // A failed copy can leave a partial destination, so enlist it in the
      // rollback before writing. The verified backup remains until completion.
      if (c.existed) {
        c.installed = true;
        Check(CopyFileW(Fs(Path(staging, c.file.name)).c_str(), Fs(target).c_str(), FALSE) != 0, L"Cannot install " + c.file.name);
      } else {
        // CREATE_NEW proves ownership before rollback is allowed to delete it.
        WriteNew(target, c.file.data, &c.installed);
      }
      progress(45 + static_cast<int>((i + 1) * 45 / changes.size()), L"Installed: " + c.file.name);
    }
    for (const auto& c : changes) if (Hash(Read(Path(game.directory, c.file.name))) != Hash(c.file.data)) Fail(L"Installed file failed verification: " + c.file.name);
    checkCancel(); committed = true;
  } catch (...) {
    bool restored = true;
    for (auto it = changes.rbegin(); it != changes.rend(); ++it) if (it->installed) {
      try {
        auto target = Path(game.directory, it->file.name);
        bool ok = it->existed ? CopyFileW(Fs(Path(backup, it->file.name)).c_str(), Fs(target).c_str(), FALSE) != 0 : DeleteFileW(Fs(target).c_str()) != 0;
        if (!ok) restored = false;
        if (ok && it->existed && Hash(Read(target)) != Hash(it->before)) restored = false;
      } catch (...) { restored = false; }
    }
    for (const auto& file : stagedFiles) DeleteFileW(Fs(file).c_str());
    if (restored) for (const auto& file : backupFiles) DeleteFileW(Fs(file).c_str());
    for (auto it = made.rbegin(); it != made.rend(); ++it) RemoveDirectoryW(Fs(*it).c_str());
    if (!restored) Fail(L"Rollback could not finish. Keep this recovery folder: " + backup);
    throw;
  }
  if (committed) {
    for (const auto& file : stagedFiles) DeleteFileW(Fs(file).c_str());
    // RemoveDirectory removes only empty, explicitly created staging folders.
    for (auto it = made.rbegin(); it != made.rend(); ++it) if (it->compare(0, staging.size(), staging) == 0) RemoveDirectoryW(Fs(*it).c_str());
  }
  progress(100, L"Installed successfully. Rollback copy: " + backup); return backup;
}
std::wstring DetectGame() {
  wchar_t module[MAX_PATH]; GetModuleFileNameW(nullptr, module, MAX_PATH);
  for (auto path : {Parent(module), std::wstring(L".")}) if (Exists(path + L"\\DD_CD.EXE")) return Full(path);
  wchar_t steam[4096]; DWORD size = sizeof(steam);
  if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", RRF_RT_REG_SZ, nullptr, steam, &size) == ERROR_SUCCESS) {
    std::vector<std::wstring> libraries{steam};
    auto vdf = std::wstring(steam) + L"\\steamapps\\libraryfolders.vdf";
    if (Exists(vdf)) try {
      const auto text = Text(Read(vdf)); size_t pos = 0;
      while ((pos = text.find("\"path\"", pos)) != std::string::npos) {
        auto begin = text.find('"', pos + 6), end = begin == std::string::npos ? begin : text.find('"', begin + 1);
        if (end == std::string::npos) break;
        auto path = text.substr(begin + 1, end - begin - 1); size_t slash = 0;
        while ((slash = path.find("\\\\", slash)) != std::string::npos) { path.erase(slash, 1); ++slash; }
        libraries.push_back(Wide(path)); pos = end + 1;
      }
    } catch (const std::exception&) { /* Manual folder selection remains available. */ }
    for (const auto& library : libraries) {
      auto game = library + L"\\steamapps\\common\\Deathtrap Dungeon";
      if (Exists(game + L"\\DD_CD.EXE")) return game;
    }
  }
  for (HKEY hive : {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER}) {
    HKEY games = nullptr;
    if (RegOpenKeyExW(hive, L"Software\\GOG.com\\Games", 0, KEY_READ | KEY_WOW64_32KEY, &games) != ERROR_SUCCESS) continue;
    for (DWORD index = 0;; ++index) {
      wchar_t key[256]; DWORD length = 256;
      LONG status = RegEnumKeyExW(games, index, key, &length, nullptr, nullptr, nullptr, nullptr);
      if (status == ERROR_NO_MORE_ITEMS) break;
      if (status != ERROR_SUCCESS) continue;
      wchar_t path[4096]; DWORD bytes = sizeof(path);
      if (RegGetValueW(games, key, L"path", RRF_RT_REG_SZ, nullptr, path, &bytes) == ERROR_SUCCESS && Exists(std::wstring(path) + L"\\DD_CD.EXE")) {
        RegCloseKey(games); return path;
      }
    }
    RegCloseKey(games);
  }
  return L"";
}
}
