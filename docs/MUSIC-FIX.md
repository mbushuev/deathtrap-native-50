# Steam music-track routing fix

## User-visible bug

The Steam release can play the same background track on every level even
though `Sounds/0.mp3` through `Sounds/14.mp3` are present. This is a known
release defect, not a renderer or interpolation timing problem.

The long-standing community fix replaces the Steam audio shim with the newer
GOG-style Miles plus `ogg-winmm` stack and converts the fifteen MP3 files to
`Music/Track02.ogg` through `Music/Track16.ogg`:

- <https://steamcommunity.com/sharedfiles/filedetails/?id=391210236>
- <https://github.com/bangstk/ogg-winmm>
- <https://www.pcgamingwiki.com/wiki/Deathtrap_Dungeon>

That solution works, but adds multiple replacement DLLs, duplicates the
soundtrack in OGG form and changes more of the audio stack than this overlay
needs.

## Verified local cause

The supported Steam installation contains the 25,600-byte
`MSS32.DLL` with SHA-256
`A87DF7DF3708BA52D04FB58180F2C8CDCF3DB55FCAF63CC12808D20DBE46839B`.
It is an Audiere-based MP3 shim in front of `mss32gog.dll`.

Static disassembly establishes two defects:

1. `_AIL_redbook_tracks@4` is hard-coded to return `9`, although the retail CD
   layout has one data track plus fifteen audio tracks (`2..16`).
2. `_AIL_redbook_play@12` ignores its `start` and `end` arguments. It starts
   the MP3 whose zero-based number remains in an internal global written by
   `_AIL_redbook_track_info@16`.

The same Audiere stream also fails across gameplay pause. It remains marked
as playing, and the game sends no Redbook stop, pause or zero-volume command,
but audible playback disappears after the wrapper's two-second worker cycle
and does not recover when gameplay resumes.

`Dungeon.dll+0x47B20` enumerates Redbook track information during sound
startup. With the shim's count of nine, the final metadata request is track 9,
so the shim retains index 9. The local x86 probe confirms `tracks=9` and zero
start/end values for every queried track `2..16`. Later playback therefore
cannot recover a track from the fake time range and reuses `Sounds/9.mp3`.

The game itself still selects the intended CD track. Its current selection is
stored at supported-build RVA `0x104C10` before each call to
`AIL_redbook_play`. The level music tables contain values through track 15.

## Overlay fix

Version 0.0.189 keeps the installed Miles, Audiere and MP3 files intact. It
hooks only the exact known Steam wrapper after validating its file size and
export machine-code signatures:

- `AIL_redbook_tracks` reports the correct CD-compatible count of 16;
- `AIL_redbook_play` reads the engine-owned selected CD track and opens the
  matching original MP3 through the built-in Windows MCI MPEG backend;
- the mapping remains `CD 2..16 -> Sounds/0.mp3..Sounds/14.mp3`;
- Redbook status, stop and volume operations are mirrored to that stream;
- gameplay pause preserves the MCI stream and playback position, so music
  reliably resumes from the same point instead of disappearing permanently;
- if MCI cannot open a track, playback falls back to the original Audiere
  wrapper rather than leaving the level silent.

The option is `[Audio] FixMusicTracks=1`. An unknown or updated `MSS32.DLL`
fails closed: no audio hook is enabled and the original wrapper is untouched.
The music patch is independent of native-render subframes and camera state.

## Acceptance test

Start a new process and visit or load at least two levels that request
different music. With diagnostics enabled, the session log must contain:

```text
music_redbook fix=active cd_tracks=16 mp3_files=15 mapping=cd_track_minus_2
music_redbook route=STEAM_MP3 cd_track=N mp3=M ...
```

The route invariant is `M = N - 2`. Music must change with the level while
sound effects, volume control and movies remain functional. Pause gameplay
for at least ten seconds, resume it, and verify that the same track continues
from its preserved position.

## Live validation

The user accepted the level-to-level music change on 2026-08-02. The live
v0.0.189 sessions recorded distinct engine requests and the expected routing:

```text
music_redbook route=STEAM_MP3 cd_track=5 mp3=3 start=0 end=0
music_redbook route=STEAM_MP3 cd_track=2 mp3=0 start=0 end=0
```

The first level, Spire, may begin in silence by design; its first music request
is CD track 2 and therefore plays `Sounds/0.mp3`. The user confirmed that
different levels now play different music. On 2026-08-30 the user also
confirmed that the MCI-backed track pauses and resumes from the same position
after the original Audiere implementation repeatedly failed to recover.
