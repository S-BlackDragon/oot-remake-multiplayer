# 11-puppets: Puppet polish

The puppet (docs/puppet.md) is how another player's Link is drawn in your game. This feature is a set of independent layers on it, each with its own switch, so that you see the others doing anything: smoother movement, effects and status, what they carry, masks and tethers, the armor they really wear, their Ocarina, their horse, the moving floor they stand on, the townsperson they talk to, and their colour. Every layer is presentation only: nothing in your own game state changes.

## What changes for players

| Part | What you see |
|---|---|
| Smoothing | Another Link moves smoothly: no jerks from network jitter, no "walking while he runs", no sliding after a stop. Enemy copies you do not own move the same way |
| Effects and status | Another player's spell casts, swimming, rolls and landings play their effects on his puppet; his puppet blinks when he is hurt; burn, freeze, shock and Nayru's crystal show on him; the item he holds up from a chest shows in his hands |
| Carried objects | A pot, rock, crate, bush, Cucco, bomb flower or child Ruto another player lifts is in his puppet's hands, and his throw is seen |
| Masks and tethers | His mask or Lens of Truth on his puppet's face; his hookshot and longshot chain and his fishing line drawn from his hands |
| Worn armor | His puppet wears his boots, gauntlets and mask, never yours |
| Ocarina | While he plays, his own Ocarina is in his puppet's hand; you hear his notes at his puppet and see his songs named |
| Horses | A player getting on, riding and getting off a horse is seen on a horse with its own look (ranch horse, Ingo's), not hidden |
| Moving floors | A player on a lift, a moving or floating floor or the Shadow Temple's boat stands on your copy of it instead of floating above or sinking through |
| Townspeople | The townsperson he talks to turns its head and face on your screen too; optionally a townsperson looks at his puppet as at your Link |
| Colours | His name tag and panel line in his chosen colour; players 5 and 6 get purple and orange by default |

## Switches

Upstream switches this feature only reads (`LICHII_COOP_PUPPET`, `LICHII_COOP_SPAWN`, `LICHII_COOP_OBJECTS`, `LICHII_COOP_TUNICS`) are not listed. Every layer needs `LICHII_COOP_PUPPET=1`, and most need a puppet pack built with this kit.

| Variable | Kit default | Code default | Values | What it does |
|---|---|---|---|---|
| `LICHII_ORM_SMOOTH` (old `LICHII_COOP_SMOOTH`) | `LICHII_ORM_SMOOTH=1` | on (`coop_puppet_config.cpp`) | on/off words | Puppet smoothing. Off: placed from presence by arrival as before |
| `LICHII_COOP_COPY_SMOOTH` | not in the kit | on (`coop_objects.cpp`) | on/off words | The same smoother for enemy copies we do not own. Off: `Sync::PlaceAt` as before |
| `LICHII_ORM_EFFECTS` | not in the kit | on (`coop_effects.cpp`) | on/off words | Allowed AS commands played under the copied pose, for their effect and sound events |
| `LICHII_ORM_DAMAGE_FLASH` | not in the kit | on | on/off words | The damage blink |
| `LICHII_ORM_STATUS_FX` | not in the kit | on | on/off words | Burn, freeze, shock and Nayru's crystal stand-ins |
| `LICHII_ORM_NAYRU_S` | not in the kit | the game's `NayruLoveTime` written by the puppet pack, else 60 | 1 to 600 (seconds) | How long Nayru's crystal stays on a puppet |
| `LICHII_ORM_EFFECTS_LIST` | not in the kit | built-in rules | a file path | Own rules instead of the built-in list: lines `play`, `skip`, `flash`, `noflash`, `status burn|freeze|shock`, `nayru`, `rest`; first match wins; `*` is the only wildcard |
| `LICHII_COOP_CARRY` | not in the kit | on (`coop_objects.cpp`) | on/off words | Carried objects claimed and drawn in the holder's hands |
| `LICHII_COOP_CARRY_BONE` | not in the kit | on | on/off words | Off: the carried copy follows the puppet's actor frame only, not its hand bone |
| `LICHII_COOP_CARRY_FLOWERS` | not in the kit | on | on/off words | Off: bomb flowers are not shared |
| `LICHII_COOP_CARRY_STILL` | not in the kit | on | on/off words | Off: a carried copy keeps its own speed |
| `LICHII_ORM_MASK_FOLLOW` | not in the kit | on (`coop_puppet.cpp`) | on/off words | Masks and the Lens of Truth placed on the puppet's head each frame |
| `LICHII_ORM_TETHERS` | not in the kit | on (`coop_spawn.cpp`) | on/off words | Hookshot chain and fishing line laid on the copies (needs `LICHII_COOP_SPAWN=1`) |
| `LICHII_COOP_OCARINA_SOUND` | `LICHII_COOP_OCARINA_SOUND=1` | off (`orm_ocarina_sound.cpp`; empty or `0` is off, any other value on) | `1` / `0` | The others' Ocarina heard at their puppets |
| `LICHII_ORM_OCARINA_NOTE` (old `LICHII_COOP_OCARINA_NOTE`) | not in the kit | as `LICHII_COOP_OCARINA_SOUND` (`coop_ocarina_note_game.cpp`) | on/off words | Our own notes and songs sent to the others |
| `LICHII_ORM_OCARINA_NOTES` | not in the kit | off (`orm_ocarina_sound.cpp`) | on/off words | Off: notes travel as upstream's `OCARINA_NOTE` (140) and ours carries songs only. 1: ours carries notes and songs, and the puppet director skips upstream's |
| `LICHII_ORM_OCARINA_HAND` (old `LICHII_COOP_OCARINA_HAND`) | `# LICHII_ORM_OCARINA_HAND=bow` (commented) | `sub` (`coop_puppet_config.cpp`) | `sub` (left hand), `bow` (right hand), an off word (not drawn in the hand) | Which hand piece the Ocarina is carried as while its player plays |
| `LICHII_ORM_HORSE` (old `LICHII_COOP_HORSE`) | `LICHII_ORM_HORSE=1` | on (`coop_epona.cpp`) | on/off words | A rider drawn on a horse. Off: a rider vanishes while he rides (his name stays) |
| `LICHII_ORM_HORSE_ACTOR` (old `LICHII_COOP_HORSE_ACTOR`) | commented | by the horse he rides | a puppet pack actor name | One horse actor for every rider |
| `LICHII_ORM_HORSE_SADDLE` (old `LICHII_COOP_HORSE_SADDLE`) | commented | `0,1.2,0` | `x,y,z` | Where the rider sits on the horse |
| `LICHII_ORM_DEBUG_HORSE` (old `LICHII_COOP_DEBUG_HORSE`) | commented | off | on/off words | Logs where your own Link sits on your Epona (the value for `_HORSE_SADDLE`) |
| `LICHII_ORM_RIDE` (old `LICHII_COOP_RIDE`) | `# LICHII_ORM_RIDE=0` (commented: on) | on (`orm_ride.cpp`) | on/off words | A mate on a moving floor drawn on your copy of it (needs `LICHII_COOP_OBJECTS=1`) |
| `LICHII_COOP_NPC_POSE` | `LICHII_COOP_NPC_POSE=1` | off (`coop_objects.cpp`, `coop_svc.cpp`) | on/off words | The talked-to townsperson's head and face sent as `OBJECT_POSE`. Needs `LICHII_COOP_NPC=1` (the talk claim, 06-world) |
| `LICHII_COOP_NPC_GAZE` | not in the kit | off (`coop_objects.cpp`) | on/off words | A townsperson turns its head to a partner's puppet |
| `color` (coop-options.ini) and `LICHII_COOP_COLOR` | `# color = blue` (commented) | the launcher's per-player colour from `coop.ini`, else `slot_color` | a colour name or `#RRGGBB` | Your colour; the kit scripts (`kit-paths.ps1`) export `coop.ini`'s colour as `LICHII_COOP_COLOR`, so upstream's own default colour no longer overwrites it |

Switches marked "on/off words" are read by `coop_switch.h` (0/1/true/false/yes/no/on/off, any case; anything else keeps the default and logs `coop: switch NAME=VALUE is not ...`).

## How it works

### Smoothing

| File | What it does |
|---|---|
| `coop/host/include/coop/host/pose_smooth.hpp`, `coop/host/src/pose_smooth.cpp` | An interpolation buffer: the puppet is drawn 100 ms behind the estimated server clock, between the two samples around that time (cubic Hermite with the sender's velocities, bounded so a stop does not overshoot). Past the newest sample it extrapolates with the sender's velocity for at most 150 ms, then holds. A late sample or a resumed stream is eased over 100 ms; a teleport or scene change snaps. The render clock never goes back. The animation and gait come from the sender's command and speed, never from the drawn speed. `Timeline<T>` gives the body poses the same time base |
| `lichii/hook/coop_copy_smooth.h`, `lichii/hook/coop_copy_smooth.cpp` | The same smoother for enemy copies from their owners' `OBJECT_SAMPLE`s, one per copy, reset when the owner changes |

### Effects and status

| File | What it does |
|---|---|
| `lichii/hook/coop_effects.h`, `lichii/hook/coop_effects.cpp`, `lichii/hook/coop_effects_game.cpp` | With the pose copy a puppet plays no AS command, so its animation events never fired. An allow-list (`kDefaultRules`, or `LICHII_ORM_EFFECTS_LIST`) plays harmless commands (casts, swimming, moves, rolls, landing) under the copied pose for their effect and sound events only; attacks, damage, lifting, throwing, items, demos and the horse are never played. A damage command makes the puppet blink (drawn under its spot every other 100 ms for 1.2 s). A burn, freeze or shock command puts a stand-in (`CoopFx_Burn`, `_Freeze`, `_Shock`) on the puppet; Nayru's cast puts `CoopFx_Nayru` on it for the spell's time |
| `coop/host/include/coop/host/worn_armor.hpp`, `coop/host/src/worn_armor.cpp` | Which armor piece a player really wears (the piece in his equipment wins over the menu flag), sent in `PLAYER_EQUIP`; and which pieces a puppet may take off |
| `lichii/hook/coop_detalles.h`, `lichii/hook/coop_detalles.cpp` | A puppet equips itself from your game's flags when it is made; a kind its player wears nothing of (boots, gauntlets, mask) is taken off with the game's own armor take-off, at most a few times per puppet |
| `lichii/hook/coop_chest_open.*` (06-world) | The chest item held up: a `CoopGet_<item>` stand-in in the opener's puppet's hands |

### Carried objects

| File | What it does |
|---|---|
| `lichii/hook/coop_carry.h`, `lichii/hook/coop_carry.cpp` | Ours: when our Link's command starts a lift, the shared liftable nearest in front of him is claimed (`OBJECT_PRESENT` targeting, as for a talked-to townsperson), and while held its samples carry an `ObjectCarry` (place in our Link's frame and in the nearest bone's frame). Throw, put or drop ends it; the claim lasts a moment more so the throw and break stay the thrower's. Theirs: a copy whose owner's sample carries one is placed each frame in the holder's puppet's hands (its bone when it reads as a rigid frame, else the actor frame), blended in; after the throw it follows the samples again |

### Masks and tethers

| File | What it does |
|---|---|
| `lichii/hook/coop_mascaras.h`, `lichii/hook/coop_mascaras.cpp` | A mask's model is bound to its wearer by bone name, so on a puppet it was drawn elsewhere. Each frame after the pose copy, the head piece (mask or lens) gets the puppet's place and turn, and the puppet's bones of the same name are copied onto it. The hookshot chain (`Item_Hookshot_Chain`'s `MainLine` bones) and a procedural fishing line (`CoopShot_FishingLine`, the rope model) are laid from the tip or lure to the hand piece of the owner's puppet |
| `tests/lichii/skeleton_fakes.hpp` | Fake skeletons and equipment for those tests |

### Ocarina

| File | What it does |
|---|---|
| `lichii/hook/coop_ocarina_note.h`, `lichii/hook/coop_ocarina_note.cpp`, `lichii/hook/coop_ocarina_note_game.cpp` | Reads our own game's Ocarina control (state, note, instrument, the song recognised) on the present thread, only reads, and sends notes and songs as our `OCARINA_NOTE` (0xF007). Since test10, by default only songs: upstream sends the notes itself (`OCARINA_NOTE` 140, from the buttons) |
| `lichii/hook/orm_ocarina_sound.h`, `lichii/hook/orm_ocarina_sound.cpp` | Plays another player's notes at his puppet through the game's own "Ocarina" sound user (each puppet its own handle), and logs his songs. Also plays the "a mate is down" cue note (04-downed). Named `orm_` because test10 added an upstream file with the old name |
| `lichii/hook/coop_puppet.cpp` (`OcarinaInHand`, `OcarinaHandPiece`) | While a player plays, his own Ocarina (`Item_Ocarina_Fairy` or `_Time`) is worn as the sub weapon (or bow) of his puppet, and put away after |

### Horses

| File | What it does |
|---|---|
| `lichii/hook/coop_epona.h`, `lichii/hook/coop_epona.cpp` | A rider's puppet is drawn where his samples say, with his own getting-on, seated and getting-off animation (no longer parked out of sight). His game finds the horse under him and sends its name in `PLAYER_EQUIP` (`mount`); the others pick the puppet horse for that look (`coop::host::epona::horse_look`). A third puppet director (svc caller 25) places the horse under the rider (place minus the saddle offset, turned by his heading) with a gait from his speed. Since test10, when a rider sends upstream's `PLAYER_RIDE` on Epona, upstream's `CoopRide` draws Epona and ours steps aside; ours still draws the ranch horses, Ingo's horse and event horses. The group's own Epona (one player wins her for all, the race hold) is in the same file and is described in 06-world |
| `coop/host/include/coop/host/epona.hpp`, `coop/host/src/epona.cpp` | Horse looks by name, the shared Epona step and race hold logic |

### Moving floors

| File | What it does |
|---|---|
| `coop/host/include/coop/host/ride.hpp`, `coop/host/src/ride.cpp` | `RideDetector`: from our own actor walk, whether a moving floor (lifts, rising, sinking and floating floors, the Shadow Temple's boat; only floors that translate) carries our Link; its placement hash and our place on it go in `PLAYER_SAMPLE`. `RideFollower`: a receiver with the same floor loaded draws the puppet on its own floor at that place, only within reach of the samples, faded in and out |
| `lichii/hook/orm_ride.h`, `lichii/hook/orm_ride.cpp` | The game glue (named `orm_` beside upstream's own `coop_ride`). Also used by the boss families (05-bosses): a Link held by a grabbing part rides it the same way |

### Townspeople

| File | What it does |
|---|---|
| `lichii/hook/coop_npc_pose.h`, `lichii/hook/coop_npc_pose.cpp` | The owner's game reads the talked-to townsperson's skeleton (head turned to the talker, face) and sends it as `OBJECT_POSE`, at most 20 a second, only changed bones, all bones every 500 ms, in parts for townspeople with many bones. The others write it on their copy bone by bone, the newest two blended |
| `lichii/hook/coop_npc_gaze.h`, `lichii/hook/coop_npc_gaze.cpp` | For a townsperson that should look at a partner (the nearest player, or the one talking to it), each look-target slot that links our Link is turned into a place slot at the partner's puppet's eyes, and given back after. Self-checks that the game left the slots as written |

### Colours

| File | What it does |
|---|---|
| `coop/protocol` (`PLAYER_LOOK`), `coop/presence` | A player's colour for his name tag and panel line, sent after each `WELCOME` and on change, kept by the server and replayed to joiners. `slot_color` gives player 5 purple `#A05CE0` and player 6 orange `#F09A28` |
| `lichii/hook/coop_hook.cpp` (`OrmRgb`) | Converts upstream's parsed `color` (`CoopColors::Parse`) for `PLAYER_LOOK` |
| `tools/release/kit/scripts/kit-paths.ps1`, `coop-ini.ps1` | Export `coop.ini`'s colour as `LICHII_COOP_COLOR` |

Upstream's own tunic colours (test10, `PLAYER_EQUIP` component 16) are what tints the puppet; our earlier tint was removed in favour of theirs.

### Puppet pack (`tools/puppet_pack`)

`build.py` builds, besides upstream's actors: the player's effect user on the puppets by default (`ELinkRef`, `ELinkUserName`; `--no-effects` builds without), effect stand-ins (`EFFECT_STAND_INS`: Din's Fire, the hammer's shock wave), status stand-ins (`FX_STAND_INS`: `CoopFx_Burn`, `_Freeze`, `_Shock`, `_Nayru`, each the effect user of an always-on effect locator), elemental arrow stand-ins (`ARROW_ELEMENT_STAND_INS`), chest item stand-ins (`GET_STAND_INS`, `CoopGet_<item>`), `CoopShot_FishingLine`, the horses (`HORSES`: `CoopPuppet_Horse` with Epona's look, `_Horse_Ranch`, `_Horse_Ingo`; no physics, no attention, no name, so nobody can get on one), the lock-on variants `CoopPuppet_*_Lock` (09-pvp) and the body actors (12-bodies). It writes `nayru_s` (the game's `NayruLoveTime`) into `coop_puppet.json`. `dumpcheck.py` extracts the optional sources (Dark Link included). A pack built before these lacks them: each layer falls back with a log line (plain event horse, lure without line, estimated crystal time).

Hook points in upstream files:

| File | Hook |
|---|---|
| `lichii/hook/coop_puppet.cpp`, `coop_puppet.h` | Smoothed placement and timed poses, the effect commands, flash and status in the planner, Ocarina in hand, rider shown on a horse, ride step added to the target, mask follow (`SetHeadFollow`), stray armor take-off, the bodies' plan observer and `LastSmoothed` (12-bodies) |
| `lichii/hook/coop_puppet_config.cpp` | `OrmSettings`: our switches into the planner settings, and their start lines |
| `lichii/hook/coop_pose.cpp` | Poses stamped with when they were read, shown on the sender's time line; `OBJECT_POSE` for any actor |
| `lichii/hook/coop_spawn.cpp` | Status and effect stand-ins on puppets, tethers on the copies |
| `lichii/hook/coop_objects.cpp`, `coop_objects_sync.cpp` | Copy smoothing, carry, NPC pose and gaze switches and routing, moving floors handed to `orm_ride` |
| `lichii/hook/coop_svc.cpp`, `coop_fairy.cpp` | The svc callers for our directors; the fairy stays by a rider on his horse |
| `lichii/hook/coop_ocarina.cpp` | Reads the Ocarina note and song nodes (`<note>_lp`, `Melody_<name>`) out of a puppet's AS command, skipping the idle finger nodes |

## Messages and components

| Name | Number | Use |
|---|---|---|
| `PLAYER_LOOK` | 0xF004 | A player's colour |
| `OBJECT_POSE` | 0xF005 | A townsperson's bones |
| `OCARINA_NOTE` (ours, `MessageType::orm_ocarina_note`) | 0xF007 | Notes and songs (songs only by default); upstream's `OCARINA_NOTE` 140 carries the notes |
| `PLAYER_SAMPLE` component `ride` | 30 | The floor a player rides and his place on it |
| `PLAYER_EQUIP` component `mount` | 30 | The horse a rider sits on, by actor name |
| `OBJECT_SAMPLE` component `carry` | 29 | A lifted object in a player's hands: holder, place, bone |
| Upstream's `PLAYER_RIDE` | 385 | Epona under a rider, drawn by upstream |

Effects, flash, status, Nayru's crystal, masks, tethers and worn armor use only the existing `PLAYER_SAMPLE` command and `PLAYER_EQUIP` fields: no new message.

## Tests

| File | What it checks |
|---|---|
| `tests/host/pose_smooth_test.cpp` | The 100 ms buffer, jitter, bursts, lost and reordered samples, bounded extrapolation, eased corrections, snaps, a clock that never goes back, gait from the sender |
| `tests/orm/lichii_pose_test.cpp` | Timed poses by sender time, a stopped stream held then blended out, a pose stamped when read |
| `tests/lichii/hits_test.cpp` | Copy smoothing of enemy copies (with the hit feedback of 05-bosses) |
| `tests/lichii/effects_test.cpp` | The rules (only `*` as wildcard; casts, swimming and moves played, never attacks, damage, items or horse; shaking moves never), flash, burn, freeze, shock, Nayru's crystal and its end, the rules file and switches |
| `tests/host/worn_armor_test.cpp` | Which pieces are shown and which a puppet may take off; piece before flag; link names checked |
| `tests/lichii/carry_test.cpp` | The lift claim, the carry sample, the copy in the holder's hands on fake memory |
| `tests/lichii/mascaras_test.cpp` | Rotation layouts, frames through parents, skeleton reading, mask placed and bones copied, chain and line laid, folded without a puppet |
| `tests/lichii/ocarina_note_test.cpp` | Reading the Ocarina control and the note the game plays; a note on change, a release on stop |
| `tests/orm/orm_ocarina_sound_test.cpp`, `tests/orm/lichii_ocarina_test.cpp` | Note names, off by default, a note plays at the puppet, no double sound from command and message |
| `tests/orm/lichii_puppet_test.cpp`, `tests/lichii/puppet_test.cpp` | The Ocarina in hand and `LICHII_ORM_OCARINA_HAND`, mask follow each frame, planner settings |
| `tests/lichii/epona_test.cpp`, `tests/host/epona_test.cpp` | The horse under its rider (upright, saddle offset, heading), gait by speed, look by horse, saddle probe |
| `tests/host/ride_test.cpp`, `tests/lichii/ride_test.cpp`, `tests/lichii/ride_hook_test.cpp` | The `ride` component on the wire, which floors carry, ride start and end, a Link beside a lift does not ride it, a partner drawn on our floor only when near |
| `tests/lichii/npc_pose_test.cpp` | The owner's skeleton reaching the copy, 20 a second, changed bones only, newest pose wins, other skeletons not written |
| `tests/lichii/npc_gaze_test.cpp` | Target slots, the nearest player rule, the talker wins, at most a few townspeople turned |
| `tests/orm/presence_test.cpp` | A player's colour from `PLAYER_LOOK`, kept until he leaves |
| `tools/puppet_pack/tests/test_puppet_pack.py` | The stand-ins, horses, lock and body actors build |

Harness scenarios (they live in `tools/orm/harness`): `levantar-vasija` (A lifts and throws a pot, B sees it in A's puppet's hands and broken), `ocarina-mano` (B plays notes and Zelda's Lullaby, A sees the Ocarina in the hand and hears the notes), `efectos-cofre` (the chest item held up), `charla-dos` (the townsperson pose sent and blended back), `efectos-magia`, `efectos-dano`, `mascaras-ponerse`, `mascaras-lente`, `gancho-cadena`, `pesca-sedal`, `epona-montar`, `epona-rancho`, `epona-cancion`, `mirada-pnj`, `tunica-color`.

## Log lines

- `coop: puppets: smoothing on (LICHII_ORM_SMOOTH)` or `smoothing off`; per player, `P<n> smooth: ...` measures; for copies, `copy smooth: <s> s: <n> samples (<n> late, <n> reordered, <n> dropped), ...`; `coop: objects: share hits feedback ..., owner dedupe ..., copy smoothing on|off`.
- `coop: effects: commands under the pose on, damage flash on, burn/freeze/shock and Nayru's crystal on (60 s crystal)`; `coop: puppet: P<n> effects: '<command>' played under the copied pose ...`, `... effects: flash ('<command>')`, `... shows burn (was none)`, `... crystal on for <s> s`, `... crystal off (time|hurt|scene|downed)`; `coop: check: nayru crystal ok <s> s, the game's NayruLoveTime (PlayerGameParam, read by the puppet pack)` or `... fallback ... the estimate: the puppet pack has no nayru_s (run make-puppet-pack again)`.
- `coop: carry: on (lifted objects claimed and carried ...`; `coop: carry: lifting <actor> ... claimed)`; `coop: carry: P<n> holds <actor> ...: drawn in the puppet's hands (bone <n>|actor frame)`; `coop: carry: P<n> let go of ...: it follows the samples`; `coop: check: carry bone ok the puppet's bone <n> reads as a rigid frame`.
- `coop: puppet: P<n> head '<mask>' (actor ...) was <d> from its puppet: placed on it every frame, <n> bones of the same name copied ...`; `coop: spawn: hookshot chains and fishing lines on the copies on` (or `off (LICHII_ORM_TETHERS=0)`); `... laid to the puppet's FirePos_HookShot ...`, `... fishing line laid to the puppet's Rod_4 ...`.
- `coop: puppets: the Ocarina in the hand while its player plays: sub weapon`; `coop: puppet: P<n> wears sub weapon 'Item_Ocarina_...'`; `coop: check: ocarina hand ok P<n>'s own Ocarina is in its hand`.
- `coop: ocarina sound: on, songs only: the others' notes are upstream's (OCARINA_NOTE 140), ...` or `coop: ocarina sound: on, notes and songs by ours (LICHII_ORM_OCARINA_NOTES=1; ...` or `coop: ocarina sound: off (LICHII_COOP_OCARINA_SOUND=1 plays the others' notes)`; `coop: ocarina note: our notes are sent (LICHII_ORM_OCARINA_NOTE; ...; songs only: the notes go as upstream's OCARINA_NOTE 140)`; `coop: ocarina sound: P<n> note <note>`, `... P<n> Melody_<song>`.
- `coop: epona: riders drawn on a horse, actor <actor>, saddle (x, y, z)` or `coop: epona: riders' horses off (LICHII_ORM_HORSE=0)`; `coop: epona: P<n> on Epona: drawn by upstream's PLAYER_RIDE (CoopRide), not ours`; `coop: epona: the puppet pack has no <actor> (run make-puppet-pack again ...`; `coop: epona: saddle probe on (LICHII_ORM_DEBUG_HORSE)`.
- `coop: ride: we ride <actor> <key>, at <dx> <dy> <dz> from it`, `coop: ride: we are off ...`, `coop: ride: P<n> rides <key>: drawn on our floor`, `... our floor is <d> m from theirs, drawn where they are`, `... not loaded here, drawn where they are`, `coop: ride: P<n> off its floor`.
- `coop: npc pose: on (...`, `coop: npc pose: sending <actor> ...`, `coop: npc: copy ... blends back to its owner's pose over 600 ms`; `coop: npc gaze: ...`, `check: npc gaze ok (...)` or `check: npc gaze fallback (the game set the slots again ...)`.

## Status

| Part | Status |
|---|---|
| Carried objects | Confirmed in game on our test9-based build (scenario `levantar-vasija`, 16/16, A joining second) |
| Ocarina in hand and notes heard | Confirmed in game on our test9-based build (scenario `ocarina-mano`, 17/17), when our message still carried the notes. The test10 split (notes by upstream's 140, songs by ours) is not yet confirmed in game |
| Chest item held up on the puppet | Confirmed in game on our test9-based build (scenario `efectos-cofre`, 23/23) |
| Townsperson pose | Confirmed in game on our test9-based build (scenario `charla-dos`, 8/8: pose sent, the copy blends back) |
| Smoothing | On in every harness run, not yet confirmed in game on its own (no measure with six players) |
| Spell effects, damage flash, status stand-ins | Not yet confirmed in game (`efectos-magia` 10/20, `efectos-dano` not run green) |
| Masks, lens, hookshot chain, fishing line | Not yet confirmed in game |
| Worn armor taken off a puppet | Not yet confirmed in game |
| Horses | Not yet confirmed in game (`epona-rancho`, `epona-cancion` 5/13: the probe saves did not give a horse to ride) |
| Moving floors | Not yet confirmed in game |
| Townsperson gaze | Not yet confirmed in game |
| Colours (`PLAYER_LOOK`, slots 5 and 6) | Not yet confirmed in game |

## Known gaps

- Effects: whether a cast's effect comes from the command (seen on the puppet) or from the player's own code (not seen) is for a game run to tell; which effect locator looks right for each status stand-in too. Freeze and shock have no scenario yet.
- Masks: what the player's code does when putting on a mask (face UV shift, fringe physics) is not done; the puppet's hair could show through. The rotation layout for the chain and line is decided at run time from the puppet's spine.
- Ocarina: two messages now carry one player's Ocarina (upstream's notes, our songs); the split has unit tests only.
- Horses: a short moment with two horses when getting on Epona (ours yielding to upstream's `PLAYER_RIDE`) is possible; the saddle height is not measured (`LICHII_ORM_HORSE_SADDLE` default).
- Moving floors: rotating floors (Gerudo Training Ground, Shadow Temple) and the Forest Temple's twisted corridor are not followed; a puppet there is drawn on its samples.
- Colours: the tunic colour is upstream's; our `PLAYER_LOOK` colours only the name tag and panel line, so after `/color` in the chat (09-pvp; it sends `PLAYER_LOOK` at once and writes `coop.ini`) the tag changes at once and the tunic is expected to follow only at the next start (not checked).
