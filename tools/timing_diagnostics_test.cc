#include <iostream>
#include <limits>

#include "timing_diagnostics.h"

namespace {
using namespace deathtrap::timing;

bool Expect(bool value, const char* message) {
  if (!value) { std::cerr << message << '\n'; }
  return value;
}
bool Near(double a, double b) { return std::abs(a - b) < 0.001; }
Mode Normal() {
  Mode mode;
  mode.requested_rate = mode.scheduler_rate = 16;
  mode.subframes = mode.presentation_passes = 3;
  mode.gameplay = mode.foreground = true;
  return mode;
}
Frame Sample(double begin, double hook = 55.0, uint32_t passes = 3) {
  Frame frame;
  frame.begin_ms = begin;
  frame.end_ms = begin + hook;
  frame.synthetic_ms = 10.0;
  frame.retail_ms = 5.0;
  frame.pacing_ms = 40.0;
  frame.synthetic_renders = passes - 1u;
  frame.retail_calls = 1;
  for (uint32_t i = 0; i < passes; ++i) {
    RecordPresent(&frame, 3.0, 0, 0, 0, begin + 3.0 + i * 20.0);
  }
  return frame;
}
}  // namespace

int main() {
  Window normal;
  Summary summary;
  const Mode mode = Normal();
  for (uint32_t i = 0; i < 34; ++i) {
    const bool emitted = normal.Add(mode, Sample(i * 60.0), &summary);
    if (!Expect(emitted == (i == 33u), "two-second cadence was not bounded")) {
      return 1;
    }
  }
  if (!Expect(summary.ticks == 34u && !summary.mode_changed,
              "normal cadence window was incomplete") ||
      !Expect(Near(summary.source_intervals.Hertz(), 1000.0 / 60.0),
              "source rate must be measured, not the integer 16 Hz request") ||
      !Expect(Near(summary.outside_hooks.Average(), 5.0),
              "gameplay outside the render hook was not measured") ||
      !Expect(summary.present_ok == 102u && Near(summary.presents.Average(), 3.0),
              "actual Present submissions were not accumulated") ||
      !Expect(Near(summary.PresentHertz(), 50.0) &&
              Near(summary.RenderHertz(), 50.0), "bad measured present rate")) {
    return 1;
  }
  for (uint32_t i = 34; i < 68; ++i) {
    const bool emitted = normal.Add(mode, Sample(i * 60.0), &summary);
    if (!Expect(emitted == (i == 67u), "periodic window lost cadence")) { return 1; }
  }
  if (!Expect(summary.source_intervals.count == 34u,
              "periodic flushing discarded an inter-tick interval")) { return 1; }

  Window delayed;
  for (uint32_t i = 0; i <= 20; ++i) {
    delayed.Add(mode, Sample(i * 100.0, 90.0), &summary);
  }
  if (!Expect(Near(summary.source_intervals.Hertz(), 10.0),
              "a slow machine must report 10 Hz, not the configured rate") ||
      !Expect(summary.PresentHertz() < 31.0,
              "slowed source cadence must reduce measured presentation rate")) {
    return 1;
  }

  Window selectors;
  Mode slow = mode;
  slow.scheduler_rate = 4;
  slow.slow_motion = true;
  slow.selector_mode = 1;
  slow.presentation_passes = 12;
  for (uint32_t i = 0; i <= 8; ++i) {
    selectors.Add(slow, Sample(i * 250.0, 245.0, 12), &summary);
  }
  if (!Expect(summary.mode.slow_motion &&
              Near(summary.source_intervals.Hertz(), 4.0),
              "intentional selector time dilation was mixed with normal play")) {
    return 1;
  }

  Window transitions;
  Mode native = mode;
  native.subframes = 0;
  native.presentation_passes = 1;
  transitions.Add(mode, Sample(0), &summary);
  transitions.Add(mode, Sample(60), &summary);
  if (!Expect(transitions.Add(native, Sample(120, 55, 1), &summary) &&
              summary.mode_changed && summary.mode.subframes == 3u &&
              summary.ticks == 2u, "F11 did not flush the old mode")) { return 1; }
  transitions.Add(native, Sample(180, 55, 1), &summary);
  if (!Expect(transitions.Add(mode, Sample(240), &summary) &&
              summary.mode.subframes == 0 && summary.source_intervals.count == 1u,
              "F11 transition interval contaminated the new mode")) { return 1; }
  if (!Expect(Near(summary.PresentHertz(), 1000.0 / 60.0) &&
              Near(summary.RenderHertz(), 1000.0 / 60.0),
              "a short native-rate window inflated FPS at its boundary")) { return 1; }
  Mode background = mode;
  background.foreground = false;
  transitions.Add(background, Sample(6000), &summary);
  transitions.Add(background, Sample(6060), &summary);
  transitions.Add(mode, Sample(6120), &summary);
  if (!Expect(summary.mode.foreground == false && summary.long_gaps == 0 &&
              summary.source_intervals.count == 1u,
              "focus changes contaminated timing with the Alt-Tab gap")) { return 1; }

  Window gaps;
  gaps.Add(mode, Sample(0), &summary);
  gaps.Add(mode, Sample(1500), &summary);
  gaps.Add(native, Sample(1560, 55, 1), &summary);
  if (!Expect(summary.long_gaps == 1u &&
              Near(summary.source_intervals.maximum, 1500.0),
              "unexplained long source gaps were silently discarded")) { return 1; }
  Frame invalid = Sample(1600);
  invalid.end_ms = std::numeric_limits<double>::quiet_NaN();
  if (!Expect(!gaps.Add(mode, invalid, &summary) &&
              !gaps.Add(mode, Sample(-1), &summary), "invalid clock accepted")) { return 1; }

  Frame presents;
  RecordPresent(&presents, 1.0, 0, 0, 0);
  RecordPresent(&presents, 2.0, 2, 8, -1);
  RecordPresent(&presents, 3.0, 1, 0, 0x087A0001);  // DXGI_STATUS_OCCLUDED
  RecordPresent(&presents, 4.0, 4, 1, 0);           // DXGI_PRESENT_TEST
  RecordPresent(nullptr, 10.0, 0, 0, 0);
  if (!Expect(presents.present_calls == 4u && presents.present_ok == 1u &&
              presents.present_failures == 1u && presents.present_non_display == 2u &&
              presents.sync_min == 0u && presents.sync_max == 4u &&
              presents.present_flags == 9u && Near(presents.present_max_ms, 4.0),
              "failed/occluded/test Present calls were counted as real frames")) {
    return 1;
  }
  Window unobserved;
  Frame without_dxgi = Sample(0);
  without_dxgi.present_calls = without_dxgi.present_ok = without_dxgi.timed_present_ok = 0;
  unobserved.Add(mode, without_dxgi, &summary);
  unobserved.Add(native, Sample(60, 55, 1), &summary);
  if (!Expect(summary.present_calls == 0u && summary.retail_calls == 1u,
              "a missing DXGI hook fabricated an observed present count")) { return 1; }
  std::cout << "timing diagnostics tests passed\n";
  return 0;
}
