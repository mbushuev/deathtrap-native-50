#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "keyboard_bindings.h"

namespace deathtrap::input {

constexpr size_t kRetailBindingVisibleRows = 11;
constexpr size_t kRetailBindingActionRowsPerPage = 11;
constexpr size_t kRetailBindingPageCount =
    (kKeyboardActionCount + kRetailBindingActionRowsPerPage - 1) /
    kRetailBindingActionRowsPerPage;

constexpr int BindingActionForRow(size_t page, size_t row) {
  if (page >= kRetailBindingPageCount ||
      row >= kRetailBindingActionRowsPerPage) {
    return -1;
  }
  const size_t action = page * kRetailBindingActionRowsPerPage + row;
  return action < kKeyboardActionCount ? static_cast<int>(action) : -1;
}

constexpr bool BindingRowHasAction(size_t page, size_t row) {
  return BindingActionForRow(page, row) >= 0;
}

constexpr size_t PreviousBindingPage(size_t page) {
  return page == 0 ? kRetailBindingPageCount - 1 : page - 1;
}

constexpr size_t NextBindingPage(size_t page) {
  return (page + 1) % kRetailBindingPageCount;
}

inline void CommitBindingPage(
    size_t page,
    const std::array<uint16_t, kRetailBindingVisibleRows>& visible,
    std::array<uint16_t, kKeyboardActionCount>* bindings) {
  if (!bindings) {
    return;
  }
  for (size_t row = 0; row < kRetailBindingActionRowsPerPage; ++row) {
    const int action = BindingActionForRow(page, row);
    if (action >= 0) {
      (*bindings)[static_cast<size_t>(action)] = visible[row];
    }
  }
}

inline std::array<uint16_t, kRetailBindingVisibleRows> MakeBindingPage(
    size_t page,
    const std::array<uint16_t, kKeyboardActionCount>& bindings) {
  std::array<uint16_t, kRetailBindingVisibleRows> visible{};
  for (size_t row = 0; row < kRetailBindingActionRowsPerPage; ++row) {
    const int action = BindingActionForRow(page, row);
    if (action >= 0) {
      visible[row] = bindings[static_cast<size_t>(action)];
    }
  }
  return visible;
}

}  // namespace deathtrap::input
