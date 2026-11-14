#pragma once

#include <array>
#include <cstddef>
#include <cmath>

namespace deathtrap::projective_quad {

struct Point {
  double x, y, z;
  Point operator-(Point other) const { return {x-other.x,y-other.y,z-other.z}; }
};
struct UV { double u, v; };
struct TexturePoint { float u, v, q; };

inline Point Cross(Point a, Point b) {
  return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
}
inline double Dot(Point a, Point b) {
  return a.x*b.x+a.y*b.y+a.z*b.z;
}

struct Mapping {
  std::array<Point, 3> basis{};
  std::array<double, 3> weight{};
  std::array<UV, 3> uv{};
  bool Project(float x, float y, float rhw, TexturePoint& out) const {
    if (!std::isfinite(rhw) || rhw <= 0) return false;
    const Point screen{x / double(rhw), y / double(rhw), 1.0 / rhw};
    double u = 0, v = 0, q = 0;
    for (std::size_t i = 0; i < basis.size(); ++i) {
      const double w = weight[i] * Dot(basis[i], screen);
      u += uv[i].u * w;
      v += uv[i].v * w;
      q += w;
    }
    if (!std::isfinite(u) || !std::isfinite(v) || !(q > 0.0001 && q < 10000)) return false;
    out = {static_cast<float>(u), static_cast<float>(v), static_cast<float>(q)};
    return true;
  }
};

// Fit one homography between a planar face and its four authored UVs.
// Project operates on camera-space positions recovered from the clipped draw.
inline bool Build(const std::array<Point, 4>& points,
                  const std::array<UV, 4>& uv,
                  double scale_x, double scale_y, double center_x,
                  double center_y, Mapping& result) {
  const Point a = points[1]-points[0], b = points[3]-points[0];
  const Point c = points[2]-points[0];
  const Point n = Cross(a, b);
  const double area = Dot(n, n);
  if (!(area > 0) || std::abs(Dot(n, c)) > std::sqrt(area) * 2.0) return false;
  const double s = Dot(Cross(c, b), n) / area;
  const double t = Dot(Cross(a, c), n) / area;
  if (!(s > 0.0001 && t > 0.0001 && s+t > 1.0001)) return false;

  const auto uv_cross = [](UV x, UV y) { return x.u*y.v-x.v*y.u; };
  const UV p{uv[1].u-uv[0].u, uv[1].v-uv[0].v};
  const UV q{uv[3].u-uv[0].u, uv[3].v-uv[0].v};
  const UV r{uv[2].u-uv[0].u, uv[2].v-uv[0].v};
  const double determinant = uv_cross(p, q);
  if (std::abs(determinant) < 1e-10) return false;
  const double u = uv_cross(r, q) / determinant;
  const double v = uv_cross(p, r) / determinant;
  if (!(u > 0.0001 && v > 0.0001 && u+v > 1.0001)) return false;
  if (std::abs(u-s) < 0.002 && std::abs(v-t) < 0.002) return false;

  const auto screen = [=](Point p) {
    return Point{p.x*scale_x + center_x*p.z, p.y*scale_y + center_y*p.z, p.z};
  };
  const Point p0 = screen(points[0]), p1 = screen(points[1]), p3 = screen(points[3]);
  result.basis = {Cross(p1,p3), Cross(p3,p0), Cross(p0,p1)};
  const double volume = Dot(p0, result.basis[0]);
  if (!std::isfinite(volume) || std::abs(volume) < 1e-6) return false;
  const double normalization = (s+t-1) / (u+v-1);
  result.weight = {1.0/volume, normalization*u/(volume*s), normalization*v/(volume*t)};
  result.uv = {uv[0], uv[1], uv[3]};
  return true;
}

}  // namespace deathtrap::projective_quad
