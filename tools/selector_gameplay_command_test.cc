#include "selector_gameplay_command.h"

#include <iostream>

int main() {
  using deathtrap::selector_gameplay::DeferredConsumables;

  DeferredConsumables commands;
  if (!commands.ObserveSynthetic(0x16) ||
      commands.ObserveSynthetic(0x16) || commands.size() != 1u) {
    std::cerr << "repeated synthetic draw was not deduplicated\n";
    return 1;
  }
  if (!commands.ObserveExact(0x16) || commands.size() != 0u) {
    std::cerr << "exact dispatch did not satisfy deferred request\n";
    return 1;
  }

  if (!commands.ObserveSynthetic(0x17) ||
      !commands.ObserveSynthetic(0x18) || commands.size() != 2u) {
    std::cerr << "distinct short requests were lost\n";
    return 1;
  }
  int32_t item = 0;
  if (!commands.Pop(&item) || item != 0x17 ||
      !commands.Pop(&item) || item != 0x18 || commands.Pop(&item)) {
    std::cerr << "deferred order is incorrect\n";
    return 1;
  }

  for (int32_t index = 0;
       index < static_cast<int32_t>(DeferredConsumables::kCapacity); ++index) {
    if (!commands.ObserveSynthetic(0x20 + index)) {
      std::cerr << "queue rejected an in-capacity request\n";
      return 1;
    }
  }
  if (commands.ObserveSynthetic(0x40)) {
    std::cerr << "queue exceeded its fixed capacity\n";
    return 1;
  }
  commands.Clear();
  if (commands.size() != 0u) {
    std::cerr << "clear failed\n";
    return 1;
  }

  std::cout << "selector gameplay command tests passed\n";
  return 0;
}
