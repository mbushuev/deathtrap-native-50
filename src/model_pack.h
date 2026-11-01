#pragma once
#include <windows.h>
#include <wincrypt.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <sstream>
#include <vector>
#include <unordered_map>

namespace deathtrap::models {
using Digest=std::array<uint8_t,32>;
inline bool Hash(const std::filesystem::path& path,Digest* digest) {
  std::ifstream file(path,std::ios::binary);
  HCRYPTPROV provider=0;HCRYPTHASH hash=0;
  if (!file || !CryptAcquireContextW(&provider,nullptr,nullptr,PROV_RSA_AES,CRYPT_VERIFYCONTEXT)) return false;
  bool ok=CryptCreateHash(provider,CALG_SHA_256,0,0,&hash)!=FALSE;
  std::vector<uint8_t> buffer(65536);
  while (ok && file) {
    file.read(reinterpret_cast<char*>(buffer.data()),buffer.size());
    const auto size=file.gcount();
    if (size) ok=CryptHashData(hash,buffer.data(),static_cast<DWORD>(size),0)!=FALSE;
  }
  DWORD size=static_cast<DWORD>(digest->size());
  ok=ok && file.eof() && CryptGetHashParam(hash,HP_HASHVAL,digest->data(),&size,0) && size==digest->size();
  if (hash) CryptDestroyHash(hash);
  CryptReleaseContext(provider,0);return ok;
}
inline void Check(bool ok) { if (!ok) throw std::runtime_error("invalid model delta or game resources"); }
inline void Read(std::istream& file,void* data,size_t size) {
  file.read(static_cast<char*>(data),static_cast<std::streamsize>(size));Check(static_cast<bool>(file));
}
template<class T> inline T Read(std::istream& file) { T value{};Read(file,&value,sizeof(value));return value; }
inline void Write(std::ostream& file,const void* data,size_t size) {
  file.write(static_cast<const char*>(data),static_cast<std::streamsize>(size));Check(static_cast<bool>(file));
}
struct Entry { uint32_t offset=0,size=0,type=0; };
static_assert(sizeof(Entry)==12);
struct OverlayEntry { uint32_t offset=0,size=0,type=0,source=0; };
static_assert(sizeof(OverlayEntry)==16);
inline constexpr const wchar_t* kBaseArchive=L"ASYLUM/DUNGEON.BOX";
inline constexpr const wchar_t* kCacheFiles[]={L"model-data.bin",L"ASYLUM/PST/HEROINE.RES",
    L"ASYLUM/PST/L1.RES",L"ASYLUM/PST/L8.RES",L"ASYLUM/PST/L10.RES",L"ASYLUM/PST/NETGAME.RES",
    L"dual-models.bin",L"resource-map.bin"};

inline constexpr uint32_t kMinModelResources=15345,kMaxModelResources=15369;
inline bool ValidModelMetadata(const std::array<uint32_t,8>& metadata,uint32_t count) {
  // Ten Original texture aliases are independent of E3's atlas size. The live
  // renderer supports 10..32 E3 pages; relocating aliases keeps both models
  // resident without forcing injury artwork into the healthy atlas space.
  return metadata[0]==0x4430354Eu && metadata[1]>=1 && metadata[1]<=4 &&
      count>=kMinModelResources && count<=kMaxModelResources && metadata[2]==count &&
      metadata[3]>=14135 && metadata[3]<=14157 && metadata[4]==1200 &&
      metadata[5]==metadata[3]+metadata[4] && metadata[6]==10 && metadata[7]==14125 &&
      count==metadata[5]+metadata[6]+(metadata[1]>=2?2u:0u);
}

inline std::vector<OverlayEntry> ReadOverlay(const std::filesystem::path& game) {
  const auto root=game/L"native50/models/e3";
  const uint64_t sizes[]={std::filesystem::file_size(game/kBaseArchive),
                          std::filesystem::file_size(root/kCacheFiles[0])};
  std::ifstream map(root/kCacheFiles[7],std::ios::binary);
  Check(Read<uint32_t>(map)==0x4F30354Eu && Read<uint32_t>(map)==1);
  const auto count=Read<uint32_t>(map);Check(count>=kMinModelResources && count<=kMaxModelResources);
  std::vector<OverlayEntry> result(count);
  Read(map,result.data()+1,(count-1)*sizeof(OverlayEntry));
  Check(map.peek()==std::char_traits<char>::eof());
  for (uint32_t i=1;i<count;++i) {
    const auto& entry=result[i];Check(entry.source<=1 && entry.offset>=4 &&
        uint64_t(entry.offset)+entry.size<=sizes[entry.source]);
  }
  return result;
}

inline std::string Unshare(std::istream& input) {
  Check(Read<uint32_t>(input)==0x5A30354Eu && Read<uint32_t>(input)==1);
  const auto size=Read<uint32_t>(input);Check(size>=80 && size<=64*1024*1024);
  std::string output(size,'\0');uint32_t position=0;
  while (position<size) {
    const auto literal=Read<uint32_t>(input);Check(literal<=size-position);
    Read(input,output.data()+position,literal);position+=literal;
    if (position==size) break;
    const auto distance=Read<uint32_t>(input),length=Read<uint32_t>(input);
    Check(distance>0 && distance<=position && length>=32 && length<=size-position);
    for (uint32_t i=0;i<length;++i) output[position+i]=output[position+i-distance];
    position+=length;
  }
  Check(input.peek()==std::char_traits<char>::eof());return output;
}

// Decode only fixed resource records: the pack cannot choose output paths or
// execute code. References may read the verified base or an earlier output part.
inline void Build(const std::filesystem::path& base,const std::filesystem::path& patch,
                  const std::filesystem::path& stage) {
  std::ifstream source(base,std::ios::binary),encoded(patch,std::ios::binary);
  Check(source.good() && encoded.good());
  const bool shared=Read<uint32_t>(encoded)==0x5A30354Eu;encoded.seekg(0);
  std::istringstream decoded(std::ios::binary|std::ios::in);
  if (shared) decoded.str(Unshare(encoded));
  std::istream& delta=shared?static_cast<std::istream&>(decoded):static_cast<std::istream&>(encoded);
  const uint32_t magic=Read<uint32_t>(delta),version=Read<uint32_t>(delta);
  const uint32_t old_count=Read<uint32_t>(delta),new_count=Read<uint32_t>(delta);
  Check(magic==0x5030354Eu && version==1 && old_count==14125 &&
        new_count>=kMinModelResources && new_count<=kMaxModelResources);
  const auto expected_base=Read<Digest>(delta);
  Digest actual_base{};Check(Hash(base,&actual_base) && actual_base==expected_base);
  const auto metadata=Read<std::array<uint32_t,8>>(delta);
  Check(ValidModelMetadata(metadata,new_count));
  source.seekg(0,std::ios::end);const auto base_size=source.tellg();source.seekg(0);
  const auto index=Read<uint32_t>(source);
  Check(uint64_t(index)+4+12ull*(old_count-1)==static_cast<uint64_t>(base_size));
  source.seekg(index);Check(Read<uint32_t>(source)==old_count);
  std::vector<Entry> old(old_count);
  std::vector<OverlayEntry> entries(new_count);
  for (uint32_t id=1;id<old_count;++id) {
    old[id]=Read<Entry>(source);
    Check(old[id].offset>=4 && uint64_t(old[id].offset)+old[id].size<=index);
  }
  std::filesystem::create_directories(stage/L"ASYLUM/PST");
  std::fstream output(stage/kCacheFiles[0],std::ios::binary|std::ios::in|std::ios::out|std::ios::trunc);
  Check(output.good());const uint32_t zero=0;Write(output,&zero,4);
  uint32_t position=4;
  // Resource IDs retain separate native ownership. Equal serialized resources
  // can still reference one read-only file span, as retail BOX files do.
  std::unordered_map<uint64_t,std::vector<OverlayEntry>> unique;
  for (uint32_t id=1;id<new_count;++id) {
    const auto mode=Read<uint32_t>(delta),reference=Read<uint32_t>(delta),size=Read<uint32_t>(delta);
    const auto type=Read<uint32_t>(delta),count=Read<uint32_t>(delta);
    Check(mode<=2 && (size>0 || (type==0 && count==0)) && size<=16*1024*1024 && count<=size/4+1 &&
          uint64_t(position)+size<512*1024*1024);
    std::vector<uint8_t> bytes(size);
    if (mode==2) { Check(reference==0 && count==0);Read(delta,bytes.data(),size); }
    else {
      Check(reference>0 && reference<(mode==0?old_count:id));
      const auto entry=mode==0?OverlayEntry{old[reference].offset,old[reference].size,old[reference].type,0}:entries[reference];
      Check(entry.size==size && entry.type==type);
      if (count==0) { entries[id]=entry;continue; }
      if (entry.source==0) { source.clear();source.seekg(entry.offset);Read(source,bytes.data(),size); }
      else { output.flush();output.seekg(entry.offset);Read(output,bytes.data(),size); }
    }
    uint32_t end=0;
    for (uint32_t n=0;n<count;++n) {
      const auto offset=Read<uint32_t>(delta),length=Read<uint32_t>(delta);
      Check(length>0 && offset>=end && uint64_t(offset)+length<=size);
      Read(delta,bytes.data()+offset,length);end=offset+length;
    }
    // Unchanged resources and Original aliases read the user's BOX directly.
    // Their bytes are never duplicated into the local model cache.
    uint64_t key=14695981039346656037ull;
    for (const auto byte:bytes) { key^=byte;key*=1099511628211ull; }
    uint32_t offset=position;bool duplicate=false;
    for (const auto& previous:unique[key]) {
      if (previous.size!=size || previous.type!=type) continue;
      std::vector<uint8_t> candidate(size);
      output.flush();output.seekg(previous.offset);Read(output,candidate.data(),size);
      if (candidate==bytes) { offset=previous.offset;duplicate=true;break; }
    }
    if (!duplicate) {
      output.seekp(position);Write(output,bytes.data(),size);
      unique[key].push_back({position,size,type,1});position+=size;
    }
    entries[id]={offset,size,type,1};
  }
  output.close();Check(!output.fail());
  { std::ofstream map(stage/kCacheFiles[7],std::ios::binary);
    const uint32_t header[]={0x4F30354Eu,1,new_count};Write(map,header,sizeof(header));
    Write(map,entries.data()+1,(new_count-1)*sizeof(OverlayEntry)); }
  for (size_t i=1;i<=5;++i) {
    const auto size=Read<uint32_t>(delta);Check(size>=8 && size<=262144 && size%4==0);
    std::vector<uint32_t> words(size/4);Read(delta,words.data(),size);Check(words[0]+1==words.size());
    for (size_t j=1;j<words.size();++j) Check(words[j]>0 && words[j]<new_count);
    std::ofstream list(stage/kCacheFiles[i],std::ios::binary);Write(list,words.data(),size);
  }
  std::ofstream info(stage/kCacheFiles[6],std::ios::binary);Write(info,metadata.data(),sizeof(metadata));
  Check(delta.peek()==std::char_traits<char>::eof());
}

inline void DiscardLegacyGeneratedBox(const std::filesystem::path& backup) {
  // Only a newly renamed cache directory is passed here. Recognize the exact
  // old generated archive; never delete an original or an unknown user file.
  constexpr Digest legacy{0xA2,0x89,0x4F,0x42,0xAD,0x34,0xA2,0xBB,
      0x4E,0xB3,0x5C,0x59,0xA1,0x88,0x1B,0x57,0x4F,0x82,0x44,0x0F,
      0x3C,0x43,0xF2,0xEA,0x83,0x17,0xE9,0xAF,0x8E,0xFF,0x26,0xA8};
  const auto path=backup/L"ASYLUM/DUNGEON.BOX";
  Digest found{};
  if (Hash(path,&found) && found==legacy) {
    std::error_code ignored;std::filesystem::remove(path,ignored);
  }
}

// A complete overlay is staged and activated before the first resource open.
// Failures leave the old cache intact. After success, an exact recognized old
// generated BOX is discarded; unrelated backup files are preserved.
inline bool Prepare(const std::filesystem::path& game,const std::filesystem::path& patch,
                    std::string* failure=nullptr) {
  const auto parent=game/L"native50/models",target=parent/L"e3";
  const auto suffix=std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
  const auto stage=parent/(L"e3.build-"+suffix),backup=parent/(L"e3.backup-"+suffix);
  bool owned_stage=false,moved_old=false;
  try {
    std::array<Digest,2+std::size(kCacheFiles)> state{};
    Check(Hash(patch,&state[0]) && Hash(game/kBaseArchive,&state[1]));
    std::ifstream stored(target/L"cache.state",std::ios::binary);
    std::array<Digest,2+std::size(kCacheFiles)> previous{};
    bool valid=static_cast<bool>(stored.read(reinterpret_cast<char*>(previous.data()),sizeof(previous))) &&
        stored.peek()==std::char_traits<char>::eof() && previous[0]==state[0] && previous[1]==state[1];
    stored.close();
    for (size_t i=0;valid && i<std::size(kCacheFiles);++i)
      valid=Hash(target/kCacheFiles[i],&state[i+2]) && state[i+2]==previous[i+2];
    if (valid) return true;
    std::filesystem::create_directories(parent);
    Check(std::filesystem::create_directory(stage));owned_stage=true;
    Build(game/kBaseArchive,patch,stage);
    for (size_t i=0;i<std::size(kCacheFiles);++i) Check(Hash(stage/kCacheFiles[i],&state[i+2]));
    { std::ofstream record(stage/L"cache.state",std::ios::binary);Write(record,state.data(),sizeof(state)); }
    if (std::filesystem::exists(target)) { std::filesystem::rename(target,backup);moved_old=true; }
    try { std::filesystem::rename(stage,target);owned_stage=false; }
    catch (...) { if (moved_old) std::filesystem::rename(backup,target);throw; }
    if (moved_old) DiscardLegacyGeneratedBox(backup);
    return true;
  } catch (...) {
    if (failure) {
      try { throw; } catch (const std::exception& error) { *failure=error.what(); }
      catch (...) { *failure="model cache failure"; }
    }
    // stage is a newly-created, fixed-name child; no game/reference input can
    // supply a deletion target. Never delete the original or backup directory.
    if (owned_stage) { std::error_code ignored;std::filesystem::remove_all(stage,ignored); }
    return false;
  }
}
}  // namespace deathtrap::models
