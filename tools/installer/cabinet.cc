#include "cabinet.h"
#include <fdi.h>
#include <wincrypt.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace setup {
namespace {
constexpr size_t kMaximumPayload = 64u * 1024 * 1024;
struct MemoryCabinet {
  const unsigned char* input;
  size_t inputSize, read = 0, written = 0;
  Bytes output;
  bool opened = false, closed = false;
};
// FDI I/O callbacks have no user argument. Each decode owns its thread's
// memory-only handles; no callback ever opens or creates a filesystem path.
thread_local MemoryCabinet* active = nullptr;
FNALLOC(CabAlloc) { return cb <= kMaximumPayload ? std::malloc(cb) : nullptr; }
FNFREE(CabFree) { std::free(pv); }
FNOPEN(CabOpen) {
  (void)oflag; (void)pmode;
  if (!active || std::strcmp(pszFile, "payload.cab") != 0) return -1;
  active->read = 0;
  return 1;
}
FNREAD(CabRead) {
  if (!active || hf != 1 || active->read > active->inputSize) return static_cast<UINT>(-1);
  const auto size = (std::min)(static_cast<size_t>(cb), active->inputSize - active->read);
  if (size) std::memcpy(pv, active->input + active->read, size);
  active->read += size;
  return static_cast<UINT>(size);
}
FNWRITE(CabWrite) {
  if (!active || hf != 2 || !active->opened || active->closed ||
      active->written > active->output.size() ||
      cb > active->output.size() - active->written) return static_cast<UINT>(-1);
  if (cb) std::memcpy(active->output.data() + active->written, pv, cb);
  active->written += cb;
  return cb;
}
FNCLOSE(CabClose) { return active && (hf == 1 || hf == 2) ? 0 : -1; }
FNSEEK(CabSeek) {
  if (!active || hf != 1) return -1;
  const int64_t base = seektype == SEEK_SET ? 0 : seektype == SEEK_CUR ? static_cast<int64_t>(active->read) :
                       seektype == SEEK_END ? static_cast<int64_t>(active->inputSize) : -1;
  const int64_t next = base + dist;
  if (base < 0 || next < 0 || next > static_cast<int64_t>(active->inputSize)) return -1;
  active->read = static_cast<size_t>(next);
  return static_cast<long>(next);
}
FNFDINOTIFY(CabNotify) {
  auto* memory = static_cast<MemoryCabinet*>(pfdin->pv);
  if (!active || memory != active) return -1;
  switch (fdint) {
    case fdintCABINET_INFO: return 0;
    case fdintCOPY_FILE:
      if (memory->opened || std::strcmp(pfdin->psz1, "payload.bin") != 0 ||
          pfdin->cb != static_cast<long>(memory->output.size())) return -1;
      memory->opened = true;
      return 2;
    case fdintCLOSE_FILE_INFO:
      if (pfdin->hf != 2 || !memory->opened || memory->closed ||
          memory->written != memory->output.size()) return FALSE;
      memory->closed = true;
      return TRUE;
    case fdintENUMERATE: return 0;
    default: return -1;  // Never accept another cabinet or a split file.
  }
}
uint32_t Number(const unsigned char* p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
bool ValidHash(const unsigned char* data, size_t size, const unsigned char* expected) {
  HCRYPTPROV provider = 0; HCRYPTHASH hash = 0;
  if (!CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) return false;
  bool ok = CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash) != 0;
  BYTE digest[32]; DWORD length = sizeof(digest);
  if (ok) ok = CryptHashData(hash, data, static_cast<DWORD>(size), 0) &&
               CryptGetHashParam(hash, HP_HASHVAL, digest, &length, 0);
  if (hash) CryptDestroyHash(hash);
  CryptReleaseContext(provider, 0);
  return ok && length == sizeof(digest) && std::memcmp(digest, expected, sizeof(digest)) == 0;
}
struct Decoder {
  HFDI handle;
  ~Decoder() { if (handle) FDIDestroy(handle); active = nullptr; }
};
}
Bytes UnpackPayloadResource(const unsigned char* bytes, size_t size) {
  if (!bytes || size < 48 || size > kMaximumPayload || std::memcmp(bytes, "N50CAB01", 8) != 0)
    throw std::runtime_error("Invalid compressed installer payload.");
  const uint32_t rawSize = Number(bytes + 40), cabinetSize = Number(bytes + 44);
  if (rawSize < 44 || rawSize > kMaximumPayload || cabinetSize != size - 48 ||
      !ValidHash(bytes + 48, cabinetSize, bytes + 8))
    throw std::runtime_error("Compressed installer payload failed verification. Download it again.");
  if (active) throw std::runtime_error("Installer payload decoder is already active.");
  MemoryCabinet memory{bytes + 48, cabinetSize};
  memory.output.resize(rawSize);
  ERF error{};
  active = &memory;
  Decoder decoder{FDICreate(CabAlloc, CabFree, CabOpen, CabRead, CabWrite, CabClose, CabSeek, cpuUNKNOWN, &error)};
  if (!decoder.handle) throw std::runtime_error("Cannot initialize Windows cabinet decoder.");
  FDICABINETINFO info{};
  if (!FDIIsCabinet(decoder.handle, 1, &info) || info.cbCabinet != static_cast<long>(cabinetSize) ||
      info.cFiles != 1 || info.cFolders != 1 || info.hasprev || info.hasnext)
    throw std::runtime_error("Invalid installer cabinet layout.");
  char name[] = "payload.cab", path[] = "";
  if (!FDICopy(decoder.handle, name, path, 0, CabNotify, nullptr, &memory) ||
      !memory.closed || memory.written != rawSize)
    throw std::runtime_error("Cannot decompress installer payload. Download it again.");
  return std::move(memory.output);
}
}
