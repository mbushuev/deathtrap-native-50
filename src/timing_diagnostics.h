#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace deathtrap::timing {

// Diagnostics only: these types never schedule, sleep, poll input or write
// gameplay state. Times are monotonic wall-clock milliseconds, not game ticks.
struct Mode {
  int requested_rate = 0;
  int scheduler_rate = 0;
  uint32_t subframes = 0;
  uint32_t presentation_passes = 1;
  uint32_t selector_mode = 0;
  bool slow_motion = false;
  bool gameplay = false;
  bool foreground = false;

  bool operator==(const Mode& other) const {
    return requested_rate == other.requested_rate &&
        scheduler_rate == other.scheduler_rate && subframes == other.subframes &&
        presentation_passes == other.presentation_passes &&
        selector_mode == other.selector_mode &&
        slow_motion == other.slow_motion && gameplay == other.gameplay &&
        foreground == other.foreground;
  }
};

struct Values {
  uint64_t count = 0;
  double total = 0.0;
  double maximum = 0.0;

  void Add(double value) {
    if (!std::isfinite(value) || value < 0.0) {
      return;
    }
    ++count;
    total += value;
    maximum = std::max(maximum, value);
  }
  double Average() const { return count ? total / count : 0.0; }
  double Hertz() const { return total > 0.0 ? count * 1000.0 / total : 0.0; }
};

struct Frame {
  double begin_ms = 0.0;
  double end_ms = 0.0;
  double synthetic_ms = 0.0;
  double retail_ms = 0.0;
  double pacing_ms = 0.0;
  double present_ms = 0.0;
  double present_max_ms = 0.0;
  uint64_t synthetic_renders = 0;
  uint64_t retail_calls = 0;
  uint64_t present_calls = 0;
  uint64_t present_ok = 0;
  uint64_t timed_present_ok = 0;
  double first_present_ms = -1.0;
  double last_present_ms = -1.0;
  uint64_t present_failures = 0;
  uint64_t present_non_display = 0;
  uint64_t restore_suppressed = 0;
  uint32_t sync_min = UINT32_MAX;
  uint32_t sync_max = 0;
  uint32_t present_flags = 0;
  int32_t last_present_result = 0;
};

struct Summary {
  Mode mode;
  double begin_ms = 0.0;
  double end_ms = 0.0;
  uint64_t ticks = 0;
  Values source_intervals;
  Values hooks;
  Values outside_hooks;
  Values synthetic;
  Values retail;
  Values pacing;
  Values presents;
  uint64_t synthetic_renders = 0;
  uint64_t retail_calls = 0;
  uint64_t present_calls = 0;
  uint64_t present_ok = 0;
  uint64_t timed_present_ok = 0;
  double first_present_ms = -1.0;
  double last_present_ms = -1.0;
  uint64_t present_failures = 0;
  uint64_t present_non_display = 0;
  uint64_t restore_suppressed = 0;
  uint64_t long_gaps = 0;
  uint32_t sync_min = UINT32_MAX;
  uint32_t sync_max = 0;
  uint32_t present_flags = 0;
  int32_t last_present_result = 0;
  bool mode_changed = false;

  double WallMilliseconds() const { return std::max(0.0, end_ms - begin_ms); }
  double Rate(uint64_t count) const {
    const double elapsed = WallMilliseconds();
    return elapsed > 0.0 ? count * 1000.0 / elapsed : 0.0;
  }
  double RenderHertz() const {
    return ticks ? source_intervals.Hertz() *
        static_cast<double>(synthetic_renders + retail_calls) / ticks : 0.0;
  }
  double PresentHertz() const {
    const double span = last_present_ms - first_present_ms;
    return timed_present_ok > 1u && span > 0.0
        ? (timed_present_ok - 1u) * 1000.0 / span : 0.0;
  }
};

class Window {
 public:
  // A transition flushes the OLD mode before collecting the new one. Source
  // intervals crossing F11, selector, foreground or menu changes are excluded.
  // Periodic flushes retain the previous tick's clock, so inter-frame gameplay
  // work remains visible instead of measuring just our render hook.
  bool Add(const Mode& mode, const Frame& frame, Summary* report) {
    if (!report || !std::isfinite(frame.begin_ms) ||
        !std::isfinite(frame.end_ms) || frame.end_ms < frame.begin_ms ||
        (have_previous_ && frame.begin_ms < previous_begin_ms_)) {
      return false;
    }
    const bool changed = current_.ticks && !(mode == current_.mode);
    if (changed) {
      *report = current_;
      report->mode_changed = true;
      current_ = {};
    }
    if (!current_.ticks) {
      current_.mode = mode;
      current_.begin_ms = frame.begin_ms;
    }
    current_.end_ms = frame.end_ms;
    ++current_.ticks;
    if (have_previous_ && mode == previous_mode_) {
      const double interval = frame.begin_ms - previous_begin_ms_;
      if (interval > 0.0) {
        current_.source_intervals.Add(interval);
        current_.outside_hooks.Add(
            std::max(0.0, frame.begin_ms - previous_end_ms_));
        current_.long_gaps += interval >= 1000.0 ? 1u : 0u;
      }
    }
    current_.hooks.Add(frame.end_ms - frame.begin_ms);
    current_.synthetic.Add(frame.synthetic_ms);
    current_.retail.Add(frame.retail_ms);
    current_.pacing.Add(frame.pacing_ms);
    if (frame.present_calls) {
      current_.presents.count += frame.present_calls;
      current_.presents.total += frame.present_ms;
      current_.presents.maximum = std::max(
          current_.presents.maximum, frame.present_max_ms);
      current_.sync_min = std::min(current_.sync_min, frame.sync_min);
      current_.sync_max = std::max(current_.sync_max, frame.sync_max);
      current_.last_present_result = frame.last_present_result;
    }
    current_.synthetic_renders += frame.synthetic_renders;
    current_.retail_calls += frame.retail_calls;
    current_.present_calls += frame.present_calls;
    current_.present_ok += frame.present_ok;
    if (frame.timed_present_ok) {
      if (!current_.timed_present_ok) {
        current_.first_present_ms = frame.first_present_ms;
      }
      current_.last_present_ms = frame.last_present_ms;
      current_.timed_present_ok += frame.timed_present_ok;
    }
    current_.present_failures += frame.present_failures;
    current_.present_non_display += frame.present_non_display;
    current_.restore_suppressed += frame.restore_suppressed;
    current_.present_flags |= frame.present_flags;
    have_previous_ = true;
    previous_mode_ = mode;
    previous_begin_ms_ = frame.begin_ms;
    previous_end_ms_ = frame.end_ms;
    if (!changed && current_.WallMilliseconds() >= 2000.0) {
      *report = current_;
      current_ = {};
      return true;
    }
    return changed;
  }

 private:
  Summary current_;
  bool have_previous_ = false;
  Mode previous_mode_;
  double previous_begin_ms_ = 0.0;
  double previous_end_ms_ = 0.0;
};

// Count completed DXGI submissions, not attempted/suppressed page flips. TEST
// calls and nonzero success statuses (e.g. OCCLUDED) are not visible presents.
inline void RecordPresent(Frame* frame, double elapsed_ms,
                          uint32_t sync_interval, uint32_t flags,
                          int32_t result, double completion_ms = -1.0) {
  if (!frame || !std::isfinite(elapsed_ms) || elapsed_ms < 0.0) {
    return;
  }
  ++frame->present_calls;
  frame->present_ms += elapsed_ms;
  frame->present_max_ms = std::max(frame->present_max_ms, elapsed_ms);
  frame->sync_min = std::min(frame->sync_min, sync_interval);
  frame->sync_max = std::max(frame->sync_max, sync_interval);
  frame->present_flags |= flags;
  frame->last_present_result = result;
  if (result < 0) {
    ++frame->present_failures;
  } else if (result == 0 && !(flags & 1u)) {
    ++frame->present_ok;
    if (std::isfinite(completion_ms) && completion_ms >= 0.0) {
      if (!frame->timed_present_ok) {
        frame->first_present_ms = completion_ms;
      }
      frame->last_present_ms = completion_ms;
      ++frame->timed_present_ok;
    }
  } else {
    ++frame->present_non_display;
  }
}

}  // namespace deathtrap::timing
