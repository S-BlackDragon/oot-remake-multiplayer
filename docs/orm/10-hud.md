**English** | [Español](10-hud.es.md)

# 10-hud: In-image HUD, partner map and quick chat

Upstream draws the co-op's name tags, panel and chat as Qt windows over the emulator window, and since test10 a game-look HUD of the others' hearts as another Qt layer. This feature draws the name tags (with ping), the panel, the chat, short notices and a line of everyone's rupees **inside the game's own picture**, from the emulator's present pass. Each player can place and size his own HUD. On top of it come a partner map (edge arrows, dots on the minimap and pause map, "Ana: en <place>" lines) and quick chat (hold L and press the d-pad).

## What changes for players

- **Name tags inside the picture**: each mate's name in his colour with his ping in grey over his head, a black outline, whole-number text sizes that grow with the picture's height. They show in screenshots and recordings (a Qt layer does not), and they never sit on the black bars. A downed mate's name is orange-red. Tags hide during our own cutscene or talk and while the pause menu is open.
- **Panel (F9)**: the co-op panel drawn in the picture, top left by default. `LICHII_COOP_PANEL=1` starts it shown.
- **Chat and notices in the picture**: the last chat lines bottom left (8 s each), the sender's name in his colour, notices (joins, leaves, downed, connection) in yellow; while the chat box is open the Qt lines show instead. Short "<name> encontró <item>" notices (5 s, at most 3) for chests opened by others.
- **Everyone's rupees** on one fixed line (top centre by default): each connected player's name in his colour, a green rupee and his count (`87?` after 1 s with no sample, `-` while unknown). Wallets are each player's own (feature 06-world); this only shows them.
- **Your own HUD settings**, yours alone (never compared between players):
  - in `coop-options.ini`: rupee line on/off and where, panel place, names on/off, text size, chat lines;
  - while playing, until the game closes: `/hud` (your settings now), `/hud nombres on|off`, `/hud rupias ...`, `/hud panel <place>`, `/hud tamano <50-300>`, `/hud chat <0-6>`, `/hud reset` (back to the file), `/rupias [on|off|place]`, `/nombres [on|off]`. Places: `arriba-izquierda`, `arriba`, `arriba-derecha`, `abajo-izquierda`, `abajo`, `abajo-derecha` (or `top-left`... in English; case, `-`, `_` and spaces do not matter). Two blocks in one place stack; a block bottom left goes above the chat.
- **Partner map**:
  - a mate in your place but not tagged (behind the camera, far off screen or more than 40 m) is an arrow of his colour at the picture's edge with "Ana 52 m"; in view but far, a dot and his name; a downed mate's note is orange-red. These come from the hook's markers, so they also show on the Qt layer;
  - dots of the mates on the minimap and the pause map, in their colours; in a dungeon, a ring for a mate on another floor (and "Ana 2F" on the pause map). This needs a map table (`LICHII_COOP_MAP_FILE`); the pause map is toggled with **F7** (the game's menu is not read yet);
  - "Ana: en Bosque Kokiri": one line per mate in another place, bottom left (Spanish place names from our own table).
- **Quick chat**: hold L and press the d-pad: up `¡Aquí!`, right `¡Vamos!`, down `¡Espera!`, left `¡Ayuda!`. Everyone sees the line in the sender's colour; `¡Aquí!` and `¡Ayuda!` also leave a 5 s marker over where the sender stood (an edge arrow when off screen). While L is held the d-pad is kept from the game (no item, no ocarina). L held alone (no d-pad) stays the warp-song invitation's hold (06-world); one press never does both.
- **Chat rate limit** on both sides: a sender sends at most 8 lines in 10 s, 250 ms apart (quick ones: one a second, 5 in 10 s); the server relays at most 10 lines in 10 s per player; a receiver shows at most 5 quick lines per sender in 10 s.

## Switches

| Variable | Kit default | Code default | Values | What it does |
|---|---|---|---|---|
| `LICHII_ORM_HUD` (old name `LICHII_COOP_HUD`) | `LICHII_COOP_HUD=1` | off (`orm_hud.cpp`) | 0/1, true/false, yes/no, on/off | our HUD inside the picture. Read as `LICHII_ORM_HUD` first, then the old `LICHII_COOP_HUD` |
| `LICHII_COOP_PANEL` | not in the kit (commented) | off (`orm_hud.cpp`, `coop_window.cpp`) | as above | the panel shown at start (F9 toggles) |
| `LICHII_ORM_HUD_RUPEES` | `1` | on (`coop_hud_settings.h`) | as above | everyone's rupees on a fixed line (0: only in the panel) |
| `LICHII_ORM_HUD_RUPEES_AT` | `arriba` | top | a place | where that line goes |
| `LICHII_ORM_HUD_PANEL_AT` | `arriba-izquierda` | top left | a place | where the panel goes |
| `LICHII_ORM_HUD_NAMES` | `1` | on | on/off | names (and ping) over the mates; 0 leaves arrows and dots |
| `LICHII_ORM_HUD_SIZE` | `100` | 100 | 50 to 300 (percent) | text size |
| `LICHII_ORM_HUD_CHAT_LINES` | `6` | 6 | 0 to 6 | chat lines shown |
| `LICHII_COOP_MAP` | `LICHII_COOP_MAP=1` | on unless `0` (`coop_map.cpp`) | `0` turns it off; any other value or unset is on | the partner map; dots and lines need the HUD on |
| `LICHII_COOP_MAP_FILE` | not in the kit | none | a file path | the map table (floors, minimap and pause map views; format in `lichii/hook/coop_map.h`). Without it: arrows and lines only, no dots |
| `LICHII_COOP_MAP_DEBUG` | not in the kit | off | on/off | our own yellow dot on the maps, to calibrate the table |
| `LICHII_ORM_QUICK_CHAT` | `LICHII_ORM_QUICK_CHAT=1` | off (`orm_quick_chat.h`) | on/off | the L + d-pad combination; quick lines from others show either way |
| `LICHII_ORM_QUICK_CHAT_BUTTON` | not in the kit (commented, `R` as the example) | `L` | `L`, `R`, `ZL`, `ZR`, `PLUS`, `MINUS`, `LCLICK`, `RCLICK` | the held button. Another value: L and a warning line |
| `LICHII_COOP_NOTIFY` | not in the kit | on unless `0` (upstream's) | `0` | upstream's chat notices; our HUD draws them |

**`LICHII_COOP_HUD` is two switches.** Upstream's game-look HUD (`coop_hud.*`, the others' hearts) is on unless `LICHII_COOP_HUD=0`. Ours was called `LICHII_COOP_HUD` before test10 and is now `LICHII_ORM_HUD`; an old `LICHII_COOP_HUD=1` still turns ours on. The kit as shipped writes `LICHII_COOP_HUD=1`, so both read "on", and **ours wins**: with our HUD on, `coop_window.cpp` makes neither the Qt panel and markers nor upstream's Qt hearts layer (`HudLayer`), so upstream's hearts of the others are not seen. To get upstream's look back: `LICHII_ORM_HUD=0` (ours off; upstream's stays on). Writing `LICHII_ORM_HUD=1` in the kit instead would say the same thing more clearly.

The `LICHII_ORM_HUD_*` setting lines are read once at start. A value they do not take keeps the default and logs, for example, `coop: switch LICHII_ORM_HUD_SIZE=1000 is not a percent from 50 to 300: 100 (its default)`.

## How it works

Drawing is the cheapest that works on every Vulkan card: opaque rectangles with `vkCmdClearAttachments` inside a render pass of our own (`loadOp LOAD`) on the finished frame. No shader, no pipeline, no buffer. Text is a 7x13 bitmap font; each row of lit pixels becomes strips and equal strips on following rows one rectangle; an outline is drawn first. The cost is no blending: a tag is drawn whole or not at all (below 50 % fade it is left out) and the panel has an outline, not a see-through background. At most 6000 rectangles a frame.

| File | What it does |
|---|---|
| `lichii/hook/orm_hud.h`, `.cpp` | Portable model and layout: tags, panel, chat rows, notices, rupee line, map dots and lines, edge arrows, scales, places, the `On()` switch and F9's panel state. Built from what the hook already works out (markers, player list, panel lines) |
| `lichii/hook/coop_hud_game.cpp` | Glue in `core`: refreshes the player list and panel every 200 ms, markers every call, applies the live settings and the pause bit each frame, adds the map |
| `lichii/hook/coop_hud_vk.h`, `.cpp` | The emulator side (`video_core`): the render pass and the grouped `ClearAttachments` |
| `lichii/hook/coop_hud_font.h` | The font: ProggyClean (MIT), ASCII and Latin-1, generated by `tools/hud_font/make_font.py` from Dear ImGui's `.ttf` |
| `lichii/hook/coop_hud_settings.h`, `.cpp` | Each player's settings: reading the six lines, the `/hud`, `/rupias`, `/nombres` commands, the live store |
| `lichii/hook/coop_notices.h` | Header only: the short "found an item" notices (5 s, at most 3) |
| `lichii/hook/coop_map.h`, `.cpp` | Portable map maths: world to screen and edge arrows, world to minimap / pause map (centre, mirror, turn, span; clamped to the frame's edge with an arrow), floors from the table, the "en <place>" lines with Spanish place names |
| `lichii/hook/coop_map_game.cpp` | Reads `LICHII_COOP_MAP_FILE` once and feeds the map each frame |
| `coop/host/include/coop/host/quick_chat.hpp`, `coop/host/src/quick_chat.cpp` | The four quick texts, the button + d-pad combination (an edge with the button held; a d-pad already down sends nothing), sender and receiver limits, markers |
| `coop/host/include/coop/host/shoulder.hpp`, `coop/host/src/shoulder.cpp` | One button, two uses: L alone is the warp invitation's hold, L + d-pad a quick message that cancels that hold until L is let go |
| `lichii/hook/orm_quick_chat.h`, `.cpp` | Glue: reads the two switches, polls the pad, sets the input mask, sends the line as plain `CHAT` |
| `lichii/hook/orm_input_mask.h` | The d-pad bits the patched emulator takes off the guest's buttons while the quick-chat button is held |
| `lichii/hook/video.cmake` | Adds the `video_core` sources |
| `lichii/patches/0100-orm-hud.patch` | Fritchii patch, all under `#ifdef LICHII_COOP`: `video_core/CMakeLists.txt`, `vk_blit_screen.h/.cpp`, `renderer_vulkan.cpp`: calls our draw right after the window adapt pass in `BlitScreen::DrawToFrame`, so screenshots get it too |
| `lichii/patches/0101-orm-quick-chat-input.patch` | Fritchii patch: `NPad::RequestPadStateUpdate` takes `OrmInputMask::Guest()` bits off what the game reads. The hook's own reads (scripted input too) still see every button. Mask 0 otherwise; nothing changes with `LICHII_COOP` off |

Hook points in upstream files:

| File | Hook |
|---|---|
| `lichii/hook/coop_window.cpp` | With our HUD on: no Qt panel, markers or upstream hearts layer; F9 toggles our panel; F7 toggles the partner pause map (with the map on) instead of upstream's debug panel; Qt chat lines only while typing; no markers while paused |
| `lichii/hook/coop_hook.cpp`, `coop_hook.h` | `Marker::number`, `Markers::paused` (the pause bit of `coop_menus.cpp`, the game's `Sequence_IsPause`, copied in the same frame), chat lines with number, name, colour and "quick"; `CurrentMates` and edge arrows in `Place`; `SendChat` passes `/hud` lines to the settings; wallet lines (`coop: wallet:`) |
| `lichii/hook/coop_chat.h` | Chat lines carry sender number, name, colour, quick |
| `lichii/hook/coop_camera_probe.cpp` | The downed spectator's d-pad reader does not see the d-pad while the quick-chat button is held |
| `server/session.cpp`, `session.hpp` | The relay's chat limit: `chat_count` 10 in `chat_window_us` 10 s per player (0: off), counted in `chat_limited` |
| `coop/host/include/coop/host/notify.hpp` | Player numbers up to `kMaxPlayers` (six) |

## Messages and components

None of our own. Quick messages travel as upstream's `CHAT` (type 9, channel 0): an older game shows them as plain chat, a newer one recognises the exact text; the receiver takes the marker's position from the sender's last sample. The rupee counts come from the per-player wallet sample of 06-world (`PLAYER_EQUIP` component 29, `rupees`).

## Tests

| Test | What it checks |
|---|---|
| `tests/orm/orm_hud_test.cpp` | Switches, UTF-8, scales, the font glyph pixel by pixel with its outline, ping in grey, scene-target to picture with bars, clipping, far-to-near order, fades and NaN, arrow and dot, downed mate colour, `?` for unknown characters, panel and colours, the rectangle cap, chat rows, notices |
| `tests/lichii/hud_settings_test.cpp` | Places, size, lines, on/off; `coop-options.ini` (defaults, all set, bad values and their line); `/hud` and its text; each command and its errors; `RupeeText`; pause (no tags, map dots stay); names off (arrows stay); chat cut; the rupee line takes the list out of the panel; size in the scales; placement in every corner, above the chat, split in two lines |
| `tests/lichii/map_test.cpp` | World to screen and edge arrows (behind the camera at the bottom edge), world to map with clamp and arrow, floors, table parsing, "en <place>" lines, on unless `0` |
| `tests/orm/quick_chat_test.cpp` | The table, the combination, the mask, sender and receiver limits, markers, switch reading, the chat log |
| `tests/host/shoulder_test.cpp` | L shared between the invitation and quick chat |
| `tests/server/session_test.cpp` | 10 lines in 10 s, one log line per burst, `chat_count = 0` off |

Harness scenarios (they will live in `tools/orm/harness`): `comunicacion-aqui` (B sends `¡Aquí!`; A's log has the line and the marker; a burst of ten L+up presses sends three; A sends `¡Ayuda!`; L is checked free; runs with the HUD on), `mapa-flecha` (B near, far, behind the camera and in another place; the test is screenshots at each mark), `mapa-piso` (dots and floor rings with a map table), `cartera-cada-uno` (each player's wallet logged and listed in the panel).

## Log lines

| Line | Says |
|---|---|
| `coop: switch LICHII_ORM_HUD=<value> is not ...` | a value the switch does not take |
| `coop: hud: HUD: nombres sí, rupias sí (arriba), panel arriba-izquierda, tamaño 100 %, chat 6 líneas` | at start, when `coop-options.ini` changes a setting |
| `coop: hud: /rupias off -> HUD: ...` | each settings command |
| `coop: map: no LICHII_COOP_MAP_FILE, no dots on the game's maps` | map on, no table |
| `coop: map: <file>: N dungeons with floors, N minimap and N pause map views, ...` / `coop: map: <file>: line N: ...` | the table read, and each bad line |
| `coop: quick: on (LICHII_ORM_QUICK_CHAT): hold L and press the d-pad: up '¡Aquí!', ...` / `coop: quick: off (LICHII_ORM_QUICK_CHAT=1 turns it on); quick messages from others still show` | quick chat at start |
| `coop: quick: LICHII_ORM_QUICK_CHAT_BUTTON=<v> is not L, R, ZL, ZR, PLUS, MINUS, LCLICK or RCLICK: L is used` | bad button |
| `coop: quick: sent '¡Aquí!' (L+DUP)`, `coop: quick: '¡Aquí!' (L+DUP) not sent: at most one a second and 5 in 10 s ...`, `coop: quick: P2 Ana '¡Aquí!'`, `coop: quick: marker for P2 at (...) in <scene> for 5 s` | sender and receiver |
| `coop: chat: not sent: at most 8 lines in 10 s, 250 ms apart` | the sender's chat limit |
| Server: `player N sent chat over its rate (10 lines in 10 s): dropped until it slows down` | the relay's limit, once per burst |
| `coop: wallet: P1 'Ana' (you) has 87 rupees` | the wallets the HUD lists |

## Status

| Part | Status |
|---|---|
| Quick chat (combination, limits, markers, d-pad kept from the game, L free in play) with the HUD on | confirmed in game on our test9-based build (scenario `comunicacion-aqui`, 18/18) |
| Partner map edge arrows and "en <place>" lines | confirmed in game on our test9-based build (scenario `mapa-flecha`, 4/4) |
| Wallet list in the panel | confirmed in game on our test9-based build (scenario `cartera-cada-uno`) |
| In-image tags, panel and chat | drawn in those runs; no scenario checks them on their own |
| Per-player settings (`/hud ...`, places, size), map dots and floors, pause-menu hiding | not yet confirmed in game |

## Known gaps

- With our HUD on, upstream's Qt hearts of the others are not shown (see Switches).
- Vulkan only: with OpenGL there is no in-picture HUD (the Qt layers stay available with the switch off).
- Names outside Latin-1 (Cyrillic, CJK, emoji) show as `?`.
- Tags hide in our cutscenes, talks and the pause menu, but not over game messages that are not an event.
- Frame generation interpolates the HUD with the picture (possible trails on text when turning fast). An HDR swapchain would need the clear colours converted (Fritchii has none today).
- Map dots need a map table built from each PC's own game files; no table is shipped (no game data). The pause map is toggled with F7: the game's own map menu and its floor tab are not read yet. The minimap and pause map frames are still to calibrate in a game with `LICHII_COOP_MAP_DEBUG=1`.
- Pressing L and a d-pad direction in the same frame can let that one d-pad frame reach the game (the mask applies from the next frame).
- "en <place>" lines bottom left may overlap a panel moved to the bottom left.
- The HUD's preferences live in two places: `orm_stats.txt` (`/hud on|off`, `/mapa`) and `coop-options.ini` plus memory (the settings).
