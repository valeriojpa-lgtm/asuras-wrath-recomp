# Asura's Wrath — Theseus PC Port

## T02: Native Paths + Config

T01 introduced the **ReXGlue-independent PC platform boundary**. T02 moves
portable path discovery and launcher configuration into that boundary while
leaving runtime filesystem, input, audio, video and graphics behavior on
ReXGlue.

### Invariant

The preserved baseline is `frozen/run04-launcher-pass`.
Development happens only on `theseus/*` branches.

At T02:

```
Asura guest logic
      |
AsurawrathApp
      |
TheseusPlatform
  |-- portable paths   [native]
  |-- config           [native]
  |
  +-- runtime services [ReXGlue]
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
