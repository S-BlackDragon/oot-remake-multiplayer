# 02-network: Six players, our message block, WebSocket, one host

The network side of our work: up to six players in a session, one block of message numbers for everything we add (0xF000 to 0xF0FF) and protocol version 110, a `Transport` interface under the client so the same protocol runs over ENet/UDP or WebSocket (`ws://`, `wss://`), notices about a player's own link, the group's settings, the relay's choice of host, and interest management for poses with six players.

Upstream's join password and traffic encryption (test10) sit above our `Transport`: `Client` seals and opens, so UDP, `ws` and `wss` carry the same sealed packet on the same channel.

## What changes for players

- Up to **six** players in one session. Players 5 and 6 get purple and orange by default (1 to 4 keep upstream's blue, red, green, yellow).
- A game can join over WebSocket: `server = ws://your-server.example:27511/` or `wss://your-server.example/path` in `coop.ini`. Useful where UDP is blocked or behind a web proxy.
- When a link cannot keep up (frames dropped on the way up or down, or the server dropping a player's messages over its rate), the player is told in the chat and the log, once, and told again when it is fine. When the server is of another protocol, the refusal says which.
- One host for the whole room, picked by the server from measured round trips (lowest average to the others, steady link, game in the world), with hysteresis. A player can pick by hand with `/anfitrion <name>` (or `/host`), and `auto` gives the choice back. Each change is a chat notice ("who hosts and why").
- The group's settings: amiibo and ZELDA NOTES follow the host's value while you play as a guest (yours comes back after). A co-op switch that differs from the host's is told in the chat on both sides, once.
- Nothing visible from interest management: a game sends its Link's full pose only while another player is in its place, and the server forwards poses only to players in the sender's place.

## Switches

| Variable | Kit default | Code default | Values | What it does |
|---|---|---|---|---|
| `LICHII_COOP_TRANSPORT` (`coop.ini` key `transport`) | not in the kit | `udp` (`coop_settings.cpp`) | `udp`, `ws`, `wss` | The wire. A `server =` address with a scheme sets it too |
| `LICHII_COOP_SERVER` (`coop.ini` key `server`) | written by the launcher | none | `host`, `host:port`, `udp://host[:port]`, `ws://host[:port][/path]`, `wss://host[:port][/path]` | Default ports: 27510 udp, 27511 ws, 443 wss. A path needs `ws://` or `wss://` |
| `LICHII_COOP_PATH` (`coop.ini` key `path`) | not in the kit | `/` | a path starting with `/` | The WebSocket path |
| `LICHII_ORM_HOST` | not in the kit | `auto` (`coop_host_choice.cpp`) | `auto`, `me`, `never` | Our wish to host, sent after each `WELCOME` |
| `LICHII_ORM_HOST_FILE` | not in the kit (a launcher sets it) | unset: none | a file path | The file where the game writes who hosts (on each change and every 20 s), for a launcher that keeps the room's save with the host |
| `LICHII_ORM_GROUP_SETTINGS` | not in the kit | on unless `0` (`orm_group_settings.cpp`) | `0` turns it off | Group settings (`GROUP_SETTINGS`) |
| `LICHII_COOP_INTEREST` | not in the kit | on unless `0` (`coop_hook.cpp`) | `0` turns it off | Send poses only while another player is in our place |
| `server_websocket` (kit, host only) | commented out (`# server_websocket = no`): on | | `yes`, `no` | Kit scripts pass `--ws-port <UDP port + 1>` or `--ws-port off` to the server (see Known gaps) |

`LICHII_ORM_GROUP_SETTINGS` and `LICHII_COOP_INTEREST` predate the shared switch reader: they are off only for the exact value `0`.

## How it works

| File | What it does |
|---|---|
| `coop/protocol/include/coop/protocol/orm_types.hpp` | The one table of every number we add: message types (0xF000 block), component ids (24 to 31, ours counting down from 30), `PLAYER_STATUS` bits (ours from bit 7 down), `OBJECT_SAMPLE` state bits (from 7 down), and upstream's used blocks. `static_assert`s check ours never overlap upstream's. A new number is added here first, never as a literal |
| `coop/protocol/include/coop/protocol/orm_status.hpp` | Our `PLAYER_STATUS` bits: `at_title` (7) and `age_waits` (6) |
| `coop/protocol/include/coop/protocol/orm_messages.hpp`, `src/orm_messages.cpp` | Bodies of `GROUP_SETTINGS`, `ROOM_SWITCH`, `PLAYER_HIT`, `PLAYER_CALL`, `PLAYER_RENAME`, kept out of `codec.cpp` so a re-pin touches few lines there |
| `coop/protocol/include/coop/protocol/messages.hpp`, `src/codec.cpp` | `kProtocolVersion = 110`, `kMaxPlayers = 6`, `slot_color` 5 and 6; our structs, channel table (`check_received`) and sender table (`may_send`) |
| `coop/net/include/coop/net/transport.hpp` | The `Transport` interface: `start`, `resolve`, `connect(host, port, path, timeout)`, `send(channel, bytes)`, `service(wait)`, `flush`, `stats` (round trip, loss, MTU, bytes and frames queued, frames dropped), and upstream's small-datagram hooks (`full_datagrams_lost`, `use_small_datagrams`, ENet only) |
| `coop/net/src/enet_transport.*` | ENet over UDP, upstream's code moved behind the interface (small path, waiting-data mend) |
| `coop/net/src/ws_transport.*` | WebSocket over TCP, or TLS then WebSocket for `wss://` (the Windows ROOT store; tests can set their own roots). One binary frame per protocol message: byte 0 the channel, then the message. Channels 0 to 2 reliable (at most 16 MiB queued); 3 and 4 keep 64 frames each and drop the oldest. Nagle off. A frame bigger than any sealed message ends the connection |
| `coop/net/src/client.cpp` | Picks the transport from `transport` (`ws`/`wss` → `WsTransport`, else `EnetTransport`); seals above it; keeps the last 16 `LINK_NOTICE`s for the game; reads a `server_protocol` notice before a `REFUSED` |
| `coop/host/orm_link_watch.*` | Decides when to warn about a link and when it is fine again, from what the client measures (our dropped frames, bytes held high for 3 s, UDP loss held high) and from `LINK_NOTICE`. Each kind reaches the player at most once a minute; the log has every change |
| `coop/host/host_choice.hpp` (+ `lichii/hook/coop_host_choice.*`) | Game side of the host choice: `/anfitrion`, our wish, the relay's word to the chat, the host file |
| `coop/host/orm_group_settings.*` (+ `lichii/hook/orm_group_settings.*`) | Three kinds of setting: `host_wins` (game options that bring things into the shared world: amiibo, ZELDA NOTES), `must_match` (co-op switches, compared with the host's and told once), `local` (each player's own, never sent). Each switch is read the way its own code reads it; list files are sent as a fingerprint |
| `lichii/hook/coop_interest.*` | Two tiers only: another player in our place → poses at the native 30 a second, at any distance; nobody → no pose. The place is the scene without its era. Anything unknown counts as here. `PLAYER_SAMPLE` always goes to everyone (the partner map and the jump to a partner use it) |
| `server/orm_interest.hpp` | The server's side: a `PLAYER_POSE` goes only to the players in the sender's place (unknown scenes: everyone) |
| `server/host_choice.hpp`, `host_choice.cpp` | The relay's choice of host: it pings each player every 250 ms; score = own round trip plus the others' mean; only a player in the world with a steady link can be picked by measure; a better one takes over only when better by a margin for a hold time, outside a cooldown; an unfit host is replaced; wishes (`me`, `never`) rank before measures; a name picked by hand holds until the next pick, `auto`, or the room empties |
| `server/orm_host.cpp` | `HOST_CHOICE` handling in the `Session`: wish, pick (at most one a second per player), `told` to everyone after each change and to a joiner, `refused` to the picker |
| `server/orm_session.cpp` | `GROUP_SETTINGS` (kept per player, relayed, a repeat not relayed, sent to a joiner lowest number first), `ROOM_SWITCH`, `PLAYER_HIT`, `PLAYER_CALL`, `PLAYER_RENAME` (09-pvp) |
| `server/enemies.cpp` | Enemy ownership rules with six players (05-bosses) |

### Hook points in upstream files

| Upstream file | What we added |
|---|---|
| `server/session.cpp`, `session.hpp` | On join, after upstream's: the story events played (07), every player's colour, statuses, group settings and room switches. Pose relay through `orm::pose_goes`. Measured PONGs and the host choice tick. A player over its message rate is logged and sent `LINK_NOTICE over_rate`, at most once a minute. Group defeat, revive and PvP grace (04, 09); enemy and boss rules (05) |
| `server/enet_server.hpp` | The connection limit is `kMaxPlayers` plus one, so a seventh player is told the server is full |
| `coop/net/client.cpp`, `client.hpp` | The `Transport` split; ENet code moved to `enet_transport.*`; size limits add `kSealOverheadBytes`; `REJOIN_CLAIM` and retries go through the `Transport` |
| `lichii/hook/coop_settings.cpp`, `.h` | `server =` with `udp://`, `ws://`, `wss://` and a path; keys `transport`, `path`; their `LICHII_COOP_*` variables |
| `lichii/hook/coop_hook.cpp` | Interest, host choice, group settings and link watch wired into the frame and the network events |
| `coop/presence`, `coop/overlay` | Player numbers up to `kMaxPlayers`; each player's `PLAYER_LOOK` colour kept for the panel |

## Messages and components

Protocol **110**. Our types are 0xF000 + (old number − 240): they were 60 to 65 up to protocol 108 and 240 to 254 in 109; test10 took 241 to 255, so the whole block moved. Types are `u16` on the wire.

| Type | Number | Channel | Sent by | Carries |
|---|---|---|---|---|
| `REVIVE` | 0xF000 | 0 session | client | A mate's hold of A beside a downed player: `other_player`, `progress` (0 to 100 %, 255 = done). The server passes it only to the downed player (04) |
| (reserved, "revived") | 0xF001 | | never sent | |
| `GROUP_DEFEAT` | 0xF002 | 0 | server | Every player (two or more) dead for 2 s: players down, server time (04) |
| `STORY_EVENT` | 0xF003 | 0 | client, server | A story cutscene: kind (`start`, `end`, `played` to a joiner, `skipped`), scene, name, position, server time (07) |
| `PLAYER_LOOK` | 0xF004 | 0 | client, server | A player's colour (RGB) and a reserved style byte |
| `OBJECT_POSE` | 0xF005 | 4 object samples | client, server | A townsperson's bones, in up to 4 parts of 128 (11) |
| `GROUP_SETTINGS` | 0xF006 | 0 | client, server | Up to 48 key/value text pairs, keys in increasing order |
| `OCARINA_NOTE` (ours, `OrmOcarinaNote`) | 0xF007 | 0 | client, server | Note, release or song, instrument, serial (11) |
| `ROOM_SWITCH` | 0xF008 | 0 | client, server | Room-wide switches set live: key 1 PvP (no longer set: upstream's `PVP_MODE` is used), 2 lock on mates, 3 bodies (09, 12) |
| `CHEST_OPEN` | 0xF009 | 0 | client, server | A chest's placement hash, scene, opening animation, time (06) |
| `PLAYER_HIT` | 0xF00A | 0 | client, server | Our weapon hit on a mate's puppet, to the victim only; since test10 the PvP base is upstream's `PVP_HIT` (09 says how the two fit) |
| `PLAYER_CALL` | 0xF00B | 0 | client, server | "Come to me" to one mate, or the host's "gather" (09) |
| `PLAYER_RENAME` | 0xF00C | 0 | client, server | A new name, checked by the server (09) |
| `LINK_NOTICE` | 0xF00D | 0 | server only, never relayed | Frozen body `u8 kind, u32 a, u32 b`: `behind` (percent dropped, KiB waiting), `caught_up` (seconds behind), `server_protocol` (its version, before a `REFUSED`), `over_rate` (unreliable, reliable dropped). Unknown kinds are kept and ignored |
| `HOST_CHOICE` | 0xF00E | 0 | client (`wish`, `pick`), server (`told`) | Who hosts and why (`first`, `left`, `measured`, `unfit`, `picked`, `wished`, `refused`), who picked, the host's measured round trip, the name pinned by hand |

Components and bits (ids unchanged by the move to 110):

| Where | Id or bit | What |
|---|---|---|
| `PLAYER_SAMPLE` component | 30 `ride` | The platform a player rides and the offset on it (11) |
| `PLAYER_EQUIP` component | 30 `mount` | The horse a rider sits on, by actor name (11) |
| `PLAYER_EQUIP` component | 29 `rupees` | The player's own wallet, shown, never written into another game (06, 10) |
| `OBJECT_SAMPLE`, `OBJECT_SPAWN`, `HANDOVER_STATE` component | 29 `carry` | A lifted object in a player's hands: holder, place, bone (11) |
| `PLAYER_STATUS` bit | 7 `at_title` | On the title screen or file select (sent with `loading`) |
| `PLAYER_STATUS` bit | 6 `age_waits` | The room's age changed and our move waits (08) |
| `OBJECT_SAMPLE` state bit | 7 `owner_keeps`, 6 `owner_keeps_near` | The owner's game keeps a boss a while (05) |

## Tests

| Test file | What it checks |
|---|---|
| `tests/net/ws_transport_test.cpp` | `ws` and `wss` connect and handshake; a frame comes back with its channel and bytes; a frame bigger than any message ends the connection; the server closing is reported; a certificate for another host name is refused (18 cases, 127.0.0.1 only) |
| `tests/net/client_rtt_test.cpp` | Round trip zeros before the first PONG, a median once pings are answered |
| `tests/orm/protocol_codec_test.cpp` | Each of our types: number, channel, layout, bad bodies refused; the `carry` component skipped by an old reader |
| `tests/orm/group_settings_test.cpp`, `group_settings_codec_test.cpp`, `group_settings_server_test.cpp` | Options and switches read as their code reads them; list fingerprints; a guest plays with the host's amiibo and gets its own back; a difference told once on both sides; codec limits; relay, joiner, leaver |
| `tests/orm/host_choice_test.cpp`, `tests/server/host_choice_test.cpp` | `HOST_CHOICE` layout, channel, senders; `/anfitrion` and `/host` parsing and name matching; `LICHII_ORM_HOST` words; the chat lines; on the server: PONG accounting, steady link, the score, hold, margin, cooldown, unfit host, wishes, picks (26 cases) |
| `tests/orm/link_watch_test.cpp` | A link that keeps up says nothing; uplink drops, held bytes and held loss warn once and "fine again" after the calm time; flapping is told once a minute; reset when not connected |
| `tests/orm/net_client_test.cpp` | Our colour after every `WELCOME`; `LINK_NOTICE server_protocol` before `REFUSED`; notices kept (at most 16); a client never sends one |
| `tests/server/orm_interest_test.cpp`, `tests/lichii/interest_test.cpp` | Place = scene without era; a pose only to the sender's place; anything unknown counts as here; the townsperson pose follows the same rule |
| `tests/orm/server_session_test.cpp` | Six players welcomed as 1 to 6; the host's save with six; revive and group defeat relay rules |
| `tests/server/session_fuzz_test.cpp` | Random and mutated messages (our 0xF000 types and upstream's up to 511) never crash the session, never produce a bad message, never spoof a sender |
| `tests/server/orm_test_join.hpp` | Helper so server tests join with upstream's password and key exchange |

Harness scenarios (they will live in `tools/orm/harness`): `ajustes-amiibo`, `ajustes-distintos` (group settings), `ancho-seis` (one game and bots, bandwidth with six).

## Log lines

| Line | When |
|---|---|
| `coop: link: our uplink cannot keep up: N of our frames dropped in ...` | Our uplink drops frames |
| `coop: link: our uplink keeps up again (behind for ...` | Fine again |
| `coop: link: the server drops N% of the frames for us (...` / `... keep up again (behind for N s)` | `LINK_NOTICE behind` / `caught_up` |
| `coop: link: the server dropped N of our samples or poses and ...` | `LINK_NOTICE over_rate` |
| `coop: link: the server speaks protocol N` | `LINK_NOTICE server_protocol` |
| `player N (name) is over its message rate: ...; told the player` | Server log, a player over its rate |
| `coop: host: our wish is <wish> (LICHII_ORM_HOST), host file <file>` | Start |
| `coop: host: player N <name> hosts (<why>...)` | `HOST_CHOICE told` received |
| `coop: host: host file: <name> hosts...` / `coop: host: could not write the host file <file>` | Host file written / failed |
| `coop: settings: group settings on: amiibo and ZELDA NOTES follow the host, ...` | Group settings on |
| `coop: settings: group settings off (LICHII_ORM_GROUP_SETTINGS=0)` | Off |
| `coop: settings: ours: ...` | Our settings as sent |

## Status

- Protocol 110 and the 0xF000 block: confirmed in game on test10 (two games joined with the join password in the body scenarios `choque-bloqueo`, `choque-encima`, `choque-continuar`).
- WebSocket transport: confirmed in game on an earlier build (scenario `walk` with two games over `ws://`, before test9); not re-run on test10.
- Six players: not yet confirmed in game with six real games (a run with one game and five bots joined all five and showed their puppets; other checks of that run failed for reasons outside the network).
- Host choice, group settings, link notices, interest management: not yet confirmed in game (unit-tested).

## Known gaps

- **The server in this tree has no WebSocket loop.** `coop_server` speaks ENet/UDP only and has no `--ws-port` option, but the kit scripts (`start-server.ps1` through `orm-hosting.ps1`) always pass `--ws-port <port>` or `--ws-port off`. With this tree's server, either drop those two arguments in `Get-OrmWsArgs` or add a WebSocket loop to the server. The game side (`ws://`, `wss://`) is complete.
- `LINK_NOTICE behind` and `caught_up` need a server with its own per-receiver send queue that drops old unreliable frames; this tree's server sends only `over_rate`. The game handles all four kinds.
- The host choice moves the relay's host (flags, save transfer, clock); a launcher that keeps the room's save must read `LICHII_ORM_HOST_FILE`.
