# What we ran, and how

This file lists every two-game scenario we ran between 2026-11-12 and 2026-11-13 while building this gift: the
probe rounds 1 to 7, the body-collision runs, the first full run on the test10-based tree (we call it "the 0.12
end-to-end run", after the version number of our app) and the in-game check on the test10-based tree the next morning.
For each scenario: what it checks, green or red, how many checks passed, on which tree, and for each red the cause we
found and where the fix lives (by feature ID). What is confirmed working is summed up in [WORKS.md](WORKS.md).

Sources: our Spanish notes, kept in our own repository (`SONDAS.md` rounds 1-7, `TRIAGE_ROJOS.md`, `CHOQUE.md`, `PIN_TEST10.md`,
`T9_INTEGRADO.md`, `EDAD.md`, `PVP.md`, `PARTIDA_SALA.md`) and the summary file each run wrote. Where two sources
disagree, the most recent one wins and the text says so.

## How a scenario runs

Short version (the method is in [HOW-WE-WORK.md](HOW-WE-WORK.md)):

- **Two hidden games on one PC**, A and B, each with its own instance folder and save, and a local room server on
  127.0.0.1 (WebSocket by default, UDP on request). Windows hidden, no focus, no sound. One pair at a time.
- **Script files**: `tools/orm/harness/escenarios/<name>.A.txt` and `<name>.B.txt` (the PvP ones are in
  `tools/autotest/scenarios/`). Each game reads its script through the hook's test input: `warp`, `place`, `goto`,
  button presses, `talkthrough` (press A while our talk lasts), `await mate` (wait until the other game is in our
  scene), chat commands, and `mark <name>` to cut the log into parts.
- **Directives** (`#!` lines at the top): `expect A|B <regex> within <mark>..<mark>`, `never ...`, `#! saves <block>`
  (a synthetic save built from a recipe), `#! switches`, `#! env`, `#! join A-first|B-first`, `#! timeout`.
- **The judge is the log.** The harness (`r03_pareja.py`; batches with `secuencia.ps1` or `rapida_t9.ps1`) checks that
  both games joined, loaded and settled (fps), that the free RAM was enough to start (14 GB), and, since round 7, that
  the first life each game reads equals the `MaxLife` of its save. Then it counts the expect/never lines.
  `RESULT: PASS (expect 9/9)` means 9 checks of 9. A few scenarios print no count (`edad`, `historia`, `pvp-espada`):
  for those we only have PASS/FAIL.
- **Join order**: in round 7 and in most test10 runs B joins first and A second (A is player 2), the order that hid the
  save bug below.

Scenario names are kept as they are (Spanish); the first time each appears it has a short English gloss.

## Trees (builds)

| Name here | What it is | Protocol | Runs |
|---|---|---|---|
| **test9-based tree** | your v0.1.0-test9 plus our work, integrated (unit tests 2140 to 2157 green during the rounds); emulator with your lichii patches and ours (0100 HUD, 0101 quick chat) | 109 | probe rounds 1-7 (2026-11-12, 00:04 to 18:08) and the body runs (18:41 to 22:15): **198 scenario runs** |
| **test10-based tree** | your v0.1.0-test10 plus our work: the base of this gift (unit tests 2569/2569 at the merge, 2591/2591 at the gift's head) | 110 (sealed traffic, join password) | the 0.12 end-to-end run (2026-11-12, 22:15 to 23:20) and the in-game check (2026-11-13, 10:19 to about 11:40): **29 scenario runs** |

`PIN_TEST10.md` and `T9_INTEGRADO.md` still say "nothing seen in a game"; that was true when they were written and is
superseded by the runs below.

## Summary

A scenario counts by its **latest run on that tree**. 45 different scenarios, 227 runs in total.

| Feature | test9: scenarios | green | red | test10: scenarios | green | red |
|---|---|---|---|---|---|---|
| 02-network | 1 | 0 | 1 | (every run: WebSocket or UDP, sealed, with password) | | |
| 03-fixes | (checked in every run since round 7: the first-life check) | | | (same) | | |
| 04-downed | 1 | 1 | 0 | 1 | 1 | 0 |
| 05-bosses | 3 | 1 | 2 | 0 | 0 | 0 |
| 06-world | 15 | 11 | 4 | 4 | 4 | 0 |
| 07-cutscenes | 1 | 1 | 0 | 1 | 1 | 0 |
| 08-age | 1 | 1 (*) | 0 | 2 | 1 (*) | 1 |
| 09-pvp | 0 | 0 | 0 | 2 | 1 | 1 |
| 10-hud | 2 | 2 | 0 | 0 | 0 | 0 |
| 11-puppets | 11 | 4 | 7 | 3 | 2 | 1 |
| 12-bodies | 4 | 3 | 1 | 3 | 3 | 0 |
| 13-resume | 0 | 0 | 0 | 3 | 2 | 1 |
| **Total** | **39** | **24** | **15** | **19** | **15** | **4** |

(*) `edad` (age) passed, but the logs show the game refused the age change every time: the green was hollow (see
08-age). It is not counted as working anywhere.

No scenario of its own in these runs: 01-platform, 14-app, 15-tools (it is the harness itself), 16-docs.

## Families of reds

Before fixing, the reds were sorted by cause (`TRIAGE_ROJOS.md`, rounds 1-5, read from the logs only):

| Family | Cause | Where it was fixed |
|---|---|---|
| a | the second game to join loaded the wrong save (the host's title-screen table) | 03-fixes (see "Bugs" below) |
| b1 | the resume file of the previous run put Link somewhere else | 15-tools (the harness clears it) |
| b2 | B starts about 115 s after A; fixed waits too short | 15-tools (`await mate`) |
| b3 | lines a game writes at start-up, before the script, checked in the wrong window | 15-tools (`within boot..`) |
| b4 | B stays on the title screen (`NEVER STARTED`) | 15-tools (watcher planned) |
| b5 | script movement: `goto` times out, wrong place or facing, long talks | 15-tools (scripts) |
| b6 | PC out of RAM (fps falls, `FROZEN`) | not code: each game reserves up to 14 GB of guest memory; nothing starts with less than 16 GB free |
| b7 | scenario set up wrong (no `#! saves`, switches) | 15-tools (scenarios) |
| c1 | Din's Fire and Nayru's Love play `ItemEmpty` | open (11-puppets) |
| c2 | a carried copy breaks in the puppet's hands | 11-puppets |
| c3 | the talk reach fallback took a harness event as a talk | 06-world |
| c4 | the escape countdown word is the copy the UI draws | 05-bosses (safety fallback; open) |
| c5 | code fixes inside the rounds (pause, ocarina, chest, puppet pack) | per feature, below |
| d | a moment the synthetic saves or scripts cannot reach yet | open |

## Scenarios by feature

### 02-network

| Scenario | What it checks | Result | Reds: cause and fix |
|---|---|---|---|
| `seis-kokiri-recorrido` (six, Kokiri walk) | one game plus five bots walk Kokiri Forest room to room; bots revive A, send a shared flag, follow scene changes | test9: **red 25/37** (round 1) | all five bots and five puppets came in; red: `goto` timed out 6 times (b5), A ended in game over instead of being revived, the Deku Tree story did not arrive, two start-up lines (b3, fixed). Before that run every bot dropped on join (`message too big: read limited at 32769 bytes`, the default read limit of our bot's WebSocket library) and the bot did not know `-check`: both fixed in 15-tools. Not re-run. |

Every test10 run used protocol 110 with sealed traffic and a join password (`coop: traffic encrypted`,
`coop: on, server 127.0.0.1:... (ws /), name 'A', join password set`); `cartel` also ran over UDP (11-puppets).

### 04-downed

| Scenario | What it checks | Result | Reds: cause and fix |
|---|---|---|---|
| `caer-pareja` (downed pair) | A's life to 0 at the Kokiri start: A is downed (not dead), B revives him; the fairy bottle, the gate and the game-over event as the hook drives them | test9: **green 8/8** (round 1 with A first; round 7 with A second); test10: **green 8/8** | 3/8 once in round 7 with A second: A fell 0.7 m from the usual spot and B, 1.58 m away, was outside the 1.5 m reach. The script moves B closer (15-tools). |

### 05-bosses

| Scenario | What it checks | Result | Reds: cause and fix |
|---|---|---|---|
| `jefe-gohma-entrada` (Gohma boss entry) | A loads into Gohma's room; B, at the dungeon entrance, is moved in once A's intro starts; the intro starts and ends; nobody's boss beaten | test9: **green 9/9** (round 1; round 7 with A second) | none |
| `final-huida-reloj` (final escape clock) | both at the top of the collapsing castle; the group's escape countdown (exploratory) | test9: **red, best 7/9** (round 7) | The game takes `CrumblingGanonCastle_Arrive` with the save `21-huida-arriba`. The words the search finds are the copy the UI redraws every frame from the real timer, so they never keep our writes; the real timer is the minigame timer started by the `Defeat_Ganon` flow and probably counts up. Safety fallback in 05-bosses: after 3 writes the game does not keep (a write that bounces back counts too), stop writing (`that word is not the countdown`). `LICHII_ORM_ESCAPE_CLOCK` stays off. Open: search a word that rises 1 s per second; start the group's clock after the 88 s cutscene. Rounds 1-3 also had family a. |
| `golpes-corte` (hits, slash) | A's game owns an enemy held still in front of A; B slashes the copy four times | test9: **red 5/6** (round 1) | B never hit the copy (placement, b5). Planned: B faces the copy after `await mate`. Not re-run. |

### 06-world

| Scenario | What it checks | Result | Reds: cause and fix |
|---|---|---|---|
| `corazon` (heart) | A gets a heart container; B learns the group's new maximum and raises its own Link, keeping its life | test9: **green 9/9** (round 7, A second) | 8/9 in round 1: a start-up line checked in the wrong window (b3). `LICHII_ORM_HEARTS` is on in the kit. |
| `cartera-cada-uno` (each one's wallet) | each player keeps his own rupees; A sees B's 43 and keeps his own 87, then 25; B's never lands in A's wallet | test9: **green 21/21** (round 2; round 7 with A second) | 14/21 and 20/21 before: B's HUD line came after A had finished (b2): A now waits for B. |
| `charla` (talk) | A walks through the Kokiri tunnel to a Kokiri and talks; A claims the townsperson; B watches | test9: **green 4/4** (round 2; again in the first run of the new body design); test10: **red 3/4** in the 0.12 run, then **green 4/4** (2026-11-13, bodies on) | Round 1: the resume file (b1). Round 7 with A second: 2/4 in five batches, caused by the old body push (12-bodies, see "Bugs"). The test10 red: the claim line did not appear; not triaged; the same scenario passed the next morning. |
| `charla-dos` (talk, two) | the shared talk: `not claimed` in B, the 600 ms pose blend, `talk over` in both | test9: **green 8/8** (round 3; round 7) | 1/8 before: A appeared pushed into a corner by B's puppet (start moved 2.5 m); a Kokiri talk has about 30 pages: new script step `talkthrough` (15-tools). |
| `charla-mido` (Mido talk) | A talks to Mido (the blocker); the talk flag reaches B, also when B's Mido is not loaded | test9: **green 20/20** (round 6; round 7); test10: **green 20/20** | The talk lasts 49-57 s (`talkthrough` of 90 s); the claim's 60 s limit let go at the end, now 3 min (06-world); B's flag came in with no line when Mido was unloaded, now `written: its actors are not loaded (at once, nothing waited)` (06-world). One run 20/20 but FAIL: `FROZEN at the end`, 9.8 fps with 0.3 GB free (b6). `LICHII_COOP_TALK=shared_talk.txt` is on in the kit. |
| `charla-saria` (Saria talk) | A talks to Saria (sitting, the Ocarina holder), found by the game's event component | test9: **green 13/13** (round 7) | The test `warp KokiriForest_Child` with no start point left both Links falling into the void in a loop (`DmF_SY_FallDown`) in every round: now `KokiriForest_FrontOfLostWood` (15-tools). During that fall the reach fallback took Saria (c3): a townsperson is now taken by reach only inside a talk event (06-world): `coop: npc: our event 'DefaultBootEvent' is not a talk: no townsperson taken by reach`. |
| `charla-biggoron` (Biggoron talk) | A talks to Biggoron from the ledge | test9: **red 3/13** (round 1) | No talk by the event component; the 25 m reach took a harness `place` event as a talk (c3, fixed later in 06-world); placement (b5). Not re-run. |
| `efectos-cofre` (chest effects) | A opens a chest; B's chest opens with it (`CHEST_OPEN`); the item is shown held up over A's puppet | test9: **green 23/23** (round 4; round 7); test10: **green 23/23** | The lid starts opening on the same frame as our move: the first look counts now (06-world). The remake has no "get item" command: the opener keeps `DemoOpenTreasureBox`; new rule: still on it 2.5 s after the lid = holding it up (11-puppets). Waits and placement (15-tools). The puppet pack used was old (no stand-ins): rebuilt. |
| `meca-agua-templo` (Water Temple level) | the Water Temple level is shared; it waits while a Link swims | test9: **green 17/17** (round 3; round 7) | Link drowned (the synthetic save had no Zora Tunic on: recipe fixed); the "live" check was armed at the wrong time (06-world). Value: the level is **not live**: no water plane moves in 10 s; it applies on the room's next load. `LICHII_ORM_WATER` is on. |
| `meca-sostenido-fuego` (held switch, Fire) | A stands on the Fire Temple room 10 switch; B's fire wall goes | test9: **green 12/12** (round 2; round 7) | Value: the flag alone opens it (`door opened with the flag`, 3.3 s). Before: B started late (b2), family a. |
| `meca-sostenido-espiritu` (held switch, Spirit) | Spirit Temple room 08 switch | test9: **green 12/12** (round 3; round 7) | Green, but what it proves is that the flag path reports the truth: B writes the flag and logs that the door did **not** move. The door needs the actor's words (candidates found, not programmed: writing a contact without its pointer could crash the game). |
| `meca-sostenido-agua` (held switch, Water) | Water Temple room 14 switch (three flags: pushed, by block, heavy boots) | test9: **red 10/14** (round 7) | The iron-boots mode cannot be reached by script: with the boots on, Link sinks, stands, swims again; ZR with `HeavyBoot` does nothing while swimming (no `SwimBootsChange`) (d). Open. |
| `meca-sostenido-jabu` (held switch, Jabu-Jabu) | Jabu-Jabu room 17 stone switch | test9: **red 2/13** (round 1) | No `#! saves` line: a child save in Kokiri; the warp could not happen (b7). Planned: `#! saves 10-jabu`. Not re-run. |
| `meca-cancion-tiempo` (Song of Time block) | A plays the Song of Time at the Forest Temple block; B's block goes too | test9: **red 5/9** (round 7) | The song now moves the block (`TriggerSongOfTimeBlock`) and the game writes four saved Bool flags that have no name in `GameDataList`; they are not yet in `shared_dungeon_songs.txt`, so B gets nothing (d). Earlier 2/8: family a and B never started (b4). |
| `viaje-cancion` (song trip) | B plays the Minuet of Forest and travels; A gets the invitation and follows holding L | test9: **green 9/9** (round 4; round 7); test10: **green 9/9** | Songs were not recognised: the remake keeps a ring of 8 notes and it was full (put the Ocarina away and take it out again); the remake asks a question after the song (B answers with A); the arrival comes as `Dm_SY_Ocarina_move_Ed`, not `OcarinaWarp_Arrive` (06-world takes both). Remake Ocarina buttons read from the game: L = D, R = F, A = A, Y = B, X = high D. `LICHII_COOP_WARP_INVITE=1` is on. |

### 07-cutscenes

| Scenario | What it checks | Result | Reds |
|---|---|---|---|
| `historia` (story) | A's game starts the Deku Tree meeting; A owns it and sends `STORY_EVENT` start; B starts the same event | test9: **green** (round 1; round 7); test10: **green** | none |

### 08-age

| Scenario | What it checks | Result | Reds: cause and fix |
|---|---|---|---|
| `edad` (age) | A turns adult with a test command; B follows; then back to child | test9: **green** (rounds 1 and 7); test10: **green** (0.12 run) | **Hollow green** (most recent source, our age notes of 2026-11-13, over the round notes): the game refused the event we asked, `ReplaceEquipmentForEraChange`, 45 of 45 times in the test9 runs and 15 times in A's log of the test10 run (`the game refused ... (returned 0)`), and each try had already written the era flag without putting it back. The scenario did not check that the change took. Cause: the game starts by name only an event with a row in `RSDB/EventSetting`; that one is a subflow. Fix in 08-age: ask the game's own era change (`Market_Child_Field_ChildToAdult` / `Market_Adult_Field_AdultToChild`) and never leave the era changed. |
| `edad-grupo-cerca` (age group, near) | with the 08-age fix: A draws the sword; B, in the Market square, gets the notice and the 3-2-1 countdown, the game's own era change, and appears adult in the Temple of Time; both see "Grupo reunido" | test10: **red 7/20** (2026-11-13) | From the logs: every step happened, but after B's script had ended (B's last mark at 309 s, the move at 394-430 s), and the "age: on" line is written at start-up: the checks' windows missed them (b2, b3). Fix planned in 15-tools (longer B script, `within boot..`). Not re-run. |

### 09-pvp

| Scenario | What it checks | Result | Reds: cause and fix |
|---|---|---|---|
| `pvp-espada` (PvP sword) | the host turns PvP on; A hits B with the sword: damage, invulnerability right after a hit | test10: **green** (0.12 run) | Green, but the same run showed B's shield never stopped a hit: B had no shield (`shield ''`), and our rule looked for animation names `Guard_*` instead of the game's AS commands (`ShieldGuard`, `GuardHit`, ...), and only in the main slot. Fixed in 09-pvp: up to 8 AS slots, the guard button with a shield equipped, no shield = no block. |
| `pvp-escudo` (PvP shield) | B gets the Deku Shield; A swings three times: B facing with ZL (blocked), from behind (taken), without shield (taken) | test10: **red, best 5/8** (three runs) | Run 1: 0/8, B froze at 0 fps with 0.7 GB free RAM (b6). Runs 2 and 3: 5/8; only one of A's three swings reached B (from behind, correctly taken: `taken, takes 2; shield up (...) but the hit came from beside or behind it (cos -0.47)`), so the front block and the no-shield case were never exercised. Planned: fix A's placement and turns in the script (15-tools). |

### 10-hud

| Scenario | What it checks | Result | Reds: cause and fix |
|---|---|---|---|
| `comunicacion-aqui` (communication, "here!") | quick chat: "¡Aquí!" with L + d-pad up, the one-per-second limit, "¡Ayuda!"; and that L is free while playing | test9: **green 18/18** (round 1; round 7) | none (the value: `invitation button L: free (3 of 3 presses moved nothing of our Link)`) |
| `mapa-flecha` (map arrow) | partner map: B near (name tag), far (dot and distance), behind (edge arrow in his colour) | test9: **green 4/4** (round 1; round 7) | 3/4 once: start-up line (b3). Ran on the fallback path (`no minimap view or box in the map table: edge arrows and lines only`); calibrating the minimap needs a visible window (`mapa-piso`, not run). |

### 11-puppets

| Scenario | What it checks | Result | Reds: cause and fix |
|---|---|---|---|
| `menu-pausa` (pause menu) | A opens the pause menu 8 s and changes shield; B's puppet of A is held (not hidden), wears the new shield, and is shown again | test9: **green 15/15** (round 1; round 7) | 10/14 before: the still-frame pause detector fails in the remake (a standing Link reads the same frame, so every wait looked like a pause). Now the game's flag `Sequence_IsPause` comes first (set 0.5 s after the menu opens); the still frame only if the flag cannot be read (03-fixes, 11-puppets). |
| `cartel` (signboard) | A reads the Kokiri guide signboard (an event); B watches A's puppet, never hidden | test9: **green 4/4** (round 2; again in the first run of the new body design); test10: **green 4/4** three times in the 0.12 run (WebSocket A first, WebSocket B first, UDP) and again 4/4 on 2026-11-13 with bodies on | Round 1: the resume file (b1). Round 7 with A second: 2/4 in five batches, the old body push (12-bodies). |
| `levantar-vasija` (lift a pot) | A lifts a pot, carries it, throws it; B draws it in the puppet's hands; it breaks in both games | test9: **green 16/16** (round 7, A second); test10: **green 16/16** | Lift from 1.1 m facing the pot. B's copy broke in the puppet's hands from the game's own fall damage, **1,000,000,000**, because its physics body keeps falling between our placements: life raised while carried and 3 s after; a copy in someone else's hands never sends its break; carried copies are held still by zeroing their linear speed with the game's (11-puppets). |
| `ocarina-mano` (Ocarina in hand) | B plays; A sees B's own Ocarina in the puppet's hand and hears the notes and Zelda's Lullaby | test9: **green 17/17** (round 6; round 7) | Playing is `DemoOcarina` in the remake, not `Ocarina...`; synthetic saves had no Ocarina on its button (recipes fixed); the 8-note ring; family a (no puppet pieces, the Lullaby unknown); the hand position measured once and set every frame (11-puppets). `LICHII_COOP_OCARINA_SOUND=1` is on. |
| `objetos-uso` (items in use) | A uses hookshot, arrows, bombchu, hammer, spells, lens, iron boots; B sees each on the puppet | test9: **red 17/23** (round 4; round 7); test10: **red 17/23** | Seen: hookshot, arrows, bombchu, hammer. Red: Din's Fire never casts (`ItemEmpty`, c1), elemental arrows come out plain, lens and iron boots on ZR change nothing visible (d). c1, read without a game: `ItemEmpty` is the upper-body fail action of the player (`ExecutePlayerFailAction`), and the verb `UseDinFire` is never asked; ruled out with data: the save (MP 48 of 48, both blessings), the puppet (control run), the place, the press time, the projectile. No fix yet; next probe: hook the secondary-verb request and log who asks it. |
| `efectos-magia` (magic effects) | A casts the three spells and rolls; B's puppet plays the casts under the copied pose | test9: **red 10/20** (round 7) | Fixed: Farore's Wind on the puppet (the remake plays `DemoStartFaroreWind` / `DemoEndFaroreWindSetting`, which a `skip *Demo*` rule hid; 11-puppets). Red: Din and Nayru (c1) and the roll (`stick + A` while running does not roll in the remake; no value). |
| `sonda-magia` (magic probe) | the three spells inside Ganon's Castle entrance hall, save `20-magia` with a real double meter | test9: **red 3/8** (round 7) | c1. Farore works: `spawn: sent FaroreWindWarpPoint`, and in B `got FaroreWindWarpPoint`. |
| `sonda-magia-sin-marioneta` (magic probe, no puppet) | the same with A's puppet of B off: the control | test9: **red 3/8** (round 7) | same result as with the puppet: the co-op layer is not the cause of c1 |
| `epona-montar` (ride Epona) | A rides Epona; B sees A's puppet on a horse | test9: **red 4/13** (round 1) | no save had Epona next to Link (d); replaced by the two below |
| `epona-rancho` (ranch Epona) | the same with the ranch's Epona (`12-epona-rancho`) | test9: **red, latest 5/13** (round 7) | Epona is at another spot than the recipe said, and she cannot be mounted from any side (A pressed at 1.3-1.8 m from three sides: no event, no animation) (d) |
| `epona-cancion` (Epona's Song) | A plays Epona's Song at the ranch (`12-epona`), she comes, A rides | test9: **red 5/13** (round 7) | the song is recognised (`DemoOcarinaCorrect`) but no Epona appears (d); next: call her in Hyrule Field. No saddle height measured (`LICHII_ORM_HORSE_SADDLE` has no value). |

### 12-bodies

Two designs were run.

**Old design (test9)**: push our Link out of a mate's body. `cartel` and `charla` went red 2/4 with A second (first body
run and five round-7 batches); the same `cartel` with bodies off, `cartel-sincuerpos`, was **green 4/4** (control). The
cause is in "Bugs" (Link walks in place after `MoveLink`). This design was dropped.

**New design**: a kinematic body actor per mate, placed where the puppet is drawn, colliding only with the player
layer; our Link is never written. Turned on per room with `/choque`.

| Scenario | What it checks | test9 | test10 (2026-11-13) |
|---|---|---|---|
| `choque-bloqueo` (body block) | B turns `/choque on` and stands; A walks into B's feet and must stop against the body, then walk away without getting stuck | first run: the body create crashed the game (see "Bugs"); runs 2-5: 7/14 to 13/14 while making the body appear and turn solid (the cause of each run is not written up); last: **green 14/14** | **green 14/14** |
| `choque-encima` (on top) | A drops 1.5 m over B's head and must stand on it | 4/10 and 9/10; last: **green 10/10** | **green 10/10** |
| `choque-continuar` (Continue spot) | both appear on the same Continue spot; nobody gets stuck, no `goto` times out, nobody's Link is moved | 5/9, 7/9; last: **red 8/9** (one `goto` timed out) | **green 9/9** (the puppet's first view is drawn 1 m aside and the body is solid there) |
| `cartel-sincuerpos` (signboard, no bodies) | `cartel` with bodies off: control for the old design | **green 4/4** | not run |

On test10, `cartel` and `charla` also passed 4/4 with bodies on. Bodies are on by default since then; `/choque off`
turns them off for the room.

### 13-resume

| Scenario | What it checks | Result | Reds: cause and fix |
|---|---|---|---|
| `reanudar-sitio` (resume place) | A and B go back to their last place given by the app | test10: **red 16/19** (0.12 run) | The "resume: on" lines are written at start-up (b3), and on test10 Link is never moved into another room of a dungeon (your issue 197: a dungeon loads one room at a time), so the expected move to the slingshot ledge could not happen. This led to the dungeon rule of 13-resume (back to the spot of the entrance room last stood on) and the scenario now expects that. Not re-run. |
| `reanudar-mazmorra-1` (resume in a dungeon, session 1) | B keeps a spot in the entrance room and a last place in another room | test10: 1/4, then **green 4/4** twice | the join-time lines are read from boot now; the ledge's landed height (15-tools) |
| `reanudar-mazmorra-2` (session 2, same instances) | B comes back to that entrance-room spot, never into the other room | test10: 4/10, 8/10, then **green 10/10** | the harness cleared `coop-resume.txt` between the two sessions: `ORM_KEEP_RESUME=1` keeps session 1's file (15-tools) |

## Bugs we found in upstream code paths

**1. A host on the title screen sends a fresh save's flags table (fixed in 03-fixes).** B joins first and becomes host
while still on the title screen; being host, it sends its flags table, which on the title screen is a new game's (3
hearts, no magic, no blessing). The server keeps the first table it gets (a later clashing table does not replace it,
`flag_clashes`) and gives it to A on join; A loads its save and its guest load window sets everything back to that
table. Later the server passes the host role to A, and A's broken state becomes the group's (and can be saved). With
friends this happens when someone presses Continue before the host. Signature in every batch where A joined second
with a save of more than 3 hearts (31 pairs, none with A first):

```
coop: objects: our Link's life 12 of 12, MaxLife flag 12
```

The fix: a host with no save loaded holds its table until it reads Link, and a guest's first load from the title
screen, with no group values yet, keeps its own save's values:

```
coop: flags: we host with no save in (the title screen): our table is held until our save is in, so no joiner gets a new game's flags (3 hearts, no magic)
coop: flags: ... our table sent: our save is in now
-- A: first life 44 of 44 = its save's 44 (ok)
```

Confirmed in round 7: 50 pairs (100 games) with A second, every first life equal to its save's, no `save mismatch`;
also on test10. This was also the hidden cause of several other reds (the Ocarina puppet with no pieces, the unknown
Lullaby, magic at 0).

**2. The size-table entry of a new Phive param (fixed in 12-bodies, the puppet-pack builder).** The game parses an
actor's Phive param inside a heap sized by its resource size-table entry, and the game's own entries are about 5.1 KB
over the file and grow with what the param lists, not with its bytes (Kokiri's `ControllerSetParam`, 2,688 bytes:
10,624; the player's 15,256: 34,160; a `ShapeParam` of 548: 6,600; the air wall's 608-byte `ControllerSetParam`:
7,480, 3.9 times its bytes over the base). At 3 times the bytes, our 924-byte `ControllerSetParam` (two bodies, two
shapes) got 7,892: too small; the parse returned a null object and the create crashed:

```
body coop: puppet: P1: create CoopPuppet_Child_Body requested (returned 0x1), ...
Cannot execute instruction at unmapped address 0x00000000000000
body coop: puppet: P1 CoopPuppet_Child_Body not spawned after 10000 ms (actor 0, state -); trying again in 5 s
coop: check: bodies: collider fallback a body could not be made 2 times (see the 'body coop: puppet:' lines): the mates are drawn aside (CoopPuppet_Child_Body)
```

The entry is now 0x1400 + 8 times the bytes (over every romfs entry we looked at). After that, on test10:
`coop: check: bodies: collider ok CoopPuppet_Child_Body made: kinematic, placed with its puppet each frame, hitting only our Link`.
This matters to anyone who adds an actor with its own Phive param to the puppet pack.

**3. The skeleton's world matrices have no positions (03-fixes).** The bone census read the skeleton matrix array as
3x4 every 0x30: 301 of 309 are rotations without translation (all at the actor), and as 4x4 with the position in
12-14, none. So the nearest-bone lookup always gave bone 0. The skeleton finder now takes a 4x4 array if a game
version has it and otherwise keeps 3x4, and says which (`carry bone ok ... (3x4 (old))`).

**4. Link walks in place after repeated `MoveLink` (not explained; avoided).** With the old body push, when both
players pressed Continue on the same spot, our Link was moved with `MoveLink` (the boss-pull move) 13 times in 2 s
(`coop: boss: moved Link to -82.1 122.3 33.5`) and afterwards walked in place (animation `Move`, position fixed) for a
whole minute; every `goto` timed out. With bodies off the same scenario passed 4/4. It also happened with A first (48
times in one batch) without the stall. Why `MoveLink` leaves Link unable to advance is not found; the new body design
never writes our Link, so it does not hit this.

**5. Found while merging test10 (fixed in 03-fixes or in the merge).**
- Your new network tests listened on all interfaces: `relay_test`, `nat_loopback_test`, `small_path_test` and
  `encrypt_loopback_test` now listen on 127.0.0.1.
- With your sealed traffic over our WebSocket transport, a maximum-size message closed the WebSocket: the size limits
  must add `kSealOverheadBytes`.
- The kit's `Set-KitDefaultColor` in `play.ps1` overwrote the per-player colour from our launcher: the colour of
  `coop.ini` is exported as `LICHII_COOP_COLOR`.
- `dumpcheck.py` did not know Dark Link, so a `.nup` lost the lock-on ("Fijar aliados") puppets.

### Game behaviour we measured (useful whatever code uses it)

- The game starts an event by name only if it has a row in `RSDB/EventSetting`; a subflow such as
  `ReplaceEquipmentForEraChange` is refused (`returned 0`). `CrumblingGanonCastle_Arrive`,
  `KokiriForest_Meet_DekuTree_MeetDekuTree_01`, `WaterTemple_WaterLevelChange0x` and `Market_Child_Field_ChildToAdult`
  were taken (`returned 0x1`).
- A standing Link reads the same animation frame, so a still frame is not a pause; `Sequence_IsPause` is set about
  0.5 s after the menu opens.
- Playing the Ocarina is `DemoOcarina` (slots 4-5 `DemoOcarinaFace/Finger`); the song check compares the last notes of
  an 8-note ring; a question follows a warp song; the warp arrival can be `Dm_SY_Ocarina_move_Ed`.
- Chests have no "get item" command: the opener keeps `DemoOpenTreasureBox`; the lid's `FrontOpen` starts on the same
  frame as Link's move; the "opened" flag arrives when the chest's event ends.
- The Water Temple level applies on the room's next load, not live.
- A carried physics copy keeps falling between placements and takes a fall hit of 1,000,000,000.
- Din's Fire and Nayru's Love end in `ExecutePlayerFailAction` (`ItemEmpty`) in our runs; Farore's Wind sets its point
  only inside a dungeon (outside it ends in `EvtCaution`).
- Each emulated game reserves up to 14 GB of guest memory: two games take a 32 GB PC from 16-18 GB free to under 1 GB.

### Defects in our own branches, found while integrating (not upstream)

Listed for completeness (`T9_INTEGRADO.md`): a still enemy sent no sample, so copies slid on 150 ms (0.5 m) and jumped
back (a stop now counts as a change); the lens of truth was read after the worn armor and hid it; an ambiguous
`orm::` namespace in a server test; test names with `;` that ctest never ran, a missing `<ostream>` and other tests
that had never been compiled; the hook asked `on()` from two classes that did not have it (found by compiling the
hook with the emulator's flags, since ctest does not compile the hook); and three branches used the same key bit in
the spawn director's copies.
