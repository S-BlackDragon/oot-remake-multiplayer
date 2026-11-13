# 03-fixes: Bug fixes, including in upstream code paths

Fixes we made while building on test10 that touch code or tests that were already in the tree, or that anyone building the tree with the emulator's flags would hit. Each row points to the file changed and the test that covers it. Fixes inside our own new features are in their feature files.

## What changes for players

- **A guest who pressed Continue before the host no longer gets a new game's state.** Before: when the host's game was still on the title screen, its flag table was a fresh save's (3 hearts, magic 0, no fairy blessing); the server keeps the first table it gets, and a guest who had already loaded his save had everything put back to that table. Then the host role could move to that guest and the broken state became the group's (and could be saved). Now a host with no save in holds its table until its Link is read, and a guest's first load with no group value yet keeps its own save.
- Kit: a colour already in `coop.ini` is kept, and a comma list of kit files in `coop-options.ini` (such as `LICHII_COOP_DUNGEON_LISTS`) is turned into full paths like a single file.
- The rest are build and test fixes with no visible change.

## Switches

None. These fixes are always on.

## How it works

### Fixes in upstream code paths

| What was wrong | Where | What we changed | Test |
|---|---|---|---|
| A host on the title screen sent a fresh save's flag table; a guest who loaded first got it written over his save | `coop/host/src/flag_watcher.cpp`, `flag_watcher.hpp`; `coop/host/src/session.cpp`; `lichii/hook/coop_hook.cpp` (tells the watcher when a Link was read since the last title) | The host's table is held until its save is in, then sent. A guest's first load from the title screen with no group value yet keeps its save's values; with group values it goes back to them as before | `tests/host/flag_watcher_test.cpp`: "on the title screen the table waits until the save is in", "the title screen with only one flag back is no save load", "flags back early in the title...", "a guest's first save load with no group value yet keeps its save", "a guest's reload from a save goes back to what its game had" |
| Skeleton world matrices were read as 3x4 (0x30 apart). In game all 309 of Link's matrices are 4x4 (0x40 apart, the place in floats 12 to 14): at 0x30 only bone 0 was right, so the nearest-bone search always said bone 0 | `lichii/hook/coop_carry.cpp`, `coop_carry.h`; `lichii/hook/coop_objects.cpp` (the bone census) | The layout is told from bone 0 (4x4 by rows or by columns, from the actor's own turn); the old 3x4 reading is the fallback; the census and the carry bone check name the layout | `tests/lichii/carry_test.cpp` ("the skeleton's layout is told from bone 0: 4x4 by rows or columns, or the old 3x4"); fakes in `tests/lichii/skeleton_fakes.hpp` |
| A summed item count could go below 0 | `coop/host/src/game_items.cpp` | The sum is clamped at 0 (and, with our item rules, a gain stops at the wallet's cap: 06-world) | `tests/orm/wallet_test.cpp`, `tests/host/game_items_test.cpp` |
| Qt's `slots` keyword macro ate `ObjectComponent::slots` and `ObjectSample::slots` (test10) in any Qt file that includes the protocol | `coop/protocol/include/coop/protocol/messages.hpp` | `#pragma push_macro("slots")`, `#undef slots` around the header | builds of `coop_window.cpp` and the app |
| Locals that shadowed other names (MSVC C4456/C4459 are errors in the emulator build) | `lichii/hook/coop_puppet.cpp` (`Director::Status`'s inner `buf` → `frame_text`; the absent-slot `t` → `absent`), `coop_spawn.cpp` (`age` → `anim_age`), `coop_world.cpp` (`kParts` → `kSwitchParts`), `coop_testinput.cpp` (`number` → `is_number`) | Renamed, no behaviour change | the emulator build (`cl /Zs` of every hook file with its flags) |
| The kit's default colour (host blue, joiners red) overwrote a colour the launcher had written into `coop.ini` | `tools/release/kit/scripts/play.ps1` | A colour already in `coop.ini` is kept and exported as `LICHII_COOP_COLOR` when `coop-options.ini` has none | none (the colour parsing it uses is in `tools/release/tests/launch_test.ps1`) |
| Only a single kit file in `coop-options.ini` became a full path; a comma list did not | `tools/release/kit/scripts/play.ps1`, `kit-paths.ps1` | `Resolve-KitOptionValue` resolves each entry of a comma list | `tools/release/tests/launch_test.ps1` |
| Upstream's new network tests listened on every interface, which raises a Windows firewall prompt for `coop_tests.exe` | `tests/server/relay_test.cpp`, `nat_loopback_test.cpp`, `loopback_test.cpp`; `tests/net/small_path_test.cpp`, `encrypt_loopback_test.cpp`; `tests/host/session_test.cpp` | Every test server binds 127.0.0.1 | themselves |
| Test files that print `std::string_view` results with doctest did not include `<ostream>` | `tests/lichii/game_build_test.cpp` and others | `#include <ostream>` | themselves |
| `lichii/README.md` named our HUD patch as upstream's `0006` | `lichii/README.md` | Rows for our patches 0100, 0101, 0102, `video.cmake` and `app/` | none |

### Build and test fixes in our own code (kept here because they bite on a fresh build)

| What was wrong | Where | What we changed |
|---|---|---|
| Test names with `;` (ctest splits or refuses them at configure, so the cases never ran) | many files under `tests/` (pvp, carry, resume, songs, clock, smoothing, chest, door, hazard, endgame, link watch, visual tests) | `;` removed from every test name |
| `orm::` ambiguous in a server test once `coop::protocol::orm` (the number table) existed | `tests/server/orm_interest_test.cpp` | Names `coop::server::orm` in full |
| The hook asked `on()` of two guards that did not have it (`ctest` does not build the hook, only the emulator build saw it) | `coop/host/prizes.hpp`, `beans.hpp` | `on()` added, as `TradeGuard` has |
| `coop_window.cpp` could not find `coop/overlay` once the partner map included it | `lichii/hook/qt.cmake` | The Qt target links `coop_overlay` (and `coop_host` for the command suggestions) |
| Two `connection_of`, a `//x` that read as a command, locals that hid globals | `server/orm_session.cpp`, `session.hpp`, `coop/net/src/client.cpp`, `lichii/hook/orm_commands_game.cpp`, `coop/host/src/orm_commands.cpp` | One helper kept; renames |
| A frame whose scene was not read counted as a scene change: the downed anchor and the firm ground were dropped (a body in lava or the void out of reach again) | `coop/host/src/hazards.cpp` | Such a reading stays in the last scene read (test: `tests/host/hazards_test.cpp`, "a frame whose scene was not read is not a scene change") |

### Hook points in upstream files

All listed in the first table: `flag_watcher.*`, `session.cpp` (host), `coop_hook.cpp`, `coop_objects.cpp`, `game_items.cpp`, `messages.hpp`, `coop_puppet.cpp`, `coop_spawn.cpp`, `coop_world.cpp`, `coop_testinput.cpp`, `play.ps1`, `kit-paths.ps1`, upstream's network tests, `lichii/README.md`.

## Messages and components

None.

## Tests

See the "Test" column above. The fixes in upstream tests (127.0.0.1, `<ostream>`) are checked by the suite running without a firewall prompt.

## Log lines

| Line | When |
|---|---|
| `coop: flags: we host with no save in (the title screen): our table is held until our save is in, so no ...` | A host on the title screen |
| `coop: flags: our table sent: our save is in now (held while our game had none)` | Its save is in |
| `coop: flags: our save load from the title screen: N values of our save kept (no group value had come: the host has no save in yet)` | A guest's first load before the host |
| `coop: flags: our save load: N values back to the group's (...)` | A guest's load once the group is known (as before) |

## Status

- Title-screen flags: confirmed in game on our test9-based build: the host-on-the-title path (host's table held, then sent when its save was in; both games with their save's hearts and magic) in the probe batches `sondas17r6` and `sondas18r6` after the fix (the `ocarina-mano` and `charla-mido` pairs among them), and across 50 pairs with the second player joining first, every game's first life was its own save's, with no save mismatch. The guest-loads-first path ("values of our save kept") has been seen only in unit tests: in the runs the host always loaded first.
- 4x4 skeleton matrices: confirmed in game on our test9-based build (the carried pot and the Ocarina in the puppet's hand use the right bone: scenarios `levantar-vasija`, `ocarina-mano`).
- The other rows are build and test fixes: checked by the build and the suite, nothing to see in game.

## Known gaps

- The guest-loads-first path of the title-screen fix has not been seen in a two-player game run.
