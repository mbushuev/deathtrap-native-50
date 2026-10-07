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
  constexpr auto default_modes = DefaultKeyboardBindingModes();
  static_assert(default_modes[
                    static_cast<size_t>(KeyboardAction::kMoveForward)] == 0u);
  static_assert(default_modes[
                    static_cast<size_t>(KeyboardAction::kMoveLeft)] == 0u);
  static_assert(default_modes[
                    static_cast<size_t>(KeyboardAction::kCastSpell)] == 0u);
  static_assert(default_modes[
                    static_cast<size_t>(KeyboardAction::kOperate)] == 0u);
  static_assert(default_modes[
                    static_cast<size_t>(KeyboardAction::kPause)] == 0u);
  static_assert(CapturedKeyboardBindingMode(0x96u) == 0u);
  static_assert(CapturedKeyboardBindingMode(0x99u) == 0u);
  // On French AZERTY, logical Q maps to the physical A-position scan (0x1E),
  // while Dungeon's captured retail Q still means the physical Q position
  // (0x10). Conflict detection must compare the latter for new captures.
  static_assert(ResolveKeyboardBindingSourceScan(0x90u, 0u, 0x1Eu) ==
                0x10u);
  static_assert(ResolveKeyboardBindingSourceScan(0x90u, 1u, 0x1Eu) ==
                0x1Eu);

  KeyboardBindingArray source_bindings = DefaultKeyboardBindings();
  KeyboardBindingModeArray source_modes{};
  auto french_layout_scans =
      ResolveQwertyKeyboardBindingScans(source_bindings);
  const size_t forward_action =
      static_cast<size_t>(KeyboardAction::kMoveForward);
  french_layout_scans[forward_action] = 0x2Cu;  // Printed W on AZERTY.
  auto source_scans = ResolveKeyboardBindingSourceScans(
      source_bindings, source_modes, french_layout_scans);
  ok &= Expect(source_scans[forward_action] == 0x11u,
               "default movement must keep the physical WASD cluster");
  source_modes[forward_action] = 1u;
  source_scans = ResolveKeyboardBindingSourceScans(
      source_bindings, source_modes, french_layout_scans);
  ok &= Expect(source_scans[forward_action] == 0x2Cu,
               "legacy logical-layout mode must remain readable");

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

  // The runtime builds this table through the active Windows layout. Model
  // an AZERTY layout here: logical Z is the physical W-position scan and
  // logical Q is the physical A-position scan.
  remapped_bindings = DefaultKeyboardBindings();
  remapped_bindings[
      static_cast<size_t>(KeyboardAction::kMoveForward)] = 0x99u;  // Z
  remapped_bindings[
      static_cast<size_t>(KeyboardAction::kMoveLeft)] = 0x90u;  // Q
  auto azerty_scans = ResolveQwertyKeyboardBindingScans(remapped_bindings);
  azerty_scans[
      static_cast<size_t>(KeyboardAction::kMoveForward)] = 0x11u;
  azerty_scans[
      static_cast<size_t>(KeyboardAction::kMoveLeft)] = 0x1Eu;
  keyboard.fill(0u);
  keyboard[0x11u] = 0x80u;
  keyboard[0x1Eu] = 0x80u;
  ApplyKeyboardBindingsFromScans(azerty_scans, &keyboard);
  ok &= Expect((keyboard[0x11u] & 0x80u) != 0u &&
                   (keyboard[0x1Eu] & 0x80u) != 0u,
               "layout-aware ZQ input must reach stable W/A commands");
  auto azerty_native_targets =
      ResolveQwertyKeyboardBindingScans(DefaultKeyboardBindings());
  azerty_native_targets[
      static_cast<size_t>(KeyboardAction::kMoveForward)] = 0x2Cu;
  azerty_native_targets[
      static_cast<size_t>(KeyboardAction::kMoveLeft)] = 0x10u;
  TranslateCanonicalKeyboardTargets(azerty_native_targets, &keyboard);
  ok &= Expect((keyboard[0x2Cu] & 0x80u) != 0u &&
                   (keyboard[0x10u] & 0x80u) != 0u &&
                   (keyboard[0x11u] & 0x80u) == 0u &&
                   (keyboard[0x1Eu] & 0x80u) == 0u,
               "canonical W/A commands must reach AZERTY native targets");

  if (!ok) {
    return EXIT_FAILURE;
  }
  std::cout << "input binding page tests passed\n";
  return EXIT_SUCCESS;
}
