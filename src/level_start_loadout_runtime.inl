// The picker passes one-based entries. The start function takes a zero-based index.
constexpr uint8_t kCircusPickerEntry = 9;
constexpr uint8_t kCircusLevelId = 15;
constexpr uintptr_t kLevelPickerInput = 0x17F60u;
constexpr uintptr_t kMenuLevelStart = 0x7FD90u;
constexpr uintptr_t kLevelInventorySnapshot = 0x7B4E0u;
constexpr uintptr_t kInventoryPickup = 0x7BD60u;
constexpr deathtrap::menu::Rect kLoadoutArea{332, 175, 258, 51};
constexpr deathtrap::menu::Rect kOriginalChoice{344, 201, 105, 24};
constexpr deathtrap::menu::Rect kPresetChoice{449, 201, 133, 24};

struct StartingItem {
  uint8_t id;
  uint8_t quantity;
};
constexpr StartingItem kCircusLoadout[] = {
    {5, 15},  // Venom Sword at full durability
    {9, 2},   // Bomb
    {17, 2},  // Razor Spell
    {16, 3},  // Fireball Spell
    {22, 5},  // Health Potion
};

bool g_level_start_use_preset = false;
bool g_level_start_mouse_held = true;
bool g_level_start_tab_held = true;
bool g_circus_loadout_pending = false;

void DrawLevelLoadoutChoice(deathtrap::menu::Rect area, int y, const char* caption) {
  const auto& api = g_native50_menu_api;
  api.text(area.x + (area.width - api.width(caption)) / 2, y, 0, caption);
}

uint8_t __cdecl HookLevelPickerInput(uint8_t entry) {
  using PickerInput = uint8_t(__cdecl*)(uint8_t);
  const auto native_input = reinterpret_cast<PickerInput>(g_dungeon_base + kLevelPickerInput);
  const auto& api = g_native50_menu_api;
  g_circus_loadout_pending = false;

  uint16_t x = 0, y = 0, buttons = 0;
  api.mouse(&x, &y, &buttons);
  const bool mouse_down = (buttons & 1) != 0;
  const bool tab_down = api.key(0x0F) != 0;
  const bool clicked = !std::exchange(g_level_start_mouse_held, mouse_down) && mouse_down;
  const bool pressed_tab = !std::exchange(g_level_start_tab_held, tab_down) && tab_down;

  uintptr_t surface = 0;
  if (entry != kCircusPickerEntry ||
      !SafeReadValue(g_dungeon_base + kHardwareBackgroundSurfaceRva, &surface) || !surface) {
    const uint8_t action = native_input(entry);
    if (action == 99 || entry == kCircusPickerEntry) g_level_start_use_preset = false;
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

  if (pressed_tab) g_level_start_use_preset = !g_level_start_use_preset;
  if (clicked && kOriginalChoice.Contains(x, y)) g_level_start_use_preset = false;
  if (clicked && kPresetChoice.Contains(x, y)) g_level_start_use_preset = true;

  // Draw before the retail function draws the cursor and presents the page.
  api.font(0x248Fu);
  DrawLevelLoadoutChoice(kLoadoutArea, 180, "Starting loadout");
  DrawLevelLoadoutChoice(kOriginalChoice, 204, g_level_start_use_preset ? "Original" : "[Original]");
  DrawLevelLoadoutChoice(kPresetChoice, 204, g_level_start_use_preset ? "[Level preset]" : "Level preset");
  api.font(0x1485u);
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
  g_circus_loadout_pending = use_preset && index + 1 == kCircusPickerEntry;
  using LevelStart = void(__cdecl*)(uint8_t, uint8_t);
  reinterpret_cast<LevelStart>(g_dungeon_base + kMenuLevelStart)(index, character);
}

void __cdecl HookFreshLevelInventorySnapshot() {
  if (std::exchange(g_circus_loadout_pending, false)) {
    const auto get_context = reinterpret_cast<void*(__cdecl*)()>(g_dungeon_base + kRecoveryContextRva);
    const auto* context = static_cast<const uint8_t*>(get_context());
    uint8_t level = 0;
    if (context && SafeReadValue(context + 5, &level) && level == kCircusLevelId) {
      using Pickup = int(__cdecl*)(const uint8_t*);
      const auto pickup = reinterpret_cast<Pickup>(g_dungeon_base + kInventoryPickup);
      int granted = 0;
      for (const auto& item : kCircusLoadout) {
        // The retail pickup API reads ID, flags and quantity from this record.
        std::array<uint8_t, 9> record{};
        record[4] = item.id;
        record[8] = item.quantity;
        granted += pickup(record.data());
      }
      AppendDeathtrapSupportLog("support_level_start_loadout level=circus granted=%d", granted);
    }
  }
  // The engine captures this inventory for retrying from the beginning of the level.
  reinterpret_cast<KeyboardBindingMenuFn>(g_dungeon_base + kLevelInventorySnapshot)();
}

bool InstallLevelStartLoadoutHooks() {
  if (g_dungeon_image_variant != DungeonImageVariant::kEnglish ||
      !g_native50_menu_hooks_installed) return false;
  struct CallSite {
    uintptr_t site;
    uintptr_t native;
    uintptr_t hook;
  };
  const CallSite calls[] = {
      {0x10746u, kLevelPickerInput, reinterpret_cast<uintptr_t>(&HookLevelPickerInput)},
      {0x10C07u, kMenuLevelStart, reinterpret_cast<uintptr_t>(&HookMenuLevelStart)},
      {0x80723u, kLevelInventorySnapshot, reinterpret_cast<uintptr_t>(&HookFreshLevelInventorySnapshot)},
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
  AppendDeathtrapSupportLog("support_level_start_loadout state=active level=circus");
  return true;
}
