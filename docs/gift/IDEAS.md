# Ideas we had and did not finish (or did not confirm in a game)

These are the ideas in our work that are not finished, or that work in unit tests but have not been confirmed in a
real game yet. For each one: what the player would see, how we would implement it (or how it is built today), what is
known, what is unknown, and the exact probe that would confirm it. Take any of it, change it, or ignore it.

Status words used here:

- **confirmed in game**: seen with two players in a real game run (two hidden games), with the scenario name.
- **not yet confirmed in game**: unit-tested only, or the scenario has not passed yet.

Scenarios live in `tools/orm/harness/escenarios/` (`<name>.A.txt` and `<name>.B.txt`, one script per game). How a
scenario runs is in [HOW-WE-WORK.md](HOW-WE-WORK.md). Synthetic saves (`#! saves <block>`) are built by
`tools/orm/saves/`.

| # | Idea | Feature | Status |
|---|---|---|---|
| 1 | Age change with a group notice | 08-age | not yet confirmed in game (first run red on its time windows; the log lines look right) |
| 2 | Real collision, standing on a mate | 12-bodies | confirmed in game for blocking, standing on a head and Continue; the rest not yet |
| 3 | Held floor switches | 06-world | confirmed in game for one room (Fire Temple room 10); the others not yet |
| 4 | Escape countdown for the group | 05-bosses | not yet confirmed in game (the real countdown is not found) |
| 5 | Din's Fire and Nayru's Love end in `ItemEmpty` | 11-puppets, 09-pvp | open investigation, no fix |
| 6 | Dungeon resume under upstream issue #197 | 13-resume | confirmed in game (two sessions in a row); a few edges not yet |
| 7 | Performance with six puppets | 01-platform, 15-tools | measured with bots only; the six-player load run never ran |
| 8 | Hookshot thin layer | 01-platform | layer done; not built into the Hookshot module, no crossplay run |

---

## 1. Age change with a group notice (08-age)

**What the player sees.** When one player pulls or returns the Master Sword, everybody else follows by the game's own
era change and appears in the Temple of Time with the new age. Nobody is surprised: first a big notice in the middle of
the picture ("<Name> changed era: moving the group to the Temple of Time..."), then a 3, 2, 1 countdown. The one who
pulled the sword sees "Moving the group with you..." and, when everybody has the new age, "Group gathered in the
Temple of Time". The texts are in Spanish in the code; they are short strings, easy to change.

**How it is built.**

- Each game already sends its age (`age` in `PLAYER_SAMPLE`). A mate's age counts after 2 s stable, not loading, and
  matching the suffix of his scene (`KokiriForest_Child` / `_Adult`).
- To follow, we ask the game's own era-change flow in the right direction (`Market_Child_Field_ChildToAdult` or
  `Market_Adult_Field_AdultToChild`). The game swaps the equipment, sets the era and loads the Temple of Time itself.
- Never in the middle of something: no move during an event, cutscene or talk, during a boss fight (room shut), or
  while downed. A player who waits carries a new status bit `age_waits` (bit 6 of `PLAYER_STATUS`), so the others show
  "Waiting for <Name>...".
- If it fails (the game refuses the event 3 times, the load never comes): retry after 10 s; on the second failure in a
  row, give up. Nobody moves, the era stays the one of our Link, and a notice says so.
- Switch: `LICHII_ORM_AGE` (off in the kit options until a game confirms it; the probes turn it on).
  `LICHII_ORM_AGE_EVENT=<event>` keeps the older "reload in place" path, for tests only.

**What is known.**

- Our first approach asked the equipment-swap event `ReplaceEquipmentForEraChange` by name and reloaded the same scene.
  The game refused it 45 times out of 45. Reading the romfs explained why: the game only starts by name an event that
  has a row in `RSDB/EventSetting`, and that one is a sub-flow of the era-change flows, not an event of its own.
- Worse, that first approach wrote `Sequence_IsChildhood` before asking, and never put it back: Link stayed a child
  with the adult era in his save. Fixed: nothing is written before our event runs, and if the reload does not take
  after the event ran, the era is put back (log line `Sequence_IsChildhood put back to child (our Link's age)`).
- In the first run of the new path (`edad-grupo-cerca`, B in the market square), B's log shows the whole chain: the
  game took the era-change flow asked away from the pedestal, the 3-2-1 countdown, `now adult in Market_Adult, 32.7 s
  after the reload began`, and both games say the group is gathered. **The scenario is still red** (7 of 20 checks),
  because several checks look in the wrong time window (for example, the "switch on" line is written at start-up,
  before the scenario's `start` mark). So it is **not yet confirmed in game**.

**What is unknown.**

- The same with B far away (`edad-grupo-lejos`, B in Kokiri Forest), and the way back to child.
- Whether the big notice reads well and does not cover anything important (screenshot `told` of B).
- Whether the arrival cutscene ever leaves B half-way.
- A downed player who must wait, and is moved after the revive.

**The probe that confirms it.**

1. Fix the windows in `edad-grupo-cerca.A/B.txt` (`within boot..` for the start-up line, the arrival lines inside the
   marks that come after them), and run it again:
   `python -I tools/orm/harness/r03_pareja.py --scenario edad-grupo-cerca --transport ws --scen-dir tools/orm/harness/escenarios --capture-marks`
2. Then `edad-grupo-lejos`, and a child-again pair (A returns the sword).
3. Expected log lines: in A `coop: age: we changed age ourselves (child -> adult): the group follows us`; in B
   `coop: age: notice countdown: ... / 3` (2, 1), `the game took 'Market_Child_Field_ChildToAdult'` or
   `event running ...: the game's era change to Market_Adult runs`, then `now adult in Market_Adult`; in both
   `every mate is our age: the group is gathered`. Never: `given up`, `Sequence_IsChildhood <-` before an event runs.
4. Mutant: the same run with `LICHII_ORM_AGE=0` must go red.

---

## 2. Real collision and standing on a mate (12-bodies)

**What the player sees.** With `/choque on` (anyone in the room; `/choque` alone says the state; `/cuerpos` and
`/bodies` work too) your Link bumps into your mates like into a townsperson, and you can stand on a mate's head. On
Continue at the same spot, or after a warp, nobody gets stuck: the other's body is a "ghost" until you are apart.

**Why it exists.** Before this, when two Links stood in the same place (Continue puts everyone on the same saved
spot), our older rule wrote our Link's position to push him out. That write skips the game's collision: Link ended on
the other side of a railing and walked in place for a minute. Since then **our Link is never moved by the bodies
rule**. The fallback today is visual only: a mate's puppet that overlaps us is drawn 0.7 m aside (its real position
is still used for everything else).

**How it is built.**

- A separate actor per mate, built into the puppet pack from each player's own romfs: `CoopPuppet_Child_Body`,
  `CoopPuppet_Adult_Body` (sides and a head disc) and `CoopPuppet_*_BodySide` (sides only). It is kinematic, has no
  model, no life and no AI, and it collides only with the `Player` layer.
  - Sides: the player's own capsule prism (`Player_cct`: radius 0.23, height 1.17 for the child; 0.35 / 1.68 for the
    adult) on the `Character`/`NPC` layer with `Material_Character` (the townspeople's material, which is
    `Unridable`).
  - Head disc: 0.12 m thick, on the `GroundObject` layer, with a walkable material (the kind moving platforms use).
- Each frame the body goes exactly where the puppet is drawn, through the game's own matrix setter, so the physics
  takes the place. Bodies are created parked under the floor, so they never appear on top of anyone.
- Ghost rule: a body turns solid after 0.8 m apart (or one over the other) for 0.3 s; ghost again under 0.25 m, or when
  the mate is downed (to revive him kneeling), riding, hidden, or his samples jump more than 3 m (a warp).
- Switches: `/choque` (room switch); `LICHII_ORM_BODY_COLLIDER` = `room` (default), `1` (always on, for probes), `0`
  (never); `LICHII_ORM_BODY_TOP=0` removes the head disc.
- Fallback: if the pack has no bodies or a body cannot be made, the puppets are drawn aside as before, and `/choque`
  says so in the chat. Log: `coop: check: bodies: collider ok ...` or `collider fallback <why>`.

**Confirmed in game** (two hidden games, WebSocket, protocol 110):

| Scenario | Result | What was seen |
|---|---|---|
| `choque-bloqueo` | PASS 14/14 | A stops 0.44 m from B (the two bodies: 0.46) and walks away without getting stuck |
| `choque-encima` | PASS 10/10 | `our Link stands on P1's head` |
| `choque-continuar` | PASS 9/9 | both on the same Continue spot: puppet drawn 1 m aside, solid body there, nobody moved |
| `cartel`, `charla` | PASS 4/4 each | the old "walks in place" jam does not come back |

**What is unknown.**

1. Two bodies with high ping: each game stops its own Link at the other's body; with a lot of delay there may be a
   small push.
2. Whether the head disc carries you when the mate below walks (surface velocity).
3. What B sees with A on his head (A's body touches B's head from above; it may push B aside).
4. That swords, arrows and the camera ignore the body (they should: it only collides with `Player`).
5. Six players, and the slide-off variant with `LICHII_ORM_BODY_TOP=0`.

**Probes that would confirm it.**

- `choque-encima` with `--env LICHII_ORM_BODY_TOP=0`: A should slide off (no `stands on` line).
- A new pair `choque-llevar` (to write): A on B's head, B walks 3 m; expect A's position to follow B, and never
  `moved Link to` in either game.
- `choque-bloqueo` with added delay (the harness latency script `tools/orm/harness/latencia.ps1`), then look at the
  screenshots.
- Idea for later: a body like this could also press held switches (idea 3) if it were on a layer that the switch
  sensor sees, but not the player. Not tried.

---

## 3. Held floor switches (06-world)

**What the player sees.** "One stands on the switch, the other walks through the door it keeps open." A stands on a
held floor switch; in B's game the switch goes down and the door (or fire wall, or gate) opens as in A's. While
anyone is still on it, it stays pressed in every game. When the last one steps off, leaves the scene or disconnects,
it is released everywhere. Timed switches keep their own clock after release, as in the original.

**How it is built.**

- Rule (as in our earlier lockstep co-op for the N64 game): in my game, a switch is held by others while the feet of
  any other player in my scene are inside its zone. **No new message, no protocol or server change**: every game
  decides the same thing from the positions it already has (the ones the puppets are drawn with). Leaving and
  disconnecting come for free.
- The zone is the game's own sensor (`CheckObjectOn` of the actor package: a 0.5 x 0.5 m box for `A_03`, a 1 m
  cylinder in Jabu-Jabu), lowered to the pressed button and rotated with the placement.
- A table of 14 switches read from the romfs (Dodongo's Cavern 02 and 19, Fire Temple 08, 10, 13, Spirit Temple 08
  and 23, Water Temple 14, Jabu-Jabu 07, 14, 17, Deku Tree, Gerudo Training Ground). Forest Temple and the Well have no
  held switch in the remake.
- Two ways to press it in my game, only while my own Link is not on it:
  1. the switch's "Link on it" flag, where there is one (written 1, written again if the room puts it back to 0, put
     to 0 on release; never under my Link);
  2. the actor's own state words (written each frame, only the masked bits). **None are known yet**: a probe finds
     them.
- Switches: `LICHII_ORM_HELD_SWITCHES` = `flag` (default: press with the flag and watch the nearby door), `log` (only
  log), `1` (flag and words), `0` (off). `LICHII_ORM_HELD_SWITCH_LIST=<file>` adds `switch`/`word`/`off` lines.
  `LICHII_ORM_HELD_SWITCH_PROBE=1` logs the actor words that change when our Link steps on and off.
- Self-check after each press, in B: `held switch: ...: door opened with the flag (...)` or
  `door did not move 4.0 s after the flag: needs the actor words`.

**What is known.**

- **Confirmed in game: Fire Temple room 10** (`meca-sostenido-fuego`): B's log says `FireTemple room10 held by another
  player (P2): pressed here`, then `door opened with the flag (Obj_FireTemple_FireWallTrap_A_01 went, 3.4 s after the
  press)`, then `let go here`.
- **Spirit Temple room 08** (`meca-sostenido-espiritu`): the scenario passes its checks (B presses and releases with
  the flag), but B also says `door did not move 4.0 s after the flag: needs the actor words`. So the flag alone is not
  enough there.
- The word probe found nothing in the actor's resting copy; the general object probe sees about 40 words change when
  Link steps on, not yet separated from noise.

**What is unknown.**

1. The state words for each switch type (`A_03`, `A_03_Amos`, the Water Temple one, the Jabu-Jabu ones, `A_02`).
2. Water Temple room 14: the switch has three flags (`SwitchPushed`, `SwitchPushedByBlock`,
   `SwitchPushedHeavyBoots`). In our runs Link floats and swims on it: the heavy-boots mode is not reachable by script
   yet. It may be a block switch, not a boots switch.
3. Jabu-Jabu room 17 (the stone) and Dodongo's Cavern 02/19, Deku Tree: not run yet.

**The probe that confirms it.**

1. One game is enough to find the words: `LICHII_ORM_HELD_SWITCH_PROBE=1 LICHII_COOP_OBJECTS=1`, Link next to a
   switch of the table (scenario `meca-sostenido-espiritu`, save `19-espiritu-adulto`). Step on, wait 1 s, step off;
   two or three times. Each step leaves lines `held switch: probe:   word <actor> <place> on=0x.. off=0x..`.
2. The words that repeat on every step go to a file for `LICHII_ORM_HELD_SWITCH_LIST`; run the pair with
   `LICHII_ORM_HELD_SWITCHES=1`. Expect in B `pressed here` and `door opened` (not `door did not move`).
3. Mutant: `LICHII_ORM_HELD_SWITCHES=log` (B says "would be pressed": the `pressed here` check goes red).

---

## 4. Escape countdown for the group (05-bosses)

**What the player sees.** After Ganondorf, the castle collapses and a countdown runs. With six players, everyone should
see **the same** countdown, and someone who arrives late gets the group's countdown, not a fresh one.

**How it is built.**

- Rule (from our earlier co-op): the group's start is the first `CrumblingGanonCastle_Arrive` story event in the room
  (the server time of its `STORY_EVENT`). If our countdown shows more than 2 s above the group's, it is set to the
  group's. Never more time, never 0 (at 0 the game's own restart points apply), never touched if ours is longer than
  the whole duration (then the duration is wrong, and the log says so once).
- Switches: `LICHII_ORM_ESCAPE_CLOCK=1` (off by default); `LICHII_ORM_ESCAPE_CLOCK_WORD=main+0x<hex>[,0x<hex>...]:f32|u32|ms`
  (where the countdown lives, by hand); `LICHII_ORM_ESCAPE_CLOCK_LENGTH_S` (180 by default);
  `LICHII_ORM_ESCAPE_CLOCK_SEARCH=0` turns the search off.
- A search in the running game: with the switch on and no word given, after the collapse event it samples, once per
  second, the words of the GameUI module (the one that draws the timer) and the objects it points to that read as
  seconds; the one that goes down 1 s per second for 5 s is taken. Three failed tries: `our countdown was not found
  (...): the group's clock goes on without the game's timer` (nothing is written).

**What is known.**

- The romfs says the escape uses the minigame timer (`EventTriggerMiniGameTimerStart` and `EventTriggerShowTimerUI` in
  the `Defeat_Ganon` flow). No GameData flag holds the live value: `MiniGame_TimerValue` and
  `MiniGame_TimerMinute/Second/MilliSecond` are the result a minigame writes when it ends.
- With the synthetic save `21-huida-arriba` (Ganondorf beaten, at the top of the collapsing castle) the game takes the
  collapse.
- The search finds words that go down, but **they are the UI's copy**: a write is taken for one frame and then the
  copy goes back to its own value. Safety fix already in: a write that the game reverts counts as not taken, and after 3
  the word is dropped.

**What is unknown.** Where the real countdown lives, its exact duration, and what a floor cutscene does to a player who
is on another floor.

**The probe that confirms it.** `final-huida-reloj` (save `21-huida-arriba`). Expect in B
`escape clock: ours shows N s, the group's M s (it started K s ago): set to M`; never in A `escape clock: ... set to`
(A started it). An idea for the next search: start from the object of the game's timer-start action (the event node
that starts the minigame timer) instead of the UI module.

---

## 5. Din's Fire and Nayru's Love end in `ItemEmpty` (11-puppets, 09-pvp)

This matters for us because the puppets show the spells (Din's Fire stand-in), and our PvP rules use Nayru's Love as a
shield. But in our scripted runs the spells never start.

**The symptom.** With a save that has magic 48 of 48, both Great Fairy blessings, and the items owned
(`sonda-magia`, save `20-magia`), pressing Din's Fire or Nayru's Love plays `ItemEmpty` (the "nothing happens"
animation), with a tap or a 2 s hold. Farore's Wind works.

**Ruled out with data.** The save (several saves, the right magic bar), the puppet (`sonda-magia-sin-marioneta`: same
with no puppet in the game), the place (Kakariko and the castle), the press time, the projectile (it is in the
player's shooter table), and the verb attributes.

**Reading without a game (romfs and the game's code).**

- `ItemEmpty` is not chosen by Din's own scheduler: it is the **fail action of the upper body**, chosen by the
  secondary action selector. `StartDinFire` / `StartNayruLove` never show, so the main verb `UseDinFire` is never asked:
  the player's brain (C++ code, not an AI file) asks the fail action when the button is pressed.
- The data difference with Farore: Din and Nayru are `PouchCategory: Material` with `IsNotOpenStockNumDisplay` and go
  through the brain's item path; Farore goes through its resident event, which skips that check.

**Unknown.** Which check fails. We have no fix, and nothing in our saves, scripts or hook relies on a cause yet.

**The next probe (about a minute).** A read hook on the secondary verb request that logs the call stack when the verb
turns into the fail action after pressing the button with Din's Fire: the function that asks it is the missing check.
Then the scenario `sonda-magia` should show in A `you play '...Din...'` and `spawn: sent ExpandFireField`, in B
`spawn: got ExpandFireField`, and never `you play 'ItemEmpty'`. A cheap side check: the same save in Hookshot, to see
whether it is specific to the emulator (not tried).

---

## 6. Dungeon resume under upstream issue #197 (13-resume)

**What the player sees.** A returning player goes back to his last place. Inside a dungeon, if that place was in
another room than the entrance room, he goes back to the dungeon but **to the entrance room**, at the spot where he
last stood before leaving it (the way on to the rest of the dungeon), with the notice "Back to the entrance room, the
way you left it: your place is in another room". If that spot was not kept (another PC, an old file), he stays at the
entrance.

**Why not the exact room.** A dungeon is one scene that the game loads room by room. Upstream found (issue #197, the
`bigrun4` game) that teleporting into another room leaves Link held with a black picture (the room is not loaded), and
since then upstream does not move Link into another room of a dungeon. A scene change can only land on a start point,
and in every dungeon the start points are only in the entrance room (the others belong to cutscenes). The game's own
room change (a door) has no known request. So the safest useful point is a spot of the entrance room.

**How it is built.** While playing, the game remembers the last spot where Link stood in the entrance room of the
dungeon and keeps it in a separate line of the local resume file (`d1;room;name;dungeon;start;x;y;z;age;time`, only on
this PC). On return: a scene change to that start point, then, once settled, Link is moved to the spot, and the move is
checked against where the load really left him; if he is not in the same room, he stays where he is. Log:
`coop: resume: the last place (...) is in another room of this dungeon (issue 197: ...): going to the spot of its
entrance room we last left it by`, then `at the spot of the entrance room of <dungeon> we last stood on before leaving
it`; last resort `coop: resume: not moved to ... staying here`.

**Confirmed in game:** `reanudar-mazmorra-1` then `reanudar-mazmorra-2` (two sessions in a row with the same
instances; `ORM_KEEP_RESUME=1` keeps session 1's file), all 10 checks of session 2 met, never a move to the ledge
inside the other room, never `not moved to`.

**What is unknown.**

- Our resume trip still asks the scene change with `start_event = true`; with issue #197 in mind, this is a risk to
  watch in real play.
- That the return does not clash with the song invitation or the boss-room rule.
- A real two-PC session (the run above uses one PC's file).
- An idea, not started: if someone finds the request behind the game's own room change (doors), the player could go
  back to the exact room safely.

**The probe that confirms the rest.** The same pair after real play; the mutant is to delete the `d1` line from B's
file before session 2: expect `no spot of its entrance room kept, going to its entrance`.

---

## 7. Performance with six puppets (01-platform, 15-tools)

**What we measured** (one hidden game at half resolution, 60 fps mode, Kokiri Forest, Link walking in loops; each run
has its own 60 s "alone" reference):

| Run | fps alone -> with 5 bots | Reading |
|---|---|---|
| bots 8 m away, 30 poses/s, free PC | 61 -> 51 | the good reference |
| bots send no pose (puppets animated by the game) | 31 -> 39, 52 -> 57 | no drop |
| no puppets at all (`LICHII_COOP_PUPPET=0`), five players in the room | 58 -> 58 | network, presence and markers cost nothing |
| bots 150 m away | 59 -> 64 | puppets out of view cost nothing |
| same game with builds running on the PC | 46-53 -> 23-34 | the biggest loss is other load on the PC |

- Our pose copy for five puppets is 0.07 ms per frame (a benchmark without the game): about 2 % of what is lost.
- What costs is the game animating and drawing extra Links when they are near and in view, under CPU emulation.
- We suspect the "10 fps for five puppets" figure is inflated by the bots: they send a synthetic pose with all bones
  marked "not identity", which forces the full computation of every bone. Real players send the game's own marks.

**What is built.** A frame-time log (`LICHII_ORM_FRAME_LOG=<file.csv>`, one line per presented frame, same columns as
Hookshot's frame log) and a perf meter (`LICHII_ORM_PERF=1`: every 5 s, the ms per frame of each part of the hook,
`coop: perf: ...`). A six-player load run (`tools/orm/harness/carga6.ps1`: Kokiri, market, Deku Tree, a pair plus 4
bots, and a 30-minute run) is prepared but **never ran**: it needs about 15 GB of free RAM and it never had it.

**Ideas, ordered by gain and risk.**

1. Run the game with high priority from the launcher (estimated: gets back part of the loss when the PC is busy).
2. Cut the presentation-thread work of the hook using the perf meter's data (estimated +1 to +2 fps).
3. A lighter puppet: no ray-cast footsteps, and no own animation graph while poses arrive (estimated +2 to +5 fps,
   only if real poses cost what bot poses cost; risk: footstep sound, and the switch back to its own animation).

**The probes.**

1. A bot that replays a pose recorded from a real game (with its real marks), then `tools/orm/harness/rendimiento.ps1`:
   tells whether five puppets cost 10 fps or almost nothing.
2. The same run with `LICHII_ORM_PERF=1`: the `coop: perf:` line of each variant says where the hook's 3 fps go.
3. `carga6.ps1` on a free PC (no builds, nothing else running).
4. One minute of real play with the GPU use logged: if the GPU is above 90 %, the limit is the GPU, not this list.

---

## 8. Hookshot thin layer (01-platform)

**What it is.** One interface, `lichii/hook/platform.h`, for everything our hook code uses from the platform: game
memory (reads, writes, a 32-bit compare-exchange), calls into the game, the pad, the log and the switches.
`platform_emulator.*` is the emulator adapter; `platform_env.h` is environment and log only. Rule for new code: our hook
files never include emulator headers (`core/`, `hid_core/`, `video_core/`, `common/`); what is missing is added to the
layer and its adapter in the same commit.

**What upstream did in test10.** Hookshot co-op and crossplay, and its `hookshot/compat/` makes the same
`lichii/hook` files compile unchanged into `hookshot_coop.dll`. We widened its file filter so our `orm_*` and
`platform_*` files go in too, and the pad buttons come from its runtime. Upstream also says the test releases after
test10 are Hookshot only.

**What does not work on Hookshot yet.**

1. **The downed gate.** In the emulator a small code patch stops the game-over event while a gate word is set.
   Hookshot does not apply code patches. Idea: an entry hook on the function that starts the game over, in a file of
   the Hookshot patch, that returns 0 while the gate word is set; then the Hookshot kit writes
   `LICHII_ORM_HOOKSHOT_GATE=1` and downed turns on again. Until then, downed turns itself off on Hookshot with the
   line `coop: check: downed fallback off on Hookshot ...` (normal game over).
2. **Our in-picture HUD** is a Vulkan patch of the emulator; on Hookshot its own HUD shows.
3. **The spectator's right-stick nudge** (to wake the camera after a load) needs a new export from the Hookshot
   bridge; today it does nothing and says so once.

**Not done.** Building `hookshot_coop` with our files, and any run with one game on each platform. A side-by-side
comparison is designed but not run: `tools/orm/plataforma/comparar.py` plays the same 10-minute route on both and
decides with five rules (stability, fps, hitches, visual faults, memory growth).

**The probe that confirms it.** A crossplay pair, one game on Hookshot and one on the emulator: both join with
encryption (`coop: traffic encrypted`) and see each other's puppets. Then downed: the Hookshot player dies and stays
down (no game over), the other holds A next to him and gets him up; both down: group defeat and game over for both.
