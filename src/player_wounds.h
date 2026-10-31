#pragma once
#include <array>
#include <cstdint>

namespace deathtrap::wounds {
// Native part order differs between the base heroine and the injury/team
// families. Keep the bone and team; select only that part's healthy variant.
constexpr uint32_t HealthyPart(uint32_t id) {
  constexpr std::array<uint32_t,20> heroine{
      3890,3899,3900,3901,3902,3897,3894,3892,3889,3887,
      3885,3903,3888,3896,3886,3895,3891,3893,3904,3898};
  constexpr std::array<uint32_t,15> hero{
      3871,3872,3873,3874,3875,3876,3877,3878,3828,3879,
      3880,3881,3882,3883,3884};
  if (id>=4070 && id<=4289) return heroine[(id-4070)%20];
  if (id>=3905 && id<=4069) return hero[(id-3905)%15];
  if (id>=8139 && id<=9098) {
    const uint32_t group=8139+((id-8139)/240)*240;
    return group+(id-group)%20;
  }
  if (id>=7419 && id<=8138) {
    const uint32_t group=7419+((id-7419)/180)*180;
    return group+(id-group)%15;
  }
  return id;
}
}  // namespace deathtrap::wounds
