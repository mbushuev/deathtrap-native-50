#pragma once

#include <algorithm>
#include <cstdint>

namespace deathtrap::selector_time {

struct Plan {
  bool active = false;
  uint32_t scheduler_rate = 0;
  uint32_t presentation_passes = 0;
  uint32_t source_period_ms = 0;
};

struct KeyboardLatch {
  uint8_t observed_mode = 0;
  uint8_t pending_mask = 0;
  bool armed = false;
};

struct KeyboardCommand {
  bool apply = false;
  uint8_t mode = 0;
};

// Slow motion also slows the retail DirectInput poll. Capture selector keys at
// presentation rate, but commit only after release so the next gameplay tick
// cannot see the same held key and immediately reopen the selector.
inline KeyboardCommand UpdateKeyboardLatch(KeyboardLatch* latch,
                                           uint8_t selector_mode,
                                           uint8_t selector_key_mask,
                                           bool controller_owned) {
  KeyboardCommand command{};
  if (!latch) {
    return command;
  }
  if (controller_owned || selector_mode < 1u || selector_mode > 4u) {
    *latch = {};
    return command;
  }
  selector_key_mask &= 0x0Fu;
  if (latch->observed_mode != selector_mode) {
    latch->observed_mode = selector_mode;
    latch->pending_mask = 0;
    latch->armed = selector_key_mask == 0u;
    return command;
  }
  if (!latch->armed) {
    if (selector_key_mask == 0u) {
      latch->armed = true;
    }
    return command;
  }
  if (latch->pending_mask == 0u) {
    if (selector_key_mask != 0u) {
      latch->pending_mask = static_cast<uint8_t>(
          selector_key_mask & static_cast<uint8_t>(-selector_key_mask));
    }
    return command;
  }
  if ((selector_key_mask & latch->pending_mask) != 0u) {
    return command;
  }

  uint8_t requested_mode = 1u;
  uint8_t bit = latch->pending_mask;
  while ((bit & 1u) == 0u) {
    bit >>= 1u;
    ++requested_mode;
  }
  command.apply = true;
  command.mode = requested_mode == selector_mode ? 0u : requested_mode;
  *latch = {};
  return command;
}

// Dungeon advances gameplay once per scheduler iteration. Its rate setter
// converts the requested frequency to a 10 ms timer period using integer
// division: (100 / rate) * 10. Slowing that source rate preserves all native
// gameplay relationships; additional render-only phases keep presentation
// responsive while the selector owns input.
inline Plan MakePlan(bool enabled, bool gameplay, bool selector_open,
                     uint32_t requested_rate, uint32_t configured_subframes,
                     uint32_t percent) {
  Plan plan;
  plan.scheduler_rate = requested_rate;
  plan.presentation_passes = configured_subframes;
  if (!enabled || !gameplay || !selector_open || requested_rate == 0u ||
      percent == 0u || percent >= 100u) {
    return plan;
  }

  const uint32_t slowed_rate = std::max(
      1u, (requested_rate * std::clamp(percent, 1u, 99u) + 50u) / 100u);
  if (slowed_rate >= requested_rate) {
    return plan;
  }

  const uint32_t base_passes =
      configured_subframes >= 2u ? configured_subframes : 3u;
  const uint32_t dilation = std::max(
      1u, (requested_rate + slowed_rate - 1u) / slowed_rate);
  plan.active = true;
  plan.scheduler_rate = slowed_rate;
  plan.presentation_passes = std::clamp(base_passes * dilation, 3u, 24u);
  plan.source_period_ms = (100u / slowed_rate) * 10u;
  return plan;
}

}  // namespace deathtrap::selector_time
