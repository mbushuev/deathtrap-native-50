#include <array>
#include <cstdlib>
#include <iostream>

#include "input_binding_pages.h"

namespace {

bool Expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    return false;
  }
  return true;
}

}  // namespace

int main() {
  using namespace deathtrap::input;
  bool ok = true;
  static_assert(kRetailBindingPageCount == 2);
  static_assert(BindingActionForRow(0, 0) == 0);
  static_assert(BindingActionForRow(0, 10) == 10);
  static_assert(BindingActionForRow(1, 0) == 11);
  static_assert(BindingActionForRow(1, 8) == 19);
  static_assert(kKeyboardActions[19].default_retail_code == 0x8Fu);
  static_assert(kKeyboardActions[19].stable_scan == 0x19u);
  static_assert(BindingActionForRow(1, 9) == -1);
  static_assert(BindingActionForRow(1, 10) == -1);

  std::array<uint16_t, kKeyboardActionCount> bindings{};
  for (size_t index = 0; index < bindings.size(); ++index) {
    bindings[index] = static_cast<uint16_t>(0x80u + index);
  }
  auto first = MakeBindingPage(0, bindings);
  auto second = MakeBindingPage(1, bindings);
  ok &= Expect(first[0] == 0x80u && first[10] == 0x8Au,
               "first page must contain actions zero through ten");
  ok &= Expect(second[0] == 0x8Bu && second[8] == 0x93u,
               "second page must contain actions eleven through nineteen");
  ok &= Expect(second[9] == 0 && second[10] == 0,
               "unused final rows must not masquerade as actions");

  second[7] = 0x42u;
  second[8] = 0x43u;
  CommitBindingPage(1, second, &bindings);
  ok &= Expect(bindings[18] == 0x42u && bindings[19] == 0x43u,
               "captured keys must return to their backing actions");
  ok &= Expect(bindings[8] == 0x88u,
               "committing one page must not alter another page");
  ok &= Expect(PreviousBindingPage(0) == 1 && NextBindingPage(1) == 0,
               "page arrows must wrap deterministically");

  std::array<uint16_t, kKeyboardActionCount> isolated_edit{};
  for (size_t index = 0; index < isolated_edit.size(); ++index) {
    isolated_edit[index] = static_cast<uint16_t>(0x20u + index);
  }
  const auto before_isolated_edit = isolated_edit;
  ok &= Expect(CommitBindingRow(1, 3, 0x55u, &isolated_edit),
               "an explicit row edit must resolve to its backing action");
  ok &= Expect(isolated_edit[14] == 0x55u,
               "page two row three must update only action fourteen");
  for (size_t index = 0; index < isolated_edit.size(); ++index) {
    if (index != 14u) {
      ok &= Expect(isolated_edit[index] == before_isolated_edit[index],
                   "an explicit edit must not bulk-copy another page");
    }
  }
  ok &= Expect(!CommitBindingRow(1, 10, 0x66u, &isolated_edit),
               "an unused row must never become a backing action");

  static_assert(RetailCodeToDirectInputScan(0x80u) == 0x1Eu);
  static_assert(RetailCodeToDirectInputScan(0x96u) == 0x11u);
  static_assert(RetailCodeToDirectInputScan(0x3Bu) == 0x3Bu);
  static_assert(!IsBindableKeyboardRetailCode(0x100u));

  KeyboardBindingArray remapped_bindings = DefaultKeyboardBindings();
  std::swap(remapped_bindings[
                static_cast<size_t>(KeyboardAction::kMoveForward)],
            remapped_bindings[
                static_cast<size_t>(KeyboardAction::kMoveBackward)]);
  DirectInputKeyboardState keyboard{};
  keyboard[0x1Fu] = 0x80u;  // Physical S now owns move-forward.
  ApplyKeyboardBindings(remapped_bindings, &keyboard);
  ok &= Expect((keyboard[0x11u] & 0x80u) != 0u &&
                   (keyboard[0x1Fu] & 0x80u) == 0u,
               "swapped physical keys must resolve in stable command space");

  keyboard.fill(0u);
  keyboard[0x3Bu] = 0x80u;  // Rebound patch-owned F1 selector.
  ApplyKeyboardBindings(DefaultKeyboardBindings(), &keyboard);
  ok &= Expect((keyboard[0x3Bu] & 0x80u) != 0u,
               "patch-owned actions must survive the keyboard remap");

  remapped_bindings = DefaultKeyboardBindings();
  std::swap(remapped_bindings[
                static_cast<size_t>(KeyboardAction::kCastSpell)],
            remapped_bindings[static_cast<size_t>(KeyboardAction::kPause)]);
  keyboard.fill(0u);
  keyboard[0x10u] = 0x80u;  // Physical Q now owns pause.
  ApplyKeyboardBindings(remapped_bindings, &keyboard);
  ok &= Expect((keyboard[0x19u] & 0x80u) != 0u &&
                   (keyboard[0x10u] & 0x80u) == 0u,
               "rebound pause must reach the game's stable P key");

  if (!ok) {
    return EXIT_FAILURE;
  }
  std::cout << "input binding page tests passed\n";
  return EXIT_SUCCESS;
}
