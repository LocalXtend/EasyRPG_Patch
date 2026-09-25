# EasyRPG Player — RocketRPG edition

This repository is **EasyRPG Player 0.8.1.1** (<https://github.com/EasyRPG/Player>) with the changes RocketRPG
needs to run RPG Maker 2000/2003 games. The first commit is the unmodified upstream 0.8.1.1 source; everything
after it is RocketRPG's work, so `git diff` against the first commit shows exactly what was changed.
Tags match RocketRPG versions (`v0.6.1` = the Player shipped with RocketRPG 0.6.1), and each release has the built
`Player.exe` attached.

## What was changed

A small bridge (`src/rocket_bridge.{h,cpp}`) talks to the launcher over a named pipe (`RR_BRIDGE_PIPE`) using the
same line protocol as RocketRPG's mkxp-z agent, plus a few one-line hooks:

| File | Change |
|---|---|
| `src/player.cpp` | per-frame bridge tick, pause, speed multiplier, brightness / CRT scanlines on the final frame, autotest input hooks around the scene update |
| `src/game_player.cpp` | walk-through-walls (noclip) |
| `src/window_message.cpp` | dialogue start/end (with choice count), auto-advance, fast skip (never on choices) |
| `src/scene_battle.{h,cpp}` | `RocketForceVictory()` used only by the automated compatibility test |
| `src/filesystem_native.cpp` | **fix:** MinGW builds open files through UTF-16 paths too (upstream does this only for MSVC), so games in non-ASCII (e.g. Korean) folders load their assets |
| `src/directory_tree.cpp` | asset names containing U+FFFD (undecodable bytes) match the one character that stands there on disk |
| `src/filefinder.cpp` | when an asset is not found, retry its name read with the other CJK codepage (CP949 ↔ CP932) — Korean translations often keep Japanese RTP/asset names |
| `CMakeLists.txt` | adds the bridge sources |
| `resources/windows/player.xml` | missing manifest referenced by the MinGW resource script in 0.8.1.1 |

Bridge commands cover event positions for the ESP overlay (map-tile coordinates plus a per-frame camera line),
tile inspection, switch/variable dump and editing, warp, quick save/load and changing the message font at runtime.
Without `RR_BRIDGE_PIPE` / `RR_AUTOTEST` the bridge hooks do nothing; the three file-lookup changes are general
fixes that only ever find files upstream would miss.

## Build (Windows)

Requires [MSYS2](https://www.msys2.org/) in `C:\msys64` and PowerShell 7. liblcf 0.8.1 and inih r58 (unmodified) are
downloaded automatically.

```powershell
pwsh -File rocketrpg\build.ps1      # -> rocketrpg\work\dist\Player.exe + required DLLs
```

From an MSYS2 UCRT64 shell: `rocketrpg/build.sh [work-dir]`.

## License

EasyRPG Player is licensed under the **GNU GPL v3 or later** (see `COPYING`), and so are these changes and the
resulting `Player.exe`. RocketRPG itself only launches the Player as a separate process and talks to it over a pipe.
