// One archive stays open for the whole process. A dual pack contains both
// independently owned mesh/texture sets, so only the draw selection changes.
std::atomic<bool> g_e3_model_requested{false};
std::atomic<bool> g_e3_model_available{false};
std::atomic<int> g_model_archive_selection{-1}; // -1 unopened, 0 retail, 1 legacy E3, 2 dual
struct DualHeroinePack {
  char signature[4]{};
  uint32_t version=0,resources=0,first_mesh=0,mesh_count=0;
  uint32_t first_texture=0,texture_count=0,e3_texture_first=0;
};
static_assert(sizeof(DualHeroinePack)==32);
DualHeroinePack g_dual_heroine_pack{};
std::vector<deathtrap::models::OverlayEntry> g_model_overlay;
bool HeroineSharedChains() { return g_dual_heroine_pack.version>=2 && g_dual_heroine_pack.version<=4; }
uint32_t HeroineChainResource(uint32_t kind) {
  return g_dual_heroine_pack.first_texture+g_dual_heroine_pack.texture_count+kind;
}
bool HeroineLiveDrawReady();
bool HeroineLiveSwitchEnabled() { return g_model_archive_selection.load()==2; }
bool HeroineE3VisualActive() {
  const int selection=g_model_archive_selection.load();
  return selection==1 || (selection==2 && g_e3_model_requested.load());
}
int HeroineMeshIndex(uint32_t id) {
  if (id>=3885 && id<=3904) return static_cast<int>(id-3885);
  if (id>=4070 && id<=4289) return static_cast<int>(20+id-4070);
  if (id>=8139 && id<=9098) return static_cast<int>(240+id-8139);
  return -1;
}
uint32_t HeroineResourceFromIndex(uint32_t index) {
  return index<20 ? 3885+index : index<240 ? 4070+index-20 : 8139+index-240;
}
std::wstring g_model_game_root;
bool g_model_hooks_installed = false;
using ModelCreateFileW = HANDLE(WINAPI*)(LPCWSTR,DWORD,DWORD,LPSECURITY_ATTRIBUTES,DWORD,DWORD,HANDLE);
using ModelCreateFileA = HANDLE(WINAPI*)(LPCSTR,DWORD,DWORD,LPSECURITY_ATTRIBUTES,DWORD,DWORD,HANDLE);
ModelCreateFileW g_model_create_w = nullptr;
ModelCreateFileA g_model_create_a = nullptr;
constexpr const wchar_t* kModelFiles[] = {
    L"ASYLUM\\DUNGEON.BOX", L"ASYLUM\\PST\\HEROINE.RES", L"ASYLUM\\PST\\L1.RES",
    L"ASYLUM\\PST\\L8.RES", L"ASYLUM\\PST\\L10.RES", L"ASYLUM\\PST\\NETGAME.RES"};

std::wstring ModelFullPath(const wchar_t* name) {
  if (!name || !*name) return {};
  std::array<wchar_t,32768> buffer{};
  DWORD size=GetFullPathNameW(name,static_cast<DWORD>(buffer.size()),buffer.data(),nullptr);
  if (!size || size>=buffer.size()) return {};
  std::wstring path(buffer.data(),size);
  std::replace(path.begin(),path.end(),L'/',L'\\');
  return path;
}
std::wstring ModelPackPath(size_t index) {
  return g_model_game_root+L"\\native50\\models\\e3\\"+kModelFiles[index];
}
bool ModelPackComplete() {
  if (g_model_game_root.empty()) return false;
  for (size_t i=0;i<std::size(kModelFiles);++i) {
    if (i==0) {
      std::error_code error;
      const auto root=std::filesystem::path(g_model_game_root)/L"native50/models/e3";
      for (const auto* file:{L"model-data.bin",L"resource-map.bin"}) {
        if (!std::filesystem::is_regular_file(root/file,error) || error) return false;
      }
      continue;
    }
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(ModelPackPath(i).c_str(),GetFileExInfoStandard,&data) ||
        (data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) || data.nFileSizeHigh ||
        data.nFileSizeLow<(i==0 ? 1024u : 8u)) return false;
  }
  return true;
}
bool ReadDualHeroinePack() {
  g_dual_heroine_pack={};g_model_overlay.clear();
  const auto path=g_model_game_root+L"\\native50\\models\\e3\\dual-models.bin";
  if (GetFileAttributesW(path.c_str())==INVALID_FILE_ATTRIBUTES) {
    return false;
  }
  HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
  if (file==INVALID_HANDLE_VALUE) return false;
  DualHeroinePack pack{};DWORD read=0;LARGE_INTEGER size{};
  const bool valid=GetFileSizeEx(file,&size) && size.QuadPart==sizeof(pack) &&
      ReadFile(file,&pack,sizeof(pack),&read,nullptr) && read==sizeof(pack);
  CloseHandle(file);
  if (!valid || std::memcmp(pack.signature,"N50D",4) || pack.version<1 || pack.version>4 ||
      pack.mesh_count!=1200 || pack.first_mesh<14135 || pack.resources>=16384 ||
      pack.first_texture!=pack.first_mesh+pack.mesh_count || !pack.texture_count || pack.texture_count>16 ||
      pack.resources!=pack.first_texture+pack.texture_count+(pack.version>=2?2u:0u) || pack.e3_texture_first<14125 ||
      pack.e3_texture_first>=pack.first_mesh || pack.first_mesh-pack.e3_texture_first>32) return false;
  std::vector<deathtrap::models::OverlayEntry> entries;
  try { entries=deathtrap::models::ReadOverlay(g_model_game_root); }
  catch (...) { return false; }
  bool archive_ok=entries.size()==pack.resources;
  std::vector<uint32_t> verify_ids{pack.first_mesh,pack.first_texture-1,
      pack.e3_texture_first,pack.first_texture,pack.first_texture+pack.texture_count-1};
  if (pack.version>=2) { verify_ids.push_back(pack.resources-2);verify_ids.push_back(pack.resources-1); }
  for (uint32_t id:verify_ids) {
    if (!archive_ok || id>=entries.size()) { archive_ok=false;break; }
    const auto& entry=entries[id];
    const bool mesh=(id>=pack.first_mesh && id<pack.first_texture) ||
        (pack.version>=2 && id>=pack.resources-2);
    if ((mesh && (entry.size<52 || _strnicmp(reinterpret_cast<const char*>(&entry.type),".prt",4))) ||
        (!mesh && (entry.size!=65536 || _strnicmp(reinterpret_cast<const char*>(&entry.type),".tex",4)))) { archive_ok=false;break; }
  }
  if (!archive_ok) return false;
  g_dual_heroine_pack=pack;g_model_overlay=std::move(entries);return true;
}
int ModelFileIndex(const wchar_t* path) {
  if (g_model_game_root.empty()) return -1;
  const auto full=ModelFullPath(path);
  for (size_t i=0;i<std::size(kModelFiles);++i) {
    if (_wcsicmp(full.c_str(),(g_model_game_root+L"\\"+kModelFiles[i]).c_str())==0)
      return static_cast<int>(i);
  }
  return -1;
}
bool ModelReadOnlyOpen(DWORD access,DWORD creation) {
  return creation==OPEN_EXISTING && (access&GENERIC_READ)!=0 &&
      !(access&(GENERIC_WRITE|GENERIC_ALL|DELETE|WRITE_DAC|WRITE_OWNER|FILE_WRITE_DATA|FILE_APPEND_DATA));
}
int SelectModelArchive(bool archive) {
  int selection=g_model_archive_selection.load();
  if (selection>=0) return selection;
  // A preload before our first archive open means initialization was too late.
  // Fail closed rather than give a retail archive nonexistent E3 resource IDs.
  int desired=0;
  if (archive && ModelPackComplete()) {
    if (g_dual_heroine_pack.version) desired=HeroineLiveDrawReady()?2:0;
    else desired=g_e3_model_requested.load()?1:0;
  }
  int unset=-1;
  g_model_archive_selection.compare_exchange_strong(unset,desired);
  selection=g_model_archive_selection.load();
  AppendDeathtrapSupportLog("support_heroine_model archive=%s selected=%s first_open=%s apply=%s",
      selection==2?"dual":selection==1?"e3":"original",
      HeroineE3VisualActive()?"e3":"original",archive ? "archive" : "preload",selection==2?"immediate":"restart");
  return selection;
}
HANDLE WINAPI HookModelCreateFileW(LPCWSTR name,DWORD access,DWORD share,
    LPSECURITY_ATTRIBUTES security,DWORD creation,DWORD flags,HANDLE template_file) {
  const DWORD saved_error=GetLastError();
  const int index=ModelReadOnlyOpen(access,creation) ? ModelFileIndex(name) : -1;
  std::wstring redirected;
  if (index>=0 && SelectModelArchive(index==0)>0 && index!=0)
    redirected=ModelPackPath(static_cast<size_t>(index));
  SetLastError(saved_error);
  const HANDLE result=g_model_create_w(redirected.empty()?name:redirected.c_str(),access,share,security,creation,flags,template_file);
  const DWORD error=GetLastError();
  if (!redirected.empty()) AppendDeathtrapSupportLog("support_heroine_model file=%u opened=%u error=%lu",
      static_cast<unsigned>(index),result!=INVALID_HANDLE_VALUE ? 1u : 0u,
      result==INVALID_HANDLE_VALUE ? error : 0u);
  // Do not fall back per file: that would mix two incompatible resource tables.
  SetLastError(error); return result;
}
HANDLE WINAPI HookModelCreateFileA(LPCSTR name,DWORD access,DWORD share,
    LPSECURITY_ATTRIBUTES security,DWORD creation,DWORD flags,HANDLE template_file) {
  const DWORD saved_error=GetLastError();
  std::wstring wide;
  if (name && ModelReadOnlyOpen(access,creation)) {
    const UINT codepage=AreFileApisANSI()?CP_ACP:CP_OEMCP;
    const int length=MultiByteToWideChar(codepage,0,name,-1,nullptr,0);
    if (length>0 && length<=32768) {
      wide.resize(static_cast<size_t>(length));
      MultiByteToWideChar(codepage,0,name,-1,wide.data(),length);
    }
  }
  const int index=wide.empty() ? -1 : ModelFileIndex(wide.c_str());
  if (index>=0) {
    SetLastError(saved_error);
    return HookModelCreateFileW(wide.c_str(),access,share,security,creation,flags,template_file);
  }
  SetLastError(saved_error);
  return g_model_create_a(name,access,share,security,creation,flags,template_file);
}
#include "model_resource_runtime.inl"

struct ModelFileImport {
  void** slot=nullptr;
  void* original=nullptr;
  void* replacement=nullptr;
};
std::vector<ModelFileImport> g_model_file_imports;

bool CollectModelFileImports(uint8_t* module,std::vector<ModelFileImport>* slots) {
  if (!module || !slots) return false;
  IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS32 nt{};
  if (!SafeRead(module,&dos,sizeof(dos)) || dos.e_magic!=IMAGE_DOS_SIGNATURE ||
      dos.e_lfanew<=0 || dos.e_lfanew>1024*1024 ||
      !SafeRead(module+dos.e_lfanew,&nt,sizeof(nt)) || nt.Signature!=IMAGE_NT_SIGNATURE ||
      nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
      nt.OptionalHeader.NumberOfRvaAndSizes<=IMAGE_DIRECTORY_ENTRY_IMPORT) return false;
  const auto image_size=nt.OptionalHeader.SizeOfImage;
  const auto read=[&](uint64_t rva,void* value,size_t size) {
    return rva<image_size && size<=image_size-rva && SafeRead(module+rva,value,size);
  };
  const auto string=[&](uint64_t rva,std::string* value) {
    value->clear();
    for(size_t i=0;i<128;++i) {
      char c=0;if(!read(rva+i,&c,1)) return false;
      if(!c) return true;value->push_back(c);
    }
    return false;
  };
  const auto directory=nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  if (!directory.VirtualAddress || !directory.Size) return true;
  for(uint64_t offset=0;offset+sizeof(IMAGE_IMPORT_DESCRIPTOR)<=directory.Size;
      offset+=sizeof(IMAGE_IMPORT_DESCRIPTOR)) {
    IMAGE_IMPORT_DESCRIPTOR descriptor{};
    if(!read(uint64_t(directory.VirtualAddress)+offset,&descriptor,sizeof(descriptor))) return false;
    if(!descriptor.Name) return true;
    std::string library;
    if(!string(descriptor.Name,&library)) return false;
    if(_stricmp(library.c_str(),"kernel32.dll") && _stricmp(library.c_str(),"kernelbase.dll")) continue;
    if(!descriptor.OriginalFirstThunk || !descriptor.FirstThunk) return false;
    for(uint64_t i=0;i<image_size/sizeof(IMAGE_THUNK_DATA32);++i) {
      IMAGE_THUNK_DATA32 entry{};
      if(!read(uint64_t(descriptor.OriginalFirstThunk)+i*sizeof(entry),&entry,sizeof(entry))) return false;
      if(!entry.u1.AddressOfData) break;
      if(IMAGE_SNAP_BY_ORDINAL32(entry.u1.Ordinal)) continue;
      std::string name;
      if(!string(uint64_t(entry.u1.AddressOfData)+offsetof(IMAGE_IMPORT_BY_NAME,Name),&name)) return false;
      void* replacement=name=="CreateFileA"?reinterpret_cast<void*>(&HookModelCreateFileA):
          name=="CreateFileW"?reinterpret_cast<void*>(&HookModelCreateFileW):nullptr;
      if(!replacement) continue;
      const auto rva=uint64_t(descriptor.FirstThunk)+i*sizeof(entry);void* original=nullptr;
      if(rva%alignof(void*) || !read(rva,&original,sizeof(original)) || !original) return false;
      slots->push_back({reinterpret_cast<void**>(module+rva),original,replacement});
    }
  }
  return false;
}

bool ExchangeModelFileImport(const ModelFileImport& slot,bool restore) {
  const auto before=restore?slot.replacement:slot.original;
  const auto after=restore?slot.original:slot.replacement;
  DWORD protection=0;
  if(!VirtualProtect(slot.slot,sizeof(void*),PAGE_READWRITE,&protection)) return false;
  const bool exchanged=InterlockedCompareExchangePointer(slot.slot,after,before)==before;
  DWORD ignored=0;
  if(!VirtualProtect(slot.slot,sizeof(void*),protection,&ignored)) {
    if(exchanged) InterlockedCompareExchangePointer(slot.slot,before,after);
    return false;
  }
  return exchanged;
}
bool RestoreModelFileImports() {
  bool restored=true;
  for(auto it=g_model_file_imports.rbegin();it!=g_model_file_imports.rend();++it)
    restored=ExchangeModelFileImport(*it,true)&&restored;
  if(restored) g_model_file_imports.clear();
  return restored;
}
bool InstallModelFileImports() {
  if(!g_model_file_imports.empty()) return true;
  std::vector<ModelFileImport> slots;
  if(!CollectModelFileImports(g_dungeon_base,&slots) || slots.empty()) return false;
  // Only the game's own imports are redirected. Proton/Steam may replace the
  // system CreateFileA entry with a short thunk that cannot be inline-hooked.
  // Other modules and the OS entry points remain untouched.
  std::array<wchar_t,MAX_PATH> executable{};
  const auto main=GetModuleHandleW(nullptr);
  const auto length=GetModuleFileNameW(main,executable.data(),static_cast<DWORD>(executable.size()));
  const auto slash=std::wcsrchr(executable.data(),L'\\');
  if(length && length<executable.size() && slash && !_wcsicmp(slash+1,L"DD_CD.EXE") &&
      !CollectModelFileImports(reinterpret_cast<uint8_t*>(main),&slots)) return false;
  for(const auto& slot:slots) {
    if(!ExchangeModelFileImport(slot,false)) { RestoreModelFileImports();return false; }
    g_model_file_imports.push_back(slot);
  }
  AppendDeathtrapSupportLog("support_heroine_model file_router=game_imports slots=%u",static_cast<unsigned>(slots.size()));
  return true;
}

bool InstallHeroineModelHooks() {
  if (g_model_hooks_installed) return true;
  const auto unavailable=[](const char* reason,int status=0) {
    AppendDeathtrapSupportLog("support_heroine_model router=unavailable reason=%s status=%d",reason,status);
    return false;
  };
  g_model_game_root=ModelFullPath(ModuleDirectory().c_str());
  g_e3_model_available.store(false);
  const auto patch=std::filesystem::path(g_model_game_root)/L"native50-e3-model.n50p";
  std::error_code pack_error;
  const bool has_patch=std::filesystem::exists(patch,pack_error);
  if (pack_error) return unavailable("pack_path",pack_error.value());
  if (has_patch) {
    const bool ready=deathtrap::models::Prepare(g_model_game_root,patch);
    AppendDeathtrapSupportLog("support_heroine_model cache=%s source=installed_game_delta",ready?"ready":"failed");
    if (!ready) return unavailable("cache");
  }
  // No pack: leave the process's file APIs entirely untouched.
  if (!ModelPackComplete()) return unavailable("incomplete_cache");
  if (!ReadDualHeroinePack()) return unavailable("metadata");
  if (g_dual_heroine_pack.version && !HeroineLiveDrawReady()) {
    AppendDeathtrapSupportLog("support_heroine_model router=unavailable reason=dual_draw_not_ready");return false;
  }
  auto kernel=GetModuleHandleW(L"kernel32.dll");
  g_model_create_w=reinterpret_cast<ModelCreateFileW>(GetProcAddress(kernel,"CreateFileW"));
  g_model_create_a=reinterpret_cast<ModelCreateFileA>(GetProcAddress(kernel,"CreateFileA"));
  if (!g_model_create_w || !g_model_create_a) return unavailable("file_api");
  if (!InstallModelFileImports()) return unavailable("file_imports");
  if (!InstallModelResourceHooks()) {
    RestoreModelFileImports();return unavailable("native_resource_hooks");
  }
  g_model_hooks_installed=true;
  g_e3_model_available.store(true);
  AppendDeathtrapSupportLog("support_heroine_model router=ready requested=%s",g_e3_model_requested.load()?"e3":"original");
  return true;
}
