#include <windows.h>
#include <filesystem>
#include <iostream>
#include <memory>
#include <fstream>

#include "../src/deathtrap_native_render_patch.cc"

namespace {
void Require(bool condition, const char* message) {
  if (!condition) { std::cerr << "Native 50 menu: " << message << '\n'; std::exit(1); }
}
void CheckNavigation() {
  using namespace deathtrap::menu;
  Navigation n;
  Require(n.Poll(400, kRowTop, true, false, false, false, false, true, true) == 0,
          "entry press must not leak into options or Back");
  n.Poll(400, kRowTop, false, false, false, false, false, false, false);
  Require(n.Poll(400, kRowTop, true, false, false, false, false, false, false) == 1,
          "released click must toggle");
  Require(n.Poll(400, kRowTop+(kRowsPerPage-1)*kRowHeight, true, false, false, false, false, false, false) == 0,
          "held click must not toggle another row");
  n.Poll(0, 0, false, false, false, false, false, false, false);
  Require(n.Poll(0, 0, false, false, true, false, false, false, false) == 0 && n.selected == 6,
          "keyboard must wrap without stationary cursor stealing focus");
  n.Poll(0, 0, false, false, false, false, false, false, false);
  Require(n.Poll(0, 0, false, false, false, false, true, false, false) == 3,
          "Right must explicitly enable");
  Require(n.Poll(0, 0, false, false, false, false, true, false, false) == 0,
          "held arrow must not repeatedly change values");
  n.Poll(0, 0, false, false, false, false, false, false, false);
  Require(n.Poll(550, 445, true, false, false, false, false, false, false) == 4,
          "mouse/controller Back must leave the custom page");
  n.Poll(0,0,false,false,false,false,false,false,false);
  Require(n.Poll(400,kRowTop+2*kRowHeight,true,false,false,false,false,false,false)==1 && n.selected==8,
      "second-page mouse row must address interface scale, not the first page");
  n.Poll(0,0,false,false,false,false,false,false,false);
  Require(n.Poll(400,kRowTop+4*kRowHeight,true,false,false,false,false,false,false)==0 && n.selected==8,
      "empty second-page rows must not change a setting");
  n.Poll(0,0,false,false,false,false,false,false,false);
  Require(n.Poll(430,85,true,false,false,false,false,false,false)==0 && n.selected==0,
      "page arrow changes pages without toggling a setting");
  BookAnimation a;
  for (uint64_t tick = 100; tick <= 700; tick += 20) a.Advance(true, tick);
  Require(a.frame == kBookFrames-1, "hover must open and stop at the final frame");
  for (uint64_t tick = 720; tick <= 1320; tick += 20) a.Advance(false, tick);
  Require(a.frame == 0, "leaving book must animate closed");
  BookAnimation slow,fast;
  slow.Advance(true,100); fast.Advance(true,100);
  for (uint64_t tick=120; tick<=400; tick+=20) fast.Advance(true,tick);
  for (uint64_t tick=200; tick<=400; tick+=100) slow.Advance(true,tick);
  Require(slow.frame==fast.frame,"opening speed must follow elapsed time, not polling frequency");
  const auto before=fast.position;
  fast.Advance(false,420);
  Require(fast.position<before && fast.position>0,"reverse must continue from the current pose");
  for (int y = kBook.y; y < kBook.y + kBook.height; ++y) {
    for (int x = kBook.x; x < kBook.x + kBook.width; ++x) {
      Require(!Rect{46,128,196,98}.Contains(x,y) && !Rect{246,98,118,174}.Contains(x,y) &&
                  !Rect{77,232,133,87}.Contains(x,y) && !Rect{375,129,265,297}.Contains(x,y),
              "book must not overlap a native item hitbox");
    }
  }
  Require(RowAt(100,180) == -1 && RowAt(400,kRowTop-1) == -1 &&
              RowAt(400,kRowTop+kRowsPerPage*kRowHeight) == -1 && RowAt(400,kRowTop) == 0 &&
              RowAt(400,kRowTop+kRowHeight) == 1 && RowAt(400,kRowTop+2*kRowHeight) == 2 &&
              RowAt(400,kRowTop+3*kRowHeight) == 3,
          "page row bounds");
  Require(kBookLabel.y>=kBook.y+kBook.height+4,
          "caption must be below the whole animation, with a visible gap");
  for (const auto caption : kNativeItemCaptions) {
    Require(!kBook.Intersects(caption) && !kBookLabel.Intersects(caption),
            "neither artwork nor its opaque erase rectangle may touch a native item caption");
  }
  Require(ResolveSetupAction(4,true)==0 && ResolveSetupAction(8,true)==0 &&
              ResolveSetupAction(4,false)==4 && ResolveSetupAction(99,true)==99 &&
              ResolveSetupAction(1,true)==1,
          "caption must own only its empty-wood hardware margin, never native items or Back");
  auto decoded = std::make_unique<Image>();
  Require(!DecodePcx({}, decoded.get()) && !MakeBookFrames(nullptr, 0, *decoded, nullptr),
          "malformed artwork must fail closed");
}

void CheckColorQuality() {
  using namespace deathtrap::menu;
  auto table=std::make_unique<Image>();
  table->palette[1]={100,60,20};table->palette[2]={103,60,20};
  table->palette[3]={200,60,20};table->palette[4]={150,60,20};table->palette[5]={100,30,10};
  constexpr size_t bytes=kBook.width*kBook.height*4,mask_bytes=kBook.width*kBook.height;
  std::vector<uint8_t> resource(12+kBookFrames*bytes+mask_bytes);
  std::memcpy(resource.data(),"N50R",4);
  const auto word=[&](size_t at,size_t value) {
    resource[at]=static_cast<uint8_t>(value&255);resource[at+1]=static_cast<uint8_t>(value>>8);
  };
  word(4,kBook.width);word(6,kBook.height);word(8,kBookFrames);word(10,8);
  const auto pixel=[&](size_t frame,int x,uint8_t red,uint8_t alpha=255) {
    const size_t at=12+frame*bytes+(20*kBook.width+x)*4;
    resource[at]=red;resource[at+1]=60;resource[at+2]=20;resource[at+3]=alpha;
  };
  for (size_t f=0;f<kBookFrames;++f) {
    pixel(f,60,f==1 ? 103 : 100);pixel(f,61,f==8 ? 200 : 100);
    pixel(f,62,f==8 ? 200 : 100);pixel(f,63,f==8 ? 200 : 0,f==8 ? 255 : 0);
  }
  const size_t mask_start=12+kBookFrames*bytes;
  resource[mask_start+20*kBook.width+61]=128;
  resource[mask_start+20*kBook.width+62]=255;
  resource[mask_start+20*kBook.width+63]=128;
  auto frames=std::make_unique<std::array<Canvas,kBookFrames>>();
  Require(MakeBookFrames(resource.data(),resource.size(),*table,frames.get()),"color quality fixture");
  const size_t at=(kBook.y+20)*640+kBook.x+60;
  Require((*frames)[0][at]==1 && (*frames)[1][at]==2,
          "distinct RGB shades in the same 5-bit bucket must not collapse");
  Require((*frames)[0][at+1]==4,"soft seam must blend before palette reduction");
  Require((*frames)[0][at+3]==5,"transparent seam must use premultiplied alpha, not a black fringe");
  for (const auto& frame : *frames) Require(frame[at+2]==3,"opaque body remains immutable");
}

unsigned copies = 0, polls = 0, flips = 0, sprites = 0, changes = 0;
std::vector<bool> background_transitions;
std::vector<std::string> events;
uint16_t mouse_x = 0, mouse_y = 0, buttons = 0;
std::array<bool, 256> keys{};
void __cdecl Mouse(uint16_t* x, uint16_t* y, uint16_t* b) { *x=mouse_x; *y=mouse_y; *b=buttons; }
int __cdecl Key(uint32_t key) { return key < keys.size() && keys[key]; }
void __cdecl Font(uint32_t font) { Require(font == 0x1485 || font == 0x1486 || font == 0x248F || font == 0x2490, "native font only"); }
int32_t __cdecl Width(const char* value) { return static_cast<int32_t>(std::strlen(value) * 8); }
void __cdecl Text(int x, int y, int, const char* value) {
  Require(x >= 0 && x + Width(value) <= 640 && y >= 0 && y < 480, "text must fit native canvas");
}
void Copy(const uint8_t*, deathtrap::menu::Rect rect) {
  Require(rect.x >= 0 && rect.y >= 0 && rect.x + rect.width <= 640 &&
              rect.y + rect.height <= 480, "copy must fit native canvas");
  ++copies; events.emplace_back("copy");
}
void __cdecl Sprite(const void*, int, int, int, int) { ++sprites; events.emplace_back("cursor"); }
void __cdecl Flip() { ++flips; }
bool Change(deathtrap::settings::ControlRow, int) { ++changes; return true; }
bool Background(bool page) { background_transitions.push_back(page); return true; }
uint8_t native_setup_action = 0;
unsigned native_setup_polls = 0;
uint8_t __cdecl SetupInput() { ++native_setup_polls; return native_setup_action; }
void __cdecl Poll() {
  Require(++polls < 20, "modal must have a working exit and input pump");
  keys.fill(false); buttons = 0;
  if (polls == 1) keys[0xCD] = true; // Right after initial release.
  if (polls == 3) keys[1] = true; // Escape after release.
}
void CheckRuntime() {
  std::vector<uint8_t> fixture(0x368000);
  g_dungeon_base = fixture.data();
  g_dungeon_image_variant = DungeonImageVariant::kEnglish;
  g_native50_menu_assets = new Native50MenuAssets;
  g_native50_menu_api = {&Mouse, &Key, &Font, &Text, &Width, &Copy, &Flip, &Poll, &Change, &Background};
  g_original_menu_sprite = &Sprite;
  std::array<uint8_t,84*84> cursor{};
  const uintptr_t cursor_pointer=reinterpret_cast<uintptr_t>(cursor.data());
  Require(SafeWrite(g_dungeon_base+kMenuCursorFramesRva,&cursor_pointer,sizeof(cursor_pointer)),
          "cursor frame fixture");
  g_native50_setup_input = false;
  HookNative50MenuSprite(nullptr,84,84,0,0);
  Require(copies == 0 && sprites == 1, "other menus must remain untouched");
  events.clear(); g_native50_setup_input = true; g_native50_setup_repaint_frames = 2;
  HookNative50MenuSprite(cursor.data(),84,84,0,0);
  Require(events.size() == 3 && events.back() == "cursor" && g_native50_setup_repaint_frames == 2,
          "book draws before the actual cursor; only a native frame may consume a page repaint");
  events.clear();HookNative50MenuSprite(fixture.data(),84,84,0,0);
  Require(events.size()==1 && events.front()=="cursor" && g_native50_setup_repaint_frames==2,
          "84x84 saved-background restoration must never be mistaken for drawing the cursor");
  g_native50_setup_active=true;g_original_setup_input=&SetupInput;
  events.clear();HookNative50SetupInput();
  Require(g_native50_setup_repaint_frames==1 && events.size()==1 && events.front()=="copy",
          "parent background must be restored once before native capture begins");
  HookNative50SetupInput();
  Require(g_native50_setup_repaint_frames==0,"two native frames must rebuild the two parent pages");
  native_setup_polls=0;g_native50_setup_active=false;
  RunNative50Page();
  Require(!g_native50_page_active && polls == 3 && flips == 3 && changes == 1 &&
              g_native50_setup_repaint_frames == 2 && g_native50_setup_return_latched,
          "modal must process live controls, return, and invalidate both setup pages");
  Require(background_transitions==std::vector<bool>({true,false}),
          "native scroll palette must be entered once and the Setup palette restored before return");
  g_native50_setup_active = true; g_original_setup_input = &SetupInput;
  native_setup_action = 99;
  for (int held=0;held<5;++held) {
    Require(HookNative50SetupInput()==0 && g_native50_setup_return_latched,
            "held Escape must not close the parent Setup menu");
  }
  keys.fill(false);
  Require(HookNative50SetupInput()==0 && !g_native50_setup_return_latched,
          "the release frame must consume the old native Back event too");
  keys[1]=true;
  Require(HookNative50SetupInput()==99,"a fresh Back press must still leave Setup normally");
  keys.fill(false); buttons=1; mouse_x=550; mouse_y=445;
  g_native50_setup_return_latched = true;
  for (int held=0;held<5;++held) {
    Require(HookNative50SetupInput()==0,"held mouse/controller Back must not leak to the parent");
  }
  buttons=0;
  Require(HookNative50SetupInput()==0 && !g_native50_setup_return_latched,
          "mouse Back release must consume one neutral parent frame");
  buttons=1;
  Require(HookNative50SetupInput()==99 && native_setup_polls==14,
          "native input/presentation must keep pumping during Back suppression");
  buttons=0; keys.fill(false); g_native50_setup_active=false; g_original_setup_input=nullptr;
  for (const auto variant : {DungeonImageVariant::kEnglish, DungeonImageVariant::kFrenchGog,
                             DungeonImageVariant::kGermanGog, DungeonImageVariant::kItalianGog}) {
    g_dungeon_image_variant = variant;
    for (int row=0; row<deathtrap::menu::kPageRows; ++row) {
      deathtrap::menu::Navigation n; n.selected=row; DrawNative50Page(n);
    }
  }
  delete g_native50_menu_assets; g_native50_menu_assets = nullptr;
  g_dungeon_base = nullptr;
}

// Execute the real retail input loop. Only its device/clock/blit boundaries
// are replaced. This preserves capture -> draw -> Flip -> restore exactly,
// including the second 84x84 blit that the old mocks did not exercise.
std::array<std::unique_ptr<deathtrap::menu::Canvas>,2> frame_pages;
unsigned frame_page=0,frame_flips=0,frame_books=0;
std::vector<std::string> loaded_backgrounds;
void PatchCode(uintptr_t rva,const void* bytes,size_t size) {
  auto* target=g_dungeon_base+rva;
  DWORD old=0;
  Require(VirtualProtect(target,size,PAGE_EXECUTE_READWRITE,&old)!=0,"test code protection");
  std::memcpy(target,bytes,size);FlushInstructionCache(GetCurrentProcess(),target,size);
  DWORD ignored=0;VirtualProtect(target,size,old,&ignored);
}
void PatchJump(uintptr_t english,const void* callback) {
  const uintptr_t rva=ActiveDungeonCodeRva(english);
  std::array<uint8_t,5> jump{{0xE9,0,0,0,0}};
  const uint32_t displacement=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(callback)-
      reinterpret_cast<uintptr_t>(g_dungeon_base+rva)-5);
  std::memcpy(jump.data()+1,&displacement,4);PatchCode(rva,jump.data(),jump.size());
}
void __cdecl RecordBackground(const char* file,void*) { loaded_backgrounds.emplace_back(file); }
void __cdecl FrameFlip() { ++frame_flips;frame_page^=1; }
void __cdecl FramePoll() {}
void FrameCopy(const uint8_t* source,deathtrap::menu::Rect rect) {
  for (int y=rect.y;y<rect.y+rect.height;++y)
    std::memcpy(frame_pages[frame_page]->data()+y*640+rect.x,source+y*640+rect.x,rect.width);
  if (rect.x==deathtrap::menu::kBook.x && rect.y==deathtrap::menu::kBook.y &&
      rect.width==deathtrap::menu::kBook.width && rect.height==deathtrap::menu::kBook.height) ++frame_books;
}
void __cdecl FrameCapture(const void* output,int width,int height,int x,int y) {
  // The retail wrappers explicitly mask their packed 16-bit stack arguments.
  width&=0xFFFF;height&=0xFFFF;x&=0xFFFF;y&=0xFFFF;
  Require(x>=0 && x+width<=640 && y>=0 && y+height<=480,"retail capture bounds");
  auto* destination=const_cast<uint8_t*>(static_cast<const uint8_t*>(output));
  for (int row=0;row<height;++row)
    std::memcpy(destination+row*width,frame_pages[frame_page]->data()+(y+row)*640+x,width);
}
void __cdecl FrameSprite(const void* input,int width,int height,int x,int y) {
  width&=0xFFFF;height&=0xFFFF;x&=0xFFFF;y&=0xFFFF;
  Require(input && x>=0 && x+width<=640 && y>=0 && y+height<=480,"retail sprite bounds");
  const auto* source=static_cast<const uint8_t*>(input);
  for (int row=0;row<height;++row) for (int col=0;col<width;++col) {
    const uint8_t ink=source[row*width+col];
    if (ink) (*frame_pages[frame_page])[(y+row)*640+x+col]=ink;
  }
}
void CheckNativeTransitions(const std::filesystem::path& game) {
  // Use the ORIGINAL path builder and its real initialized menu prefix. A
  // duplicated asylum\menu\ must fail here instead of looking fine in a BMP.
  wchar_t previous[MAX_PATH]{};Require(GetCurrentDirectoryW(MAX_PATH,previous)!=0,"test cwd");
  if (!SetCurrentDirectoryW(game.c_str())) {
    std::wcerr<<L"menu fixture directory: "<<game<<L" error="<<GetLastError()<<L'\n';
    Require(false,"local game directory");
  }
  constexpr char prefix[]="asylum\\menu\\";
  Require(SafeWrite(g_dungeon_base+kLocalizationTablePointerRva+0x8C,prefix,sizeof(prefix)),
          "original menu directory prefix");
  auto fixture=std::make_unique<Native50MenuAssets>();
  const uintptr_t surface=reinterpret_cast<uintptr_t>(fixture->table.pixels.data());
  Require(SafeWrite(g_dungeon_base+kHardwareBackgroundSurfaceRva,&surface,sizeof(surface)),"native surface fixture");
  PatchJump(0x12A0u,reinterpret_cast<const void*>(&RecordBackground));
  loaded_backgrounds.clear();
  Require(Native50Background(true) && Native50Background(false) &&
      loaded_backgrounds==std::vector<std::string>({"asylum\\menu\\hardset2.pcx","asylum\\menu\\setup.pcx"}),
      "native background/palette calls must resolve to existing original files, without a duplicated prefix");
  Require(SetCurrentDirectoryW(previous)!=0,"restore test cwd");
  constexpr std::array<uint8_t,3> zero{{0x33,0xC0,0xC3}};
  for (uintptr_t rva : {0xF1A0u,0xF170u,0xEDF0u,0xED60u,0x69890u})
    PatchCode(ActiveDungeonCodeRva(rva),zero.data(),zero.size());
  PatchJump(0x68DC0u,reinterpret_cast<const void*>(&Mouse));
  PatchJump(0x2FE0u,reinterpret_cast<const void*>(&FrameCapture));
  PatchJump(0x9A850u,reinterpret_cast<const void*>(&FrameFlip));
  PatchJump(0x1ACE0u,reinterpret_cast<const void*>(&FramePoll));
  std::vector<uint8_t> cursor(84*84),cursor_background(84*84,201);
  std::vector<uint8_t> flame(117*52),flame_background(117*52,201);
  cursor[0]=cursor[85]=180;
  const uintptr_t cp=reinterpret_cast<uintptr_t>(cursor.data());
  const uintptr_t fp=reinterpret_cast<uintptr_t>(flame.data());
  const uintptr_t cb=reinterpret_cast<uintptr_t>(cursor_background.data());
  const uintptr_t fb=reinterpret_cast<uintptr_t>(flame_background.data());
  for (size_t f=0;f<16;++f) {
    Require(SafeWrite(g_dungeon_base+kMenuCursorFramesRva+f*4,&cp,4) &&
        SafeWrite(g_dungeon_base+kMenuCursorFramesRva-0x40+f*4,&fp,4),"retail animation pointers");
  }
  Require(SafeWrite(g_dungeon_base+kMenuCursorFramesRva-0x44,&cb,4) &&
      SafeWrite(g_dungeon_base+kMenuCursorFramesRva+0x44,&fb,4),"retail saved-background buffers");
  fixture->table.pixels.fill(17);
  for (auto& book : fixture->books) {
    book.fill(17);
    const auto r=deathtrap::menu::kBook;
    for (int y=r.y;y<r.y+r.height;++y) for (int x=r.x;x<r.x+r.width;++x) book[y*640+x]=23;
  }
  g_native50_menu_assets=fixture.get();
  g_native50_menu_api={&Mouse,&Key,&Font,&Text,&Width,&FrameCopy,&FrameFlip,&FramePoll,&Change,&Background};
  g_original_menu_sprite=&FrameSprite;
  g_native50_setup_active=true;g_native50_setup_input=false;
  g_native50_book_hovered=false;g_native50_setup_return_latched=true;
  g_native50_setup_repaint_frames=2;
  frame_page=frame_flips=frame_books=0;mouse_x=220;mouse_y=195;buttons=0;keys.fill(false);
  for (auto& page : frame_pages) { page=std::make_unique<deathtrap::menu::Canvas>();page->fill(201); }
  for (unsigned cycle=0;cycle<6;++cycle) {
    Require(HookNative50SetupInput()==0,"real retail loop must stay in Setup");
    Require(frame_flips==cycle+1 && frame_books==cycle+1,
            "one retail Flip and one book draw per cycle, never a second draw during cursor restoration");
    const auto& displayed=*frame_pages[frame_page^1];
    Require(std::find(displayed.begin(),displayed.end(),uint8_t{201})==displayed.end(),
            "no child-page fragment may survive on either displayed page after return");
    if (cycle>=1) for (const auto& page : frame_pages)
      Require(std::find(page->begin(),page->end(),uint8_t{201})==page->end(),
              "both native pages and saved cursor/flame regions must be clean");
  }
  Require(g_native50_setup_repaint_frames==0,"the two real Flip cycles must consume both repaints");
  g_native50_setup_active=false;g_native50_menu_assets=nullptr;
  frame_pages[0].reset();frame_pages[1].reset();
}

// Render the production page with the actual retail font bitmaps and metrics.
// A fixed-width mock cannot detect localized text spilling onto dark wood or
// into the next paragraph. No original font/art is copied into the repository.
std::array<std::vector<uint8_t>,4> preview_fonts;
size_t preview_font = 0;
deathtrap::menu::Canvas* preview_page = nullptr;
std::array<int,deathtrap::menu::kPageRows> page_label_ends{};
bool preview_secondary=false, preview_recovery=false, preview_interface=false;
void __cdecl PageFont(uint32_t id) {
  Require(id==0x1485u || id==0x1486u || id==0x248Fu || id==0x2490u,"page must use native fonts");
  preview_font=id==0x248Fu ? 1 : id==0x2490u ? 2 : id==0x1486u ? 3 : 0;
}
const deathtrap::text::RetailFontGlyph& PageGlyph(unsigned char character) {
  const auto& bytes=preview_fonts[preview_font];
  const auto* header=reinterpret_cast<const deathtrap::text::RetailFontHeader*>(bytes.data());
  Require(character>=32 && static_cast<unsigned>(character-32)<header->glyph_count,
          "localized page characters must exist in the retail font");
  return reinterpret_cast<const deathtrap::text::RetailFontGlyph*>(bytes.data()+8)[character-32];
}
int32_t __cdecl PageWidth(const char* value) {
  int32_t width=0;
  for (const auto* p=reinterpret_cast<const unsigned char*>(value);*p;++p) width+=PageGlyph(*p).advance_x;
  return width;
}
void __cdecl PageText(int x,int y,int,const char* value) {
  const int width=PageWidth(value);
  Require(x>=0 && x+width<=640,"real font text must fit the canvas");
  Require(std::strcmp(value,"Native 50")!=0,"embossed scroll title must not be overwritten by the small body font");
  if (y>=deathtrap::menu::kRowTop && y<416) {
    const size_t expected_font=preview_recovery ? (y<306 ? 1u : 2u) :
        y==deathtrap::menu::kSavedTop && g_hardware_save_error_row>=0
        ? 3u : y>=deathtrap::menu::kDescriptionTop ? 2u : 1u;
    Require(preview_font==expected_font,
            "settings must be light and secondary text must use a muted dark native font");
    if (x<(preview_recovery ? 116 : 118) || x+width>444)
      std::cerr<<"text bounds: x="<<x<<" y="<<y<<" width="<<width<<" text="<<value<<'\n';
    Require(x>=(preview_recovery ? 116 : 118) && x+width<=444,
            "body/help text must stay inside the light parchment margins");
  }
  const int setting_row=(y-deathtrap::menu::kRowTop)/deathtrap::menu::kRowHeight;
  if (!preview_recovery && y>=deathtrap::menu::kRowTop && setting_row>=0 && setting_row<deathtrap::menu::kPageRows &&
      y==deathtrap::menu::kRowTop+setting_row*deathtrap::menu::kRowHeight) {
    if (x==deathtrap::menu::kTextLeft) page_label_ends[setting_row]=x+width;
    else if (x>deathtrap::menu::kTextLeft)
      Require(x>=page_label_ends[setting_row]+12,"localized setting labels and values need a visible gap");
  }
  for (const auto* p=reinterpret_cast<const unsigned char*>(value);*p;++p) {
    const auto& glyph=PageGlyph(*p);
    const auto& bytes=preview_fonts[preview_font];
    Require(static_cast<size_t>(glyph.pixels)+glyph.width*glyph.height<=bytes.size(),"font pixel bounds");
    for (int row=0;row<glyph.height;++row) for (int column=0;column<glyph.width;++column) {
      const int px=x+glyph.offset_x+column,py=y+glyph.offset_y+row;
      const uint8_t ink=bytes[glyph.pixels+row*glyph.width+column];
      if (!ink) continue;
      Require(px>=0 && px<640 && py>=0 && py<480,"visible glyph must fit canvas");
      if (y>=deathtrap::menu::kRowTop && y<416) {
        Require(py<416,"localized help must remain on parchment above the bottom roller");
        if (!preview_recovery && y>=deathtrap::menu::kDescriptionTop && y<deathtrap::menu::kSavedTop) {
          if(py>=deathtrap::menu::kSavedTop-4) std::cerr<<"description bounds: y="<<y<<" py="<<py<<" text="<<value<<'\n';
          Require(py<deathtrap::menu::kSavedTop-4,"wrapped description must not overlap save status");
        }
        if (!preview_recovery && y>=deathtrap::menu::kSavedTop && y<deathtrap::menu::kHelpTop) {
          if (py>=deathtrap::menu::kHelpTop-4)
            std::cerr<<"status bounds: y="<<y<<" py="<<py<<" text="<<value<<'\n';
          Require(py<deathtrap::menu::kHelpTop-4,"wrapped save status must not overlap controls");
        }
      }
      if (!preview_secondary) (*preview_page)[py*640+px]=ink;
    }
    x+=glyph.advance_x;
  }
}
void __cdecl PageGlyphSprite(const void* input,int width,int height,int x,int y) {
  const auto* pixels=static_cast<const uint8_t*>(input);
  Require(input && x>=0 && y>=0 && x+width<=640 && y+height<=480,"private glyph sprite bounds");
  for (int row=0;row<height;++row) for (int column=0;column<width;++column) {
    const uint8_t ink=pixels[row*width+column];
    if (ink) (*preview_page)[(y+row)*640+x+column]=ink;
  }
}
void __cdecl PageSecondaryText(int x,int y,int flags,const char* value) {
  preview_secondary=true;PageText(x,y,flags,value);preview_secondary=false;
  Native50SecondaryText(x,y,flags,value);
}
void CheckBrownFont(const std::vector<uint8_t>& font,const deathtrap::menu::Palette& palette) {
  const auto* header=reinterpret_cast<const deathtrap::text::RetailFontHeader*>(font.data());
  const auto* glyphs=reinterpret_cast<const deathtrap::text::RetailFontGlyph*>(font.data()+8);
  size_t ink_pixels=0;
  for (size_t c=0;c<header->glyph_count;++c) {
    const auto& glyph=glyphs[c];
    for (size_t p=glyph.pixels;p<static_cast<size_t>(glyph.pixels)+glyph.width*glyph.height;++p) {
      if (!font[p]) continue;
      const auto& color=palette[font[p]];
      Require(color[0]>color[1]+5 && color[1]>color[2]+5 && color[0]>=32 &&
          color[0]<128 && color[0]<color[1]*2,
          "actual helper glyph RGB must be dark brown, never olive, orange or black");
      ++ink_pixels;
    }
  }
  Require(ink_pixels>1000,"all localized helper glyphs must retain visible brown strokes");
}
void PageCopy(const uint8_t* source,deathtrap::menu::Rect rect) {
  page_label_ends.fill(0);
  for (int y=rect.y;y<rect.y+rect.height;++y)
    std::memcpy(preview_page->data()+y*640+rect.x,source+y*640+rect.x,rect.width);
}
void WritePagePreview(const std::filesystem::path& game,const std::filesystem::path& output) {
  std::ifstream box(game/L"ASYLUM"/L"DUNGEON.BOX",std::ios::binary);
  Require(box.good(),"page preview requires local retail fonts");
  uint32_t index=0;box.read(reinterpret_cast<char*>(&index),4);
  constexpr std::array<uint32_t,4> font_ids{{0x1485u,0x248Fu,0x2490u,0x1486u}};
  for (size_t font=0;font<font_ids.size();++font) {
    const uint32_t id=font_ids[font];
    box.seekg(static_cast<std::streamoff>(index)+4+static_cast<std::streamoff>(id-1)*12);
    uint32_t offset=0,size=0;char extension[4]{};
    box.read(reinterpret_cast<char*>(&offset),4);box.read(reinterpret_cast<char*>(&size),4);
    box.read(extension,4);
    Require(box.good() && std::memcmp(extension,".fnt",4)==0 && size>=8 && size<65536,"bounded font resource");
    preview_fonts[font].resize(size);box.seekg(offset);
    box.read(reinterpret_cast<char*>(preview_fonts[font].data()),size);
    Require(box.good(),"complete font resource");
  }
  auto assets=std::make_unique<Native50MenuAssets>();
  const auto directory=game/L"ASYLUM"/L"MENU";
  Require(Native50ReadPcx((directory/L"SETUP.PCX").wstring(),&assets->table) &&
      Native50ReadPcx((directory/L"HARDSET2.PCX").wstring(),&assets->paper),"real page source images");
  deathtrap::menu::MakeSettingsPage(assets->paper,assets->paper.palette,&assets->page);
  Require(deathtrap::menu::PaintSettingsHeading(preview_fonts[0].data(),preview_fonts[0].size(),
      assets->paper.palette,&assets->page),"native chancery heading must be built from original glyphs");
  size_t changed_heading=0;
  for (int y=0;y<480;++y) for (int x=0;x<640;++x) {
    if (deathtrap::menu::Rect{138,10,332,46}.Contains(x,y)) {
      if (assets->page[y*640+x]!=assets->paper.pixels[y*640+x]) ++changed_heading;
    } else Require(assets->page[y*640+x]==assets->paper.pixels[y*640+x],
        "parchment, shadows and decoration must remain pixel-identical: no whitening or filtering");
  }
  Require(changed_heading>1000,"baked retail title must be replaced without touching the parchment");
  assets->secondary_font=preview_fonts[2];
  Require(deathtrap::menu::TintSecondaryFont(&assets->secondary_font,assets->paper.palette),
          "private secondary font tint");
  CheckBrownFont(assets->secondary_font,assets->paper.palette);
  g_native50_menu_assets=assets.get();
  g_native50_menu_api={&Mouse,&Key,&PageFont,&PageText,&PageWidth,&PageCopy,&Flip,&Poll,&Change,&Background,&PageSecondaryText};
  g_original_menu_sprite=&PageGlyphSprite;
  constexpr int width=1280,height=960;
  std::vector<uint8_t> rgb(width*height*3);
  auto canvas=std::make_unique<deathtrap::menu::Canvas>();preview_page=canvas.get();
  constexpr std::array<DungeonImageVariant,4> variants{{DungeonImageVariant::kEnglish,
      DungeonImageVariant::kFrenchGog,DungeonImageVariant::kGermanGog,DungeonImageVariant::kItalianGog}};
  for (size_t language=0;language<variants.size();++language) {
    g_dungeon_image_variant=variants[language];
    deathtrap::menu::Navigation navigation;
    if (preview_recovery) {
      deathtrap::menu::MakeSettingsPage(assets->paper,assets->paper.palette,&assets->page);
      Require(deathtrap::menu::PaintSettingsHeading(preview_fonts[0].data(),preview_fonts[0].size(),
          assets->paper.palette,&assets->page,deathtrap::recovery::Title(ActiveGameTextLanguage())),
          "native death dialog heading");
      g_recovery_page=&assets->page;
      DrawRecoveryPage(0);DrawRecoveryPage(1);
    } else {
    // Validate every selection and all movement modes, including the longest
    // localized mixed setting and persistence error values.
    for (int row=0;row<deathtrap::menu::kPageRows;++row) for (int state=0;state<5;++state) {
      navigation.selected=row;
      g_keyboard_mouse_camera_relative_movement=state!=1;
      g_xinput_camera_relative_movement=state==0;
      g_keep_bodies_enabled.store(state%2==0);
      g_e3_model_available.store(state!=2);
      g_e3_model_requested.store(state%2==0);
      g_show_body_wounds.store(state%2==0);
      g_model_archive_selection.store(state==4?2:0);
      g_hardware_save_error_row=state==3 ? row : -1;
      DrawNative50Page(navigation);
    }
    g_keyboard_mouse_camera_relative_movement=g_xinput_camera_relative_movement=true;
    g_hardware_save_error_row=-1;navigation.selected=preview_interface ? 8 : 5;g_model_archive_selection.store(2);DrawNative50Page(navigation);
    }
    for (int y=0;y<480;++y) for (int x=0;x<640;++x) {
      const auto& color=assets->paper.palette[(*canvas)[y*640+x]];
      const int px=x+static_cast<int>(language%2)*640,py=y+static_cast<int>(language/2)*480;
      const size_t at=((height-1-py)*width+px)*3;
      rgb[at]=color[2];rgb[at+1]=color[1];rgb[at+2]=color[0];
    }
  }
  BITMAPFILEHEADER file{};file.bfType=0x4D42;
  file.bfOffBits=sizeof(file)+sizeof(BITMAPINFOHEADER);
  file.bfSize=file.bfOffBits+static_cast<DWORD>(rgb.size());
  BITMAPINFOHEADER info{};info.biSize=sizeof(info);info.biWidth=width;info.biHeight=height;
  info.biPlanes=1;info.biBitCount=24;info.biSizeImage=static_cast<DWORD>(rgb.size());
  std::ofstream stream(output,std::ios::binary);
  stream.write(reinterpret_cast<const char*>(&file),sizeof(file));
  stream.write(reinterpret_cast<const char*>(&info),sizeof(info));
  stream.write(reinterpret_cast<const char*>(rgb.data()),rgb.size());
  Require(stream.good(),"page preview output");
  g_native50_menu_assets=nullptr;preview_page=nullptr;g_recovery_page=nullptr;
  std::cout<<"Actual retail fonts: all four page languages and settings fit the parchment\n";
}
deathtrap::menu::Image* version_preview = nullptr;
unsigned version_glyphs = 0;
void __cdecl VersionSprite(const void* source,int width,int height,int x,int y) {
  Require(version_preview && source && x>=0 && x+width<=640 && y>=420 && y+height<=448,
          "build label must fit above the original version without touching it");
  const auto* pixels=static_cast<const uint8_t*>(source);
  for (int row=0;row<height;++row) for (int col=0;col<width;++col)
    if (pixels[row*width+col]) version_preview->pixels[(y+row)*640+x+col]=pixels[row*width+col];
  ++version_glyphs;
}
uint8_t __cdecl VersionInput() {
  Require(version_glyphs>0,"version must draw before the retail cursor capture/present");
  return 42;
}
void CheckRootVersion(const std::filesystem::path& game,const std::filesystem::path& output={}) {
  auto page=std::make_unique<deathtrap::menu::Image>();
  Require(Native50ReadPcx((game/L"ASYLUM"/L"MENU"/L"MAIN.PCX").wstring(),page.get()),"main menu background");
  const auto original=page->pixels;
  Require(Native50ReadHeadingFont(game.wstring(),&g_root_menu_version_font,0x1486u),"native version font");
  const auto sprite=g_original_menu_sprite; const auto input=g_original_root_menu_input;
  version_preview=page.get();g_original_menu_sprite=&VersionSprite;g_original_root_menu_input=&VersionInput;
  Require(std::strcmp(kRootMenuBuildVersion,"native-50 v." DEATHTRAP_NATIVE_VERSION)==0,
          "display version must follow the compiled patch version");
  for (int frame=0;frame<4;++frame) {
    page->pixels=original;version_glyphs=0;
    if (g_dungeon_base) Require(HookRootMenuInput()==42,"version must preserve native menu actions");
    else DrawRootMenuBuildVersion();
    unsigned changed=0;
    for (int y=0;y<480;++y) for (int x=0;x<640;++x) {
      const size_t p=y*640+x;
      if (page->pixels[p]!=original[p]) {
        Require(y>=420 && y<448 && x>400,"main art and original 1.0 must remain byte-exact");++changed;
      }
    }
    Require(changed>200 && version_glyphs>10,"label must remain visible on every pending page");
  }
  if (!output.empty()) {
    std::ofstream file(output,std::ios::binary);file<<"P6\n640 480\n255\n";
    for (uint8_t ink:page->pixels) file.write(reinterpret_cast<const char*>(page->palette[ink].data()),3);
    Require(file.good(),"version preview output");
  }
  version_preview=nullptr;g_original_menu_sprite=sprite;g_original_root_menu_input=input;
  g_root_menu_version_font.clear();g_root_menu_version_font_attempted=false;
}
void CheckOriginal(int argc, wchar_t** argv) {
  for (int i=1; i<argc; ++i) {
    Require(MH_Initialize() == MH_OK, "MinHook init");
    HMODULE module = LoadLibraryExW(argv[i], nullptr, DONT_RESOLVE_DLL_REFERENCES);
    Require(module != nullptr, "original DLL mapping");
    g_dungeon_base = reinterpret_cast<uint8_t*>(module);
    g_dungeon_image_variant = IdentifyDungeonImage(g_dungeon_base);
    g_native50_menu_hooks_installed = false;
    Require(g_dungeon_image_variant != DungeonImageVariant::kUnsupported && InstallNative50MenuHooks() &&
                InstallNative50MenuHooks(), "real localized signatures and idempotent install");
    auto assets = std::make_unique<Native50MenuAssets>();
    const auto dir = std::filesystem::path(argv[i]).parent_path() / L"ASYLUM" / L"MENU";
    Require(Native50ReadPcx((dir/L"SETUP.PCX").wstring(), &assets->table) &&
                Native50ReadPcx((dir/L"HARDSET2.PCX").wstring(), &assets->paper),
            "bounded decoder must accept real localized menu art");
    std::vector<uint8_t> heading_font;
    Require(Native50ReadHeadingFont(dir.parent_path().parent_path().wstring(), &heading_font),
            "production bounded BOX reader must find the native heading font");
    Require(Native50ReadHeadingFont(dir.parent_path().parent_path().wstring(),&assets->secondary_font,0x2490u),
            "production BOX reader must find localized helper glyphs");
    const auto original_secondary=assets->secondary_font;
    Require(deathtrap::menu::TintSecondaryFont(&assets->secondary_font,assets->paper.palette),
            "localized helper font tint");
    const size_t metadata=8+reinterpret_cast<const deathtrap::text::RetailFontHeader*>(
        original_secondary.data())->glyph_count*8;
    Require(std::memcmp(original_secondary.data(),assets->secondary_font.data(),metadata)==0,
            "brown tint must preserve original font metrics and accent metadata");
    CheckBrownFont(assets->secondary_font,assets->paper.palette);
    deathtrap::menu::MakeSettingsPage(assets->paper, assets->paper.palette, &assets->page);
    Require(deathtrap::menu::PaintSettingsHeading(heading_font.data(),heading_font.size(),
                assets->paper.palette,&assets->page),"real localized native heading glyphs");
    for (int y=56;y<480;++y) for (int x=0;x<640;++x) {
      Require(assets->page[y*640+x]==assets->paper.pixels[y*640+x],
              "the complete original parchment must remain untouched in every installation");
    }
    HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(501), RT_RCDATA);
    Require(resource != nullptr, "book must be embedded, not depend on loose files");
    const auto* data = static_cast<const uint8_t*>(LockResource(LoadResource(nullptr, resource)));
    Require(deathtrap::menu::MakeBookFrames(data, SizeofResource(nullptr,resource),
                                          assets->table, &assets->books), "real book animation resource");
    constexpr auto w=deathtrap::menu::kBook.width,h=deathtrap::menu::kBook.height;
    const size_t reference=deathtrap::menu::U16(data+10);
    const auto* mask=data+12+deathtrap::menu::kBookFrames*w*h*4;
    size_t static_ink=0,static_outline=0,moving_ink=0,soft_edges=0;
    for (int y=0;y<h;++y) for (int x=0;x<w;++x) {
      const size_t pixel_index=(deathtrap::menu::kBook.y+y)*640+deathtrap::menu::kBook.x+x;
      if (mask[y*w+x]==255) {
        for (const auto& frame : assets->books) {
          Require(frame[pixel_index]==assets->books[reference][pixel_index],
                  "the entire stationary spine, board and outline must be pixel-identical in ALL poses");
        }
        if (assets->books[reference][pixel_index]!=assets->table.pixels[pixel_index]) ++static_ink;
        else ++static_outline;
      } else {
        if (mask[y*w+x]) ++soft_edges;
        if (assets->books.front()[pixel_index]!=assets->books.back()[pixel_index]) ++moving_ink;
      }
    }
    Require(static_ink>600 && static_outline>10,
            "static layer must include real textured body AND transparent edge cleanup, not just one point");
    Require(moving_ink>150,"the cover and several painted pages must still animate independently");
    Require(soft_edges>100,"layer boundaries must be antialiased rather than binary stair steps");
    Require(LoadNative50Caption(assets->table,&assets->label),
            "embedded chancery caption must render on every localized setup palette");
    for (size_t f=1;f<assets->books.size();++f) {
      Require(assets->books[f]!=assets->books[f-1],"every painted animation frame must actually differ");
    }
    unsigned blue=0;
    for (size_t p=0;p<assets->label.size();++p) if (assets->label[p]!=assets->table.pixels[p]) {
      const auto rgb=assets->table.palette[assets->label[p]];
      if (rgb[2]>rgb[0]+15 && rgb[2]>rgb[1]+10) ++blue;
    }
    Require(blue>400,"caption must contain large visible blue glyphs, not black page-font text");
    CheckRootVersion(std::filesystem::path(argv[i]).parent_path());
    CheckNativeTransitions(std::filesystem::path(argv[i]).parent_path());
    std::cout << "Native 50 book/page: " << ActiveDungeonProfileName() << " passed\n";
    Require(MH_Uninitialize() == MH_OK, "test hooks teardown");
    FreeLibrary(module); g_dungeon_base = nullptr;
  }
}
void WritePreview(const std::filesystem::path& game,const std::filesystem::path& output) {
  auto assets=std::make_unique<Native50MenuAssets>();
  Require(Native50ReadPcx((game/L"ASYLUM"/L"MENU"/L"SETUP.PCX").wstring(),&assets->table),
          "preview table must load");
  // Display every native item caption in the diagnostic scene, so collisions
  // with Sounds/Graphics cannot hide behind a preview of the bare table.
  auto atlas=std::make_unique<deathtrap::menu::Image>();
  Require(Native50ReadPcx((game/L"ASYLUM"/L"MENU"/L"KEYS2.PCX").wstring(),atlas.get()),
          "preview must include genuine native captions");
  constexpr std::array<int,4> source_y{{136,232,184,280}};
  for (size_t c=0;c<deathtrap::menu::kNativeItemCaptions.size();++c) {
    const auto rect=deathtrap::menu::kNativeItemCaptions[c];
    for (int y=0;y<rect.height;++y) for (int x=0;x<rect.width;++x) {
      assets->table.pixels[(rect.y+y)*640+rect.x+x]=atlas->pixels[(source_y[c]+y)*640+x];
    }
  }
  const HRSRC resource=FindResourceW(nullptr,MAKEINTRESOURCEW(501),RT_RCDATA);
  Require(resource!=nullptr,"embedded sprite for preview");
  const auto* data=static_cast<const uint8_t*>(LockResource(LoadResource(nullptr,resource)));
  Require(deathtrap::menu::MakeBookFrames(data,SizeofResource(nullptr,resource),assets->table,
                                         &assets->books) &&
          LoadNative50Caption(assets->table,&assets->label),"preview assets");
  constexpr int width=1280,height=480;
  std::vector<uint8_t> rgb(width*height*3);
  for (int y=0;y<height;++y) for (int x=0;x<width;++x) {
    const size_t index=y*640+x%640;
    const size_t frame=x<640 ? 0 : deathtrap::menu::kBookFrames-1;
    uint8_t ink=assets->books[frame][index];
    if (deathtrap::menu::kBookLabel.Contains(x%640,y) &&
        assets->label[index]!=assets->table.pixels[index]) ink=assets->label[index];
    const auto color=assets->table.palette[ink];
    const size_t dst=((height-1-y)*width+x)*3;
    rgb[dst]=color[2];rgb[dst+1]=color[1];rgb[dst+2]=color[0];
  }
  BITMAPFILEHEADER file{};file.bfType=0x4D42;
  file.bfOffBits=sizeof(file)+sizeof(BITMAPINFOHEADER);
  file.bfSize=file.bfOffBits+static_cast<DWORD>(rgb.size());
  BITMAPINFOHEADER info{};info.biSize=sizeof(info);info.biWidth=width;info.biHeight=height;
  info.biPlanes=1;info.biBitCount=24;info.biSizeImage=static_cast<DWORD>(rgb.size());
  std::ofstream stream(output,std::ios::binary);
  stream.write(reinterpret_cast<const char*>(&file),sizeof(file));
  stream.write(reinterpret_cast<const char*>(&info),sizeof(info));
  stream.write(reinterpret_cast<const char*>(rgb.data()),rgb.size());
  Require(stream.good(),"preview bitmap output");
}
}
int wmain(int argc, wchar_t** argv) {
  if (argc==4 && std::wcscmp(argv[1],L"--version-preview")==0) { CheckRootVersion(argv[2],argv[3]);return 0; }
  if (argc==4 && std::wcscmp(argv[1],L"--death-preview")==0) {
    preview_recovery=true;WritePagePreview(argv[2],argv[3]);return 0;
  }
  if (argc==4 && std::wcscmp(argv[1],L"--preview")==0) { WritePreview(argv[2],argv[3]);return 0; }
  if (argc==4 && std::wcscmp(argv[1],L"--interface-preview")==0) { preview_interface=true; WritePagePreview(argv[2],argv[3]);return 0; }
  if (argc==4 && std::wcscmp(argv[1],L"--page-preview")==0) { WritePagePreview(argv[2],argv[3]);return 0; }
  if (argc > 1) { CheckOriginal(argc, argv); return 0; }
  CheckNavigation(); CheckColorQuality(); CheckRuntime();
  std::cout << "Native 50 book animation, native item isolation, modal input/return, redraw and localization passed\n";
}
