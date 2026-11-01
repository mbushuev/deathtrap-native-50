#pragma once
#include <algorithm>
#include <cmath>

namespace deathtrap::ui {
struct Transform {
  double x = 0, y = 0;
  int percent = 100;
  double offset_x = 0, offset_y = 0;
  double X(double value) const { return x + (value-x)*percent/100.0 + offset_x; }
  double Y(double value) const { return y + (value-y)*percent/100.0 + offset_y; }
};
inline Transform Anchored(int left,int top,int right,int bottom,int horizontal,int vertical,int percent) {
  return {horizontal==0 ? double(left) : horizontal==1 ? double(right) : (left+right)/2.0,
          vertical==0 ? double(top) : vertical==1 ? double(bottom) : (top+bottom)/2.0,
          std::clamp(percent,75,150)};
}
inline Transform Radial(int left,int top,int right,int bottom,int radius,int from_bottom,int percent) {
  Transform result{(left+right)/2.0,double(bottom-from_bottom+32),std::clamp(percent,75,150)};
  const double extent=std::min((radius+16)*result.percent/100.0,(bottom-top)/2.0);
  result.offset_y=std::clamp(result.y,top+extent,bottom-extent)-result.y;
  return result;
}
} // namespace deathtrap::ui
