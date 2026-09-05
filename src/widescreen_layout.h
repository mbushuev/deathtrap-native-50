#pragma once
#include <algorithm>
#include <cstdint>

inline uint32_t MonitorAspectX1000(int32_t width, int32_t height) {
  if (width <= 0 || height <= 0) return 0;
  return static_cast<uint32_t>(std::clamp<int64_t>(
      (int64_t(width) * 1000 + height / 2) / height, 1, 10000));
}
inline int32_t WidescreenLogicalWidth(int32_t height, uint32_t aspect) {
  return static_cast<int32_t>((int64_t(height) * aspect + 500) / 1000);
}
inline bool WorldPassMarker(uint32_t aspect, bool expanded,
                            bool context, bool player) {
  return context && player && (aspect <= 1333 || expanded);
}
