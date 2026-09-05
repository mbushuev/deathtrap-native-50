#include "widescreen_layout.h"
#include <cmath>
#include <iostream>

int main() {
  const int displays[][2] = {{640,480},{800,600},{1024,768},{1280,1024},
      {1280,720},{1920,1080},{2560,1440},{3840,2160},{1920,1200},
      {2880,1800},{2560,1080},{3440,1440},{5120,1440}};
  for (auto& d : displays) {
    const auto aspect = MonitorAspectX1000(d[0], d[1]);
    const auto width = aspect > 1333 ? WidescreenLogicalWidth(600, aspect) : 800;
    for (int scale = 2; scale <= 4; ++scale) {
      const int height = 600 * scale;
      const int physical = aspect > 1333 ?
          std::max(800 * scale, WidescreenLogicalWidth(height, aspect)) : 800 * scale;
      const double ui_offset = (physical - 800 * scale) / 2.0;
      if (ui_offset < 0 || ui_offset + 800 * scale > physical ||
          std::abs(width * scale - physical) > scale) return 1;
      // Centered 4:3 UI remains uniform; both edge anchors stay on canvas.
      if ((ui_offset + 800 * scale / 2.0) != physical / 2.0) return 2;
    }
    std::cout << d[0] << 'x' << d[1] << " aspect=" << aspect
              << " logical=" << width << "x600 PASS\n";
  }
  if (!WorldPassMarker(1333, false, true, true) ||
      !WorldPassMarker(1600, true, true, true) ||
      WorldPassMarker(1600, false, true, true) ||
      WorldPassMarker(1600, true, true, false)) return 3;
  if (MonitorAspectX1000(0, 0) != 0) return 4;
}
