#include "../src/model_pack.h"
#include <iostream>
#include <iterator>
using namespace deathtrap::models;
namespace {
void Require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
std::vector<uint8_t> Bytes(const std::filesystem::path& path) {
  std::ifstream file(path,std::ios::binary);Require(file.good(),"fixture read");
  return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
}
void File(const std::filesystem::path& path,const std::vector<uint8_t>& bytes) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path,std::ios::binary);Write(out,bytes.data(),bytes.size());
}
void Word(std::ostream& out,uint32_t word) { Write(out,&word,4); }
void Synthetic(const std::filesystem::path& root) {
  const auto game=root/L"game",base=game/kBaseArchive,patch=root/L"model.n50p";
  std::filesystem::create_directories(base.parent_path());
  { std::ofstream out(base,std::ios::binary);Word(out,8);Word(out,1234);Word(out,14125);
    Entry e{4,4,0x7865742E};for (int i=1;i<14125;++i) Write(out,&e,sizeof(e)); }
  Digest digest{};Require(Hash(base,&digest),"native SHA256 available");
  { std::ofstream out(patch,std::ios::binary);
    for (auto value:{0x5030354Eu,1u,14125u,15345u}) Word(out,value);
    Write(out,digest.data(),digest.size());
    for (auto value:{0x4430354Eu,1u,15345u,14135u,1200u,15335u,10u,14125u}) Word(out,value);
    for (uint32_t i=1;i<15345;++i) {
      Word(out,i==3?2:i==2?1:0);Word(out,i==3?0:1);Word(out,4);Word(out,0x7865742E);
      Word(out,i==2?1:0);
      if (i==2) {Word(out,0);Word(out,4);Word(out,5678);}
      if (i==3) Word(out,9012);
    }
    for (int i=0;i<5;++i) {Word(out,8);Word(out,1);Word(out,1);}
  }
  const auto original=Bytes(base),encoded=Bytes(patch);
  std::string failure;
  if (!Prepare(game,patch,&failure)) throw std::runtime_error("cold cache builds: "+failure);
  const auto cache=game/L"native50/models/e3/model-data.bin";
  { std::ifstream out(cache,std::ios::binary);
    Read<uint32_t>(out);Require(Read<uint32_t>(out)==5678 &&
        Read<uint32_t>(out)==9012,"only changed and literal bytes stored in overlay"); }
  const auto mapping=ReadOverlay(game);
  Require(mapping[1].source==0 && mapping[1].offset==4 && mapping[2].source==1 &&
      mapping[3].source==1 && mapping.back().source==0 &&
      std::filesystem::file_size(cache)==12 &&
      !std::filesystem::exists(game/L"native50/models/e3/ASYLUM/DUNGEON.BOX"),
      "retail resources and aliases stay in original BOX without duplication");
  const auto timestamp=std::filesystem::last_write_time(cache);
  Require(Prepare(game,patch) && std::filesystem::last_write_time(cache)==timestamp,
          "warm cache validates without rewriting");
  auto bad=encoded;bad[80]=9;File(patch,bad);
  Require(!Prepare(game,patch) && std::filesystem::last_write_time(cache)==timestamp &&
          Bytes(base)==original,"malformed delta leaves game and existing cache unchanged");
  File(patch,encoded);File(cache,{1,2,3});
  Require(Prepare(game,patch) && Bytes(base)==original,"damaged cache rebuilds while base stays untouched");
  File(base,{1,2,3});const auto rebuilt=Bytes(cache);
  Require(!Prepare(game,patch) && Bytes(cache)==rebuilt,"unknown base never replaces cache");
}
void Real(const std::filesystem::path& root,const wchar_t* base,const wchar_t* patch,const wchar_t* fixture) {
  const auto game=root/L"game";
  std::filesystem::create_directories((game/kBaseArchive).parent_path());
  std::filesystem::copy_file(base,game/kBaseArchive);
  Digest before{},after{};Require(Hash(base,&before),"base fingerprint");
  std::string failure;
  if (!Prepare(game,patch,&failure))
    throw std::runtime_error("real release delta reconstruction: "+failure);
  const auto output=game/L"native50/models/e3/model-data.bin";
  const auto aa=ReadOverlay(game);const auto count=static_cast<uint32_t>(aa.size());
  std::ifstream retail(game/kBaseArchive,std::ios::binary),overlay(output,std::ios::binary),
      b(std::filesystem::path(fixture)/L"DUNGEON-E3-RECONSTRUCTION.BOX",std::ios::binary);
  const auto bi=Read<uint32_t>(b);b.seekg(bi);
  Require(count==Read<uint32_t>(b),"resource count matches tested model");
  std::vector<Entry> bb(count);Read(b,bb.data()+1,(count-1)*12);
  for (uint32_t i=1;i<count;++i) {
    Require(aa[i].size==bb[i].size && aa[i].type==bb[i].type,"resource metadata matches");
    std::vector<uint8_t> x(aa[i].size),y(bb[i].size);
    auto& a=aa[i].source?overlay:retail;
    a.seekg(aa[i].offset);b.seekg(bb[i].offset);Read(a,x.data(),x.size());Read(b,y.data(),y.size());
    Require(x==y,"every decoded resource must exactly match the playtested dual model");
  }
  for (size_t i=1;i<=5;++i) Require(Bytes(game/L"native50/models/e3"/kCacheFiles[i])==
      Bytes(std::filesystem::path(fixture)/L"patched-pst"/std::filesystem::path(kCacheFiles[i]).filename()),"preload list matches");
  Require(Bytes(game/L"native50/models/e3/dual-models.bin")==Bytes(std::filesystem::path(fixture)/L"dual-models.bin"),"metadata matches");
  Require(Hash(base,&after) && before==after && Hash(game/kBaseArchive,&after) &&
      before==after && Prepare(game,patch),"original input unchanged; actual warm cache passes");
  Require(!std::filesystem::exists(game/L"native50/models/e3/ASYLUM/DUNGEON.BOX"),"no copied retail archive");
  std::cout<<"Model overlay: "<<count-1<<" resource bytes and all preload lists match; cache data="
      <<std::filesystem::file_size(output)<<" bytes, Original aliases read retail BOX directly\n";
}
}
int wmain(int argc,wchar_t** argv) {
  const auto root=std::filesystem::temp_directory_path()/(L"native50-model-pack-test-"+std::to_wstring(GetCurrentProcessId()));
  bool owned=false;
  try {
    Require(!std::filesystem::exists(root),"private test root must be new");
    Require(std::filesystem::create_directory(root),"create private test root");owned=true;
    if (argc==1) Synthetic(root);
    else {Require(argc==4,"Pass base BOX, model delta and tested dual-model fixture directory");Real(root,argv[1],argv[2],argv[3]);}
    std::filesystem::remove_all(root);
    std::cout<<"Model delta cache: validation, recovery and original-file isolation passed\n";return 0;
  } catch (const std::exception& e) {
    std::cerr<<e.what()<<'\n';std::error_code ignored;
    if (owned) std::filesystem::remove_all(root,ignored);return 1;
  }
}
