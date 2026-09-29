# Asura's Wrath — Theseus PC Port

## T08: Native Threading / Synchronization

T01 introduced the **ReXGlue-independent PC platform boundary**. T02 moved
portable path discovery and launcher configuration into that boundary. T03
moved physical PC filesystem ownership into Theseus. T04 moves save/profile
policy into Theseus while retaining one explicit XAM compatibility bridge for
the Xbox-shaped ABI still used by the recompiled game.

### Invariant

The preserved baseline is `frozen/run04-launcher-pass`.
Development happens only on `theseus/*` branches.

At T04:

```
Asura guest logic
      |
AsurawrathApp
      |
TheseusPlatform
  |-- portable paths       [native]
  |-- config               [native]
  |-- NativeFileSystem     [native]
  |-- NativeSaveSystem     [native]
  |
  +-- GuestFsBridge        [temporary ReXGlue path/ABI adapter]
  +-- XamSaveProfileBridge [temporary ReXGlue save/profile ABI]
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


## T04 save/profile boundary

T04 preserves the game's existing Xbox 360 save package format but changes who
owns the host-side storage policy.

Canonical host layout:

```
UserData/
  Saves/
    TheseusProfile.ini
    <XUID>/
      <TitleID>/
        00000001/
          <SavedGame packages>
        Headers/
          00000001/
    <TitleID>/
      profile/
        <ProfileName>/
```

The stable compatibility profile initially keeps the same identity used by the
validated ReXGlue baseline so existing saves can migrate without changing their
XUID association.

Older layouts are migrated conservatively:
- 16-hex XUID trees under `UserData` move to `UserData/Saves`.
- 8-hex title trees are moved only when they contain a `profile` directory.
- unrelated `UserData` files are never touched.
- failed moves fall back to copy-first behavior; data safety has priority.

Marketplace/DLC content intentionally remains on the legacy content root in
T04. Only `SavedGame` packages and title-profile settings use the native Saves
root, so this milestone does not disturb the later TU/DLC work.

### First-run save prompt

The green XAM-style save-creation dialog is not suppressed globally. The bridge
arms a one-shot condition only when the game enumerates the `SavedGame`
content type and receives zero results. If the next dialog has two buttons,
T04 selects the first (affirmative) option headlessly and immediately clears
the condition.

All unrelated message boxes retain their normal UI.


## T04.1 first-run polish

The validated T04 build proved native save/profile persistence works, but the
green XAM message box could appear before SavedGame enumeration armed the
original one-shot bypass.

T04.1 moves that decision earlier. `NativeSaveSystem::IsFirstRun` checks for
both SavedGame packages and existing title-profile/options data before the
runtime starts. The launcher passes that native decision through a temporary
`asura_first_run` compatibility cvar.

Only a two-button XAM message with the first button focused is eligible for
automatic confirmation, and the explicit first-run condition is one-shot.
Normal message boxes remain untouched.


## T05 input boundary

T05 separates **input policy** from **physical device drivers** and from the
guest Xbox input ABI.

```
Asura guest logic
      |
   XamInput*                 [temporary Xbox ABI]
      |
ReXGlue SDL/XInput/MnK      [temporary physical-driver bridge]
      |
Theseus NativeInputPolicy   [native policy/configuration]
      |
Asura.ini + Launcher
```

Theseus owns:
- controller backend preference (SDL / XInput);
- keyboard/mouse enablement;
- mouse-look enablement and sensitivity;
- cursor visibility policy;
- keyboard bindings;
- the stable PC-facing configuration format.

ReXGlue temporarily owns:
- SDL controller polling;
- Windows XInput polling/vibration;
- keyboard/mouse event translation into Xbox controller state;
- the `XamInput*` guest ABI.

The launcher exposes a **Controls...** panel. Bindings are stored under
`[Keybinds]` in `UserData/Config/Asura.ini`, not as ReXGlue-specific
configuration. During T05 the launcher translates those values into temporary
runtime CVars.

### Cursor/focus policy

Cursor visibility and mouse capture are deliberately separate:
- the pointer is hidden while the game window has focus when
  `HideCursorInGame=1`;
- losing focus restores the pointer immediately;
- regaining focus hides it again;
- mouse-look / relative capture remains opt-in and is controlled independently.

This gives T05 the first focus-loss behavior needed for the later
windowed/strong-Alt-Tab stability milestone without claiming that the full
Alt-Tab stack is solved yet.

Mouse-driven menu selection is explicitly out of scope for T05 and reserved for
late-stage PC polish.


## T06 audio boundary

The original T06 target was "native video". The audit found that Asura's
cinematics are Bink (.bik) assets and ReXGlue does not contain a standalone
Bink decoder. Movie decoding therefore remains inside the recompiled game/UE3
path and ultimately reaches the Xenos graphics pipeline. Replacing it honestly
would require a later Bink-to-host-decoder integration and belongs with deeper
media/graphics work.

T06 therefore moves the next real host-owned boundary: audio policy.

```
Asura / UE3
   |
XAudio* + XMA*             [temporary Xbox ABI]
   |
ReXGlue XMA decoder        [temporary FFmpeg bridge]
   |
ReXGlue SDL audio driver   [temporary sample-output bridge]
   |
Theseus NativeAudioPolicy  [native policy/configuration]
   |
Asura.ini + Launcher
```

Theseus owns:
- mute policy;
- queued-frame / latency policy;
- the stable PC-facing audio configuration;
- the host-facing application identity.

ReXGlue temporarily owns:
- XMA context emulation and decoding;
- Xbox XAudio/XMA ABI calls;
- SDL audio-device stream submission.

The SDL audio metadata is patched to identify the application as
`Asura's Wrath` instead of `rexglue`.

The launcher exposes a minimal AUDIO section:
- Mute
- Buffer / latency: 4, 8, 16 or 32 queued frames

Values are stored in `UserData/Config/Asura.ini` under `[Audio]`.


## T07 timing boundary

T07 moves the host clock and Xbox-compatible guest clock conversion into
project-owned Theseus code.

```
Asura / UE3
   |
KeQueryPerformanceFrequency / KeQuerySystemTime / duration scaling
   |
ReXGlue xboxkrnl exports          [temporary ABI bridge]
   |
Theseus timing core              [native/project-owned]
   |
Windows QPC + FILETIME           [native host clock]
```

The timing core preserves the validated baseline behavior:
- guest performance-counter frequency: 50 MHz;
- normal time scale: 1.0x;
- monotonic host clock: QueryPerformanceCounter;
- host wall clock: FILETIME;
- guest duration scaling remains compatible with ReXGlue's existing rules.

The project-owned timing bridge is compiled temporarily into `rexruntime.dll`
because the Xbox kernel exports still live there. It contains no ReXGlue
dependencies and can move out when that ABI bridge is removed.

T07 does **not** migrate wait objects, timer objects, events, semaphores,
thread scheduling or synchronization. Those remain the scope of the following
threading/synchronization milestone.


## T08 host synchronization boundary

T08 moves the Windows synchronization foundation into project-owned Theseus
code while preserving the existing guest Xbox kernel object ABI.

```
Asura / UE3
   |
Xbox kernel thread/sync exports
   |
ReXGlue XThread / XEvent / XSemaphore / XMutant / XTimer wrappers
                    [temporary guest ABI + semantics]
   |
Theseus sync bridge [native/project-owned]
   |
Windows primitives  [native host]
```

Theseus owns in T08:
- yield and memory barriers;
- normal and alertable sleep;
- host TLS allocation/access;
- single waits, signal-and-wait and multiple waits;
- manual/auto-reset events;
- semaphores;
- mutexes;
- waitable timers and cancellation;
- native handle lifetime for those wrappers.

Still temporary compatibility code:
- Xbox XThread lifecycle and guest thread structures;
- APC and DPC semantics;
- guest affinity/priority translation;
- guest IRQL/spinlock semantics;
- the public rex::thread wrapper classes used by the Xbox kernel layer.

The sync core under `src/compat/theseus_sync_bridge.*` contains no ReXGlue
dependency and is temporarily compiled into rexcore while the compatibility
wrappers remain.


## T09 executable stability and crash telemetry

T09 deliberately pauses further guest-kernel migration to make the Windows
executable diagnosable and less disruptive during long RUNs.

### PE audit

The Windows build is required to remain:
- AMD64 / PE32+;
- Large Address Aware;
- ASLR-enabled;
- NX-compatible;
- High-Entropy-VA enabled.

T09 also changes the executable subsystem from console to **Windows GUI**.
The app already has a native `wWinMain`, so no console window is required
behind the launcher.

The classic 32-bit LAA workaround is therefore not a missing fix here: the
64-bit executable already carries the LAA characteristic. T09 keeps auditing it
so future linker changes cannot silently regress the flag.

### Crash black box

A project-owned process-wide crash handler is installed before the native
launcher and reasserted before runtime setup.

On an unhandled failure it writes:

```
UserData/Logs/
├─ StabilitySession.txt
└─ Crashes/
   ├─ TheseusCrash_<timestamp>_pid..._tid....txt
   └─ TheseusCrash_<timestamp>_pid..._tid....dmp
```

The text report records:
- exception code and address;
- module containing the faulting address;
- process/thread IDs;
- working set and private memory usage;
- available physical memory;
- loaded modules;
- the associated session breadcrumb log.

The minidump contains thread information, loaded/unloaded module information,
handles and indirectly referenced memory without taking a full-memory dump.

### Breadcrumbs

The session log records important transitions such as:
- launcher entry and selected graphics settings;
- runtime pre/post setup;
- guest main-thread creation and exit;
- focus loss/gain;
- minimize/restore;
- shutdown.

This is intentionally lightweight and portable. It does not upload anything and
does not require an installer, debugger, Visual Studio or external crash tool.

### CI self-test

T09 exposes a hidden developer-only crash trigger used by GitHub Actions.
The workflow intentionally crashes the built executable and fails unless both a
text report and a non-empty minidump are created. This validates the crash path
end to end rather than merely compiling `MiniDumpWriteDump`.


## T10: Native Graphics Boundary

T10 begins the graphics migration without destabilizing the validated T09.3
runtime. It deliberately separates **stable PC-facing graphics policy** from
the still-temporary Xbox/Xenos renderer implementation.

```
Asura / UE3 guest rendering
        |
Xbox 360 Xenos command stream
        |
rexgpu-xenos                 [temporary compatibility renderer]
  |-- D3D12
  `-- Vulkan
        |
Theseus NativeGraphicsPolicy [native/project-owned policy]
        |
Asura.ini + native launcher
```

Theseus owns in T10:
- renderer preference (D3D12 / Vulkan);
- host adapter preference;
- VSync policy;
- asynchronous shader-compilation policy;
- the stable PC-facing graphics configuration contract.

The current `rexgpu-xenos` plugin temporarily owns:
- Xenos command processor and register semantics;
- guest shader translation;
- texture/render-target conversion and caches;
- D3D12/Vulkan graphics providers;
- swapchain/presenter implementation;
- guest frontbuffer presentation.

This is intentional. ReXGlue already exposes an abstract `IGraphicsSystem`
and a versioned GPU plugin ABI, so T10 can put a Theseus-owned boundary in
front of the validated renderer before replacing deeper pieces.

### T10 migration rule

T09.3 remains immutable. Every deeper graphics change must be developed on a
T10+ branch and remain A/B-testable against the validated `rexgpu-xenos`
path. No command processor, shader translator, presentation path or Xenos
semantic is removed merely to reduce ReXGlue line count.

The target direction is:

```
T10  policy ownership / explicit compatibility boundary
  -> host presentation ownership
  -> display + resolution ownership
  -> shader/pipeline/cache ownership
  -> progressively thinner Xenos compatibility layer
```

The long-term goal is maximum practical native-PC independence while preserving
game correctness and stability over architectural purity.


## T10.2: Explicit Graphics Backend Bridge

T10 RUN 01 proved that graphics policy can move to Theseus without changing
runtime behavior. T10.2 removes the next hidden ownership point: on the normal
portable Windows path, ReXApp no longer decides which concrete GPU backend to
instantiate.

The runtime child now:
1. bootstraps Theseus;
2. reads the stable `UserData/Config/Asura.ini`;
3. reconstructs `NativeGraphicsPolicy`;
4. asks the explicit `rexglue_graphics_bridge` to instantiate the validated
   `rexgpu-xenos` D3D12 or Vulkan backend;
5. injects that instance into `RuntimeConfig.graphics`.

```
Asura launcher / Asura.ini
        |
Theseus NativeGraphicsPolicy
        |
rexglue_graphics_bridge      [explicit temporary adapter]
        |
rexgpu-xenos                 [validated compatibility renderer]
        |
D3D12 / Vulkan
```

The legacy ReXApp plugin-loading path is retained only as a safety fallback.
Presentation, Xenos command processing, shader translation and guest graphics
semantics remain unchanged in T10.2.

This is deliberately reversible: T10 RUN 01 remains the known-good baseline,
and the bridge can be bypassed without touching the guest code.
