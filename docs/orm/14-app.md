# 14-app: The "Cooperativo" screen and its helper protocol

A Qt screen inside the emulator's own window that replaces the game list: it shows whether everything needed to play is in place, prepares the puppet pack and the shaders, takes a room, a name and a password, and starts the game in the same process with co-op on. The screen does no work of its own: it talks to a separate **helper** program over stdin/stdout, one JSON object per line. Any program that speaks this protocol can be the helper. A second, in-process channel links the game's hook and the screen (chat commands that need the helper, and the room's PvP and "lock on mates" toggles).

## What changes for players

- With an `orm-app.ini` file next to the emulator's exe, the emulator opens on the "Cooperativo" screen instead of the game list. Without that file nothing changes (game list first, co-op started at once, as before).
- Status rows with ✓, ✗ or … (not known yet): keys, firmware, game, puppet pack, shaders. Files dropped on the screen are handed to the helper, which puts them where they belong. Buttons open each row's folder.
- "Preparar" builds the puppet pack, then prepares the shaders, with a progress bar and a line of what is being done. It starts by itself once keys, firmware and game are there and the pack or the shaders are not.
- Room, name (required) and an optional password, then "Jugar". Changing room or name after co-op started asks to restart.
- "Quiero ser el anfitrión" (I want to host), when the helper reports a host wish.
- "PvP" and "Fijar aliados" (lock on mates) toggles while connected, mirroring the room's switches.
- A "Versión y actualizaciones" block with update buttons and release notes, and a non-modal notice when the helper reports a newer version.
- When the game stops, the screen comes back.
- In the game, `/registros`, `/reintentar` and `/nombre` reach the helper through the screen.

## Switches

| Variable | Kit default | Code default | Values | What it does |
|---|---|---|---|---|
| `orm-app.ini` next to the exe (a file, not a variable) | not in the kit | absent: screen off | present / absent | Turns the screen on (`OrmApp::Enabled`) |
| `[app] helper=` in `orm-app.ini` | | `../orm-launcher.exe` (relative to the exe's folder) | a path, relative or absolute | The helper program |
| `[app] helper_args=` in `orm-app.ini` | | none | extra arguments, split like a command line | Added after `--service` |
| `ORM_RESTART_WAIT_PID` | not in the kit | | a process id | Set by the screen for the restart stub when it restarts the app (an older stub ignores it) |

## How it works

| File | What it does |
|---|---|
| `lichii/app/orm_app.h`, `.cpp` | Entry points the main window calls: `Enabled()`, `Attach(...)` (puts the screen in the central row in place of the game list; steps aside when the render window or the loading screen shows), `OnGameStopped()`. Reads `orm-app.ini` and builds the helper's command line |
| `lichii/app/orm_helper_client.h`, `.cpp` | `HelperClient`: the JSON-lines client. Qt Core only, no moc. Starts the helper with no window (`CREATE_NO_WINDOW`), or runs in memory for tests. Assigns ids, keeps one callback per request, applies timeouts, joins partial lines, drops over-long ones. `StatusPoller`: one `status` at a time every 2 s, only when the screen allows it |
| `lichii/app/orm_app_model.h`, `.cpp` | Lenient readers of the helper's replies (`ParseStatus`, `ParsePlay`, `ParseUpdate`, `ParseImport`, progress texts) and the Spanish texts. A missing field leaves its default, so screen and helper can grow apart a little |
| `lichii/app/orm_app_window.h`, `.cpp` | The screen's widgets and flows (start, status, prepare, play, import, updates, restart, toggles) |
| `lichii/app/app.cmake` | Adds the files to the Qt frontend target; stamps the co-op tree's `git describe` into the version lines. Included by `lichii/hook/qt.cmake`, so Fritchii's own CMake is not touched |
| `lichii/app/tests.cmake`, `tests/orm_app_tests.cpp` | Unit tests (built when Qt 6 is found). The test exe doubles as a fake helper with `--fake-helper` |
| `lichii/hook/orm_app_channel.h`, `.cpp` | The in-process channel game ↔ app: a few callbacks and a queue behind one mutex. Hook → app: `upload_logs`, `retry_downloads`, `save_name`; app → hook: answers for the chat and the toggles (`SetSwitch`, through a setter the hook registers); both: the room's switches with a version counter |
| `lichii/patches/0102-orm-app.patch` | One upstream Fritchii file, `src/yuzu/main_window.cpp`: `CoopWindow::Attach` only without `orm-app.ini`; `OrmApp::Attach` after the loading screen exists (booting through the window's own `BootGame`); `OrmApp::OnGameStopped()` in `OnEmulationStopped` |

### Hook points in upstream files

| Upstream file | What we added |
|---|---|
| Fritchii `src/yuzu/main_window.cpp` (patch 0102) | The three calls above |
| `lichii/hook/qt.cmake` | `include(.../lichii/app/app.cmake)` |
| `lichii/hook/core.cmake` | `orm_app_channel.cpp` |
| `lichii/hook/orm_commands_game.cpp` (ours) | `/registros`, `/reintentar`, `/nombre` ask the app; registers the switch setter and publishes the switches each frame |

## The helper protocol

### Starting and stopping

- The screen starts `<helper> --service [helper_args...]` with no window, its working folder unchanged. stdout carries replies and events; every stderr line goes to the emulator's log as `ayudante: <line>`.
- One JSON object per line, UTF-8, LF or CRLF. A line over 4 MiB without a newline is dropped.
- On close the screen sends `quit`, waits up to 3 s for the helper to end, then kills it. If an update is waiting to be applied, `update_apply_on_restart` goes before `quit`.
- If the helper exits, every pending request fails with a Spanish error (`el ayudante se cerró ...`).

### Shapes

| Kind | Shape |
|---|---|
| Request | `{"id":N,"cmd":"<name>", ...fields}`; `id` counts up from 1 |
| Reply | `{"id":N,"ok":true\|false,"error":"<text>", ...fields}` |
| Event | `{"event":"<name>", ...fields}`, no `id` |

Rules a helper must follow:

- **Answer requests in the order they came.** The screen relies on it (a poll never piles up behind a long task; `update_apply_on_restart` before `quit`).
- Every request gets exactly one reply with its `id`. A reply with an unknown `id` is logged and ignored; a line that is neither a reply nor an event is logged and ignored.
- `error` is shown to the player as it is: write it in the player's language.
- A field the screen does not know is ignored; a field it expects and does not get keeps its default.
- A helper that does not implement a command answers `{"ok":false,"error":"..."}`.

### Timeouts (the screen's side)

| Request | Timeout |
|---|---|
| `status` | 20 s |
| `game_ended`, `room`, `shortcut` | 30 s |
| `update_check`, `import_finish` | 60 s |
| `play` | 180 s (it may fetch the room's save first) |
| `build_puppet_pack`, `prepare_shaders`, `import`, `update_download`, `update_apply_on_restart`, `update_rollback`, `upload_logs`, `retry_downloads` | none (long tasks report with `progress` events) |

A request that times out fails with `el ayudante no respondió a tiempo (<cmd>)`; its late reply is ignored.

### Requests

| `cmd` | Fields sent | Reply fields read | When |
|---|---|---|---|
| `status` | none | see "status reply" below | At start, after each task, every 2 s while idle |
| `room` | `room`, `name`, `password` (may be empty), `host` (`me` or `auto`, only when the host toggle is shown and enabled); or only `name` (from `/nombre`) | `ok`, `error` | "Jugar", `/nombre` |
| `play` | none | `game` (or `path`; else `-g <file>` or the last non-option in `args`), `args` (array), `env` (object: variable → value), `fullscreen` (or `-f` in `args`) | After `room` |
| `game_ended` | none | `ok`, `error` | The game stopped |
| `build_puppet_pack` | none | `ok`, `error` | "Preparar", first step |
| `prepare_shaders` | none | `ok`, `error` | "Preparar", after the pack |
| `import` | `paths` (array of local paths dropped on the screen) | `import_id` (any JSON value, sent back verbatim), `imported` (array), `skipped` (array), `ask` (a yes/no question; empty: none), `summary` (one line) | Files dropped |
| `import_finish` | `import_id`, `delete` (bool: the player's answer to `ask`) | `summary` | Only when `ask` was not empty |
| `shortcut` | `action`: `create` or `remove` | `ok`, `error` | The shortcut button (shown when `status` reports `shortcut`) |
| `upload_logs` | none | `ok`, `error` | `/registros` in the game |
| `retry_downloads` | none | `ok`, `error` | `/reintentar` in the game, or the retry button |
| `update_check` | none | `available`, `version` (or `channel_version`), `notes` (text or array of lines), `ready` (or `update_ready`) | At start and on the button |
| `update_download` | none | `ok`, `error` | A newer version is available and not downloaded |
| `update_apply_on_restart` | none | `ok`, `error` | "Actualizar ahora", "Reiniciar", or closing with a downloaded update. After `ok`, the files must be in place: the screen restarts the app |
| `update_rollback` | none | `ok`, `error` | "Volver a la versión anterior", after a confirmation; then a restart |
| `quit` | none | (any) | Closing |

### `status` reply

All fields are optional.

| Field | Type | Meaning |
|---|---|---|
| `keys`, `firmware`, `game`, `puppet_pack`, `shaders`, `shortcut` | bool, a path string (non-empty = there), or `{"ok":bool, "detail"\|"text"\|"path"\|"version"\|"error":"...", "folder":"..."}` | One row each. `folder` is what the row's button opens |
| `paths` | `{"game","keys","firmware"}` (or top-level `game_path`, `keys_path`, `firmware_path`) | Shown when a row has no detail |
| `shaders_version` | text | Shown as the shaders row's detail when it has none |
| `version` (or `app_version`) | text | The installed app |
| `channel_version` (or `latest_version`, or `channel.version`) | text | The newest one the helper knows |
| `kit_version`, `protocol` | text | Version lines |
| `room` | text, or `{"room","name"}` | The last room (and name) to fill the fields |
| `name` | text | The last name |
| `host` | `me`, `auto`, `never`, or absent | The host toggle: checked for `me`, greyed out for `never`, hidden when absent |
| `notes`, `channel_notes` (or `channel.notes`) | text or array of lines | Release notes |
| `update_ready` | bool | A newer version is downloaded and waits for a restart |
| `previous_version`, `rollback` | text, bool | A previous version to go back to |
| `app_dir` | text | The app's folder |
| `downloads` | `{"pending":bool}` | Shows the retry button |

### Events

| `event` | Fields | What the screen does |
|---|---|---|
| `progress` | `task`, `pct` (0 to 100; outside clamped; missing or -1: busy without a percent), `text`, `label`, `detail`, `note`; optional `bytes_done`, `bytes_total`, `speed_bps`, `eta_s` for a transfer | Progress bar and line. `task` names it: `puppet_pack`/`build_puppet_pack`, `shaders`/`prepare_shaders`, `import`, `firmware`, `check_game`, `update`/`update_download` (the update line), anything else shown as it is |
| `toast` | `text`, `version`, `restart` | A notice. With a `version` other than the installed one, or `restart: true`, it gets the update button |
| `room_saved` | `stored` | With `stored` empty or `main`: "Guardado" on the game's HUD (the room's save was stored while the game ran) |
| `log` | `line` | Written to the emulator's log as `ayudante: <line>` |

### What `play` must give

The screen sets each `env` entry in its own process (a key with `=` or empty is skipped), then starts co-op (`CoopWindow::Attach`, which reads `coop.ini` and the environment at that moment), then boots `game` (full screen when asked). So `env` is where the helper passes the co-op switches, `LICHII_COOP_SERVER`, `LICHII_COOP_NAME`, `LICHII_COOP_PASSWORD`, `LICHII_ORM_HOST_FILE`, `LICHII_ORM_ROOM` and the like. Variables the emulator reads only when the process starts (`nx2_switches.txt`) cannot come through `play`: they must be in that file before the app opens.

### A minimal helper

The fake helper in `lichii/app/tests/orm_app_tests.cpp` (`--fake-helper`) is a complete example in about 60 lines: read a line, parse it, write `{"id":N,"ok":true,...}` and a newline, flush, exit on `quit`.

## Messages and components

No network messages of its own. The toggles send `ROOM_SWITCH` keys through the hook's own command path (PvP goes to upstream's PvP, as `/pvp on`; 09-pvp).

## Tests

| Test file | What it checks |
|---|---|
| `lichii/app/tests/orm_app_tests.cpp` | Client: a request is one JSON line with id, cmd and fields; replies in any chunks and CRLF; events and `log`; bad lines logged and ignored; a request that cannot be written fails at once; `FailAll`; over-long lines; a callback may send the next request; a timed-out request fails and its late reply is ignored. Model: status rows from booleans, objects and paths; the host wish; an empty status leaves every row unknown; `play` gives game, environment and full screen; update, progress, versions and notes; a transfer's line; row folders and shortcut; dropped URLs to local paths; import replies; auto "Preparar" and the update toast; `room_saved`. Poller: one status at a time. With a real child process (the fake helper): import then import_finish, a missing helper, replies, events, stderr, quit, a helper that dies fails what is pending |
| `tests/orm/app_channel_test.cpp` | With no app listening the hook is told and nothing is queued; the app gets each ask and answers come back for the chat; a new listener replaces the old; switches go out with a version that steps only on a change, and the toggles come in |

## Log lines

| Line | When |
|---|---|
| `orm-app: on (<ini>), helper <path>` | The screen is on |
| `ayudante en marcha: <program> <args>` | The helper started |
| `no arranca el ayudante <program>: <why>` | It did not start |
| `ayudante: <line>` | A helper stderr line or `log` event |
| `sin respuesta del ayudante a <cmd>` | A timeout |
| `orm-app: play <game> (N variables)` | "Jugar" booted the game |
| `orm-app: PvP\|Fijar aliados <on> from the screen: <sent>` | A toggle |
| `orm-app: /registros from the game: upload_logs` (also `/reintentar`, `/nombre`) | The game asked the app |
| `orm-app: the game asked N but the helper is not running` | No helper |
| `coop: commands: /registros: no app in this process (fallback: nothing sent)` | In a build without the screen |
| `orm-app: restarting ...` | Restart |

## Status

- Not yet confirmed in game in a two-player harness scenario. The screen's flows are unit-tested, including against a real child process.

## Known gaps

- The screen's texts are in Spanish only.
- Restart uses a small stub next to the app's folder when it exists (it waits for the old process to end); without it the screen starts its own exe again.
- Window mode where the screen stays visible while the game runs in its own window has not been tried.
