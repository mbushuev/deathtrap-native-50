// Included inside the render patch's private namespace, after configuration
// and control accessors. All art is embedded; retail files are read, never edited.
using NativeMenuSpriteFn = void(__cdecl*)(const void*, int, int, int, int);
using NativeMenuCopyFn = void(__cdecl*)(const void*, void*, int, int, int, int, int, int);
using NativeMenuDimensionsFn = void(__cdecl*)(int, int, int, int);
constexpr DungeonCodeRva kSetupMenuRva{0x147F0u};
constexpr DungeonCodeRva kSetupInputRva{0x18E10u};
constexpr DungeonCodeRva kRootMenuQuitInputRva{0x1A600u};
constexpr DungeonCodeRva kMenuSpriteRva{0x2F20u};
constexpr DungeonCodeRva kMenuCopyRva{0x9B630u};
constexpr DungeonCodeRva kMenuDimensionsRva{0x9B590u};
constexpr DungeonCodeRva kMenuFlipRva{0x9A850u};
constexpr DungeonCodeRva kMenuPollRva{0x1ACE0u};
constexpr DungeonCodeRva kMenuBackgroundRva{0x12A0u};
constexpr DungeonDataRva kMenuCursorFramesRva{
    0x350460u, 0x350EC0u, 0x350930u, 0x350B10u};
constexpr DungeonDataRva kMenuQuitRva{
    0x21644Cu, 0x216EACu, 0x21691Cu, 0x216AFCu};
struct Native50MenuAssets {
  deathtrap::menu::Image table;
  deathtrap::menu::Image paper;
  deathtrap::menu::Canvas page;
  std::array<deathtrap::menu::Canvas, deathtrap::menu::kBookFrames> books;
  deathtrap::menu::Canvas label, caption;
  std::vector<uint8_t> secondary_font;
};
Native50MenuAssets* g_native50_menu_assets = nullptr;
KeyboardBindingMenuFn g_original_setup_menu = nullptr;
KeyboardBindingInputFn g_original_setup_input = nullptr;
RootMenuInputFn g_original_root_menu_quit_input = nullptr;
NativeMenuSpriteFn g_original_menu_sprite = nullptr;
bool g_native50_menu_hooks_installed = false;
bool g_native50_setup_active = false, g_native50_setup_input = false;
bool g_native50_page_active = false, g_native50_book_hovered = false;
bool g_native50_book_mouse_latched = true, g_native50_book_key_latched = true;
bool g_native50_setup_return_latched = false;
uint8_t g_native50_setup_repaint_frames = 0;
deathtrap::menu::BookAnimation g_native50_book_animation;
struct Native50MenuApi {
  KeyboardBindingMouseStateFn mouse = nullptr;
  KeyboardBindingKeyPressedFn key = nullptr;
  KeyboardBindingSetFontFn font = nullptr;
  KeyboardBindingDrawTextFn text = nullptr;
  int32_t(__cdecl* width)(const char*) = nullptr;
  void (*copy)(const uint8_t*, deathtrap::menu::Rect) = nullptr;
  KeyboardBindingMenuFn flip = nullptr, poll = nullptr;
  bool (*change)(deathtrap::settings::ControlRow, int) = nullptr;
  bool (*background)(bool page) = nullptr;
  KeyboardBindingDrawTextFn secondary_text = nullptr;
};
Native50MenuApi g_native50_menu_api;
std::vector<uint8_t> g_root_menu_version_font;
bool g_root_menu_version_font_attempted = false;
constexpr char kRootMenuBuildVersion[] = "native-50 v." DEATHTRAP_NATIVE_VERSION;

bool Native50ReadHeadingFont(const std::wstring& game, std::vector<uint8_t>* bytes,
                             uint32_t id = 0x1485u) {
  const HANDLE file = CreateFileW((game + L"\\ASYLUM\\DUNGEON.BOX").c_str(),
      GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER length{};
  const auto read_at = [&](uint64_t offset, void* output, DWORD size) {
    LARGE_INTEGER position{}; position.QuadPart = static_cast<LONGLONG>(offset);
    DWORD read = 0;
    return offset + size <= static_cast<uint64_t>(length.QuadPart) &&
        SetFilePointerEx(file, position, nullptr, FILE_BEGIN) &&
        ReadFile(file, output, size, &read, nullptr) && read == size;
  };
  uint32_t index = 0, count = 0;
  std::array<uint32_t,3> entry{};
  bool ok = id && GetFileSizeEx(file,&length) && read_at(0,&index,4) &&
      read_at(index,&count,4) && id < count &&
      read_at(static_cast<uint64_t>(index)+4+(id-1)*12,entry.data(),12) &&
      std::memcmp(&entry[2],".fnt",4)==0 && entry[1]>=8 && entry[1]<65536;
  if (ok) { bytes->resize(entry[1]); ok=read_at(entry[0],bytes->data(),entry[1]); }
  CloseHandle(file);
  return ok;
}

bool Native50ReadPcx(const std::wstring& path, deathtrap::menu::Image* image);
bool LoadRootMenuVersionFont(const std::wstring& game,std::vector<uint8_t>* font) {
  auto main=std::make_unique<deathtrap::menu::Image>();
  if (!Native50ReadHeadingFont(game,font,0x1486u) ||
      !Native50ReadPcx(game+L"\\ASYLUM\\MENU\\MAIN.PCX",main.get()) ||
      !deathtrap::menu::TintVersionFont(font,*main)) {
    font->clear();return false;
  }
  return true;
}

void DrawRootMenuBuildVersion() {
  if (!g_original_menu_sprite) return;
  if (!g_root_menu_version_font_attempted && g_root_menu_version_font.empty()) {
    g_root_menu_version_font_attempted = true;
    LoadRootMenuVersionFont(ModuleDirectory(), &g_root_menu_version_font);
  }
  const auto& font = g_root_menu_version_font;
  if (font.size() < sizeof(deathtrap::text::RetailFontHeader)) return;
  const auto* header = reinterpret_cast<const deathtrap::text::RetailFontHeader*>(font.data());
  if (header->signature != deathtrap::text::kRetailFontSignature ||
      header->glyph_count > deathtrap::text::kMaximumFontGlyphs ||
      font.size() < 8u + header->glyph_count * 8u) return;
  const auto* glyphs = reinterpret_cast<const deathtrap::text::RetailFontGlyph*>(font.data() + 8);
  int width = 0;
  for (const auto* p = reinterpret_cast<const unsigned char*>(kRootMenuBuildVersion); *p; ++p) {
    const size_t index = *p - deathtrap::text::kFirstFontCharacter;
    if (index >= header->glyph_count) return;
    const auto& glyph = glyphs[index];
    if (glyph.advance_x < 0 || glyph.height > 32 ||
        static_cast<size_t>(glyph.pixels) + glyph.width * glyph.height > font.size()) return;
    width += glyph.advance_x;
  }
  if (width < 1 || width > 600) return;
  // Retail MAIN/MAINMP.PCX contains 1.0 at the lower right. Leave those pixels
  // untouched and use the same right margin, with a separate line above it.
  int x = 628 - width;
  constexpr int y = 428;
  for (const auto* p = reinterpret_cast<const unsigned char*>(kRootMenuBuildVersion); *p; ++p) {
    const auto& glyph = glyphs[*p - deathtrap::text::kFirstFontCharacter];
    const int left = x + glyph.offset_x, top = y + glyph.offset_y;
    if (glyph.width && glyph.height && left >= 0 && left + glyph.width <= 640 &&
        top >= 0 && top + glyph.height <= 448) {
      g_original_menu_sprite(font.data() + glyph.pixels, glyph.width, glyph.height, left, top);
    }
    x += glyph.advance_x;
  }
}

uint8_t __cdecl HookRootMenuQuitInput() {
  if (!g_original_root_menu_quit_input) return 0;
  // The confirmation owns a separate capture -> cursor -> Flip -> restore
  // loop, so the root-menu input hook does not run while it is on screen.
  DrawRootMenuBuildVersion();
  return g_original_root_menu_quit_input();
}

bool Native50ReadPcx(const std::wstring& path, deathtrap::menu::Image* image) {
  const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
      nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  const DWORD size = GetFileSize(file, nullptr);
  if (size < 897 || size > 2 * 1024 * 1024) { CloseHandle(file); return false; }
  std::vector<uint8_t> bytes(size);
  DWORD read = 0;
  const bool ok = ReadFile(file, bytes.data(), size, &read, nullptr) && read == size;
  CloseHandle(file);
  return ok && deathtrap::menu::DecodePcx(bytes, image);
}

bool LoadNative50Caption(const deathtrap::menu::Image& table,deathtrap::menu::Canvas* caption) {
  HMODULE module=nullptr;
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
      GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
      reinterpret_cast<LPCWSTR>(&LoadNative50Caption),&module);
  const HRSRC resource=module ? FindResourceW(module,MAKEINTRESOURCEW(502),RT_RCDATA) : nullptr;
  const HGLOBAL loaded=resource ? LoadResource(module,resource) : nullptr;
  const auto* pixels=loaded ? static_cast<const uint8_t*>(LockResource(loaded)) : nullptr;
  return pixels && deathtrap::menu::MakeBookLabel(pixels,SizeofResource(module,resource),table,caption);
}

bool LoadNative50MenuAssets() {
  if (g_native50_menu_assets) return true;
  auto* assets = new Native50MenuAssets;
  const std::wstring directory = ModuleDirectory() + L"\\ASYLUM\\MENU\\";
  HMODULE module = nullptr;
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
      GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
      reinterpret_cast<LPCWSTR>(&LoadNative50MenuAssets), &module);
  const HRSRC resource = module ? FindResourceW(module, MAKEINTRESOURCEW(501), RT_RCDATA) : nullptr;
  const HGLOBAL loaded = resource ? LoadResource(module, resource) : nullptr;
  const auto* sprite = loaded ? static_cast<const uint8_t*>(LockResource(loaded)) : nullptr;
  if (!sprite || !Native50ReadPcx(directory + L"SETUP.PCX", &assets->table) ||
      !Native50ReadPcx(directory + L"HARDSET2.PCX", &assets->paper) ||
      !deathtrap::menu::MakeBookFrames(sprite, SizeofResource(module, resource),
                                      assets->table, &assets->books) ||
      !LoadNative50Caption(assets->table,&assets->label)) {
    delete assets;
    AppendDeathtrapSupportLog("support_native50_menu state=assets_unavailable fallback=native");
    return false;
  }
  assets->caption=assets->table.pixels;
  // Use the scroll's own palette, exactly as a native submenu does. Quantizing
  // it into Setup's darker table palette destroyed shades and produced bands.
  deathtrap::menu::MakeSettingsPage(assets->paper, assets->paper.palette, &assets->page);
  std::vector<uint8_t> heading_font;
  if (!Native50ReadHeadingFont(ModuleDirectory(), &heading_font) ||
      !deathtrap::menu::PaintSettingsHeading(heading_font.data(),heading_font.size(),
          assets->paper.palette,&assets->page) ||
      !Native50ReadHeadingFont(ModuleDirectory(),&assets->secondary_font,0x2490u) ||
      !deathtrap::menu::TintSecondaryFont(&assets->secondary_font,assets->paper.palette)) {
    delete assets;
    AppendDeathtrapSupportLog("support_native50_menu state=heading_unavailable fallback=native");
    return false;
  }
  g_native50_menu_assets = assets;
  return true;
}

void Native50Copy(const uint8_t* pixels, deathtrap::menu::Rect rect) {
  if (!g_dungeon_base || !pixels || rect.x < 0 || rect.y < 0 ||
      rect.width < 1 || rect.height < 1 || rect.x + rect.width > 640 ||
      rect.y + rect.height > 480) return;
  uintptr_t destination = 0;
  if (!SafeRead(g_dungeon_base + kHardwareBackgroundSurfaceRva,
                &destination, sizeof(destination)) || !destination) return;
  const auto dimensions = reinterpret_cast<NativeMenuDimensionsFn>(
      g_dungeon_base + kMenuDimensionsRva);
  const auto copy = reinterpret_cast<NativeMenuCopyFn>(g_dungeon_base + kMenuCopyRva);
  dimensions(640, 480, 640, 480);
  // OPAQUE copy: unlike +0x19A0's transparent-zero sprite copy this also clears
  // black pixels, so cursor/text fragments cannot survive page transitions.
  copy(pixels, reinterpret_cast<void*>(destination), rect.width, rect.height,
       rect.x, rect.y, rect.x, rect.y);
}

bool Native50Background(bool page) {
  uintptr_t surface = 0;
  if (!g_dungeon_base || !SafeRead(g_dungeon_base+kHardwareBackgroundSurfaceRva,
      &surface,sizeof(surface)) || !surface) return false;
  const auto path = reinterpret_cast<KeyboardBindingBuildPathFn>(
      g_dungeon_base+kKeyboardBindingBuildPathRva);
  const auto background = reinterpret_cast<KeyboardBindingLoadImageFn>(
      g_dungeon_base+kMenuBackgroundRva);
  // +0x59830 already prepends the current asylum\menu\ directory. The retail
  // callers pass only the filename; adding that directory twice silently
  // makes +0x12A0 return without loading either pixels or their palette.
  const char* file = path(page ? "hardset2.pcx" : "setup.pcx");
  if (!file || GetFileAttributesA(file) == INVALID_FILE_ATTRIBUTES) {
    AppendDeathtrapSupportLog("support_native50_menu state=background_path_unavailable page=%d",page ? 1 : 0);
    return false;
  }
  // Same palette/fade transaction as the retail Hardware and Setup entry
  // points. It does not replace the cursor/widget atlas in the scratch surface.
  background(file,reinterpret_cast<void*>(surface));
  return true;
}

void __cdecl Native50SecondaryText(int x, int y, int, const char* text) {
  if (!g_native50_menu_assets || !g_original_menu_sprite || !text) return;
  const auto& font=g_native50_menu_assets->secondary_font;
  if (font.size()<8) return;
  const auto* header=reinterpret_cast<const deathtrap::text::RetailFontHeader*>(font.data());
  const auto* glyphs=reinterpret_cast<const deathtrap::text::RetailFontGlyph*>(font.data()+8);
  for (const auto* p=reinterpret_cast<const unsigned char*>(text);*p;++p) {
    if (*p<32 || static_cast<size_t>(*p-32)>=header->glyph_count) continue;
    const auto& glyph=glyphs[*p-32];
    if (glyph.width && glyph.height) g_original_menu_sprite(font.data()+glyph.pixels,
        glyph.width,glyph.height,x+glyph.offset_x,y+glyph.offset_y);
    x+=glyph.advance_x;
  }
}

void Native50WrappedText(int x, int y, int width, const char* text,
                         KeyboardBindingDrawTextFn draw = nullptr) {
  const auto& api = g_native50_menu_api;
  if (!draw) draw=api.text;
  std::string line;
  for (const char* p = text; *p;) {
    const char* end = std::strchr(p, ' ');
    if (!end) end = p + std::strlen(p);
    std::string next = line + (line.empty() ? "" : " ") + std::string(p, end);
    if (!line.empty() && api.width(next.c_str()) > width) {
      draw(x, y, 0, line.c_str()); y += 21; line.clear();
      next.assign(p, end);
    }
    line = std::move(next);
    p = *end ? end + 1 : end;
  }
  if (!line.empty()) draw(x, y, 0, line.c_str());
}

void DrawNative50Book() {
  const auto& api = g_native50_menu_api;
  if (!g_native50_menu_assets || !api.copy) return;
  g_native50_book_animation.Advance(g_native50_book_hovered, GetTickCount64());
  // Clear the previous caption independently, entirely below the book.
  api.copy(g_native50_menu_assets->table.pixels.data(), deathtrap::menu::kBookLabel);
  const auto& frame=g_native50_menu_assets->books[g_native50_book_animation.frame];
  api.copy(frame.data(),
           deathtrap::menu::kBook);
  if (g_native50_book_hovered) {
    const auto rect=deathtrap::menu::kBookLabel;
    for (int y=rect.y;y<rect.y+rect.height;++y) for (int x=rect.x;x<rect.x+rect.width;++x) {
      const size_t i=y*640+x;
      g_native50_menu_assets->caption[i]=g_native50_menu_assets->label[i]!=
          g_native50_menu_assets->table.pixels[i] ? g_native50_menu_assets->label[i] : frame[i];
    }
    api.copy(g_native50_menu_assets->caption.data(),rect);
  }
}

void __cdecl HookNative50MenuSprite(const void* source, int w, int h, int x, int y) {
  // The same 84x84 blitter also RESTORES the saved cursor background after
  // Flip. Dimensions alone cannot identify a cursor draw. Restoring that saved
  // block after a page repaint reintroduced fragments of the custom page.
  if (g_native50_setup_input && !g_native50_page_active && w == 84 && h == 84) {
    std::array<uintptr_t,16> cursors{};
    if (source && SafeRead(g_dungeon_base+kMenuCursorFramesRva,cursors.data(),sizeof(cursors)) &&
        std::find(cursors.begin(),cursors.end(),reinterpret_cast<uintptr_t>(source))!=cursors.end()) {
      DrawNative50Book();
    }
  }
  if (g_original_menu_sprite) g_original_menu_sprite(source, w, h, x, y);
}

void DrawNative50Page(const deathtrap::menu::Navigation& navigation) {
  const auto& api = g_native50_menu_api;
  const auto language = ActiveGameTextLanguage();
  api.copy(g_native50_menu_assets->page.data(), {0, 0, 640, 480});
  api.font(0x248Fu); // Native light text with a dark outline, also on shaded parchment.
  const auto values = CurrentModernControlSettings();
  const int first = deathtrap::menu::PageFirst(navigation.selected);
  api.text(134,78,0,"<");
  api.text(274,78,0,first ? "2 / 2" : "1 / 2");
  api.text(428,78,0,">");
  for (int i = first; i < std::min(first+deathtrap::menu::kRowsPerPage,deathtrap::menu::kPageRows); ++i) {
    const auto row = static_cast<deathtrap::settings::ControlRow>(i);
    const int y = deathtrap::menu::kRowTop + (i-first) * deathtrap::menu::kRowHeight;
    if (navigation.selected == i) api.text(118, y, 0, ">");
    api.text(deathtrap::menu::kTextLeft, y, 0, deathtrap::settings::ControlLabel(language, row));
    const char* value = deathtrap::settings::ControlValue(language, row, values);
    api.text(deathtrap::menu::kTextRight - api.width(value), y, 0, value);
  }
  // Wrap descriptions using the actual localized font, not byte-count guesses.
  constexpr int left = deathtrap::menu::kTextLeft;
  constexpr int width = deathtrap::menu::kTextRight - left;
  api.font(0x2490u); // Original metrics; the private glyph copy supplies brown pixels.
  const auto secondary=api.secondary_text ? api.secondary_text : api.text;
  Native50WrappedText(left, deathtrap::menu::kDescriptionTop, width,
      deathtrap::menu::Description(language, navigation.selected,HeroineLiveSwitchEnabled()),secondary);
  const bool model_selected=navigation.selected==static_cast<int>(deathtrap::settings::ControlRow::kHeroineModel);
  if (model_selected && !g_e3_model_available.load()) {
    Native50WrappedText(left,deathtrap::menu::kSavedTop,width,
        deathtrap::menu::ModelStatus(language,false),secondary);
  } else if (g_hardware_save_error_row >= 0) {
    // Long localized errors belong in the status line, where they cannot
    // collide with a setting label in the compact upper value column.
    api.font(0x1486u);
    api.text(left, deathtrap::menu::kSavedTop, 0, deathtrap::settings::ControlSaveError(language));
    api.font(0x2490u);
  } else if (model_selected && HeroineLiveSwitchEnabled()) {
    Native50WrappedText(left,deathtrap::menu::kSavedTop,width,
        deathtrap::menu::ModelLiveStatus(language),secondary);
  } else if (model_selected && g_model_archive_selection.load()>=0 &&
             (g_model_archive_selection.load()==1)!=values.e3_model) {
    Native50WrappedText(left,deathtrap::menu::kSavedTop,width,
        deathtrap::menu::ModelStatus(language,true),secondary);
  } else secondary(left, deathtrap::menu::kSavedTop, 0, deathtrap::menu::Saved(language));
  secondary(left, deathtrap::menu::kHelpTop, 0, deathtrap::menu::Help(language));
  secondary(left, deathtrap::menu::kHelpTop + 21, 0, deathtrap::menu::BackHelp(language));
  // The candle/scroll/backdrop are retail art read from this installation.
  api.font(0x248Fu);
  api.text(std::max(520, 633 - api.width(deathtrap::menu::Back(language))), 443, 0,
           deathtrap::menu::Back(language));
  api.font(0x1485u);
}

void RunNative50Page() {
  const auto& api = g_native50_menu_api;
  if (g_native50_page_active || !g_native50_menu_assets || !api.poll || !api.flip ||
      !api.mouse || !api.key || !api.copy || !api.change || !api.background ||
      !g_original_menu_sprite) return;
  g_native50_page_active = true;
  if (!api.background(true)) { g_native50_page_active=false; return; }
  g_hardware_save_error_row = -1;
  deathtrap::menu::Navigation navigation;
  AppendDeathtrapSupportLog("support_native50_menu page=open");
  bool done = false;
  while (!done) {
    uint32_t quitting = 0;
    if (!SafeRead(g_dungeon_base + kMenuQuitRva, &quitting, sizeof(quitting)) || quitting) break;
    uint16_t x = 0, y = 0, buttons = 0;
    api.mouse(&x, &y, &buttons);
    const int action = navigation.Poll(x, y, buttons != 0,
        api.key(0xC8) != 0, api.key(0xD0) != 0, api.key(0xCB) != 0, api.key(0xCD) != 0,
        api.key(0x1C) || api.key(0x9C) || api.key(0x39), api.key(1) != 0);
    if (action == 4) { done = true; continue; }
    if (action) api.change(static_cast<deathtrap::settings::ControlRow>(navigation.selected),
                            action == 1 ? -1 : action == 3 ? 1 : 0);
    DrawNative50Page(navigation);
    uintptr_t cursor = 0;
    const size_t frame = (GetTickCount64() / 90) % 16;
    if (SafeRead(g_dungeon_base + kMenuCursorFramesRva + frame * sizeof(uintptr_t),
                  &cursor, sizeof(cursor)) && cursor) {
      g_original_menu_sprite(reinterpret_cast<void*>(cursor), 84, 84, x, y);
    }
    api.flip();
    api.poll(); // Same message/input pump as native submenus; gameplay stays paused.
  }
  api.background(false);
  g_native50_page_active = false;
  // The native parent also acts on Escape/the bottom-right mouse button.
  // Let its input pump consume this press and its release, but suppress their
  // actions until one neutral parent frame has passed. Otherwise Back closes
  // both our page and Setup, especially with a held controller B button.
  g_native50_setup_return_latched = true;
  g_native50_setup_repaint_frames = 2;
  AppendDeathtrapSupportLog("support_native50_menu page=close");
}

uint8_t __cdecl HookNative50SetupInput() {
  if (!g_original_setup_input) return 0;
  g_native50_setup_input = g_native50_setup_active && g_native50_menu_assets;
  if (g_native50_setup_input && g_native50_setup_repaint_frames && g_native50_menu_api.copy) {
    // One repaint per native input/Flip cycle, BEFORE the native code captures
    // the cursor/flame background. Both captures must contain Setup pixels,
    // never the page we just closed. The post-Flip restoration then stays clean.
    --g_native50_setup_repaint_frames;
    g_native50_menu_api.copy(g_native50_menu_assets->table.pixels.data(),{0,0,640,480});
  }
  uint8_t action = g_original_setup_input();
  g_native50_setup_input = false;
  if (!g_native50_setup_active || !g_native50_menu_assets) return action;
  uint16_t x = 0, y = 0, buttons = 0;
  g_native50_menu_api.mouse(&x, &y, &buttons);
  if (g_native50_setup_return_latched) {
    const auto key_down = g_native50_menu_api.key;
    const bool held = buttons || key_down(1) || key_down(0x1C) ||
        key_down(0x9C) || key_down(0x39) || key_down(0x3F);
    if (!held) g_native50_setup_return_latched = false;
    g_native50_book_mouse_latched = g_native50_book_key_latched = true;
    return 0;
  }
  g_native50_book_hovered = deathtrap::menu::kBook.Contains(x, y) ||
                            deathtrap::menu::kBookLabel.Contains(x, y);
  action=deathtrap::menu::ResolveSetupAction(action,g_native50_book_hovered);
  const bool key = g_native50_menu_api.key(0x3F) != 0; // F5 only in Setup.
  const bool activate = (buttons && !g_native50_book_mouse_latched && g_native50_book_hovered) ||
                         (key && !g_native50_book_key_latched);
  g_native50_book_mouse_latched = buttons != 0;
  g_native50_book_key_latched = key;
  if (activate && !(action >= 1 && action <= 4) && action != 99 &&
      !g_native50_menu_api.key(1)) {
    RunNative50Page();
    g_native50_book_mouse_latched = g_native50_book_key_latched = true;
    return 0;
  }
  return action;
}

void __cdecl HookNative50SetupMenu() {
  if (!g_original_setup_menu) return;
  if (g_native50_setup_active) { g_original_setup_menu(); return; }
  LoadNative50MenuAssets();
  g_native50_setup_active = true;
  g_native50_book_hovered = false;
  g_native50_book_mouse_latched = g_native50_book_key_latched = true;
  g_native50_setup_return_latched = false;
  g_native50_setup_repaint_frames = 2;
  g_native50_book_animation = {};
  g_original_setup_menu();
  g_native50_setup_active = g_native50_setup_input = false;
  g_native50_setup_repaint_frames = 0;
}

bool InstallNative50MenuHooks() {
  if (g_native50_menu_hooks_installed) return true;
  constexpr std::array<uint8_t, 5> setup = {0x83, 0xEC, 0x04, 0x33, 0xC0};
  constexpr std::array<uint8_t, 6> input = {0x83, 0xEC, 0x08, 0x53, 0x6A, 0x02};
  constexpr std::array<uint8_t, 7> quit_input = {0x83,0xEC,0x08,0x8D,0x44,0x24,0x02};
  constexpr std::array<uint8_t, 6> sprite = {0x56, 0x57, 0x8B, 0x7C, 0x24, 0x14};
  constexpr std::array<uint8_t, 6> background = {0x8B,0x44,0x24,0x04,0x81,0xEC};
  if (!g_dungeon_base || !MatchesDungeonCode(kSetupMenuRva, setup) ||
      !MatchesDungeonCode(kSetupInputRva, input) || !MatchesDungeonCode(kMenuSpriteRva, sprite) ||
      !MatchesDungeonCode(kRootMenuQuitInputRva,quit_input) ||
      !MatchesDungeonCode(kMenuBackgroundRva,background)) {
    AppendDeathtrapSupportLog("support_native50_menu state=signature_mismatch fallback=native");
    return false;
  }
  struct Hook { void* target; void* detour; void** original; bool made = false, enabled = false; };
  std::array<Hook, 4> hooks = {{
    {g_dungeon_base + kSetupMenuRva, reinterpret_cast<void*>(&HookNative50SetupMenu),
      reinterpret_cast<void**>(&g_original_setup_menu)},
    {g_dungeon_base + kSetupInputRva, reinterpret_cast<void*>(&HookNative50SetupInput),
      reinterpret_cast<void**>(&g_original_setup_input)},
    {g_dungeon_base + kMenuSpriteRva, reinterpret_cast<void*>(&HookNative50MenuSprite),
      reinterpret_cast<void**>(&g_original_menu_sprite)},
    {g_dungeon_base + kRootMenuQuitInputRva, reinterpret_cast<void*>(&HookRootMenuQuitInput),
      reinterpret_cast<void**>(&g_original_root_menu_quit_input)}}};
  const auto rollback = [&]() {
    for (auto& hook : hooks) {
      if (hook.enabled) MH_DisableHook(hook.target);
      if (hook.made) { MH_RemoveHook(hook.target); *hook.original = nullptr; }
    }
  };
  for (auto& hook : hooks) {
    if (MH_CreateHook(hook.target, hook.detour, hook.original) != MH_OK) { rollback(); return false; }
    hook.made = true;
  }
  g_native50_menu_api = {
    reinterpret_cast<KeyboardBindingMouseStateFn>(g_dungeon_base + kKeyboardBindingMouseStateRva),
    reinterpret_cast<KeyboardBindingKeyPressedFn>(g_dungeon_base + kHardwareKeyDownRva),
    reinterpret_cast<KeyboardBindingSetFontFn>(g_dungeon_base + kKeyboardBindingSetFontRva),
    reinterpret_cast<KeyboardBindingDrawTextFn>(g_dungeon_base + kKeyboardBindingDrawTextRva),
    reinterpret_cast<int32_t(__cdecl*)(const char*)>(g_dungeon_base + kGameTextWidthRva),
    &Native50Copy,
    reinterpret_cast<KeyboardBindingMenuFn>(g_dungeon_base + kMenuFlipRva),
    reinterpret_cast<KeyboardBindingMenuFn>(g_dungeon_base + kMenuPollRva),
    +[](deathtrap::settings::ControlRow row, int value) { return ChangeModernControlSetting(row, value); },
    &Native50Background, &Native50SecondaryText};
  for (auto& hook : hooks) {
    if (MH_EnableHook(hook.target) != MH_OK) { rollback(); return false; }
    hook.enabled = true;
  }
  g_native50_menu_hooks_installed = true;
  AppendDeathtrapSupportLog("support_native50_menu state=active entry=setup_book animation=16painted_pages label=blue_chancery_sprite save=immediate");
  return true;
}
