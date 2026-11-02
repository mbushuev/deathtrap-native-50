#pragma once
#include "core.h"

namespace setup {
// Decode one checksum-verified CAB entirely in memory, before any game writes.
Bytes UnpackPayloadResource(const unsigned char* bytes, size_t size);
}
