**English** | [Español](08-age.es.md)

# 08-age: Age change and the Temple of Time

In this game the era is part of the save (`Sequence_IsChildhood`), and the child and adult worlds are different scenes (`KokiriForest_Child` / `KokiriForest_Adult`). When one player draws or puts back the Master Sword, the others would stay in the other era with nothing to share. This feature makes the group follow one player's age change through the game's own era change, and keeps the Temple of Time's cutscenes for the players who are in the temple. It is **off in the kit** until a game confirms that the game takes the era change asked for away from the pedestal.

## What changes for players

With `LICHII_ORM_AGE=1`:

- When a mate grows up or goes back to being a child at the Temple of Time, every other game follows with the game's own era change (`Market_Child_Field_ChildToAdult` or `Market_Adult_Field_AdultToChild`): the game swaps the equipment, sets the era and loads the Temple of Time itself. Everyone arrives in the temple already changed.
- Nobody is moved by surprise. A big notice in the centre of the picture (bigger than the chest notices; the newest replaces the last) tells each player what happens. The texts are the HUD's Spanish lines:

| Who | Notice | Also a chat line |
|---|---|---|
| The one who drew or put back the sword | `Moviendo al grupo contigo...` | yes |
| The others, 2 s after they see the change | `<Nombre> ha cambiado de época: moviendo al grupo al Templo del Tiempo...` | yes |
| The others, once it is safe to move them | the same notice with `3`, `2`, `1` under it, one a second; then the era change | no |
| One who cannot move yet | the same notice with `Te moverás cuando termine la escena` / `cuando acabe el combate` / `cuando te reanimen` | the first time |
| Those already changed while a mate waits | `Esperando a <Nombre> para cambiar de época...` (and `Está caído` or `Está en una escena` when known), again every 15 s | the first time |
| Who arrives in the temple | `Grupo reunido en el Templo del Tiempo` when everyone is there; otherwise `En el Templo del Tiempo` / `Esperando al resto del grupo...` | yes (gathered) |
| When the change does not take twice in a row | `No se pudo cambiar de época: inténtalo en el pedestal` | yes |

- The move never runs in the middle of something: not during an event or cutscene (a talk counts), not while a boss room holds the player in (intro, fight or exit), and not while the player is down (also not lying down: the move would take him away from whoever can revive him). When the reason is over, the 3, 2, 1 starts again.
- The Temple of Time (with the story cutscenes on, [07-cutscenes](07-cutscenes.md)): the temple is part of the market's scene (`Market_Child` / `Market_Adult`), so its cutscenes (the Door of Time, the pedestal, Sheik in the temple) play only for the players inside the temple, not for one shopping in the square. Whoever is inside when the sword is drawn travels with that cutscene; the others follow by the era change above once nothing runs in their game.

With `LICHII_ORM_AGE=0` (the kit's line) each game keeps its own era, as in upstream.

## Switches

| Variable | Kit default | Code default | Values | What it does |
|---|---|---|---|---|
| `LICHII_ORM_AGE` (old name `LICHII_COOP_AGE` still read) | `LICHII_ORM_AGE=0` | off (`coop_age_game.cpp`, `CoopSwitch::On`) | 0/1/true/false/yes/no/on/off | Follow a mate's age change |
| `LICHII_ORM_AGE_EVENT` (old `LICHII_COOP_AGE_EVENT`) | `# LICHII_ORM_AGE_EVENT=` (commented out) | unset: the game's own era change | an event name (A-Z, a-z, 0-9, _) | Tests only: reload our own scene in place while that event runs instead of the era change; no notices, no countdown. `ReplaceEquipmentForEraChange` is ignored (the game never starts it by name, see below) |
| `LICHII_ORM_TIME` | not set (only a comment showing `LICHII_ORM_TIME=0`) | on (`coop_time_temple_game.cpp`) | 0/1/... | The Temple of Time rules; they work only with `LICHII_ORM_STORY_EVENTS` on |
| `LICHII_ORM_TIME_LIST` | not set (comment shows `shared_time_temple.txt`) | the built-in list | a file path (relative to the kit folder) | The temple rules from a file; the kit's `shared_time_temple.txt` is the same text as the built-in list (a test checks it) |

## How it works

| File | What it does |
|---|---|
| `coop/host/include/coop/host/age.hpp`, `coop/host/src/age.cpp` | `AgeFollow`, the portable rule (no memory, no sockets). A mate's age counts once it has been the same for 2 s, he is not loading and it matches his scene's suffix (the first reading of a mate is never a change). A change to the other age makes ours pending; with calm it asks the game's era change in the right direction, writes nothing before that event runs, and gives a 120 s limit (cutscene, load and arrival; a gap of up to 1 s with no event is the cutscene hand-over, not the end). It also makes the notices (`AgeNotice`: `moving_you`, `announce`, `countdown`, `waiting_mate`, `gathered`, `arrived`, `failed`), the waits with their reason (`AgeInput::boss_fight`, downed, event) and the give-up after `era_fail_limit` (2) misses. `AgeInput::wait` (the Temple of Time) keeps a seen change for later; `AgeInput::held` (the endgame) drops it |
| `lichii/hook/coop_age_game.cpp`, `.h` | The glue: reads the switch, the mates' ages from presence, our era, whether a boss room holds us (`CoopBoss`), whether we are down (`CoopDowned`), and carries out what `AgeFollow` asks: the event by name through the story layer's request (`OrmStoryEvents::Ask`), the notices (`coop_notices.h` `Banner`, drawn by `orm_hud` `Model::banner`), the chat lines (`TakeLines`) and our `age_waits` status bit |
| `coop/host/include/coop/host/time_temple.hpp`, `coop/host/src/time_temple.cpp` | The temple list (rules `temple`, `travel`, `chain`, `sage`, `scene`; `area`, `noage`, `flag`), `check_time_list` (the list must agree with the story list and the shared items) and `TimeTemple`: `filter` (a temple cutscene's start is taken only in the temple; one not taken counts as played) and `tick` (the reason the room's age waits while the sword's story runs here) |
| `lichii/hook/coop_time_temple.h`, `coop_time_temple_game.cpp` | Glue: reads the switch and the list, adds the temple's flags to the shared flags, and tells `CoopAge` to wait (`HoldsAge`) while our game runs the sword's story |
| `tools/release/kit/shared_time_temple.txt` | The temple rules as a kit file |

The temple box was read from the romfs (`LocationArea` "TimeTemple" of both market scenes, with a margin): x 58..160, y 95..125, z -56..-4. It holds the pedestal, the altar and the entrance, and leaves the square out.

Why the game's era change: the first design asked the game for `ReplaceEquipmentForEraChange` by name and reloaded the scene in place. The game refused it every time: it is a sub flow of the pedestal's flows with no row in `RSDB/EventSetting`, and the game only starts by name an event that has a row. The pedestal's own flows have one, so the era change is now asked by those names.

Never leaving the era changed: nothing is written before one of our events runs, and if the reload does not take after it ran, `Sequence_IsChildhood` is put back to our Link's age.

Hook points in upstream files:

| File | Hook |
|---|---|
| `lichii/hook/coop_hook.cpp` | `CoopAge::Frame` each frame after the story layer's frame, and its chat lines (`CoopAge::TakeLines`) into the chat |
| `lichii/hook/coop_testinput.cpp` | The test command `age adult|child` (this game follows as if a mate had changed) |
| `coop/host/include/coop/host/sampler.hpp`, `session.hpp` (and `sampler.cpp`) | `set_age_waits`: our `PLAYER_STATUS` carries bit 6 while our move waits |

## Messages and components

| Name | Number | Use |
|---|---|---|
| `PLAYER_STATUS` bit `age_waits` (`orm_status::age_waits`) | bit 6 of the flags byte (`orm_types.hpp` `kStatusAgeWaits`) | Our move to the Temple of Time waits; the others show "Esperando a..." |
| `PLAYER_SAMPLE` `age` (upstream) | upstream's field | Each mate's age, as before |

No new message type. A server that does not know bit 6 relays it untouched.

## Tests

| File | What it checks |
|---|---|
| `tests/host/age_test.cpp` (26 cases) | Scene names by age; a mate's change followed; no change asked for a mate going to our age or back; retries and the 10 s retry; the event that ends with no load; a mate who leaves and comes back seen afresh; the endgame hold; the Temple of Time's wait; Spirit Temple start places; our own reading while loading; lying down and the reload; the game's era change by default with nothing written by us; the way back to child; refused change never touching the era; era put back after a change with no load; the group cases: notice order and 3, 2, 1, the waits (cutscene, boss room, down) with their reason, "Esperando a..." every 15 s, "Moviendo al grupo contigo...", give-up after two misses, the in-place reload saying nothing |
| `tests/host/time_temple_test.cpp` (9 cases) | The built-in list equals the kit file; its rules, areas and flags; agreement with the story list and shared items; bad lines refused with their number; the filter (temple starts only in the temple, the sword's chain, Sheik elsewhere, the sages); the age hold |
| `tests/orm/orm_hud_test.cpp` | The big centred notice (`Banner`) |
| `tests/orm/host_sampler_test.cpp` | The `age_waits` bit |

Harness scenarios (they live in `tools/orm/harness`): `edad` (the older in-place reload, with the test command `age`), `edad-grupo-cerca` (B in the market square) and `edad-grupo-lejos` (B in Kokiri Forest): A draws the sword, B gets the notice, counts and arrives adult in the temple, both see "Grupo reunido". `tiempo-espada` and `tiempo-medallon` cover the temple rules.

## Log lines

All start with `coop: age:` or `coop: temple:`.

| Line | Says |
|---|---|
| `coop: age: on (LICHII_ORM_AGE): a mate's age change is followed by the game's own era change ('Market_Child_Field_ChildToAdult' / 'Market_Adult_Field_AdultToChild': ...)` | On, default path |
| `coop: age: on (LICHII_ORM_AGE): a mate's age change is followed (the reload asks the event '...' and reloads our scene in place; ...)` | On, test path (`LICHII_ORM_AGE_EVENT`) |
| `we changed age ourselves (child -> adult): the group follows us to the Temple of Time ...` | We drew the sword |
| `told: '<Nombre> ha cambiado de época: ...'`, `countdown 3 before the move to the Temple of Time` (2, 1) | The announcement and the countdown |
| `coop: age: notice <kind>: <text> / <second line>` | Each notice shown |
| `coop: age: our PLAYER_STATUS carries age_waits (bit 6)` / `drops age_waits` | Our move waits / goes on |
| `every mate is our age: the group is gathered (told 'Grupo reunido en el Templo del Tiempo')` | Gathered |
| `the reload as adult did not take: ...; tried again in 10 s` / `...; 2 in a row: given up, no move (the era stays our Link's; ...)` | A miss, the give-up |
| `Sequence_IsChildhood put back to child (our Link's age): the era stays his` | The era restored after a miss |
| `reloads wait: the Temple of Time: our game runs '...' (travel)`, `reloads may begin again` | The temple's hold |
| `coop: temple: on (...): N Temple of Time events, N temple areas, N scenes with no age ...` / `coop: temple: off (LICHII_ORM_TIME=0)` | Temple rules on or off |
| `coop: temple: list: agrees with the story list and the shared items` | The list check |
| `coop: temple: the room's age waits: ...` / `coop: temple: the room's age is followed again` | The temple hold, from the temple side |

## Status

- Age change: **not yet confirmed in game.** The older in-place reload ran in the `edad` scenario on our test9-based build, but the game refused its event (`ReplaceEquipmentForEraChange`) every time, which is why the kit ships `LICHII_ORM_AGE=0` and why the code now asks the game's own era change. That path, the notices and the group move are unit-tested only (`edad-grupo-cerca` and `edad-grupo-lejos` are written, not run).
- Temple of Time rules: **not yet confirmed in game** (`tiempo-espada`, `tiempo-medallon` not run).

## Known gaps

- A game run has to show that the game takes `Market_Child_Field_ChildToAdult` asked away from the temple (the line `the game took '...'`), that its cutscene looks right there, that B arrives adult in `Market_Adult` with the Master Sword and his age's shield, and the way back to child.
- Whether the big notice reads well and covers nothing important.
- The Door of Time seen from the square: its look is decided by its own graph reading `Market_Child_TimeTempleDoor_Opened`, not by a flag presence in the scene; whether a flag written from outside shows it open without a reload is not seen.
- On Hookshot (upstream's next target) nothing of this was built or run.
