#pragma once

#include <array>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>
#include <unordered_map>

#include "modern_control_settings.h"

namespace deathtrap::menu {
constexpr int kWidth = 640, kHeight = 480;
constexpr size_t kPixels = kWidth * kHeight;
using Canvas = std::array<uint8_t, kPixels>;
using Palette = std::array<std::array<uint8_t, 3>, 256>;
struct Image { Canvas pixels{}; Palette palette{}; };
struct Rect {
  int x, y, width, height;
  constexpr bool Contains(int px, int py) const {
    return px >= x && py >= y && px < x + width && py < y + height;
  }
  constexpr bool Intersects(Rect other) const {
    return x<other.x+other.width && x+width>other.x &&
           y<other.y+other.height && y+height>other.y;
  }
};
// Reserve actual DRAWN captions too, not only native object click regions.
// Opaque page restoration must never erase Sounds/Graphics while hovering.
constexpr std::array<Rect,4> kNativeItemCaptions{{
    {16,208,144,40},{264,256,96,40},{112,312,136,40},{456,288,152,40}}};
constexpr Rect kBook{250,300,124,134};
constexpr Rect kBookLabel{235,440,155,34};
constexpr size_t kBookFrames = 16;
constexpr Rect kBack{520, 426, 120, 54};
constexpr int kPageRows = static_cast<int>(settings::ControlRow::kCount);
constexpr int kRowsPerPage = 6;
constexpr Rect kPreviousPage{134,78,132,25}, kNextPage{298,78,142,25};
constexpr int PageFirst(int selected) { return selected / kRowsPerPage * kRowsPerPage; }
constexpr int kRowTop = 112, kRowHeight = 26;
// Keep the upper value column left of the candle, rather than pushing the
// entire page below it. Descriptions/help have their own quieter text style.
constexpr int kTextLeft = 134, kTextRight = 440;
constexpr int kDescriptionTop = 280, kSavedTop = 341, kHelpTop = 374;
constexpr int RowAt(int x, int y) {
  return Rect{118, kRowTop, kTextRight - 118, kRowsPerPage * kRowHeight}.Contains(x, y)
      ? (y - kRowTop) / kRowHeight : -1;
}
inline uint16_t U16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}
// Only the game's bounded, one-plane 640x480 8-bit PCX files are accepted.
inline bool DecodePcx(const std::vector<uint8_t>& file, Image* image) {
  if (!image || file.size() < 128 + 769 || file[0] != 10 || file[2] != 1 ||
      file[3] != 8 || file[65] != 1 || file[file.size() - 769] != 12 ||
      U16(file.data() + 8) < U16(file.data() + 4) ||
      U16(file.data() + 10) < U16(file.data() + 6) ||
      U16(file.data() + 8) - U16(file.data() + 4) + 1 != kWidth ||
      U16(file.data() + 10) - U16(file.data() + 6) + 1 != kHeight) return false;
  const unsigned stride = U16(file.data() + 66);
  if (stride < kWidth || stride > 4096) return false;
  Image decoded;
  size_t offset = 128;
  const size_t end = file.size() - 769;
  for (int y = 0; y < kHeight; ++y) {
    unsigned x = 0;
    while (x < stride) {
      if (offset >= end) return false;
      uint8_t value = file[offset++];
      unsigned count = 1;
      if ((value & 0xC0) == 0xC0) {
        count = value & 0x3F;
        if (!count || offset >= end) return false;
        value = file[offset++];
      }
      if (count > stride - x) return false;
      for (unsigned i = 0; i < count; ++i, ++x) {
        if (x < kWidth) decoded.pixels[y * kWidth + x] = value;
      }
    }
  }
  std::memcpy(decoded.palette.data(), file.data() + end + 1, 768);
  *image = decoded;
  return true;
}
inline uint8_t Closest(const Palette& palette, unsigned r, unsigned g, unsigned b) {
  unsigned best = UINT32_MAX;
  uint8_t result = 0;
  for (unsigned i = 0; i < 256; ++i) {
    const int dr = static_cast<int>(r) - palette[i][0];
    const int dg = static_cast<int>(g) - palette[i][1];
    const int db = static_cast<int>(b) - palette[i][2];
    const unsigned distance = 2 * dr * dr + 3 * dg * dg + db * db;
    if (distance < best) { best = distance; result = static_cast<uint8_t>(i); }
  }
  return result;
}
inline bool TintSecondaryFont(std::vector<uint8_t>* font, const Palette& palette) {
  if (!font || font->size()<8) return false;
  text::RetailFontHeader header{};
  std::memcpy(&header,font->data(),sizeof(header));
  if (header.signature!=text::kRetailFontSignature || !header.glyph_count ||
      header.glyph_count>text::kMaximumFontGlyphs || 8+header.glyph_count*8>font->size()) return false;
  const auto original=*font;
  const auto* glyphs=reinterpret_cast<const text::RetailFontGlyph*>(original.data()+8);
  const uint8_t ink=Closest(palette,88,65,45),outline=Closest(palette,40,29,20);
  for (size_t c=0;c<header.glyph_count;++c) {
    const auto& glyph=glyphs[c];
    const size_t end=static_cast<size_t>(glyph.pixels)+glyph.width*glyph.height;
    if (end>font->size() || (glyph.width && glyph.height &&
        glyph.pixels<8+header.glyph_count*8)) return false;
    for (size_t p=glyph.pixels;p<end;++p) {
      const uint8_t source=original[p];
      if (!source) continue;
      const auto& color=palette[source];
      // Preserve all original glyphs, accents, metrics and their outline.
      // Recolor a private copy; neither the shared font nor the page palette
      // changes. Built-in 0x2490 is olive under this scroll's palette.
      (*font)[p]=(static_cast<unsigned>(color[0])+color[1]+color[2]>96) ? ink : outline;
    }
  }
  return true;
}
inline Canvas Remap(const Image& source, const Palette& destination) {
  std::array<uint8_t, 256> map{};
  for (size_t i = 0; i < map.size(); ++i) {
    map[i] = Closest(destination, source.palette[i][0], source.palette[i][1],
                     source.palette[i][2]);
  }
  Canvas result;
  for (size_t i = 0; i < result.size(); ++i) result[i] = map[source.pixels[i]];
  return result;
}
inline void MakeSettingsPage(const Image& paper, const Palette& palette, Canvas* destination) {
  if (!destination) return;
  // Write into the heap-owned page directly. Returning multiple 300 KiB
  // canvases by value can exhaust the retail game's small x86 menu stack.
  auto& result = *destination;
  std::array<uint8_t, 256> map{};
  const bool same_palette = paper.palette == palette;
  for (size_t i = 0; i < map.size(); ++i) {
    map[i] = same_palette ? static_cast<uint8_t>(i) :
        Closest(palette, paper.palette[i][0], paper.palette[i][1], paper.palette[i][2]);
  }
  for (size_t i = 0; i < result.size(); ++i) result[i] = map[paper.pixels[i]];
  // Remove the baked retail title using a continuous wood gradient between
  // clean margins of this SAME roller. Repeating a short strip produced bands
  // and could copy fragments of localized lettering into the replacement.
  constexpr Rect title{138, 10, 332, 46};
  for (int y = title.y; y < title.y + title.height; ++y) {
    std::array<unsigned, 3> left{}, right{};
    for (int sample = 0; sample < 12; ++sample) {
      const auto& a = paper.palette[paper.pixels[y * kWidth + 116 + sample]];
      const auto& b = paper.palette[paper.pixels[y * kWidth + 478 + sample]];
      for (size_t c = 0; c < 3; ++c) { left[c] += a[c]; right[c] += b[c]; }
    }
    for (int x = title.x; x < title.x + title.width; ++x) {
      const int edge = std::min(x - title.x, title.x + title.width - 1 - x);
      const unsigned alpha = static_cast<unsigned>(std::min(edge, 10) * 255 / 10);
      const unsigned position = static_cast<unsigned>(x - title.x);
      const auto& original = paper.palette[paper.pixels[y * kWidth + x]];
      std::array<unsigned, 3> color{};
      for (size_t c = 0; c < 3; ++c) {
        const unsigned wood = (left[c] * (title.width - 1 - position) +
            right[c] * position) / (12 * (title.width - 1));
        color[c] = (wood * alpha + original[c] * (255 - alpha) + 127) / 255;
      }
      result[y * kWidth + x] = Closest(palette, color[0], color[1], color[2]);
    }
  }
}
inline bool PaintSettingsHeading(const uint8_t* font, size_t size,
                                  const Palette& palette, Canvas* page,
                                  const char* title = "Native 50") {
  // The scroll headings use the same chancery letterforms as the native menu
  // text, enlarged and embossed. Read those original glyphs locally; do not
  // substitute a system font or distribute any retail font bitmap.
  if (!font || !page || size < 8) return false;
  text::RetailFontHeader header{};
  std::memcpy(&header, font, sizeof(header));
  if (header.signature != text::kRetailFontSignature || header.glyph_count < 95 ||
      header.glyph_count > text::kMaximumFontGlyphs || 8 + header.glyph_count * 8 > size) return false;
  const auto* glyphs = reinterpret_cast<const text::RetailFontGlyph*>(font + 8);
  int width = 0;
  if (!title) return false;
  for (const auto* p = reinterpret_cast<const unsigned char*>(title); *p; ++p) {
    const unsigned char c = *p;
    if (c < 32 || static_cast<unsigned>(c - 32) >= header.glyph_count) return false;
    const auto& glyph = glyphs[c - 32];
    if (glyph.advance_x < 0 || static_cast<size_t>(glyph.pixels) + glyph.width * glyph.height > size)
      return false;
    width += glyph.advance_x;
  }
  constexpr int source_height = 24;
  if (width < 1 || width > 256) return false;
  std::vector<uint8_t> source(static_cast<size_t>(width) * source_height);
  int pen = 0;
  for (const auto* p = reinterpret_cast<const unsigned char*>(title); *p; ++p) {
    const unsigned char c = *p;
    const auto& glyph = glyphs[c - 32];
    for (int y = 0; y < glyph.height; ++y) for (int x = 0; x < glyph.width; ++x) {
      const int px = pen + glyph.offset_x + x, py = glyph.offset_y + y;
      if (px >= 0 && px < width && py >= 0 && py < source_height &&
          font[glyph.pixels + y * glyph.width + x]) source[py * width + px] = 255;
    }
    pen += glyph.advance_x;
  }
  constexpr double scale = 1.7;
  const int heading_width = static_cast<int>(std::lround(width * scale));
  const int heading_height = static_cast<int>(source_height * scale);
  std::vector<uint8_t> coverage(static_cast<size_t>(heading_width) * heading_height);
  const auto sample = [&](int x, int y) -> unsigned {
    return x >= 0 && x < width && y >= 0 && y < source_height ? source[y * width + x] : 0;
  };
  for (int y = 0; y < heading_height; ++y) for (int x = 0; x < heading_width; ++x) {
    const double sx = (x + 0.5) / scale - 0.5, sy = (y + 0.5) / scale - 0.5;
    const int ix = static_cast<int>(std::floor(sx)), iy = static_cast<int>(std::floor(sy));
    const double fx = sx - ix, fy = sy - iy;
    coverage[y * heading_width + x] = static_cast<uint8_t>(std::lround(
        sample(ix, iy) * (1-fx) * (1-fy) + sample(ix+1, iy) * fx * (1-fy) +
        sample(ix, iy+1) * (1-fx) * fy + sample(ix+1, iy+1) * fx * fy));
  }
  const auto alpha = [&](int x, int y) -> unsigned {
    return x >= 0 && x < heading_width && y >= 0 && y < heading_height
        ? coverage[y * heading_width + x] : 0;
  };
  const int left = 300 - heading_width / 2, top = 12;
  const auto blend = [&](int x, int y, unsigned ink, unsigned a) {
    const size_t at = (top + y) * kWidth + left + x;
    const auto& bg = palette[(*page)[at]];
    (*page)[at] = Closest(palette, (ink*a+bg[0]*(255-a))/255,
        (ink*a+bg[1]*(255-a))/255, (ink*a+bg[2]*(255-a))/255);
  };
  for (int y = 0; y < heading_height; ++y) for (int x = 0; x < heading_width; ++x)
    if (alpha(x,y)) blend(x+2,y+2,12,alpha(x,y)*3/4);
  for (int y = 0; y < heading_height; ++y) for (int x = 0; x < heading_width; ++x) {
    const unsigned a = alpha(x,y);
    if (!a) continue;
    const int bevel = (static_cast<int>(alpha(x+1,y+1)) - static_cast<int>(alpha(x-1,y-1))) * 105 / 255;
    blend(x,y,static_cast<unsigned>(std::clamp(117+bevel,20,225)),a);
  }
  return true;
}
inline bool MakeBookFrames(const uint8_t* rgba, size_t size, const Image& table,
                            std::array<Canvas, kBookFrames>* frames) {
  constexpr size_t bytes=kBook.width*kBook.height*4;
  constexpr size_t mask_bytes=kBook.width*kBook.height;
  if (!rgba || !frames || size!=12+kBookFrames*bytes+mask_bytes || std::memcmp(rgba,"N50R",4) ||
      U16(rgba+4)!=kBook.width || U16(rgba+6)!=kBook.height ||
      U16(rgba+8)!=kBookFrames || U16(rgba+10)>=kBookFrames) return false;
  // Whole painted sprites: cover, separate leaves, spine and page block are
  // preserved together. No flat-cover mesh, invented paper polygon or warping.
  std::unordered_map<uint32_t,uint8_t> cache;
  cache.reserve(65536);
  const auto closest=[&](unsigned r,unsigned g,unsigned b) {
    // Cache exact RGB, not a 5-bit bucket whose first color contaminated all
    // nearby shades and caused additional banding on leather and paper.
    const uint32_t key=(r<<16)|(g<<8)|b;
    const auto found=cache.find(key);
    if (found!=cache.end()) return found->second;
    const uint8_t value=Closest(table.palette,r,g,b);
    cache.emplace(key,value);
    return value;
  };
  const size_t reference=U16(rgba+10);
  const auto* mask=rgba+12+kBookFrames*bytes;
  for (size_t f=0;f<kBookFrames;++f) {
    auto& canvas=(*frames)[f];canvas=table.pixels;
    for (int y=0;y<kBook.height;++y) for (int x=0;x<kBook.width;++x) {
      const auto* p=rgba+12+f*bytes+(y*kBook.width+x)*4;
      const auto* fixed=rgba+12+reference*bytes+(y*kBook.width+x)*4;
      const unsigned mix=mask[y*kBook.width+x];
      // Feather the layer seam in premultiplied RGBA BEFORE palette mapping.
      // A 255 mask still selects exactly the same immutable body texel.
      const unsigned moving_alpha=p[3]*(255-mix),fixed_alpha=fixed[3]*mix;
      const unsigned weight=moving_alpha+fixed_alpha;
      if (!weight) continue;
      const unsigned a=(weight+127)/255;
      const unsigned red=(p[0]*moving_alpha+fixed[0]*fixed_alpha+weight/2)/weight;
      const unsigned green=(p[1]*moving_alpha+fixed[1]*fixed_alpha+weight/2)/weight;
      const unsigned blue=(p[2]*moving_alpha+fixed[2]*fixed_alpha+weight/2)/weight;
      const size_t index=(kBook.y+y)*kWidth+kBook.x+x;
      const auto& bg=table.palette[table.pixels[index]];
      canvas[index]=closest((red*a+bg[0]*(255-a))/255,
          (green*a+bg[1]*(255-a))/255,(blue*a+bg[2]*(255-a))/255);
    }
  }
  // A single unchanging body layer, not merely an aligned anchor. Generated
  // textures/silhouettes differ between poses; replace the WHOLE spine and
  // lower board (including transparent outer edges) with this reference.
  for (size_t f=0;f<kBookFrames;++f) if (f!=reference) {
    for (int y=0;y<kBook.height;++y) for (int x=0;x<kBook.width;++x) {
      if (mask[y*kBook.width+x]==255) {
        const size_t i=(kBook.y+y)*kWidth+kBook.x+x;
        (*frames)[f][i]=(*frames)[reference][i];
      }
    }
  }
  return true;
}
inline bool MakeBookLabel(const uint8_t* rgba,size_t size,const Image& table,Canvas* label) {
  constexpr size_t bytes=kBookLabel.width*kBookLabel.height*4;
  if (!rgba || !label || size!=8+bytes || std::memcmp(rgba,"N50L",4) ||
      U16(rgba+4)!=kBookLabel.width || U16(rgba+6)!=kBookLabel.height) return false;
  *label=table.pixels;
  for (int y=0;y<kBookLabel.height;++y) for (int x=0;x<kBookLabel.width;++x) {
    const auto* p=rgba+8+(y*kBookLabel.width+x)*4;
    if (!p[3]) continue;
    const size_t index=(kBookLabel.y+y)*kWidth+kBookLabel.x+x;
    const auto bg=table.palette[table.pixels[index]];
    const unsigned a=p[3];
    (*label)[index]=Closest(table.palette,(p[0]*a+bg[0]*(255-a))/255,
        (p[1]*a+bg[1]*(255-a))/255,(p[2]*a+bg[2]*(255-a))/255);
  }
  return true;
}
inline uint8_t ResolveSetupAction(uint8_t action,bool book_or_caption_hovered) {
  // Hardware's retail hitbox includes unused wood below the book. Only this
  // new object/caption owns its exact rectangles; real Hardware/Back stay native.
  return book_or_caption_hovered && (action==4 || action==8) ? uint8_t{0} : action;
}
struct BookAnimation {
  unsigned frame = 0;
  uint64_t last_ms = 0;
  double position = 0;
  void Advance(bool hovered, uint64_t now) {
    if (!last_ms || now<last_ms) { last_ms=now; return; }
    const double step=std::min<uint64_t>(now-last_ms,100)/460.0;
    position=std::clamp(position+(hovered ? step : -step),0.0,1.0);
    const double eased=position*position*(3-2*position);
    frame=static_cast<unsigned>(std::lround(eased*(kBookFrames-1)));
    last_ms=now;
  }
};
struct Navigation {
  int selected = 0;
  bool mouse_latched = true, key_latched = true, escape_latched = true;
  uint16_t previous_x = UINT16_MAX, previous_y = UINT16_MAX;
  // Only edges act. A stationary cursor does not steal keyboard focus.
  int Poll(uint16_t x, uint16_t y, bool click, bool up, bool down,
           bool left, bool right, bool accept, bool escape) {
    const int local = RowAt(x,y);
    const int hovered = local < 0 || PageFirst(selected)+local >= kPageRows ? -1 : PageFirst(selected)+local;
    if ((x != previous_x || y != previous_y) && hovered >= 0) selected = hovered;
    previous_x = x; previous_y = y;
    const bool key = up || down || left || right || accept;
    int action = 0; // 1 toggle, 2 false, 3 true, 4 back.
    if (escape && !escape_latched) action = 4;
    else if (click && !mouse_latched) {
      if (kBack.Contains(x, y)) action = 4;
      else if (kPreviousPage.Contains(x,y)) selected = PageFirst(selected) ? 0 : kRowsPerPage;
      else if (kNextPage.Contains(x,y)) selected = PageFirst(selected) ? 0 : kRowsPerPage;
      else if (hovered >= 0) { selected = hovered; action = 1; }
    } else if (key && !key_latched && !click) {
      if (up != down) selected = (selected + (down ? 1 : kPageRows-1)) % kPageRows;
      else if (left != right) action = right ? 3 : 2;
      else if (accept) action = 1;
    }
    mouse_latched = click; key_latched = key; escape_latched = escape;
    return action;
  }
};
inline const char* Back(text::Language language) {
  switch (language) {
    case text::Language::kFrench: return "RETOUR";
    case text::Language::kGerman: return "ZUR\x9A" "CK";
    case text::Language::kItalian: return "INDIETRO";
    default: return "BACK";
  }
}
inline const char* Help(text::Language language) {
  switch (language) {
    case text::Language::kFrench: return "Fl\x8A" "ches / clic : modifier";
    case text::Language::kGerman: return "Pfeile / Klick: \x84ndern";
    case text::Language::kItalian: return "Frecce / clic: modifica";
    default: return "Arrows / click: change";
  }
}
inline const char* BackHelp(text::Language language) {
  switch (language) {
    case text::Language::kFrench: return "Echap : retour";
    case text::Language::kGerman: return "Esc: zur\x81" "ck";
    case text::Language::kItalian: return "Esc: indietro";
    default: return "Esc: back";
  }
}
inline const char* Saved(text::Language language) {
  switch (language) {
    case text::Language::kFrench: return "Sauvegarde automatique";
    case text::Language::kGerman: return "Automatisch gespeichert";
    case text::Language::kItalian: return "Salvataggio automatico";
    default: return "Saved automatically";
  }
}
inline const char* ModelStatus(text::Language language, bool available) {
  constexpr const char* missing[] = {"E3 model pack is missing.",
      "Pack E3 non install\x82.", "E3-Modellpaket fehlt.",
      "Pacchetto E3 mancante."};
  constexpr const char* restart[] = {"Saved. Restart to apply.",
      "Enregistr\x82. Relancer le jeu.", "Gespeichert. Neustart n\x94tig.",
      "Salvato. Riavvia il gioco."};
  const auto index=static_cast<size_t>(language);
  return (available ? restart : missing)[index<4 ? index : 0];
}
inline const char* ModelLiveStatus(text::Language language) {
  constexpr const char* messages[]={"Applied and saved.",
      "Appliqu\x82 et sauvegard\x82.",
      "Aktiv und gespeichert.","Applicato e salvato."};
  const auto index=static_cast<size_t>(language);return messages[index<4?index:0];
}
inline const char* Description(text::Language language, int row, bool live_model=false) {
  if (row==4 && live_model) {
    constexpr const char* messages[]={"Switch between the original and E3 heroine immediately.",
        "Changer de mod\x8Ale original ou E3 imm\x82\x64iatement.",
        "Sofort zwischen Original- und E3-Modell wechseln.","Passa subito tra eroina originale ed E3."};
    const auto index=static_cast<size_t>(language);return messages[index<4?index:0];
  }
  if (row >= 6 && row < kPageRows) {
    constexpr const char* extra[4][3] = {
      {"Slow the world during F1-F4 or pad selection.",
       "25%: quarter speed. UI and camera remain fluid.",
       "Health, items, counters, messages and selectors."},
      {"Ralentir le monde pendant la s\x82lection.",
       "25% : quart de vitesse. Cam\x82ra fluide.",
       "Vie, objets, compteurs, messages et s\x82lection."},
      {"Spielwelt bei der Auswahl verlangsamen.",
       "25%: Vierteltempo. Kamera bleibt fl\x81ssig.",
       "Leben, Objekte, Z\x84hler, Meldungen und Auswahl."},
      {"Rallenta il mondo durante la selezione.",
       "25%: un quarto. Telecamera sempre fluida.",
       "Vita, oggetti, contatori, messaggi e selettori."}};
    const auto lang=static_cast<size_t>(language);return extra[lang<4?lang:0][row-6];
  }
  constexpr const char* messages[4][6] = {
    {"Modern: camera-relative. Tank: character-relative.",
     "Reverse horizontal movement of the modern camera.",
     "Reverse vertical movement of the modern camera.",
     "Keep normal enemy bodies while space is available.",
     "Original or E3 heroine. Restart the game to apply.",
     "Show body wounds. Damage stays the same."},
    {"Moderne : cam\x82ra. Classique : personnage.",
     "Inverser le mouvement horizontal de la cam\x82ra.",
     "Inverser le mouvement vertical de la cam\x82ra.",
     "Conserve les corps tant qu'il reste de la place.",
     "H\x82ro\x8Bne originale ou E3. Relancer le jeu pour appliquer.",
     "Blessures visibles. D\x82g\x83ts inchang\x82s."},
    {"Modern: Kamera. Klassisch: Blick der Spielfigur.",
     "Horizontale Bewegung der modernen Kamera umkehren.",
     "Vertikale Bewegung der modernen Kamera umkehren.",
     "Leichen bleiben, solange Platz vorhanden ist.",
     "Original- oder E3-Modell. Das Spiel neu starten.",
     "Wunden anzeigen. Schaden unver\x84ndert."},
    {"Moderno: telecamera. Classico: personaggio.",
     "Inverti il movimento orizzontale della telecamera.",
     "Inverti il movimento verticale della telecamera.",
     "Conserva i corpi finch\x82 rimane spazio.",
     "Eroina originale o E3. Riavvia il gioco per applicare.",
     "Mostra ferite. Danni invariati."}};
  const size_t lang = static_cast<size_t>(language);
  return messages[lang < 4 ? lang : 0][row >= 0 && row < 6 ? row : 0];
}
} // namespace deathtrap::menu
