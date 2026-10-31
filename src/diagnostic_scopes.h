#pragma once

#include <cstdint>

namespace deathtrap::timing {

// Observational scopes are inclusive and may overlap. In particular the guard
// and its GPU readback belong to the complete Present hook, not DXGI Present.
enum class Part : uint8_t {
  kSourceHook,
  kRenderer,
  kVisibleFlip,
  kRestoreFlip,
  kPresentHook,
  kGuard,
  kGuardCopies,
  kGuardMap,
  kCount,
};

struct Stamp {
  uint64_t wall_ticks = 0;
  uint64_t thread_cpu_100ns = 0;
  bool cpu_valid = false;
};

inline const char* PartName(Part part) {
  switch (part) {
    case Part::kSourceHook: return "source_hook";
    case Part::kRenderer: return "renderer";
    case Part::kVisibleFlip: return "visible_flip";
    case Part::kRestoreFlip: return "restore_flip";
    case Part::kPresentHook: return "present_hook";
    case Part::kGuard: return "guard";
    case Part::kGuardCopies: return "guard_copies";
    case Part::kGuardMap: return "guard_map";
    case Part::kCount: return "invalid";
  }
  return "invalid";
}

}  // namespace deathtrap::timing

bool DeathtrapExtendedLogsEnabled();
deathtrap::timing::Stamp BeginDeathtrapDiagnosticTiming();
void EndDeathtrapDiagnosticTiming(deathtrap::timing::Part part,
                                  deathtrap::timing::Stamp stamp);

class DeathtrapDiagnosticScope {
 public:
  explicit DeathtrapDiagnosticScope(deathtrap::timing::Part part,
                                    bool enabled = true)
      : part_(part), stamp_(enabled ? BeginDeathtrapDiagnosticTiming()
                                   : deathtrap::timing::Stamp{}) {}
  ~DeathtrapDiagnosticScope() { EndDeathtrapDiagnosticTiming(part_, stamp_); }
  DeathtrapDiagnosticScope(const DeathtrapDiagnosticScope&) = delete;
  DeathtrapDiagnosticScope& operator=(const DeathtrapDiagnosticScope&) = delete;

 private:
  deathtrap::timing::Part part_;
  deathtrap::timing::Stamp stamp_;
};
