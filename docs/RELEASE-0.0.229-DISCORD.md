# Deathtrap Native 50 v0.0.229 — Full Steam Deck Support is here!

Steam Deck support is finally here. You can now play Deathtrap Dungeon on Deck
with the complete Native 50 experience: ~50 FPS presentation, widescreen,
modern camera and controls, controller menus and selectors, every other patch
improvement, plus stronger Deck-tuned vibration.

The Deck rendering path now runs directly through Proton/DXVK. This fixes the
slow-motion feel, uneven frame delivery, triple-image trails during movement,
and stale scene fragments appearing inside black fog.

**Installation is straightforward:**

1. Launch the unmodified Steam game once, then close it.
2. In Desktop Mode, extract the complete release ZIP and run
   `bash INSTALL-DECK.sh` in its folder.
3. Return to Gaming Mode and set the game's Steam Input layout to the standard
   **Gamepad** template for comfortable play. Do not use a keyboard-and-mouse
   template.
4. Launch normally through Steam. All buttons map automatically—no custom
   Launch Options or manual button bindings are needed.

The installer finds the purchased Steam copy automatically. It also supports
installing directly inside the game folder or to a custom path containing
`DD_CD.EXE`:

`bash INSTALL-DECK.sh --game-dir "/path/to/Deathtrap Dungeon"`

Normal Steam libraries on a microSD card are detected automatically.

**Download and full notes:**
https://github.com/mbushuev/deathtrap-native-50/releases/tag/v0.0.229
