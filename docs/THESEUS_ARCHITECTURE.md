# Asura's Wrath — Theseus PC Port

## T03: Native Filesystem

T01 introduced the **ReXGlue-independent PC platform boundary**. T02 moved
portable path discovery and launcher configuration into that boundary. T03
moves physical PC filesystem ownership into Theseus while retaining one
explicit guest-path compatibility bridge for the Xbox-shaped ABI still used by
the recompiled game.

### Invariant

The preserved baseline is `frozen/run04-launcher-pass`.
Development happens only on `theseus/*` branches.

At T03:

```
Asura guest logic
      |
AsurawrathApp
      |
TheseusPlatform
  |-- portable paths     [native]
  |-- config             [native]
  |-- NativeFileSystem   [native]
  |
  +-- GuestFsBridge      [temporary ReXGlue path/ABI adapter]
      |
Windows
```

Later milestones replace one service at a time:

```
filesystem: rexglue -> native
saves:      rexglue -> native
input:      rexglue -> native
...
graphics:   rexglue -> native
```

No service is removed until the native implementation can be A/B tested
against the known-good implementation.

## Portable filesystem contract

The executable directory is the root. No installer, registry state,
Documents folder, or AppData dependency is required by the Theseus layer.

```
Asura's Wrath/
  Asura's Wrath.exe
  Data/
    Game/
      Content/
      Cinematics/
      Xbox360TOC.txt
  DLC/
  Runtime/
  UserData/
    cache/
    Config/
    Logs/
    Saves/
```

The user's existing `Data/Game` layout is therefore the canonical Theseus
layout; large game data does not need to be reorganized.

## T01 audit

`tools/theseus_audit.py` enforces the first architectural rule:
`src/platform` may not directly include or reference ReXGlue.

The rest of the host application is expected to contain ReXGlue references at
T01. Their count becomes a measurable migration signal in later milestones.


## T02 validation contract

A test build may live in a sibling folder such as `THESEUS_T02` while sharing
the immutable game data in `../Data`. The executable must locate that Data
folder by itself; the validation launch must not rely on a helper CMD or a
`--game_data_root` argument.

Launcher settings are stored in:

```
UserData/Config/Asura.ini
```

The file format is owned by Theseus. The current ReXGlue CVar mapping is only a
temporary backend adapter and may disappear later without changing the user's
configuration file.


## T03 filesystem boundary

T03 deliberately separates two different concepts that used to be mixed
together:

1. **Physical PC filesystem I/O** — file existence, directory creation, copying,
   reading, writing and portable host layout. This belongs to Theseus.
2. **Guest Xbox filesystem ABI/path translation** — exposing clean PC folders
   under paths such as `\\Device\\Harddisk0\\Partition1\\BCGame`. This is a
   temporary compatibility bridge and still uses ReXGlue.

The only source files allowed to reference ReXGlue filesystem classes in T03
are:

```
src/compat/rexglue_filesystem_bridge.h
src/compat/rexglue_filesystem_bridge.cpp
```

CI fails if ReXGlue filesystem references escape that bridge.

ISO/GDFX parsing is also kept inside the bridge for compatibility. It is an
import path, not the canonical runtime layout. The canonical portable runtime
remains:

```
Root/
  Data/
    default.xex
    Game/
      Content/
      Cinematics/
      Xbox360TOC.txt
  UserData/
```

T03 does **not** claim that the Xbox guest file ABI has disappeared. That
removal is a later milestone; T03's achievement is that ordinary PC file I/O
and ownership no longer belong to ReXGlue.
