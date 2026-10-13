// Execute the actual retail font CPU loop in mapped original DLLs, without
// running their entry points, imports, graphics drivers or the game itself.
#include <filesystem>
#include <fstream>
#include <iostream>

#include "../src/deathtrap_native_render_patch.cc"

namespace {
void Require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "Game text test failed: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

std::vector<uint8_t> ReadBoxResource(const std::filesystem::path& directory,
                                   uint32_t id, const char* extension) {
  std::ifstream box(directory / L"ASYLUM" / L"DUNGEON.BOX",
                    std::ios::binary);
  Require(box.good(), "original BOX must open");
  uint32_t index = 0;
  box.read(reinterpret_cast<char*>(&index), 4);
  box.seekg(index);
  uint32_t count = 0;
  box.read(reinterpret_cast<char*>(&count), 4);
  Require(id > 0 && id < count, "BOX resource ID must be in range");
  box.seekg(static_cast<std::streamoff>(index) + 4 +
            static_cast<std::streamoff>(id - 1) * 12);
  uint32_t offset = 0;
  uint32_t size = 0;
  char actual_extension[4]{};
  box.read(reinterpret_cast<char*>(&offset), 4);
  box.read(reinterpret_cast<char*>(&size), 4);
  box.read(actual_extension, 4);
  Require(std::memcmp(actual_extension, extension, 4) == 0 && size >= 8,
          "BOX resource must have the expected type");
  std::vector<uint8_t> font(size);
  box.seekg(offset);
  box.read(reinterpret_cast<char*>(font.data()), size);
  Require(box.good(), "complete original font must be readable");
  return font;
}

std::vector<uint8_t> ReadBoxFont(const std::filesystem::path& directory,
                               uint32_t id) {
  return ReadBoxResource(directory, id, ".fnt");
}

void CheckGameplayFontPalette(const std::filesystem::path& directory,
                              const std::vector<uint8_t>& font) {
  const auto palette = ReadBoxResource(directory, 1u, ".pal");
  Require(palette.size() >= 768u, "original gameplay RGB palette must be complete");
  const auto* glyphs = reinterpret_cast<const deathtrap::text::RetailFontGlyph*>(
      font.data() + sizeof(deathtrap::text::RetailFontHeader));
  // Verify visible Latin and accented glyphs against the REAL palette, not
  // just a chosen font ID or palette index. This catches the black-font bug.
  for (uint32_t character : {65u, 97u, 130u}) {
    const auto& glyph = glyphs[character - 32u];
    Require(glyph.width && glyph.height &&
                static_cast<size_t>(glyph.pixels) + glyph.width * glyph.height <= font.size(),
            "yellow gameplay font must contain Latin and accented glyphs");
    size_t golden = 0;
    for (size_t pixel = 0; pixel < static_cast<size_t>(glyph.width) * glyph.height; ++pixel) {
      const size_t color = font[glyph.pixels + pixel];
      const uint8_t red = palette[color * 3u];
      const uint8_t green = palette[color * 3u + 1u];
      const uint8_t blue = palette[color * 3u + 2u];
      if (color != 0 && red >= 90 && green >= 60 && red >= green &&
          static_cast<int>(green) - blue >= 40) ++golden;
    }
    Require(golden >= 4u, "name glyph foreground must be visibly gold/yellow in the gameplay palette");
  }
}

void WriteCode(uintptr_t rva, const void* bytes, size_t size) {
  DWORD previous = 0;
  Require(VirtualProtect(g_dungeon_base + rva, size,
                         PAGE_EXECUTE_READWRITE, &previous) != 0,
          "mapped test code must be writable");
  Require(SafeWrite(g_dungeon_base + rva, bytes, size),
          "mapped test code must be written");
  FlushInstructionCache(GetCurrentProcess(), g_dungeon_base + rva, size);
  DWORD ignored = 0;
  VirtualProtect(g_dungeon_base + rva, size, previous, &ignored);
}

void CheckAtlas(const std::vector<uint8_t>& font) {
  deathtrap::text::RetailFontHeader header{};
  std::memcpy(&header, font.data(), sizeof(header));
  Require(header.signature == deathtrap::text::kRetailFontSignature &&
              8u + header.glyph_count * 8u <= font.size(),
          "original FNT header must be valid");
  const auto* glyphs = reinterpret_cast<
      const deathtrap::text::RetailFontGlyph*>(font.data() + 8);
  const size_t count = deathtrap::text::AtlasGlyphCount(
      header, glyphs, header.glyph_count);
  Require(count == header.glyph_count,
          "all original glyphs must fit the unchanged atlas");
  const uintptr_t pointer = reinterpret_cast<uintptr_t>(font.data());
  Require(SafeWrite(g_dungeon_base + kCurrentFontPointerRva, &pointer, 4),
          "current font fixture must be installed");
  const auto setup = reinterpret_cast<FontAtlasSetupFn>(
      g_dungeon_base + kFontAtlasSetupRva);
  setup(font.data());

  std::array<int32_t, 256u * 8u> cache{};
  std::array<uint8_t, 256u * 256u> atlas{};
  Require(SafeRead(g_dungeon_base + kFontGlyphCacheRva, cache.data(),
                   sizeof(cache)) &&
              SafeRead(g_dungeon_base + kFontAtlasPixelsRva, atlas.data(),
                       sizeof(atlas)),
          "real native glyph cache and atlas must be readable");
  std::array<int32_t, 256u * 8u> expected_cache{};
  std::array<uint8_t, 256u * 256u> expected_atlas{};
  size_t x = 0;
  size_t y = 0;
  const auto height = reinterpret_cast<int(__cdecl*)(uint32_t)>(
      g_dungeon_base + ActiveDungeonCodeRva(0x3A160u));
  const auto width = reinterpret_cast<int(__cdecl*)(uint32_t)>(
      g_dungeon_base + ActiveDungeonCodeRva(0x3A060u));
  for (size_t index = 0; index < count; ++index) {
    const auto& glyph = glyphs[index];
    if (x + glyph.width > 256) {
      x = 0;
      y += 32;
    }
    const size_t character = index + 32;
    int32_t* entry = expected_cache.data() + character * 8;
    entry[0] = static_cast<int32_t>(x);
    entry[1] = static_cast<int32_t>(y);
    entry[2] = glyph.width;
    entry[3] = glyph.height;
    entry[4] = glyph.advance_x;
    entry[5] = glyph.advance_y;
    entry[6] = glyph.offset_x;
    entry[7] = glyph.offset_y;
    Require(height(static_cast<uint32_t>(character)) == glyph.advance_y,
            "height must preserve the retail metadata for high-byte glyphs");
    Require(width(static_cast<uint32_t>(character)) == glyph.advance_x,
            "width behavior must remain native");
    if (glyph.width && glyph.height) {
      Require(static_cast<size_t>(glyph.pixels) +
                  static_cast<size_t>(glyph.width) * glyph.height <= font.size(),
              "native font bitmap must stay in the original resource");
      for (size_t row = 0; row < glyph.height; ++row) {
        std::memcpy(expected_atlas.data() + (y + row) * 256 + x,
                    font.data() + glyph.pixels + row * glyph.width,
                    glyph.width);
      }
    }
    x += glyph.width;
  }
  Require(cache == expected_cache,
          "actual retail CPU loop must build every glyph and clear stale slots");
  Require(atlas == expected_atlas,
          "actual atlas pixels must match all original FNT bitmaps exactly");
  Require(height(0) == 0 && height(31) == 0,
          "control characters must remain nonprinting");
  if (count == 160) {
    Require(cache[130u * 8u + 2u] > 0 &&
                cache[130u * 8u + 3u] > 0,
            "CP850 e-acute from Potion de sante must exist in the GPU atlas");
  }
}

const char* captured_text = nullptr;
void __cdecl CaptureText(int32_t, int32_t, int32_t, const char* text) {
  captured_text = text;
}

void CheckMenu(const std::filesystem::path& directory) {
  const wchar_t* name = L"ENGMENU.LST";
  switch (g_dungeon_image_variant) {
    case DungeonImageVariant::kFrenchGog: name = L"FRENMENU.LST"; break;
    case DungeonImageVariant::kGermanGog: name = L"GERMENU.LST"; break;
    case DungeonImageVariant::kItalianGog: name = L"ITNMENU.LST"; break;
    default: break;
  }
  std::ifstream menu(directory / L"ASYLUM" / L"MENU" / name,
                     std::ios::binary);
  Require(menu.good(), "original localization file must open");
  std::string line;
  for (int index = 0; index < 4; ++index) {
    std::getline(menu, line);
  }
  g_keyboard_binding_original_labels = {};
  while (std::getline(menu, line)) {
    std::string id;
    std::string text;
    std::getline(menu, id);
    std::getline(menu, text);
    std::getline(menu, line);
    const int value = std::atoi(id.c_str());
    if (value >= 31 && value <= 41) {
      if (!text.empty() && text.back() == '\r') text.pop_back();
      auto& label = g_keyboard_binding_original_labels[value - 31];
      Require(text.size() < label.size(), "original label must fit native row");
      std::memcpy(label.data(), text.c_str(), text.size() + 1);
    }
  }
  Require(g_keyboard_binding_original_labels[0][0] != '\0',
          "all eleven original action labels must be loaded");
  g_keyboard_binding_backing = deathtrap::input::DefaultKeyboardBindings();
  g_keyboard_binding_layout_aware.fill(1u);
  g_keyboard_binding_menu_active = true;
  for (size_t page = 0; page < deathtrap::input::kRetailBindingPageCount; ++page) {
    g_keyboard_binding_page = page;
    Require(ApplyVisibleKeyboardBindingPage(), "real menu page must update");
    KeyboardBindingLabels labels{};
    RetailBindingWindow values{};
    Require(SafeRead(g_dungeon_base + kKeyboardBindingLabelsRva,
                     labels.data(), sizeof(labels)) &&
                ReadKeyboardBindingValues(&values), "real menu must be readable");
    Require(values == deathtrap::input::MakeBindingPage(
                          page, g_keyboard_binding_backing),
            "localization must not alter any keyboard bindings");
    for (size_t row = 0;
         row < deathtrap::input::kRetailBindingActionRowsPerPage; ++row) {
      const int action = deathtrap::input::BindingActionForRow(page, row);
      if (action < 0) {
        Require(labels[row][0] == 0, "unused rows must remain empty");
      } else if (action < 11 &&
                 ActiveGameTextLanguage() != deathtrap::text::Language::kEnglish) {
        Require(labels[row] == g_keyboard_binding_original_labels[action],
                "localized original labels must remain byte-exact");
      } else {
        Require(std::strcmp(labels[row].data(),
                            deathtrap::text::AddedActionLabel(
                                ActiveGameTextLanguage(), action)) == 0,
                "added actions must use the active game's language");
      }
    }
  }
  g_keyboard_binding_menu_active = false;
  g_original_keyboard_binding_draw_text = &CaptureText;
  HookKeyboardBindingDrawText(100, 100, 0, "Invert First Person");
  Require(std::strcmp(captured_text, deathtrap::text::HardwareCaption(
      ActiveGameTextLanguage(), "Invert First Person")) == 0,
      "hardware caption must be localized outside the keyboard menu");
  HookKeyboardBindingDrawText(100, 100, 0, "Potion de sant\x82");
  Require(std::strcmp(captured_text, "Potion de sant\x82") == 0,
          "original item strings must not be transliterated or re-encoded");
  g_original_keyboard_binding_draw_text = nullptr;
}

void CheckImage(const std::filesystem::path& path) {
  const HMODULE image = LoadLibraryExW(path.c_str(), nullptr,
                                     DONT_RESOLVE_DLL_REFERENCES);
  Require(image != nullptr, "original localized DLL must map");
  g_dungeon_base = reinterpret_cast<uint8_t*>(image);
  g_dungeon_image_variant = IdentifyDungeonImage(g_dungeon_base);
  Require(g_dungeon_image_variant != DungeonImageVariant::kUnsupported,
          "original localized DLL must pass production compatibility gate");
  // An unexpected instruction must reject the WHOLE fix without partially
  // patching heights/atlas. Restore only this test-owned in-memory byte.
  const uint8_t wrong = 0x90;
  WriteCode(kFontHeightBranchRva, &wrong, 1);
  Require(!InstallGameTextFix(), "signature mismatches must fail closed");
  const uint8_t original = 0x7D;
  WriteCode(kFontHeightBranchRva, &original, 1);
  Require(InstallGameTextFix(), "all real localized signatures must match");
  Require(InstallGameTextFix(), "font hook installation must be idempotent");
  // Stop immediately after the authentic retail CPU loop, before any
  // DirectDraw/driver upload. Only this mapped test instance is altered.
  constexpr uint8_t return_after_cpu[] = {
      0x5D, 0x5F, 0x5E, 0x5B, 0x81, 0xC4, 0xA8, 0, 0, 0, 0xC3};
  WriteCode(ActiveDungeonCodeRva(0x59B82u), return_after_cpu,
            sizeof(return_after_cpu));
  for (uint32_t id : {0x1112u, 0x1485u, 0x1486u, 0x248Eu, 0x248Fu}) {
    CheckAtlas(ReadBoxFont(path.parent_path(), id));
  }
  auto metric_fixture = ReadBoxFont(path.parent_path(), 0x1485u);
  // Real fonts usually have zero vertical advance. A nonzero value makes
  // unsigned high-byte indexing observable rather than accepting a zero
  // returned by the old signed-character early exit.
  metric_fixture[8u + (130u - 32u) * 8u + 5u] = 13;
  CheckAtlas(metric_fixture);
  // Measure every original radial name with the actual original gameplay
  // font and width routine (not a fake fixed-width font).
  auto name_font = ReadBoxFont(path.parent_path(), deathtrap::selector_text::kNameFontResource);
  CheckGameplayFontPalette(path.parent_path(), name_font);
  const uintptr_t name_font_pointer = reinterpret_cast<uintptr_t>(name_font.data());
  SafeWrite(g_dungeon_base + kCurrentFontPointerRva, &name_font_pointer, 4);
  const auto width = reinterpret_cast<GameTextWidthFn>(
      g_dungeon_base + kGameTextWidthRva);
  for (uint32_t category = 1; category <= 4; ++category) {
    for (uint32_t slot = 0; slot < 8; ++slot) {
      std::array<char, 128> name{};
      if (!ReadControllerSelectorName(category, slot, &name)) continue;
      const auto lines = deathtrap::selector_text::WrapName(name.data(), 144, width);
      Require(!lines[0].empty(), "real localized radial names must be visible");
      for (const auto& line : lines)
        Require(line.empty() || (width(line.c_str()) > 0 && width(line.c_str()) <= 144),
                "every real font/language name must fit the center in at most two lines");
    }
  }
  // Switching to a smaller external font must clear the preceding accents.
  std::ifstream credits(path.parent_path() / L"ASYLUM" / L"MENU" /
                        L"CREDIT.FNT", std::ios::binary);
  std::vector<uint8_t> small((std::istreambuf_iterator<char>(credits)), {});
  Require(!small.empty(), "original smaller credits font must open");
  CheckAtlas(small);
  CheckMenu(path.parent_path());
  std::cout << "Localized font pixels, CP850 metrics, menus and safety: "
            << ActiveDungeonProfileName() << " passed\n";
  MH_DisableHook(g_dungeon_base + kFontAtlasSetupRva);
  MH_RemoveHook(g_dungeon_base + kFontAtlasSetupRva);
  g_original_font_atlas_setup = nullptr;
  g_font_text_fix_installed = false;
  g_dungeon_base = nullptr;
  FreeLibrary(image);
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
  using deathtrap::text::Language;
  for (Language language : {Language::kEnglish, Language::kFrench,
                            Language::kGerman, Language::kItalian}) {
    for (size_t action = 0; action < deathtrap::input::kKeyboardActionCount;
         ++action) {
      const char* label = deathtrap::text::AddedActionLabel(language, action);
      Require(std::strlen(label) > 0 && std::strlen(label) < 20,
              "localized action must fit the original 20-byte field");
      for (const unsigned char* cursor =
               reinterpret_cast<const unsigned char*>(label); *cursor; ++cursor) {
        Require(*cursor >= 32 && *cursor < 192,
                "labels must use characters available in original CP850 fonts");
      }
    }
  }
  std::array<deathtrap::text::RetailFontGlyph, 224> glyphs{};
  for (auto& glyph : glyphs) { glyph.width = 127; glyph.height = 32; }
  deathtrap::text::RetailFontHeader header{
      deathtrap::text::kRetailFontSignature, 224};
  Require(deathtrap::text::AtlasGlyphCount(header, glyphs.data(), 224) == 16,
          "too-wide fonts must stop before overflowing the texture");
  header.glyph_count = 225;
  Require(!deathtrap::text::AtlasGlyphCount(header, glyphs.data(), 224),
          "out-of-range headers must be rejected");
  header.glyph_count = 160;
  glyphs[0].height = 33;
  Require(!deathtrap::text::AtlasGlyphCount(header, glyphs.data(), 224),
          "glyphs taller than the native row must be rejected");
  Require(MH_Initialize() == MH_OK, "test hooks must initialize");
  // Force PE relocation as well; none of the patch signatures may assume
  // Dungeon.dll loaded at its preferred 0x10000000 image address.
  void* reserved = VirtualAlloc(reinterpret_cast<void*>(0x10000000u), 4096,
                                MEM_RESERVE, PAGE_NOACCESS);
  for (int index = 1; index < argc; ++index) CheckImage(argv[index]);
  if (reserved) VirtualFree(reserved, 0, MEM_RELEASE);
  MH_Uninitialize();
  std::cout << "Game text dictionaries and font bounds passed\n";
  return 0;
}
