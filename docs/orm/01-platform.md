# 01-platform: Thin platform layer, one switch reader, frame-time log

Everything our hook code needs from what runs the game (guest memory, the pad, calls into the game, folders, the environment and the log) goes through one small interface, so moving from the emulator to Hookshot is one adapter, not a change in every feature. With it come one reader for every on/off switch and an optional per-frame time log.

## What changes for players

Nothing they can see. Two small things they can notice:

- Every ORM switch takes `1`, `true`, `yes`, `on` for on and `0`, `false`, `no`, `off` for off, in any case. Any other value keeps the switch's default and writes one log line naming the switch, the value and the default. (Before, a value such as `false` could turn a switch on.)
- With `LICHII_ORM_FRAME_LOG` set, the game writes one line per presented frame to a CSV file, for comparing smoothness between the emulator and Hookshot.

## Switches

| Variable | Kit default | Code default | Values | What it does |
|---|---|---|---|---|
| `LICHII_ORM_FRAME_LOG` | not in the kit | unset: off (`coop_frame_log.cpp`, `CoopPlatform::Env`) | a file path | One CSV line `frame,seconds,ms` per presented frame, and every 10 s one summary log line |
| `LICHII_ORM_HOOKSHOT_GATE` | not in the kit | off (`coop_downed_game.cpp`, Hookshot builds only) | on/off words | Tells a Hookshot build that its game over gate exists, so downed (04-downed) may stay on there |

The reader itself (`coop_switch.h`) is not a switch. Its rules, for every switch read through it:

| Value | Reading |
|---|---|
| unset or empty | the switch's default |
| `0`, `false`, `no`, `off` (any case, spaces around ignored) | off |
| `1`, `true`, `yes`, `on` (any case) | on |
| anything else | the default, and the log line below |

Switches we renamed from `LICHII_COOP_*` to `LICHII_ORM_*` are read under the new name first and, when that is unset, under the old name, so older kits and test scenarios keep working (`CoopSwitch::On(name, old_name, default)`). Upstream's own switches keep their own readings.

## How it works

| File | What it does |
|---|---|
| `lichii/hook/platform.h` | The interface. Per frame: a `CoopPlatform::Platform` object the hook's frame makes and hands to each feature's `Frame`: `MainBase()`, `Memory()` (reads, writes, the 32-bit compare-and-swap the flag queues use), `Write64()`, `PadButtons()` (Npad bits), `NudgeRightStick()`, and two game requests that upstream's boss glue makes, `AskSceneChange()` and `MoveLink()`. Process-wide free functions the adapter defines at link time: `AddCaller()` (calls into the game on its own thread, same contract as `CoopPuppet::Director::Step`, run by order), `FolderPath()` (`mods`, `config`). Helpers: `GuestView` (the byte view callers and the camera walk use) and `MainCall()` (a call to `main + offset`). No emulator header, so it builds in the unit tests |
| `lichii/hook/platform_env.h` | The two services that need no game: `CoopPlatform::Env(name)` (tells unset from empty; `_dupenv_s` on MSVC) and the `ORM_LOG_INFO/WARNING/ERROR/DEBUG` macros. The platform is picked at compile time: `LICHII_COOP` = the emulator (the macros are `LOG_*(Core, ...)`, so each line keeps its file and line), `ORM_PLATFORM_HOOKSHOT` = a Hookshot build (the adapter defines `CoopPlatform::Log`), neither = the unit tests (the macros do nothing). Both at once is a compile error. `ORM_PLATFORM_LIVE` is 1 when a game runs |
| `lichii/hook/platform_emulator.h`, `.cpp` | The emulator's adapter, `CoopPlatform::Emulator`: each call is what our hook files did themselves before the layer, moved here unchanged. `AddCaller` goes to `CoopSvc::AddCaller`; `FolderPath` to the emulator's load and config folders. In upstream's Hookshot module (`COOP_HOOKSHOT`) the pad comes from `CoopRuntime::PadButtons`; with an older bridge it logs once and returns no buttons |
| `lichii/hook/coop_switch.h` | The one switch reader (header only): `Parse`, `From`, `Value`, `Find`, `On(name, default)`, `On(name, old_name, default)`, and the log line for an unknown value. Selector switches (more than on/off) build their own line with `Unknown(..., accepted, default)` |
| `lichii/hook/coop_frame_log.h`, `.cpp` | `CoopFrameLog::Recorder` (portable: frame number, seconds since the first frame, interval) and `Tick()`, called first thing in `CoopHook::OnFrame` so it measures the guest's presents. A 10 s window gives the median, the worst frame and the frames of 50 ms or more. The CSV columns match Hookshot's `RX_VK_FRAME_LOG`, so one script can read both |
| `lichii/hook/video.cmake` | Included by the patched video core CMake (patch 0100): adds the in-image HUD's Vulkan pass (10-hud). Changing what it builds needs no new patch |

Our feature files that take the platform in their `Frame` include, among others, the age, downed, held switch, camera probe and story event glue; the clock, Epona, Continue and the switch reader use its callers, environment and log. Rule we kept for new code: a file of ours in `lichii/hook` does not include `core/`, `hid_core/`, `video_core/` or `common/`; it uses `platform.h`, `ORM_LOG_*` and `CoopPlatform::Env`, and anything missing is added to the layer.

Upstream files that still talk to the emulator directly (test input, objects, spawn, the hook, drops, boss glue, teleport, time, svc, scene probe, save, flags, camera track) were left as they are: changing them would conflict at every re-pin. Almost all of what they use is memory and callers, which the layer already offers.

### Hook points in upstream files

| Upstream file | What we added |
|---|---|
| `lichii/hook/coop_hook.cpp` | Builds one `CoopPlatform::Emulator` per frame and passes it to our features' `Frame`; calls `CoopFrameLog::Tick()` first in `OnFrame` |
| `lichii/hook/core.cmake` | Our `coop_*`, `orm_*` and `platform_*` sources, `coop_frame_log.cpp` and `platform_emulator.cpp` |
| `lichii/hook/qt.cmake` | `orm_chat_complete.cpp`, links `coop_overlay` and `coop_host` into the Qt target, includes `lichii/app/app.cmake` (14-app) |
| `hookshot/CMakeLists.txt` | The module's source filter read only `coop_*.cpp` from `core.cmake`; it now reads `(coop\|orm\|platform)_*.cpp`, so our files and the adapter build into `hookshot_coop.dll` against `hookshot/compat/` |
| `lichii/hook/coop_testinput.cpp` | On Hookshot the spectator's right-stick nudge has no input injector: it does nothing and says so once |

### What a Hookshot build still lacks

Checked by reading the tree, nothing built: the game over gate of 04-downed is a code patch at `main+0x4ae6bc` (a `bl` into our cave), and Hookshot applies no code patches. The port would be an `ENTRY_HOOK` on the called function (`main+0x4aa890`) that returns 0 while the gate word is set; such a build then sets `LICHII_ORM_HOOKSHOT_GATE=1`. Until then downed turns itself off on Hookshot (log line below) and the normal game over runs. Also missing there: the in-image HUD (a Vulkan patch of the emulator) and the spectator's stick nudge (no stick export in the bridge).

## Messages and components

None.

## Tests

| Test file | What it checks |
|---|---|
| `tests/orm/switch_test.cpp` | The on and off words in any case; unset, empty and unknown values take the default; one log line naming the switch, the value and the default; the `LICHII_ORM_` name wins and the old `LICHII_COOP_` name is read when it is unset; `Find` names the variable that was set; a selector's line says what it takes; the held-switch selector takes the on/off words and `press`, `log` |
| `tests/lichii/platform_test.cpp` (built with `coop_frame_log.cpp`) | `GuestView` reads and writes through guest memory and refuses what is not mapped; `MainCall` puts the function at main + offset; `Env` tells unset from empty and switches read empty as unset; the frame log's first line has no interval; a window gives the median, the worst and the spikes |

## Log lines

| Line | When |
|---|---|
| `coop: switch NAME=VALUE is not 0/1/true/false/yes/no/on/off: on (its default)` (or `off`) | A switch got a value it does not take |
| `coop: frames: one line per presented frame to <file> (LICHII_ORM_FRAME_LOG); a summary every ...` | The frame log started |
| `coop: frames: LICHII_ORM_FRAME_LOG <file> cannot be written: no frame log` | The file could not be opened |
| `coop: frames: N in S s (F fps), median M ms, worst W ms, K of 50 ms or more` | Every 10 s while the frame log runs |
| `coop: check: downed fallback off on Hookshot: no game over gate in this build (needs the ENTRY_HOOK at main+0x4aa890 and LICHII_ORM_HOOKSHOT_GATE=1): the normal game over` | Hookshot build without the gate |
| `coop: platform: Hookshot's bridge gives no pad buttons (an older build): our features that read the pad (revive with A, quick messages) do nothing` | Hookshot with an older bridge |
| `coop: spectator: right-stick nudge not played on Hookshot (no input injector); the camera stays as it is` | Hookshot, spectator camera wake |

## Status

- Layer and switch reader on the emulator: every feature confirmed in game runs through them (see the other files), so the emulator path is confirmed in game on our test9-based build and on test10. The layer itself changes no behaviour.
- Frame log: not yet confirmed in game in a two-player run (unit-tested).
- Hookshot: not yet confirmed in game. Nothing of ours has been built or run in `hookshot_coop.dll`; only the source filter was changed.

## Known gaps

- Upstream files listed above still call the emulator directly and must move to the layer before a Hookshot-only build works.
- On Hookshot: no game over gate (downed off), no in-image HUD, no spectator stick nudge.
