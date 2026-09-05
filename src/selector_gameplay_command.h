#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace deathtrap::selector_gameplay {

// The retail selector dispatches item actions from the same routine that
// draws its rows.  Synthetic presentation passes may therefore observe an
// input edge before the next exact gameplay render.  Keep those actions out
// of the rollback transaction and execute each distinct request once after
// the exact pass.
class DeferredConsumables {
 public:
  static constexpr size_t kCapacity = 8u;

  bool ObserveSynthetic(int32_t item_id) {
    for (size_t index = 0; index < size_; ++index) {
      if (items_[index] == item_id) {
        return false;
      }
    }
    if (size_ >= items_.size()) {
      return false;
    }
    items_[size_++] = item_id;
    return true;
  }

  // An exact render owns gameplay.  If it saw the same request, its normal
  // dispatch satisfies the deferred observation and must not be repeated.
  bool ObserveExact(int32_t item_id) {
    for (size_t index = 0; index < size_; ++index) {
      if (items_[index] != item_id) {
        continue;
      }
      for (size_t move = index + 1u; move < size_; ++move) {
        items_[move - 1u] = items_[move];
      }
      --size_;
      return true;
    }
    return false;
  }

  bool Pop(int32_t* item_id) {
    if (!item_id || size_ == 0u) {
      return false;
    }
    *item_id = items_[0];
    for (size_t index = 1u; index < size_; ++index) {
      items_[index - 1u] = items_[index];
    }
    --size_;
    return true;
  }

  void Clear() { size_ = 0u; }
  size_t size() const { return size_; }

 private:
  std::array<int32_t, kCapacity> items_{};
  size_t size_ = 0u;
};

}  // namespace deathtrap::selector_gameplay
