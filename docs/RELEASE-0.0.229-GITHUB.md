# Deathtrap Native 50 v0.0.229 — Full Steam Deck Support

[![Buy me a coffee](https://raw.githubusercontent.com/mbushuev/deathtrap-native-50/v0.0.229/assets/ko-fi-support.png)](https://ko-fi.com/utkiduck)

![Deathtrap Native 50 v0.0.229 — Steam Deck support](https://raw.githubusercontent.com/mbushuev/deathtrap-native-50/v0.0.229/assets/deathtrap-native50-banner-v0.0.229.png)

Steam Deck support is finally here. Deathtrap Native 50 now installs and runs
through SteamOS and Proton with the complete modernized experience. You can
finally play on Steam Deck with the modern camera and controls, widescreen
rendering, controller menus and selectors, the intended ~50 FPS presentation,
and every other Native 50 improvement, plus vibration tuned specifically for
the Deck.

## Steam Deck highlights

- Full support for the built-in controls through Steam Input in Gaming Mode.
  For comfortable play, select the standard **Gamepad** layout rather than a
  keyboard-and-mouse template; the controls are configured for gamepad play
  and the Deck buttons map automatically.
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
command there. For a custom installation path, pass the directory containing
`DD_CD.EXE`, not the extracted patch directory, and keep paths with spaces in
quotes:

```sh
bash INSTALL-DECK.sh --game-dir "/path/to/Deathtrap Dungeon"
```

Registered Steam libraries, including normal microSD libraries, are detected
automatically. If both the game and the App ID `245010` Proton prefix are in
manually selected locations, use:

```sh
bash INSTALL-DECK.sh \
  --game-dir "/run/media/deck/GAMES/Deathtrap Dungeon" \
  --compat-data "/run/media/deck/CUSTOM/steamapps/compatdata/245010"
```

The `--compat-data` directory must already contain `pfx`; launch the game once
through Steam before installing. Most users do not need this option.

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
