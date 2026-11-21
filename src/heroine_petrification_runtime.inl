// The original effect paints the mesh attached to the animation node. The
// visible mesh can be a different resource when the model or wounds are
// switched. Apply the native sweep to the mesh we actually submit.
constexpr uint32_t kHeroineStonePage = 5975;
constexpr uint32_t kIronManStonePage = 5973;
constexpr size_t kPrtFaceStride = 52;
constexpr size_t kPrtFaceMaterialOffset = 0x18;
constexpr size_t kPrtFaceColorOffset = 0x1A;
constexpr size_t kPrtFaceTextureOffset = 0x24;
constexpr size_t kStonePageSize = 65536;
using PaintStoneMesh = void(__cdecl*)(uintptr_t, void*, uint32_t, int32_t, int32_t);
PaintStoneMesh g_native_paint_stone_mesh = nullptr;
const uint8_t* g_stone_palette = nullptr;

struct HeroineStoneSweep {
  uintptr_t player = 0;
  uintptr_t root = 0;
  int32_t lower = 0;
  int32_t upper = 0;
  uint32_t texture = 0;
};
HeroineStoneSweep g_heroine_stone_sweep;

uintptr_t PetrifyingPlayer() {
  uintptr_t player = 0, entity = 0, callback = 0;
  if (!ReadPlayerObject(&player) ||
      !SafeReadValue(reinterpret_cast<void*>(player + kEntityDataOffset), &entity) || !entity ||
      !SafeReadValue(reinterpret_cast<void*>(entity + 0x1C), &callback) ||
      callback != reinterpret_cast<uintptr_t>(g_dungeon_base + ActiveDungeonCodeRva(0x83A10)))
    return 0;
  return player;
}

bool HeroineStoneDrawActive(uintptr_t node) {
  if (!g_heroine_stone_sweep.player) return false;
  const auto player = PetrifyingPlayer();
  if (!player) {
    g_heroine_stone_sweep = {};
    return false;
  }
  return player == g_heroine_stone_sweep.player &&
         g_heroine_stone_sweep.root == ResolveLivePlayerRenderRoot() &&
         IsPlayerVisualNode(node);
}

void __cdecl HookHeroineStonePaint(uintptr_t node, void* mesh, uint32_t texture,
                                  int32_t lower, int32_t upper) {
  uint32_t id = 0;
  if ((texture == kHeroineStonePage || texture == kIronManStonePage) &&
      SafeReadValue(reinterpret_cast<void*>(node + kRenderResourceHandleOffset), &id) &&
      IsPlayerVisualNode(node)) {
    if (const auto player = PetrifyingPlayer()) {
      g_heroine_stone_sweep = {player, ResolveLivePlayerRenderRoot(), lower, upper, texture};
      if (HeroineMeshIndex(id) >= 0 && texture == kHeroineStonePage) return;
    }
  }
  g_native_paint_stone_mesh(node, mesh, texture, lower, upper);
}

struct StonePrtFaces {
  uint32_t count = 0;
  uint8_t* first = nullptr;
};

StonePrtFaces ReadStonePrtFaces(void* mesh) {
  StonePrtFaces faces;
  if (!mesh || !SafeReadValue(static_cast<uint8_t*>(mesh) + 0x18, &faces.count) ||
      !faces.count || faces.count > 65536 ||
      !SafeReadValue(static_cast<uint8_t*>(mesh) + 0x1C, &faces.first) || !faces.first)
    return {};
  return faces;
}

std::array<uint8_t, 256> StonePaletteIndices(const std::array<uint8_t, 768>& palette) {
  std::array<uint8_t, 256> indices{};
  for (size_t i = 1; i < indices.size(); ++i) {
    const auto* rgb = palette.data() + i * 3;
    const auto [darkest, brightest] = std::minmax({rgb[0], rgb[1], rgb[2]});
    indices[i] = static_cast<uint8_t>(4 + (darkest + brightest) / 64);
  }
  return indices;
}

void WarmHeroineStonePages(void* mesh) {
  if (!g_dual_heroine_load || !g_stone_palette) return;
  const auto faces = ReadStonePrtFaces(mesh);
  std::array<bool, 32> visited{};
  for (uint32_t i = 0; i < faces.count; ++i) {
    const auto* face = faces.first + i * kPrtFaceStride;
    uint8_t flags = 0;
    uint32_t source = 0;
    if (!SafeReadValue(face + kPrtFaceMaterialOffset, &flags) || !(flags & 1) ||
        !SafeReadValue(face + kPrtFaceTextureOffset, &source)) continue;
    const auto stone = HeroineStoneTexture(source);
    if (!stone) continue;
    const size_t index = source - g_dual_heroine_pack.e3_texture_first;
    if (index >= visited.size() || visited[index] || LoadedHeroineResource(stone))
      continue;
    visited[index] = true;
    std::array<uint8_t, 768> palette{};
    if (!SafeRead(g_stone_palette, palette.data(), palette.size())) return;
    const auto indices = StonePaletteIndices(palette);
    auto* page = static_cast<uint8_t*>(g_dual_heroine_load(stone, 0x41000002u,
                                                         "Native 50 stone texture"));
    if (page) {
      for (size_t pixel = 0; pixel < kStonePageSize; ++pixel) page[pixel] = indices[page[pixel]];
    }
  }
}

struct StoneFaceMaterial {
  uint32_t texture = 0;
  uint8_t color = 0;
  uint8_t flags = 0;
};

struct HeroineStoneMaterials {
  StonePrtFaces faces;
  std::vector<StoneFaceMaterial> original;

  bool Capture(void* mesh) {
    faces = ReadStonePrtFaces(mesh);
    if (!faces.count) return false;
    original.resize(faces.count);
    for (uint32_t i = 0; i < faces.count; ++i) {
      const auto* face = faces.first + i * kPrtFaceStride;
      auto& material = original[i];
      if (!SafeReadValue(face + kPrtFaceColorOffset, &material.color) ||
          !SafeReadValue(face + kPrtFaceMaterialOffset, &material.flags) ||
          !SafeReadValue(face + kPrtFaceTextureOffset, &material.texture))
        return false;
    }
    return true;
  }

  void RemapPaintedPages() const {
    for (uint32_t i = 0; i < faces.count; ++i) {
      const auto& material = original[i];
      if (!(material.flags & 1)) continue;
      const auto stone = HeroineStoneTexture(material.texture);
      if (!stone) continue;
      auto* texture = faces.first + i * kPrtFaceStride + kPrtFaceTextureOffset;
      uint32_t painted = 0;
      if (!SafeReadValue(texture, &painted) || painted != kHeroineStonePage) continue;
      const auto visible = LoadedHeroineResource(stone) ? stone : material.texture;
      SafeWrite(texture, &visible, sizeof(visible));
    }
  }

  void Restore() const {
    for (uint32_t i = 0; i < faces.count; ++i) {
      auto* face = faces.first + i * kPrtFaceStride;
      const auto& material = original[i];
      SafeWrite(face + kPrtFaceColorOffset, &material.color, sizeof(material.color));
      SafeWrite(face + kPrtFaceTextureOffset, &material.texture, sizeof(material.texture));
    }
  }
};

int CallStoneDrawAndRestore(void* mesh, uintptr_t node,
                            const HeroineStoneMaterials& materials) {
  int result = 0;
  __try {
    g_native_paint_stone_mesh(node, mesh, g_heroine_stone_sweep.texture,
                            g_heroine_stone_sweep.lower, g_heroine_stone_sweep.upper);
    materials.RemapPaintedPages();
    result = g_original_heroine_prt_draw(mesh);
  } __finally {
    materials.Restore();
  }
  return result;
}

int DrawHeroineMesh(void* mesh, uintptr_t node) {
  if (g_native_paint_stone_mesh && HeroineStoneDrawActive(node)) {
    HeroineStoneMaterials materials;
    if (materials.Capture(mesh)) return CallStoneDrawAndRestore(mesh, node, materials);
  }
  return g_original_heroine_prt_draw(mesh);
}

bool InstallHeroineStonePaint() {
  if (g_native_paint_stone_mesh) return true;
  auto* paint = g_dungeon_base + ActiveDungeonCodeRva(0x844C0);
  const auto* palette_reference = g_dungeon_base + ActiveDungeonCodeRva(0x83DA3);
  const std::array<uint8_t, 8> paint_prefix{0x8B, 0x44, 0x24, 0x08, 0x53, 0x56, 0x57, 0x55};
  const std::array<uint8_t, 3> palette_prefix{0x8D, 0x94, 0x49};
  std::array<uint8_t, 8> paint_bytes{};
  std::array<uint8_t, 3> palette_bytes{};
  const uint8_t* palette = nullptr;
  if (!SafeRead(paint, paint_bytes.data(), paint_bytes.size()) || paint_bytes != paint_prefix ||
      !SafeRead(palette_reference, palette_bytes.data(), palette_bytes.size()) ||
      palette_bytes != palette_prefix || !SafeReadValue(palette_reference + 3, &palette) ||
      MH_CreateHook(paint, reinterpret_cast<void*>(&HookHeroineStonePaint),
                    reinterpret_cast<void**>(&g_native_paint_stone_mesh)) != MH_OK)
    return false;
  if (MH_EnableHook(paint) != MH_OK) {
    MH_RemoveHook(paint);
    g_native_paint_stone_mesh = nullptr;
    return false;
  }
  g_stone_palette = palette;
  AppendDeathtrapSupportLog("support_heroine_petrification state=ready");
  return true;
}
