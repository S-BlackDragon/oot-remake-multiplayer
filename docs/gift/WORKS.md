# What works in a real game

Only what is **confirmed in game**: seen with two players in a real game run (two emulated games, a local room server,
the scenario green), with the scenario name, the tree, what was seen and the evidence. How we ran them, and every red,
is in [TESTED.md](TESTED.md). "test9" and "test10" below mean our work on top of your v0.1.0-test9 or v0.1.0-test10;
this gift is the test10-based tree.

Log lines are copied from the game logs, shortened with "..." and with addresses and paths taken out.

## Confirmed in game

### 02-network

| Item | Scenario | Tree | What was seen | Evidence |
|---|---|---|---|---|
| Protocol 110 with your sealed traffic and join password, over our WebSocket transport and over UDP | every test10 run; `cartel` (signboard) over WebSocket and over UDP | test10 | both games join with the password, traffic sealed, scenarios pass on both transports | `coop: on, server 127.0.0.1:... (ws /), name 'A', join password set`; `coop: traffic encrypted`; `cartel` PASS 4/4 on WebSocket and 4/4 on UDP |

### 03-fixes

| Item | Scenario | Tree | What was seen | Evidence |
|---|---|---|---|---|
| A host on the title screen no longer hands a fresh save's flags to whoever joins | all of round 7 (50 pairs, 100 games, A joining second) and the test10 runs | test9, test10 | every game's first life is its own save's; no `save mismatch` | `coop: flags: we host with no save in (the title screen): our table is held until our save is in, so no joiner gets a new game's flags (3 hearts, no magic)`; harness: `-- A: first life 44 of 44 = its save's 44 (ok)` |

### 04-downed

| Item | Scenario | Tree | What was seen | Evidence |
|---|---|---|---|---|
| Downed, not dead; revived by a mate | `caer-pareja` (downed pair) | test9 (A first and A second), test10 | A at life 0 goes downed, B revives him, life back to full | PASS 8/8. `coop: downed: alive -> downed (life 0 of 12, ...)`; `coop: downed: downed -> reviving: revive requested by P2`; `coop: downed: reviving -> alive: revived (by P2, life 12 of 12, 6.96 s after the gate opened)` |

### 05-bosses

| Item | Scenario | Tree | What was seen | Evidence |
|---|---|---|---|---|
| Entering a boss room together | `jefe-gohma-entrada` (Gohma boss entry) | test9 (A first and A second) | A in Gohma's room; B, at the dungeon entrance, is moved in when A's intro starts; the intro starts and ends in both | PASS 9/9. checks: `boss: in the boss room of InsideTheDekuTree: Enemy_DungeonBoss_Gohma` (A and B), `another player is in the boss room of InsideTheDekuTree: moving in`, `intro started`, `intro ended`; never `boss beaten` |

### 06-world

| Item | Scenario | Tree | What was seen | Evidence |
|---|---|---|---|---|
| Group heart capacity | `corazon` (heart) | test9 (A second) | A gets a heart container; B raises its own maximum and keeps its life | PASS 9/9. B: `coop: hearts: max life 12 -> 16 (from P2)`, `coop: hearts: our Link's max 12 -> 16 (life 12 kept)` |
| Each player's own wallet | `cartera-cada-uno` (each one's wallet) | test9 (A second) | each keeps his rupees; each game knows the others' | PASS 21/21. `coop: wallet: P2 'A' (you) has 87 rupees (was 7)`, `... has 25 rupees (was 87)`, `coop: wallet: P1 'B' has 43 rupees (was 7)` |
| Talking claims the townsperson; the other watches | `charla` (talk), `charla-dos` (talk, two) | test9; `charla` also test10 | A talks to a Kokiri; B sees the shared talk, `not claimed`, the pose blend, `talk over` | `charla` PASS 4/4 (test9 and test10); `charla-dos` PASS 8/8, B: `coop: npc: Npc_Kokiri_004_Child ... talks with its owner P2: not claimed, our talk plays on our copy only` |
| Talk flags reach the other game (Mido) | `charla-mido` (Mido talk) | test9, test10 | A talks to Mido to the end; the flag reaches B, also with Mido not loaded there | PASS 20/20. `coop: npc: talking to Npc_Kokiri_Mido_Child ... (owner P1, claimed), found by its event`; `coop: npc: talk over, claim let go` |
| The game's own event finds the townsperson; no false talk by reach | `charla-saria` (Saria talk) | test9 (A second) | A talks to Saria where she sits | PASS 13/13. `coop: npc: talking to Npc_Kokiri_Saria_Child_OcarinaHolder ... (ours), found by its event`; `coop: npc: our event 'DefaultBootEvent' is not a talk: no townsperson taken by reach` |
| Chests open in every game (`CHEST_OPEN`) | `efectos-cofre` (chest effects) | test9, test10 | A opens a chest; B's lid opens with it, and stays open | PASS 23/23. B: `coop: objects: share Obj_TBox_A_02 ...: opened in another game, its lid opened here with it (CHEST_OPEN)`; `coop: check: chest lid ok Obj_TBox_A_02 kept our 'FrontOpen' 300 ms in its slot 0` |
| Shared Water Temple level, waiting under a swimmer | `meca-agua-templo` (Water Temple level) | test9 (A second) | the level from the other player waits while our Link swims, then is written; it shows on the room's next load (not live) | PASS 17/17. `coop: water: WaterTemple_Room00_WaterLevel (...) from another player waits: our Link plays 'SwimWait'`; `... waited 7.5 s: written, our Link stands` |
| Held floor switch, Fire Temple room 10 | `meca-sostenido-fuego` (held switch, Fire) | test9 (A second) | A stands on the switch; B's fire wall goes | PASS 12/12. B: `coop: held switch: FireTemple room10: door opened with the flag (Obj_FireTemple_FireWallTrap_A_01 went, 3.4 s after the press)` |
| Warp-song invitation | `viaje-cancion` (song trip) | test9, test10 | B plays the Minuet of Forest and travels; A gets the invitation, holds L and follows | PASS 9/9. `coop: songs: our Link arrives by a warp song in SacredForestMeadow_Child ... (event Dm_SY_Ocarina_move_Ed)`; `coop: songs: following P2 to SacredForestMeadow_Child (held L)` |

### 07-cutscenes

| Item | Scenario | Tree | What was seen | Evidence |
|---|---|---|---|---|
| Story cutscenes for everyone | `historia` (story) | test9, test10 | A's game starts the Deku Tree meeting and owns it; B's game starts the same | PASS (no count printed). A: `coop: story: STORY_EVENT start sent ('KokiriForest_Meet_DekuTree_MeetDekuTree_01' in KokiriForest_Child, ...)`; B: `coop: story: STORY_EVENT start received from P1 (...): starting` |

### 09-pvp

| Item | Scenario | Tree | What was seen | Evidence |
|---|---|---|---|---|
| Our weapon damage and invulnerability on your PvP | `pvp-espada` (PvP sword) | test10 | the host turns PvP on; A's sword hurts B by 2 quarter hearts; a second hit right after does 0 | PASS. A: `coop: pvp: P2 hurt by P1: 2 (life 10, flags 0)`, `coop: pvp: P2 hurt by P1: 0 (life 10, flags 2)`; B: `coop: pvp: our rules on P1's hit (2 quarter hearts by upstream's): taken, takes 2`, `coop: pvp: the game took the hit (life 10)` |

### 10-hud

| Item | Scenario | Tree | What was seen | Evidence |
|---|---|---|---|---|
| Quick chat (patch 0101), and L is free while playing | `comunicacion-aqui` (communication, "here!") | test9 (A second) | "¡Aquí!" and "¡Ayuda!" arrive; the one-per-second limit holds; L moves nothing | PASS 18/18. `coop: songs: invitation button L: free (3 of 3 presses moved nothing of our Link): L alone`; `coop: quick: on (LICHII_ORM_QUICK_CHAT): hold L and press the d-pad: ...` |
| Partner map: name tag, distance dot, edge arrow | `mapa-flecha` (map arrow) | test9 (A second) | B near, far and behind A | PASS 4/4, on the fallback path: `coop: check: map mini KokiriForest_Child fallback no minimap view or box in the map table (coop_map.txt): edge arrows and lines only` |

### 11-puppets

| Item | Scenario | Tree | What was seen | Evidence |
|---|---|---|---|---|
| A paused player's puppet is held, not hidden, and still changes gear | `menu-pausa` (pause menu) | test9 (A second) | A opens the menu and changes shield; B's puppet holds, wears the shield, shows again | PASS 15/15. B: `coop: puppet: P2 show -> hold (paused)` |
| Puppets stay visible during an event (signboard) | `cartel` (signboard) | test9, test10 (A first, B first, UDP, and with bodies on) | A reads the signboard; B sees A's puppet throughout | PASS 4/4 each time. A: `coop: event running 'Obj_Kokiri_Signboard_GuideAction_A_03' (we play 'EvtTurnSlow')` |
| Carried objects | `levantar-vasija` (lift a pot) | test9 (A second), test10 | A lifts, carries and throws a pot; B draws it in the puppet's hands, held still; it breaks in both games | PASS 16/16. A: `coop: carry: lifting Obj_Pot_A_08 ... (owner P2, claimed), by bone 0`; B: `coop: check: carry hold ok carried copies held still between our writes (largest drop 0.019 in 60 frames)`, `coop: world: ... Obj_Pot_A_08 broken by another player` |
| Ocarina in the puppet's hand, others' notes and songs heard | `ocarina-mano` (Ocarina in hand) | test9 (A second) | B plays Zelda's Lullaby; A's puppet of B holds B's own Ocarina; A hears it | PASS 17/17. `coop: check: ocarina hand ok P1's own Ocarina is in its hand while P1 plays ('DemoOcarina', up 0.74)`; B: `coop: ocarina note: song ZeldaLullaby sent (serial 21)`; A: `coop: ocarina sound: P1 Melody_ZeldaLullaby at (...)` |

### 12-bodies

| Item | Scenario | Tree | What was seen | Evidence |
|---|---|---|---|---|
| Real body collision between Links (`/choque`) | `choque-bloqueo` (body block) | test10 (B first) | A walks into B and stops against his body, then walks away free | PASS 14/14. `coop: check: bodies: collider ok CoopPuppet_Child_Body made: kinematic, placed with its puppet each frame, hitting only our Link`; `bodies: our Link against P1's body (0.44 m apart, the bodies 0.46)` |
| Standing on a mate's head | `choque-encima` (on top) | test10 | A drops on B's head and stands there | PASS 10/10. `bodies: our Link stands on P1's head (1.17 m over his feet)` |
| Nobody stuck on a shared Continue spot | `choque-continuar` (Continue spot) | test10 | both on the same Continue spot; the puppet is drawn 1 m aside, the body solid there; nobody's Link moved | PASS 9/9. `coop: puppet: P1 stands on our Link when first seen here (one Continue spot): drawn 1.00 to its side ...` |
| Talks and signboards with bodies on | `cartel`, `charla` | test10 | the stall of the old design does not appear | PASS 4/4 and 4/4 |

### 13-resume

| Item | Scenario | Tree | What was seen | Evidence |
|---|---|---|---|---|
| Resume inside a dungeon: back to the entrance-room spot | `reanudar-mazmorra-1`, `reanudar-mazmorra-2` (resume in a dungeon, two sessions) | test10 | session 1 keeps the spot; session 2 goes to it, never into the other room | PASS 4/4 and 10/10. `coop: resume: the last place (InsideTheDekuTree at -51.45 111.25 15.08) is in another room of this dungeon (issue 197: ...): going to the spot of its entrance room ...`; `coop: resume: at the spot of the entrance room of InsideTheDekuTree we last stood on before leaving it` |

## Seen in a game log, but the scenario is still red

Not counted as confirmed; listed so you know how close they are.

- **08-age, the group follows an age change** (`edad-grupo-cerca`, test10, red 7/20 only because the lines came after
  B's script ended): B got the notice and the 3-2-1 countdown, then `coop: story: the game took 'Market_Child_Field_ChildToAdult' (returned 0x1, component phase 1)`
  and `coop: age: now adult in Market_Adult, 32.7 s after the reload began: the game's own era change, its equipment swap and era; ...`,
  and both logged `notice gathered: Grupo reunido en el Templo del Tiempo`.
- **11-puppets, Farore's Wind** (`sonda-magia`, `efectos-magia`, test9): the point is shared in a dungeon
  (`spawn: sent FaroreWindWarpPoint`, B `got FaroreWindWarpPoint`) and B's puppet plays the cast
  (`P2 effects: 'DemoStartFaroreWind' played under the copied pose`).
- **11-puppets, hookshot, arrows, bombchu, hammer on the puppet** (`objetos-uso`, red 17/23 because of Din's Fire and
  the lens/boots).
- **09-pvp, the shield read** (`pvp-escudo`, red 5/8): `coop: pvp: shield read from 8 AS slots of our Link, the equipped shield (Weapon_Shield_Deku) and the guard button 0x100`
  and a hit from behind correctly taken; the front block was never reached.

## Not yet confirmed in game

Unit-tested (or seen only in older runs on earlier trees that this file does not cover). One line per feature:

- **01-platform**: the platform layer builds into your Hookshot module's file filter; never run on Hookshot.
- **02-network**: six real games (only one game plus five bots, red); host election (`HOST_CHOICE`, `/anfitrion`); link notices; interest management for six.
- **03-fixes**: the guest-loads-before-the-host path (`our save load from the title screen: N values of our save kept`) only in unit tests.
- **04-downed**: group defeat, own bottled fairy, spectator camera, Continue after game over, mate-down sound.
- **05-bosses**: escape countdown (`LICHII_ORM_ESCAPE_CLOCK` off), hits on another game's enemy (`golpes-corte` red), endgame with six, Gerudo/Spirit rules, hazards, per-family boss rules.
- **06-world**: held switches that need actor words (Spirit room 08: the flag alone does not open it), Water room 14, Jabu; Song of Time blocks; Biggoron; most kit lists; item rules.
- **07-cutscenes**: skipping a shared cutscene on the owner's explicit skip.
- **08-age**: the group move to the Temple of Time (red only by timing, above); the way back to child; Temple of Time rules.
- **09-pvp**: shield block from the front, Nayru, last one up, shove; lock-on allies; the command registry with Tab completion; calls and renames; `/fps`, `/stats`.
- **10-hud**: the in-image HUD with everything at once; per-player HUD settings; minimap calibration (needs a visible window).
- **11-puppets**: Din's Fire and Nayru's Love (the game refuses them: `ItemEmpty`); elemental arrows, lens, iron boots; horses and Epona; moving floors; townspeople gaze; colours.
- **12-bodies**: with real network delay (each game stops its Link against the other's body); the no-head-disc variant (`LICHII_ORM_BODY_TOP=0`).
- **13-resume**: resuming a place outside a dungeon given by the app (`reanudar-sitio` red, not re-run with the new rule).
- **14-app**: the "Cooperativo" screen was not part of these runs.
