#include <windows.h>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {
int64_t test_clock = 1000;
uint32_t clock_reads = 0;
uint64_t test_cpu_time = 100000;
uint32_t cpu_clock_reads = 0;
bool cpu_clock_available = true;
BOOL WINAPI ReadTimingTestClock(LARGE_INTEGER* out) {
  ++clock_reads;
  out->QuadPart = test_clock;
  return TRUE;
}
BOOL WINAPI ReadTimingTestThreadTimes(HANDLE, LPFILETIME creation,
                                     LPFILETIME exit, LPFILETIME kernel,
                                     LPFILETIME user) {
  ++cpu_clock_reads;
  if (!cpu_clock_available) { return FALSE; }
  *creation = *exit = *kernel = {};
  user->dwLowDateTime = static_cast<DWORD>(test_cpu_time);
  user->dwHighDateTime = static_cast<DWORD>(test_cpu_time >> 32u);
  return TRUE;
}
}

// Exercise the actual opt-in scopes and log writer with a deterministic clock,
// without launching a game, changing input, or requiring a graphics device.
#define QueryPerformanceCounter ReadTimingTestClock
#define GetThreadTimes ReadTimingTestThreadTimes
#include "../src/deathtrap_native_render_patch.cc"
#undef GetThreadTimes
#undef QueryPerformanceCounter

namespace {
uint32_t retail_calls = 0;
void __cdecl FakeRetailRender(void*, int) {
  ++retail_calls;
  const uint64_t present = BeginDeathtrapPresentTiming();
  test_clock += 5;
  EndDeathtrapPresentTiming(present, 0, 0, 0);
}
deathtrap::timing::Mode TestMode(uint32_t subframes) {
  deathtrap::timing::Mode mode;
  mode.requested_rate = mode.scheduler_rate = 16;
  mode.subframes = subframes;
  mode.presentation_passes = subframes ? subframes : 1u;
  mode.gameplay = mode.foreground = true;
  return mode;
}
void MeasuredFrame(uint32_t index, uint32_t subframes) {
  test_clock = 1000 + static_cast<int64_t>(index) * 60;
  NativeTimingTick tick;
  tick.Configure(TestMode(subframes));
  if (subframes) {
    {
      const NativeTimingSpan synthetic(NativeTimingSpanKind::kSynthetic);
      for (uint32_t i = 0; i < 2; ++i) {
        const uint64_t present = BeginDeathtrapPresentTiming();
        test_clock += 3;
        EndDeathtrapPresentTiming(present, 0, 0, 0);
        g_interpolated_frames.fetch_add(1, std::memory_order_relaxed);
      }
      test_clock += 4;
    }
    {
      const NativeTimingSpan pacing(NativeTimingSpanKind::kPacing);
      test_clock += 40;
    }
  }
  CallOriginalRenderPresentWait(nullptr, 16);
}
std::string ReadLog() {
  HANDLE file = CreateFileW(GetDeathtrapSessionLogPath(), GENERIC_READ,
      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
  if (file == INVALID_HANDLE_VALUE) { return {}; }
  const DWORD size = GetFileSize(file, nullptr);
  if (size == INVALID_FILE_SIZE || size > 1024u * 1024u) {
    CloseHandle(file); return {};
  }
  std::vector<char> data(size);
  DWORD read = 0;
  const bool ok = ReadFile(file, data.data(), size, &read, nullptr) != FALSE;
  CloseHandle(file);
  return ok ? std::string(data.data(), read) : std::string();
}
}

int main() {
  g_qpc_frequency.QuadPart = 1000;
  g_original_render_present_wait = &FakeRetailRender;
  g_debug_log = true;
  g_extended_log_enabled.store(false);
  {
    NativeTimingTick disabled;
    if (disabled.Active() || BeginDeathtrapPresentTiming() != 0) { return 1; }
    const DeathtrapDiagnosticScope guard(deathtrap::timing::Part::kGuard);
    CallOriginalRenderPresentWait(nullptr, 16);
    const NativeTimingSpan pacing(NativeTimingSpanKind::kPacing);
    EndDeathtrapPresentTiming(123, 0, 0, 0);
  }
  if (clock_reads != 0 || cpu_clock_reads != 0 || retail_calls != 1 ||
      g_active_timing_frame || DeathtrapExtendedLogsEnabled()) {
    std::cerr << "disabled ExtendedLogs performed a clock query or changed a call\n";
    return 2;
  }
  g_extended_log_enabled.store(true);
  g_debug_log = false;
  g_qpc_frequency.QuadPart = 0;
  {
    NativeTimingTick invalid_clock;
    const DeathtrapDiagnosticScope guard(deathtrap::timing::Part::kGuard);
    if (invalid_clock.Active()) { return 3; }
  }
  if (clock_reads != 0 || cpu_clock_reads != 0) { return 4; }
  g_qpc_frequency.QuadPart = 1000;
  {
    const DeathtrapDiagnosticScope unscoped(deathtrap::timing::Part::kGuard);
  }
  if (clock_reads != 0 || cpu_clock_reads != 0) {
    std::cerr << "unscoped diagnostics queried a clock\n";
    return 9;
  }
  for (uint32_t i = 0; i < 35; ++i) { MeasuredFrame(i, 3); }
  MeasuredFrame(35, 0);
  MeasuredFrame(36, 0);
  MeasuredFrame(37, 3);
  if (g_active_timing_frame || BeginDeathtrapPresentTiming() != 0 ||
      retail_calls != 39 || !clock_reads) { return 5; }
  const std::string log = ReadLog();
  for (const char* field : {
           "support_timing schema=1", "reason=periodic", "reason=mode_change",
           "source_hz=16.667", "outside_avg_ms=5.000", "present_observed=1",
           "dxgi_ok_hz=16.667", "present_avg_ms=3.667", "subframes=0 passes=1",
           "pacing_avg_ms=40.000", "retail_avg_ms=5.000"}) {
    if (log.find(field) == std::string::npos) {
      std::cerr << "missing actual runtime log field: " << field << '\n';
      return 6;
    }
  }
  const size_t rate_field = log.find("dxgi_ok_hz=");
  const double observed = rate_field == std::string::npos ? 0.0
      : std::strtod(log.c_str() + rate_field + 11u, nullptr);
  if (observed < 49.0 || observed > 51.0) {
    std::cerr << "periodic Present cadence was not measured correctly\n";
    return 8;
  }
  if (log.find("presentation_pair") != std::string::npos) { return 7; }

  // A separate window preserves the original 50 Hz cadence assertions above.
  // Model the actual ordering: guard readback completes before the legacy
  // Present timer starts. Inclusive hook/guard spans must retain that wait.
  using deathtrap::timing::Part;
  g_timing_window = {};
  g_timing_clock_origin = 0;
  g_qpc_frequency.QuadPart = 1000000;
  test_clock = 10000000;
  {
    NativeTimingTick tick;
    tick.Configure(TestMode(3));
    const auto wall_before = clock_reads;
    const auto cpu_before = cpu_clock_reads;
    { const DeathtrapDiagnosticScope disabled(Part::kGuard, false); }
    if (wall_before != clock_reads || cpu_before != cpu_clock_reads) {
      std::cerr << "explicitly disabled scope queried a clock\n";
      return 10;
    }
    {
      const DeathtrapDiagnosticScope hook(Part::kPresentHook);
      {
        const DeathtrapDiagnosticScope guard(Part::kGuard);
        {
          const DeathtrapDiagnosticScope copies(Part::kGuardCopies);
          test_clock += 1000;
          test_cpu_time += 5000;  // 0.5 ms of CPU in 1 ms of wall time.
        }
        {
          const DeathtrapDiagnosticScope map(Part::kGuardMap);
          test_clock += 24000;
          test_cpu_time += 1000;  // 0.1 ms of CPU in a 24 ms wait.
        }
      }
      const uint64_t present = BeginDeathtrapPresentTiming();
      test_clock += 250;
      test_cpu_time += 500;
      EndDeathtrapPresentTiming(present, 1, 0, 0);
    }
    const auto& guard = g_active_timing_frame->parts[static_cast<size_t>(Part::kGuard)];
    const auto& hook = g_active_timing_frame->parts[static_cast<size_t>(Part::kPresentHook)];
    if (guard.wall.count != 1 || std::abs(guard.wall.total - 25.0) > 0.001 ||
        std::abs(guard.cpu.total - 0.6) > 0.001 || hook.wall.count != 1 ||
        std::abs(hook.wall.total - 25.25) > 0.001 ||
        std::abs(g_active_timing_frame->present_ms - 0.25) > 0.001) {
      std::cerr << "guard wait was hidden in or confused with original Present\n";
      return 11;
    }
    cpu_clock_available = false;
    {
      const DeathtrapDiagnosticScope no_cpu(Part::kRenderer);
      test_clock += 1000;
    }
    cpu_clock_available = true;
    const auto& renderer = g_active_timing_frame->parts[static_cast<size_t>(Part::kRenderer)];
    if (renderer.wall.count != 1 || renderer.cpu.count != 0) {
      std::cerr << "failed thread-clock read fabricated CPU work or lost wall time\n";
      return 12;
    }
  }
  test_clock += 1000;
  { NativeTimingTick flush; flush.Configure(TestMode(0)); }
  if (retail_calls != 39 || g_active_timing_frame) {
    std::cerr << "diagnostic scopes replayed original calls or leaked frame state\n";
    return 13;
  }
  const std::string detailed_log = ReadLog();
  for (const char* field : {
           "support_timing_part", "name=guard calls=1 wall_avg_ms=25.000",
           "name=guard_map calls=1 wall_avg_ms=24.000",
           "name=guard_copies calls=1 wall_avg_ms=1.000",
           "name=present_hook calls=1 wall_avg_ms=25.250",
           "present_avg_ms=0.250", "cpu_samples=1 cpu_avg_ms=0.600",
           "cpu_samples=0", "inclusive=1"}) {
    if (detailed_log.find(field) == std::string::npos) {
      std::cerr << "missing component runtime log field: " << field << '\n';
      return 14;
    }
  }
  std::cout << "timing runtime tests passed: off=no clock queries; "
               "on=bounded logs with DebugLog=0; original calls unchanged\n";
  return 0;
}
