#include "core.h"
#include <commctrl.h>
#include <shlobj.h>
#include <shellapi.h>
#include <algorithm>
#include <memory>
#include <stdexcept>

namespace {
constexpr UINT progressMessage = WM_APP + 1, finishedMessage = WM_APP + 2;
constexpr int browseId = 10, installId = 11, closeId = 12;
struct Result { bool success; std::wstring message; };
struct App {
  HWND window = nullptr, heading = nullptr, intro = nullptr, label = nullptr, path = nullptr,
       browse = nullptr, keep = nullptr, notice = nullptr, bar = nullptr, status = nullptr,
       detail = nullptr, install = nullptr, close = nullptr;
  HFONT font = nullptr, titleFont = nullptr; int dpi = 96;
  HANDLE worker = nullptr; std::atomic<bool> cancel{false}; bool busy = false;
  setup::Inspection game; bool keepSettings = true;
  ~App() { if (worker) CloseHandle(worker); if (font) DeleteObject(font); if (titleFont) DeleteObject(titleFont); }
};
std::wstring WindowText(HWND window) {
  const int n = GetWindowTextLengthW(window); std::wstring result(n + 1, L'\0');
  GetWindowTextW(window, &result[0], n + 1); result.resize(n); return result;
}
HWND Control(App& a, const wchar_t* cls, const wchar_t* text, DWORD style, int id = 0) {
  HWND h = CreateWindowExW(cls == std::wstring(L"EDIT") ? WS_EX_CLIENTEDGE : 0, cls, text,
    WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, a.window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
  SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(a.font), TRUE); return h;
}
void Layout(App& a) {
  RECT r; GetClientRect(a.window, &r);
  auto px = [&](int value) { return MulDiv(value, a.dpi, 96); };
  const int width = r.right, height = r.bottom, margin = px(22), gap = px(10);
  auto place = [&](HWND h, int x, int y, int w, int ht) { MoveWindow(h, x, y, w, ht, TRUE); };
  int body = width - 2 * margin;
  place(a.heading, margin, px(18), body, px(34));
  place(a.intro, margin, px(58), body, px(48));
  place(a.label, margin, px(120), body, px(22));
  place(a.path, margin, px(145), body - px(102), px(28));
  place(a.browse, width - margin - px(92), px(144), px(92), px(30));
  place(a.keep, margin, px(185), body, px(24));
  place(a.notice, margin, px(221), body, px(53));
  place(a.bar, margin, px(288), body, px(18));
  place(a.status, margin, px(316), body, px(42));
  int buttonsY = height - margin - px(34);
  place(a.detail, margin, px(366), body, (std::max)(px(50), buttonsY - gap - px(366)));
  place(a.install, width - margin - px(216), buttonsY, px(112), px(34));
  place(a.close, width - margin - px(94), buttonsY, px(94), px(34));
}
void Busy(App& a, bool value) {
  a.busy = value;
  for (HWND h : {a.path, a.browse, a.keep, a.install}) EnableWindow(h, !value);
  SetWindowTextW(a.close, value ? L"Cancel" : L"Close");
  EnableWindow(a.close, TRUE);
}
DWORD WINAPI Worker(void* context) {
  App& a = *static_cast<App*>(context); auto result = std::make_unique<Result>();
  try {
    auto payload = setup::Payload();
    auto backup = setup::Install(a.game, payload, a.keepSettings, a.cancel, [&](int value, const std::wstring& text) {
      auto message = std::make_unique<std::wstring>(text);
      if (PostMessageW(a.window, progressMessage, value, reinterpret_cast<LPARAM>(message.get()))) message.release();
    });
    result->success = true;
    result->message = L"Native 50 " NATIVE50_VERSION L" is installed.\r\n\r\nStart the game normally through Steam, GOG or DD_CD.EXE.\r\n"
      L"Choose the optional E3 model in Setup / Native 50.\r\n\r\nRollback copy:\r\n" + backup;
  } catch (const std::exception& e) { result->success = false; result->message = setup::Wide(e.what()); }
  if (PostMessageW(a.window, finishedMessage, 0, reinterpret_cast<LPARAM>(result.get()))) result.release();
  return 0;
}
LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM w, LPARAM l) {
  App* a = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (message == WM_NCCREATE) { a = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); a->window = window; SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(a)); }
  if (!a) return DefWindowProcW(window, message, w, l);
  switch (message) {
    case WM_CREATE: {
      a->font = CreateFontW(-MulDiv(10, a->dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
      a->titleFont = CreateFontW(-MulDiv(17, a->dpi, 72), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
      a->heading = Control(*a, L"STATIC", L"Deathtrap Native 50 " NATIVE50_VERSION, 0);
      SendMessageW(a->heading, WM_SETFONT, reinterpret_cast<WPARAM>(a->titleFont), TRUE);
      a->intro = Control(*a, L"STATIC", L"Install the unofficial modernization patch into your existing copy of Deathtrap Dungeon. Close the game before continuing.", 0);
      a->label = Control(*a, L"STATIC", L"Game folder (the folder containing DD_CD.EXE)", 0);
      a->path = Control(*a, L"EDIT", setup::DetectGame().c_str(), WS_TABSTOP | ES_AUTOHSCROLL);
      a->browse = Control(*a, L"BUTTON", L"Browse...", WS_TABSTOP, browseId);
      a->keep = Control(*a, L"BUTTON", L"Keep my current Native 50 settings when updating", WS_TABSTOP | BS_AUTOCHECKBOX);
      SendMessageW(a->keep, BM_SETCHECK, BST_CHECKED, 0);
      a->notice = Control(*a, L"STATIC", L"Includes the complete Windows patch and optional E3 model. Previous files are backed up. Saved games are preserved. No PowerShell, .NET or extra runtime installation is needed.", 0);
      a->bar = Control(*a, PROGRESS_CLASSW, L"", 0);
      a->status = Control(*a, L"STATIC", L"Ready. Select the installed game folder, then choose Install.", 0);
      a->detail = Control(*a, L"EDIT", L"For Steam Deck, use the ZIP download and INSTALL-DECK.sh instead.\r\n\r\nThe installer does not include the original game.", ES_MULTILINE | ES_READONLY | WS_VSCROLL | WS_TABSTOP);
      a->install = Control(*a, L"BUTTON", L"Install", WS_TABSTOP | BS_DEFPUSHBUTTON, installId);
      a->close = Control(*a, L"BUTTON", L"Close", WS_TABSTOP, closeId);
      Layout(*a); return 0;
    }
    case WM_SIZE: Layout(*a); return 0;
    case WM_GETMINMAXINFO: {
      auto* limits = reinterpret_cast<MINMAXINFO*>(l);
      limits->ptMinTrackSize.x = MulDiv(570, a->dpi, 96);
      limits->ptMinTrackSize.y = MulDiv(520, a->dpi, 96); return 0;
    }
    case WM_COMMAND: {
      if (LOWORD(w) == browseId) {
        BROWSEINFOW info{}; info.hwndOwner = window; info.lpszTitle = L"Select the Deathtrap Dungeon folder containing DD_CD.EXE";
        info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE; auto selected = SHBrowseForFolderW(&info);
        if (selected) { wchar_t path[MAX_PATH]; if (SHGetPathFromIDListW(selected, path)) SetWindowTextW(a->path, path); CoTaskMemFree(selected); }
      } else if (LOWORD(w) == installId && !a->busy) {
        try {
          a->game = setup::Inspect(WindowText(a->path));
          if (!a->game.known && MessageBoxW(window,
            L"These game files are not one of the tested Steam or GOG versions. The patch may not work with them. Install anyway?",
            L"Unrecognized game version", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return 0;
          a->keepSettings = SendMessageW(a->keep, BM_GETCHECK, 0, 0) == BST_CHECKED;
          a->cancel.store(false); Busy(*a, true); SendMessageW(a->bar, PBM_SETPOS, 0, 0);
          a->worker = CreateThread(nullptr, 0, Worker, a, 0, nullptr);
          if (!a->worker) { Busy(*a, false); throw std::runtime_error("Unable to start installation."); }
        } catch (const std::exception& e) { MessageBoxW(window, setup::Wide(e.what()).c_str(), L"Cannot install Native 50", MB_OK | MB_ICONERROR); }
      } else if (LOWORD(w) == closeId) SendMessageW(window, WM_CLOSE, 0, 0);
      return 0;
    }
    case progressMessage: {
      std::unique_ptr<std::wstring> text(reinterpret_cast<std::wstring*>(l));
      SendMessageW(a->bar, PBM_SETPOS, w, 0); SetWindowTextW(a->status, text->c_str()); return 0;
    }
    case finishedMessage: {
      std::unique_ptr<Result> result(reinterpret_cast<Result*>(l)); Busy(*a, false);
      if (a->worker) { CloseHandle(a->worker); a->worker = nullptr; }
      SetWindowTextW(a->status, result->success ? L"Installation complete." : L"Installation stopped.");
      SetWindowTextW(a->detail, result->message.c_str());
      if (!result->success && !a->cancel.load()) MessageBoxW(window, result->message.c_str(), L"Installation stopped", MB_OK | MB_ICONERROR);
      return 0;
    }
    case WM_CLOSE:
      if (a->busy) { a->cancel.store(true); EnableWindow(a->close, FALSE); SetWindowTextW(a->status, L"Cancelling and restoring previous files..."); return 0; }
      DestroyWindow(window); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
  }
  return DefWindowProcW(window, message, w, l);
}
void Output(const std::string& text, const std::wstring& log) {
  HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE); DWORD written = 0;
  if (out && out != INVALID_HANDLE_VALUE) WriteFile(out, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
  if (!log.empty()) {
    HANDLE file = CreateFileW(log.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) { WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr); CloseHandle(file); }
  }
}
int CommandLine(int count, wchar_t** args) {
  std::wstring directory, log; bool acceptUnknown = false, reset = false; std::string output;
  try {
    const std::wstring command(args[1]);
    for (int i = 2; i < count; ++i) {
      std::wstring arg(args[i]);
      if (arg == L"--game-dir" && i + 1 < count) directory = args[++i];
      else if (arg == L"--log" && i + 1 < count) log = args[++i];
      else if (arg == L"--accept-unknown") acceptUnknown = true;
      else if (arg == L"--reset-settings") reset = true;
      else throw std::runtime_error("Unknown or incomplete option. Use --help.");
    }
    if (command == L"--help") {
      Output("Native 50 " NATIVE50_VERSION " Windows installer\r\n"
        "--verify-payload [--log file]\r\n--verify --game-dir folder [--log file]\r\n"
        "--install --game-dir folder [--reset-settings] [--accept-unknown] [--log file]\r\n", log); return 0;
    }
    auto payload = setup::Payload(); output = "Embedded payload: verified (18 reviewed files).\r\n";
    if (command == L"--verify-payload") { Output(output, log); return 0; }
    if ((command != L"--verify" && command != L"--install") || directory.empty()) throw std::runtime_error("Specify --install or --verify with --game-dir. Use --help.");
    auto game = setup::Inspect(directory);
    if (!game.known && !acceptUnknown) throw std::runtime_error("Unrecognized game files. Use --accept-unknown only if you accept the compatibility risk.");
    output += game.known ? "Game version: recognized.\r\n" : "Game version: unrecognized (explicitly accepted).\r\n";
    if (command == L"--install") {
      std::atomic<bool> cancel{false};
      auto backup = setup::Install(game, payload, !reset, cancel, [&](int percent, const std::wstring& text) { output += std::to_string(percent) + "% " + setup::Utf8(text) + "\r\n"; });
      output += "Rollback: " + setup::Utf8(backup) + "\r\n";
    }
    Output(output, log); return 0;
  } catch (const std::exception& e) { Output(output + "ERROR: " + e.what() + "\r\n", log); return 1; }
}
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
  int count = 0; wchar_t** args = CommandLineToArgvW(GetCommandLineW(), &count);
  if (!args) return 1;
  if (count > 1) { const int result = CommandLine(count, args); LocalFree(args); return result; }
  LocalFree(args); CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_PROGRESS_CLASS}; InitCommonControlsEx(&controls);
  WNDCLASSW cls{}; cls.lpfnWndProc = WindowProc; cls.hInstance = instance; cls.lpszClassName = L"Native50Setup";
  cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1); cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  cls.hIcon = LoadIconW(nullptr, IDI_APPLICATION); RegisterClassW(&cls);
  App app; HDC dc = GetDC(nullptr); const int dpi = GetDeviceCaps(dc, LOGPIXELSY); ReleaseDC(nullptr, dc);
  RECT area{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0);
  // Keep all actions reachable on small displays even at a high system DPI.
  app.dpi = (std::min)(dpi, MulDiv(static_cast<int>(area.bottom - area.top) - 48, 96, 520));
  app.dpi = (std::max)(72, app.dpi);
  int width = (std::min)(MulDiv(720, app.dpi, 96), static_cast<int>(area.right - area.left));
  int height = (std::min)(MulDiv(600, app.dpi, 96), static_cast<int>(area.bottom - area.top));
  HWND window = CreateWindowExW(WS_EX_CONTROLPARENT, cls.lpszClassName, L"Deathtrap Native 50 Setup", WS_OVERLAPPEDWINDOW,
    area.left + (area.right - area.left - width) / 2, area.top + (area.bottom - area.top - height) / 2, width, height, nullptr, nullptr, instance, &app);
  if (!window) { CoUninitialize(); return 1; }
  ShowWindow(window, show); UpdateWindow(window); MSG message{};
  while (GetMessageW(&message, nullptr, 0, 0) > 0) if (!IsDialogMessageW(window, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
  CoUninitialize(); return 0;
}
