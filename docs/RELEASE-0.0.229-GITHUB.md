# Deathtrap Native 50 v0.0.229 — Full Steam Deck Support

Steam Deck support is finally here. Deathtrap Native 50 now installs and runs
through SteamOS and Proton with the complete modernized experience: the
intended ~50 FPS presentation, widescreen rendering, modern camera and input,
controller menus and selectors, and vibration tuned specifically for the
Deck.

## Steam Deck highlights

- Full support for the built-in controls through Steam Input in Gaming Mode.
  Select the standard **Gamepad** layout and the buttons map automatically.
- Direct Proton/DXVK rendering on Deck. The installer does not use dgVoodoo on
  SteamOS, avoiding its hangs and black-frame flashing under Proton.
- Correct ~50 FPS presentation cadence without speeding up or slowing down the
  game.
- Fixes the duplicated/trailing frames that looked like a triple image during
  movement.
- Fixes stale pieces of previous scenes appearing inside black fog and portal
  regions.
- Deck-only 300% final vibration-output gain for stronger haptics. Windows
  vibration remains unchanged.
- No custom Steam Launch Options and no global Proton changes.

## Steam Deck installation

1. Install Deathtrap Dungeon from Steam, launch the unmodified game once, then
   close it. This creates the game's Proton prefix.
2. Switch to Desktop Mode, download the complete release ZIP from **Assets**,
   and extract every file from it.
3. Open a terminal in the extracted directory and run:

   ```sh
   bash INSTALL-DECK.sh
   ```

   The installer automatically locates the purchased Steam copy by App ID
   `245010` and creates a timestamped rollback before changing any files.
4. Return to Gaming Mode. Open **Controller Settings** for Deathtrap Dungeon
   and select the standard **Gamepad** layout. Native 50 maps the controls
   automatically, so no manual button configuration is needed.
5. Launch the game normally through Steam.

You can also extract the ZIP directly beside `DD_CD.EXE` and run the same
command there. For a custom installation path, use:

```sh
bash INSTALL-DECK.sh --game-dir "/path/to/Deathtrap Dungeon"
```

The built-in display is configured for `1280x800`. For an external display or
an explicit Gamescope resolution, pass both dimensions, for example:

```sh
bash INSTALL-DECK.sh --width 1920 --height 1080
```

## Windows

Windows 10/11 support remains unchanged. Extract the complete ZIP beside
`DD_CD.EXE`, run `INSTALL.cmd`, and launch the game normally.

This is Steam Deck support provided by the unofficial patch; it is not an
official Valve Deck Verified announcement.
