#pragma once
#include <windows.h>
#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace setup {
using Bytes = std::vector<unsigned char>;
struct File { std::wstring name; Bytes data; };
struct Inspection { std::wstring directory; bool known = false; bool musicFix = false; };
using Progress = std::function<void(int, const std::wstring&)>;
std::wstring Wide(const std::string& text);
std::string Utf8(const std::wstring& text);
std::vector<File> Payload();
Inspection Inspect(const std::wstring& directory);
std::wstring Install(const Inspection& game, const std::vector<File>& payload,
                     bool keepSettings, std::atomic<bool>& cancel, const Progress& progress);
std::wstring DetectGame();
}
