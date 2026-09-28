# Asura's Wrath — Theseus PC Port

## T01: Platform Bootstrap

T01 introduces a **ReXGlue-independent PC platform boundary** without
intentionally changing the frozen RUN04 runtime behavior.

### Invariant

The preserved baseline is `frozen/run04-launcher-pass`.
Development happens only on `theseus/*` branches.

At T01:

```
Asura guest logic
      |
AsurawrathApp
      |
TheseusPlatform   <- new stable PC-facing boundary
      |
ReXGlue           <- still supplies every game service
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
