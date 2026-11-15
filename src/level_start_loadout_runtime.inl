constexpr DungeonCodeRva kLevelPickerInput{0x17F60u};
constexpr DungeonCodeRva kMenuLevelStart{0x7FD90u};
constexpr DungeonCodeRva kLevelInventorySnapshot{0x7B4E0u};
constexpr DungeonCodeRva kInventoryPickup{0x7BD60u};
// The character caption ends above this strip. The level preview starts at y=228.
constexpr deathtrap::menu::Rect kLoadoutArea{332, 204, 258, 22};
using LoadoutCaption = std::array<uint8_t, kLoadoutArea.width * kLoadoutArea.height>;
std::array<LoadoutCaption, 2> g_loadout_captions{};
bool g_loadout_captions_ready = false;
bool g_loadout_captions_attempted = false;

// IDs in the original inventory table. Melee quantities are durability.
enum class LoadoutItem : uint8_t {
  kWarhammer = 1, kSilverSword = 4, kVenomSword = 5, kMagicWarhammer = 6,
  kBlunderbuss = 8, kBomb = 9, kGrenadiersCompanion = 10, kInfernalDevice = 11,
  kFlameThrower = 12, kFlameLance = 13,
  kFirefly = 14, kStar = 15, kFireball = 16, kRazor = 17, kJet = 18,
  kGreaterRazor = 19, kArcOfPower = 20, kWarPig = 21,
  kHealth = 22, kAntidote = 23, kStrength = 24, kSpeed = 25,
  kIcyCool = 26, kWarding = 27, kAntiMagic = 28, kInvisibility = 29,
};
struct StartingItem {
  LoadoutItem id;
  uint8_t quantity;
};
using Item = LoadoutItem;
constexpr StartingItem kCircusLoadout[] = {
    {Item::kBomb, 2},
    {Item::kRazor, 2}, {Item::kFireball, 3},
    {Item::kHealth, 5},
};
constexpr StartingItem kLabyrinthLoadout[] = {
    {Item::kSilverSword, 15}, {Item::kBomb, 4},
    {Item::kFirefly, 2}, {Item::kStar, 4}, {Item::kFireball, 4}, {Item::kRazor, 3},
    {Item::kJet, 1}, {Item::kGreaterRazor, 1}, {Item::kArcOfPower, 1}, {Item::kWarPig, 2},
    {Item::kHealth, 6}, {Item::kAntidote, 2}, {Item::kStrength, 1}, {Item::kWarding, 1},
};
constexpr StartingItem kQuarryLoadout[] = {
    {Item::kSilverSword, 15}, {Item::kBlunderbuss, 12}, {Item::kBomb, 3},
    {Item::kFirefly, 3}, {Item::kStar, 5}, {Item::kFireball, 6}, {Item::kRazor, 3},
    {Item::kJet, 2}, {Item::kGreaterRazor, 1}, {Item::kArcOfPower, 2}, {Item::kWarPig, 2},
    {Item::kHealth, 10}, {Item::kAntidote, 3}, {Item::kStrength, 1}, {Item::kWarding, 1},
};
constexpr StartingItem kPitLoadout[] = {
    {Item::kSilverSword, 15}, {Item::kMagicWarhammer, 10},
    {Item::kBlunderbuss, 24}, {Item::kBomb, 3},
    {Item::kFirefly, 2}, {Item::kStar, 5}, {Item::kFireball, 4}, {Item::kRazor, 3},
    {Item::kArcOfPower, 1}, {Item::kWarPig, 2},
    {Item::kHealth, 12}, {Item::kAntidote, 2}, {Item::kStrength, 1}, {Item::kInvisibility, 1},
};
constexpr StartingItem kBelfryLoadout[] = {
    {Item::kWarhammer, 1}, {Item::kSilverSword, 15},
    {Item::kBlunderbuss, 32}, {Item::kBomb, 3}, {Item::kFlameThrower, 128},
    {Item::kFirefly, 3}, {Item::kStar, 6}, {Item::kFireball, 4}, {Item::kRazor, 3},
    {Item::kJet, 2}, {Item::kGreaterRazor, 1}, {Item::kArcOfPower, 2}, {Item::kWarPig, 1},
    {Item::kHealth, 12}, {Item::kAntidote, 3}, {Item::kStrength, 1}, {Item::kSpeed, 2},
    {Item::kIcyCool, 1}, {Item::kWarding, 2}, {Item::kAntiMagic, 2},
};
constexpr StartingItem kHiveLoadout[] = {
    {Item::kWarhammer, 1}, {Item::kVenomSword, 15},
    {Item::kBlunderbuss, 32}, {Item::kBomb, 3},
    {Item::kFirefly, 3}, {Item::kStar, 8}, {Item::kFireball, 2}, {Item::kRazor, 3},
    {Item::kJet, 2}, {Item::kGreaterRazor, 2}, {Item::kArcOfPower, 2}, {Item::kWarPig, 3},
    {Item::kHealth, 12}, {Item::kAntidote, 3}, {Item::kStrength, 1}, {Item::kSpeed, 2},
    {Item::kIcyCool, 1}, {Item::kWarding, 1}, {Item::kAntiMagic, 2},
};
constexpr StartingItem kSewerLoadout[] = {
    {Item::kWarhammer, 1}, {Item::kSilverSword, 15}, {Item::kVenomSword, 15},
    {Item::kBlunderbuss, 36}, {Item::kBomb, 3}, {Item::kInfernalDevice, 8},
    {Item::kFlameThrower, 128}, {Item::kFlameLance, 16},
    {Item::kFirefly, 4}, {Item::kStar, 8}, {Item::kFireball, 6}, {Item::kRazor, 4},
    {Item::kJet, 3}, {Item::kGreaterRazor, 2}, {Item::kArcOfPower, 4}, {Item::kWarPig, 3},
    {Item::kHealth, 16}, {Item::kAntidote, 3}, {Item::kStrength, 2}, {Item::kSpeed, 2},
    {Item::kIcyCool, 1}, {Item::kWarding, 2}, {Item::kAntiMagic, 1},
};
constexpr StartingItem kInversionLoadout[] = {
    {Item::kWarhammer, 1}, {Item::kSilverSword, 15}, {Item::kVenomSword, 15}, {Item::kMagicWarhammer, 10},
    {Item::kBlunderbuss, 32}, {Item::kBomb, 5}, {Item::kGrenadiersCompanion, 12},
    {Item::kInfernalDevice, 12}, {Item::kFlameThrower, 128}, {Item::kFlameLance, 16},
    {Item::kFirefly, 3}, {Item::kStar, 12}, {Item::kFireball, 6}, {Item::kRazor, 4},
    {Item::kJet, 2}, {Item::kGreaterRazor, 1}, {Item::kArcOfPower, 3}, {Item::kWarPig, 4},
    {Item::kHealth, 12}, {Item::kAntidote, 4}, {Item::kStrength, 2}, {Item::kSpeed, 1},
    {Item::kIcyCool, 2}, {Item::kWarding, 2}, {Item::kAntiMagic, 3},
};
constexpr StartingItem kDragonLoadout[] = {
    {Item::kWarhammer, 1}, {Item::kSilverSword, 15}, {Item::kVenomSword, 15},
    {Item::kBlunderbuss, 32}, {Item::kBomb, 6}, {Item::kGrenadiersCompanion, 24},
    {Item::kInfernalDevice, 20}, {Item::kFlameThrower, 196}, {Item::kFlameLance, 16},
    {Item::kFirefly, 4}, {Item::kStar, 14}, {Item::kFireball, 8}, {Item::kRazor, 5},
    {Item::kJet, 4}, {Item::kGreaterRazor, 1}, {Item::kArcOfPower, 4}, {Item::kWarPig, 6},
    {Item::kHealth, 20}, {Item::kAntidote, 4}, {Item::kStrength, 2}, {Item::kSpeed, 2},
    {Item::kIcyCool, 1}, {Item::kWarding, 4}, {Item::kAntiMagic, 3}, {Item::kInvisibility, 1},
};

struct LevelStartPreset {
  uint8_t level;
  const StartingItem* items;
  size_t count;
};
template <size_t Count>
constexpr LevelStartPreset MakeLevelPreset(uint8_t level, const StartingItem (&items)[Count]) {
  return {level, items, Count};
}
// Indexed by the native menu's zero-based start index. The engine's stage IDs
// skip internal maps which cannot be selected from this menu.
constexpr LevelStartPreset kLevelStartPresets[] = {
    {},  // Spire has no preset.
    MakeLevelPreset(1, kCircusLoadout),
    MakeLevelPreset(2, kLabyrinthLoadout),
    MakeLevelPreset(3, kQuarryLoadout),
    MakeLevelPreset(4, kPitLoadout),
    MakeLevelPreset(9, kBelfryLoadout),
    MakeLevelPreset(12, kHiveLoadout),
    MakeLevelPreset(13, kSewerLoadout),
    MakeLevelPreset(15, kInversionLoadout),
    MakeLevelPreset(17, kDragonLoadout),
};

const LevelStartPreset* LevelPreset(size_t index) {
  if (index >= std::size(kLevelStartPresets) || !kLevelStartPresets[index].items) return nullptr;
  return &kLevelStartPresets[index];
}

bool g_level_start_use_preset = false;
bool g_level_start_mouse_held = true;
bool g_level_start_tab_held = true;
const LevelStartPreset* g_level_loadout_pending = nullptr;

bool PaintLevelLoadoutCaption(const std::vector<uint8_t>& font, const char* text,
                             LoadoutCaption* caption) {
  using namespace deathtrap::text;
  if (font.size() < sizeof(RetailFontHeader)) return false;
  const auto* header = reinterpret_cast<const RetailFontHeader*>(font.data());
  const auto* glyphs = reinterpret_cast<const RetailFontGlyph*>(font.data() + sizeof(*header));
  if (header->signature != kRetailFontSignature || header->glyph_count > kMaximumFontGlyphs ||
      font.size() < sizeof(*header) + header->glyph_count * sizeof(*glyphs)) return false;
  const auto* characters = reinterpret_cast<const uint8_t*>(text);
  int width = 0;
  for (const auto* character = characters; *character; ++character) {
    const size_t index = *character - kFirstFontCharacter;
    if (index >= header->glyph_count) return false;
    const auto& glyph = glyphs[index];
    if (glyph.advance_x < 0 ||
        static_cast<size_t>(glyph.pixels) + glyph.width * glyph.height > font.size()) return false;
    width += glyph.advance_x;
  }
  if (width > kLoadoutArea.width) return false;
  int x = (kLoadoutArea.width - width) / 2;
  caption->fill(0);
  for (const auto* character = characters; *character; ++character) {
    const auto& glyph = glyphs[*character - kFirstFontCharacter];
    const int left = x + glyph.offset_x;
    if (left < 0 || left + glyph.width > kLoadoutArea.width ||
        glyph.offset_y < 0 || glyph.offset_y + glyph.height > kLoadoutArea.height) return false;
    for (int row = 0; row < glyph.height; ++row) {
      for (int column = 0; column < glyph.width; ++column) {
        const uint8_t ink = font[glyph.pixels + row * glyph.width + column];
        if (ink) (*caption)[(glyph.offset_y + row) * kLoadoutArea.width + left + column] = ink;
      }
    }
    x += glyph.advance_x;
  }
  return true;
}

bool PrepareLevelLoadoutCaptions(const std::wstring& game) {
  using deathtrap::text::Language;
  // The retail menu font uses CP850.
  const char* original = "Loadout: [Original]";
  const char* preset = "Loadout: [Level preset]";
  switch (ActiveGameTextLanguage()) {
    case Language::kFrench:
      original = "\x90" "quipement : [Original]";
      preset = "\x90" "quipement : [Du niveau]";
      break;
    case Language::kGerman:
      original = "Ausr\x81stung: [Original]";
      preset = "Ausr\x81stung: [Levelvorgabe]";
      break;
    case Language::kItalian:
      original = "Dotazione: [Originale]";
      preset = "Dotazione: [Del livello]";
      break;
    default:
      break;
  }
  std::vector<uint8_t> font;
  return Native50ReadHeadingFont(game, &font, 0x1486u) &&
      PaintLevelLoadoutCaption(font, original, &g_loadout_captions[0]) &&
      PaintLevelLoadoutCaption(font, preset, &g_loadout_captions[1]);
}

uint8_t __cdecl HookLevelPickerInput(uint8_t entry) {
  using PickerInput = uint8_t(__cdecl*)(uint8_t);
  const auto native_input = reinterpret_cast<PickerInput>(g_dungeon_base + kLevelPickerInput);
  const auto& api = g_native50_menu_api;
  g_level_loadout_pending = nullptr;

  uint16_t x = 0, y = 0, buttons = 0;
  api.mouse(&x, &y, &buttons);
  const bool mouse_down = (buttons & 1) != 0;
  const bool tab_down = api.key(0x0F) != 0;
  const bool clicked = !std::exchange(g_level_start_mouse_held, mouse_down) && mouse_down;
  const bool pressed_tab = !std::exchange(g_level_start_tab_held, tab_down) && tab_down;

  // The picker passes a one-based entry or zero if no playable level is selected.
  const bool has_preset = entry && LevelPreset(entry - 1u);
  if (has_preset && !g_loadout_captions_attempted) {
    g_loadout_captions_attempted = true;
    g_loadout_captions_ready = PrepareLevelLoadoutCaptions(ModuleDirectory());
    AppendDeathtrapSupportLog("support_level_start_loadout picker_caption=%u",
                              g_loadout_captions_ready ? 1u : 0u);
  }
  uintptr_t surface = 0;
  if (!has_preset || !g_loadout_captions_ready || !g_original_menu_sprite ||
      !SafeReadValue(g_dungeon_base + kHardwareBackgroundSurfaceRva, &surface) || !surface) {
    const uint8_t action = native_input(entry);
    if (action == 99 || has_preset) g_level_start_use_preset = false;
    return action;
  }
  auto* pixels = reinterpret_cast<uint8_t*>(surface) +
      kLoadoutArea.y * deathtrap::menu::kWidth + kLoadoutArea.x;
  std::array<std::array<uint8_t, kLoadoutArea.width>, kLoadoutArea.height> background;
  for (size_t row = 0; row < background.size(); ++row) {
    if (!SafeRead(pixels + row * deathtrap::menu::kWidth, background[row].data(),
                  background[row].size())) {
      g_level_start_use_preset = false;
      return native_input(entry);
    }
  }

  if (pressed_tab || (clicked && kLoadoutArea.Contains(x, y))) {
    g_level_start_use_preset = !g_level_start_use_preset;
  }

  // The picker presents the 8-bit book surface. Use the same blit as its
  // cursor so the caption is part of that surface before the native flip.
  g_original_menu_sprite(g_loadout_captions[g_level_start_use_preset].data(),
                         kLoadoutArea.width, kLoadoutArea.height, kLoadoutArea.x, kLoadoutArea.y);
  const uint8_t action = native_input(entry);
  for (size_t row = 0; row < background.size(); ++row) {
    SafeWrite(pixels + row * deathtrap::menu::kWidth, background[row].data(),
              background[row].size());
  }
  if (action == 99) g_level_start_use_preset = false;  // Back or Escape
  return action;
}

void __cdecl HookMenuLevelStart(uint8_t index, uint8_t character) {
  const bool use_preset = std::exchange(g_level_start_use_preset, false);
  g_level_loadout_pending = use_preset ? LevelPreset(index) : nullptr;
  using LevelStart = void(__cdecl*)(uint8_t, uint8_t);
  reinterpret_cast<LevelStart>(g_dungeon_base + kMenuLevelStart)(index, character);
}

void __cdecl HookFreshLevelInventorySnapshot() {
  if (const auto* preset = std::exchange(g_level_loadout_pending, nullptr)) {
    const auto get_context = reinterpret_cast<void*(__cdecl*)()>(g_dungeon_base + kRecoveryContextRva);
    const auto* context = static_cast<const uint8_t*>(get_context());
    uint8_t level = 0;
    if (context && SafeReadValue(context + 5, &level) && level == preset->level) {
      using Pickup = int(__cdecl*)(const uint8_t*);
      const auto pickup = reinterpret_cast<Pickup>(g_dungeon_base + kInventoryPickup);
      int granted = 0;
      for (size_t i = 0; i < preset->count; ++i) {
        const auto& item = preset->items[i];
        // The retail pickup API reads ID, flags and quantity from this record.
        std::array<uint8_t, 9> record{};
        record[4] = static_cast<uint8_t>(item.id);
        record[8] = item.quantity;
        granted += pickup(record.data());
      }
      AppendDeathtrapSupportLog("support_level_start_loadout level=%u granted=%d requested=%zu",
                                level, granted, preset->count);
    }
  }
  // The engine captures this inventory for retrying from the beginning of the level.
  reinterpret_cast<KeyboardBindingMenuFn>(g_dungeon_base + kLevelInventorySnapshot)();
}

bool InstallLevelStartLoadoutHooks() {
  if (g_dungeon_image_variant == DungeonImageVariant::kUnsupported ||
      !g_native50_menu_hooks_installed) return false;
  struct CallSite {
    uintptr_t site;
    uintptr_t native;
    uintptr_t hook;
  };
  const CallSite calls[] = {
      {ActiveDungeonCodeRva(0x10746u), kLevelPickerInput, reinterpret_cast<uintptr_t>(&HookLevelPickerInput)},
      {ActiveDungeonCodeRva(0x10C07u), kMenuLevelStart, reinterpret_cast<uintptr_t>(&HookMenuLevelStart)},
      {ActiveDungeonCodeRva(0x80723u), kLevelInventorySnapshot,
       reinterpret_cast<uintptr_t>(&HookFreshLevelInventorySnapshot)},
  };
  const auto base = reinterpret_cast<uintptr_t>(g_dungeon_base);
  for (const auto& call : calls) {
    if (!MatchesDungeonCode(call.site, RenderQueueCall<5>(base + call.site, base + call.native))) {
      return false;
    }
  }
  if (!MatchesDungeonCode(kInventoryPickup,
          std::array<uint8_t, 9>{0x8B, 0x44, 0x24, 0x04, 0x53, 0x56, 0x33, 0xC9, 0x57})) {
    return false;
  }
  size_t installed = 0;
  for (const auto& call : calls) {
    if (!PatchDungeonCode(call.site, RenderQueueCall<5>(base + call.site, base + call.native),
                          RenderQueueCall<5>(base + call.site, call.hook))) {
      while (installed) {
        const auto& prior = calls[--installed];
        PatchDungeonCode(prior.site, RenderQueueCall<5>(base + prior.site, prior.hook),
                         RenderQueueCall<5>(base + prior.site, base + prior.native));
      }
      return false;
    }
    ++installed;
  }
  AppendDeathtrapSupportLog("support_level_start_loadout state=active presets=%zu",
                            std::size(kLevelStartPresets) - 1);
  return true;
}
