#pragma once

#include <cstdint>

namespace deathtrap::input {

// Patch-owned commands are deliberately independent from both physical
// keyboard keys and XInput button numbers. Producers publish commands; the
// DirectInput boundary converts them to the stable retail keys in keys.cfg.
enum class Command : uint32_t {
  kMoveForward = 1u << 0,
  kMoveBackward = 1u << 1,
  kMoveLeft = 1u << 2,
  kMoveRight = 1u << 3,
  kRun = 1u << 4,
  kJumpClimb = 1u << 5,
  kOperate = 1u << 6,
  kCastSpell = 1u << 7,
  kLeftSidestep = 1u << 8,
  kRightSidestep = 1u << 9,
  kRetailFirstPerson = 1u << 10,
  kMenu = 1u << 11,
  kMenuUp = 1u << 12,
  kMenuDown = 1u << 13,
  kMenuLeft = 1u << 14,
  kMenuRight = 1u << 15,
  kMenuConfirm = 1u << 16,
  kMenuSpace = 1u << 17,
};

constexpr uint32_t CommandBit(Command command) {
  return static_cast<uint32_t>(command);
}

constexpr bool CommandActive(uint32_t mask, Command command) {
  return (mask & CommandBit(command)) != 0u;
}

struct RetailKeyboardPlan {
  bool w = false;
  bool s = false;
  bool a = false;
  bool d = false;
  bool left_shift = false;
  bool space = false;
  bool e = false;
  bool q = false;
  bool j = false;
  bool k = false;
  bool tab = false;
  bool escape = false;
  bool up = false;
  bool down = false;
  bool left = false;
  bool right = false;
  bool enter = false;
  bool menu_space = false;
};

constexpr RetailKeyboardPlan ResolveRetailKeyboardPlan(uint32_t mask) {
  return {
      CommandActive(mask, Command::kMoveForward),
      CommandActive(mask, Command::kMoveBackward),
      CommandActive(mask, Command::kMoveLeft),
      CommandActive(mask, Command::kMoveRight),
      CommandActive(mask, Command::kRun),
      CommandActive(mask, Command::kJumpClimb),
      CommandActive(mask, Command::kOperate),
      CommandActive(mask, Command::kCastSpell),
      CommandActive(mask, Command::kLeftSidestep),
      CommandActive(mask, Command::kRightSidestep),
      CommandActive(mask, Command::kRetailFirstPerson),
      CommandActive(mask, Command::kMenu),
      CommandActive(mask, Command::kMenuUp),
      CommandActive(mask, Command::kMenuDown),
      CommandActive(mask, Command::kMenuLeft),
      CommandActive(mask, Command::kMenuRight),
      CommandActive(mask, Command::kMenuConfirm),
      CommandActive(mask, Command::kMenuSpace),
  };
}

// The retail root-menu input routine returns 6 for Quit, including when its
// own Escape scan-code check fires.  Return-to-game is the native action 5.
// Rewrite only keyboard/controller Escape; a real mouse click on Quit keeps
// its original action, and submenus do not call this routine at all.
constexpr uint8_t ResolveRootMenuEscapeAction(uint8_t retail_action,
                                               bool escape_down,
                                               bool active_game) {
  if (retail_action != 6u || !escape_down) {
    return retail_action;
  }
  return active_game ? 5u : 0u;
}

}  // namespace deathtrap::input
