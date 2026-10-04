#pragma once

#include <cstdint>
#include <limits>

namespace deathtrap::input {

inline uint16_t ApplyVibrationOutputGain(uint16_t motor_value,
                                         uint32_t gain_percent) {
  const uint64_t scaled =
      static_cast<uint64_t>(motor_value) * gain_percent + 50u;
  const uint64_t gained = scaled / 100u;
  return static_cast<uint16_t>(
      gained > std::numeric_limits<uint16_t>::max()
          ? std::numeric_limits<uint16_t>::max()
          : gained);
}

}  // namespace deathtrap::input
