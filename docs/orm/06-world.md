# 06-world: One world for the group

Everything that makes the world the same in every game beyond upstream's shared flags: water levels, held floor switches, chests and doors seen opening on every screen, what a talk changes, the warp-song invitation, the kit's flag lists for each part of the game, the item rules with a wallet per player, the heart capacity guard, one clock, the group's Epona, and the pause and title-screen status. Each part has its own switch, and each can be taken alone.

The rule behind all of it (our earlier lockstep co-op for the N64 game on a Ship of Harkinian fork had it by construction): what the group did for good is the group's; what a player has in his hands, his life, his rupees and his greetings stay his.

## What changes for players

| Part | What a player sees |
|---|---|
| Water levels | A water level another player sets is the same in your game (the Water Temple's three levels, the Deku Tree basement, the Bottom of the Well's drain, two Water Temple rooms, the Forest Temple well; Jabu-Jabu's room 14 on request). It never moves under your Link while he swims, falls or climbs in that water: it waits until he stands (at most 20 s). |
| Held floor switches | One player stands on a switch, another walks through the door it holds open (the Fire Temple's fire wall, Spirit Temple bars). |
| Chests | When a mate opens a chest you see his chest's lid open on your screen with his kick, and what he found shows over his puppet's head while he holds it up, with a short notice "<nombre> encontró <objeto>" (the notice is drawn by [10-hud](10-hud.md)). |
| Doors | A door another player pushes opens and closes on your screen too, instead of him walking through a closed door. A player leaving or arriving by a loading zone walks out of sight instead of freezing or sliding across the room. |
| Talks | What a talk changes in the world is the same for all (Mido letting you pass, a Kokiri who moved, a deal done once); greetings and "already talked" stay each player's. The others see it once they cannot watch it jump (out of the scene, or the townsperson far or gone). The townsperson you talk to is yours while you talk, so the others see your talk on their copy. |
| Warp songs | When a mate leaves by a warp song, a notice says where; holding L for the 10 s the invitation stays open takes you there too. |
| Kit lists | Rocks, walls, grotto covers, blockers, temple Gorons, minigame prizes, trade chains, bottles, beans and the rest of the overworld state are shared by part of the game (table below). |
| Items | Items, upgrades, bags, wallets (their size) and bottles are shared; rupees, ammo, bottle contents, worn gear, item buttons, life and magic now are each player's. The magic meter is filled for everyone when it grows. Everyone's rupees are shown in the HUD. |
| Hearts | A heart container anyone takes raises every Link's hearts, and the highest value seen is kept even if the game writes a lower one back. |
| Clock | One clock for the group, owned by the host; the others' clocks are nudged smoothly to it (the sky runs a little faster or slower), not jumped. The Sun's Song or a night's sleep jumps everyone. |
| Epona | One player wins her at Lon Lon and every game has her; each player calls and rides his own; Ingo's race waits for the group. |
| Pause | A mate in a menu stands still with "(pausa)" on his tag and gives up his enemies; one on the title screen is not given objects. |

## Switches

Kit default is the line as shipped in `tools/release/kit/coop-options.ini`. Code default is what the code does with the variable unset. On/off switches take 0/1/true/false/yes/no/on/off (any case); an unknown value keeps the default and logs `coop: switch NAME=VALUE is not ...`. Old `LICHII_COOP_*` names are read when the `LICHII_ORM_*` name is unset, where noted.

### Water levels

| Variable | Kit default | Code default | Values | What it does |
|---|---|---|---|---|
| `LICHII_ORM_WATER` (old `LICHII_COOP_WATER`) | `LICHII_ORM_WATER=1` | off (`coop_hook.cpp`) | on/off | Share the water levels, with the swim hold |
| `LICHII_ORM_WATER_MAX_WAIT_S` (old `LICHII_COOP_WATER_MAX_WAIT_S`) | commented out (`# ...=20`) | 20 | 0 to 3600; 0 waits for ever | Longest wait under a swimming Link |
| `LICHII_ORM_WATER_JABU` (old `LICHII_COOP_WATER_JABU`) | commented out | off | on/off | Also Jabu-Jabu's room 14 (upstream left it out: its step switches set it both ways) |

### Held floor switches

| Variable | Kit default | Code default | Values | What it does |
|---|---|---|---|---|
| `LICHII_ORM_HELD_SWITCHES` (old `LICHII_COOP_HELD_SWITCHES`) | commented out (unset) | `flag` (`coop_held_switch_game.cpp`) | `flag`; `log` (watch and log only); `1`/on/`press` (the flag and the actor words of a list); `0`/off | How a switch another player stands on is pressed here |
| `LICHII_ORM_HELD_SWITCH_LIST` (old `..._HELD_SWITCH_LIST`) | commented out | the built-in list | a file path | More switches and their actor words |
| `LICHII_ORM_HELD_SWITCH_PROBE` (old `..._HELD_SWITCH_PROBE`) | commented out | off | on/off | Debug: log the words of a switch our Link steps on or off, as list lines |
| `LICHII_ORM_FLAG_PROBE`, `LICHII_ORM_FLAG_PROBE_NAMES` | not in the kit | off | on/off; flag names | Debug: log each change of chosen flags and say when one bounces between games (a `held:` candidate) |

### Chests and doors

| Variable | Kit default | Code default | Values | What it does |
|---|---|---|---|---|
| `LICHII_ORM_CHEST_OPEN` | not in the kit | on (`coop_chest_open.cpp`) | on/off | The lid on every screen (`CHEST_OPEN`) and the find over the puppet |
| `LICHII_COOP_DOORS` | not in the kit | on; only `0` turns it off (`coop_doors_game.cpp`) | `0` or anything | The door mirror |
| `LICHII_COOP_PASSAGES` | not in the kit | on; only `0` turns it off (`coop_puppet_config.cpp`, `coop_fairy.cpp`) | `0` or anything | Puppets walking out of and into loading zones |

### Talks

| Variable | Kit default | Code default | Values | What it does |
|---|---|---|---|---|
| `LICHII_COOP_TALK` | `LICHII_COOP_TALK=shared_talk.txt` | unset: no talk flags shared (`coop_flags.cpp`) | a file path | The talk flags to share, with the rule that holds them while seen |
| `LICHII_COOP_FLAGS_DEFER` | commented out | off; on by itself with `LICHII_COOP_TALK` (`coop_hook.cpp`) | on/off | The "wait while seen" rule for the built-in talk rules alone |
| `LICHII_COOP_FLAGS_DEFER_LIST` | commented out | none | a file path | More or overriding defer rules |
| `LICHII_COOP_NPC` | `LICHII_COOP_NPC=1` | off (`coop_objects.cpp`) | on/off | The talk claim: the townsperson you talk to is yours while you talk and 1.5 s after |
| `LICHII_COOP_FLAG_BURST_MS` | not in the kit | 100 | milliseconds; 0 sends every frame | The flag changes of one burst go out as one message |

The townsperson's pose and gaze on the others' screens (`LICHII_COOP_NPC_POSE`, `LICHII_COOP_NPC_GAZE`) are part of [11-puppets](11-puppets.md).

### Warp songs

| Variable | Kit default | Code default | Values | What it does |
|---|---|---|---|---|
| `LICHII_COOP_WARP_INVITE` | `LICHII_COOP_WARP_INVITE=1` | off (`coop_songs_game.cpp`) | on/off | The warp-song invitation |
| `LICHII_COOP_WARP_INVITE_BUTTON` | not set (comment names `R`) | `L` | `L`, `R`, `ZL`, `ZR`, `PLUS`, `MINUS`, `LCLICK`, `RCLICK` | The button to hold |
| `LICHII_COOP_WARP_INVITE_FALLBACK` | not in the kit | the twin on the other side of the pad | the same names, not the main button | Used if a check finds the game uses the main button |
| `LICHII_ORM_LIVE_CHECK` | not in the kit | on (`coop_songs_game.cpp`) | on/off | Read-only check that a world flag moves its actor live (first user: the Song of Time blocks) |

### Kit lists and items

| Variable | Kit default | Code default | Values | What it does |
|---|---|---|---|---|
| `LICHII_COOP_DUNGEON_LISTS` | the nine `shared_dungeon_*.txt` files, comma-separated | none (`coop_hook.cpp`) | comma-separated paths, or a folder (`.`: every `shared_dungeon_*.txt` in it) | Per-part flag lists on top of upstream's built-in lists |
| `LICHII_ORM_ITEM_RULES` (old `LICHII_COOP_ITEM_RULES`) | `LICHII_ORM_ITEM_RULES=shared_items.txt` | none: upstream's item rules (`coop_flags.cpp`) | a file path | Item rule changes |
| `LICHII_ORM_TRADE_GUARD` | not in the kit | on (`coop_hook.cpp`) | on/off | Biggoron's chain step never goes back under a player holding a timed trade item |
| `LICHII_ORM_BEAN_CHECK` | not in the kit | on | on/off | Read-only: checks per visit that a soil's shared Bool means "a bean is planted" |
| `LICHII_ORM_PRIZE_ARBITER` | not in the kit | on | on/off | Two players winning one prize at once: the lowest player number keeps it, the other gives his back |
| `LICHII_ORM_HEARTS` | `LICHII_ORM_HEARTS=1` | off (`coop_hook.cpp`) | on/off | The heart capacity guard (upstream's `LICHII_COOP_HEARTS_TO_LINK` raise stays as it is) |

### Clock, Epona, pause

| Variable | Kit default | Code default | Values | What it does |
|---|---|---|---|---|
| `LICHII_ORM_CLOCK` (old `LICHII_COOP_CLOCK`) | commented out (`# ...=0` shown) | on (`coop_clock.cpp`) | on/off | One shared clock instead of upstream's time rules |
| `LICHII_ORM_CLOCK_NUDGE` (old `..._CLOCK_NUDGE`) | commented out | on | on/off | Off: the clock is set, not nudged |
| `LICHII_ORM_CLOCK_TEST_OFF`, `LICHII_ORM_CLOCK_TEST_JUMP` (old names read) | commented out | none | `N@D`: N seconds, D seconds after the start | Tests only: move our clock quietly, or jump it as the Sun's Song would |
| `LICHII_ORM_CLOCK_TEST_PUT_BACK` | not in the kit | off | on/off | Tests only: undo each nudge, as a game that keeps its own count |
| `LICHII_ORM_EPONA` (old `LICHII_COOP_EPONA`) | commented out (`# ...=0` shown) | on (`coop_epona.cpp`) | on/off | Epona is the group's; the race waits |
| `LICHII_ORM_MENUS` | not in the kit | on (`coop_menus.cpp`) | on/off | Our pause and title screen go out in `PLAYER_STATUS` |
| `LICHII_ORM_PAUSE_DETECT` | not in the kit | flag first | `flag` (the flag alone), `either` (flag or a frozen frame until the flag is seen); anything else: flag first | How our own pause is detected |

## How it works

Most parts are a pure rule in `coop/host` (no memory, no sockets, unit-tested) and a glue file in `lichii/hook` that reads the game and carries out what the rule asks. Nothing below changes the wire protocol except `CHEST_OPEN`, the `rupees` equip component and the `at_title` status bit.

### Water levels

| File | What it does |
|---|---|
| `coop/host/water.hpp`, `water.cpp` | The table of water bodies (scene, level flag, Int or Bool); `merge_water` adds the level flags to the shared list and takes off the Water Temple's one-hot Bools; `WaterGuard` holds a level from another player while our Link, in that water's scene, is in an event, swims, dives, falls, jumps or climbs, writes it when he stands or after the limit, and derives the one-hot Bools from the level Int in each game (never sent: two players changing the level at once could otherwise leave the Int from one and the Bools from the other). It also watches whether a written level moved the loaded water planes ("live") or applies on the next load |
| `lichii/hook/coop_hook.cpp` | Session start: reads the switches and merges the flags; each frame: `received`, `derive` and the guard's notes into the log |

### Held floor switches

| File | What it does |
|---|---|
| `coop/host/held_switch.hpp`, `held_switch.cpp` | `HeldSwitchList` (the switches: placement hash, place, actor, area; built in plus a list file), `HeldSwitchRule` (who stands on what, from the others' positions as presence has them; no new message), `HeldFlagApply` (the switch's own "Link on it" flag, the `held:` lines of `shared_dungeon_adult.txt`, written 1 while others hold it), `HeldWordApply` (actor words, `press` mode only) |
| `lichii/hook/coop_held_switch.cpp`, `.h`, `coop_held_switch_game.cpp` | Glue: the switch actors from the objects walk, the mode, the probe |

A held switch is on only while something stands on it, and the game keeps that in the actor, not in a flag. The puppets have no physics, so without this nothing stands on B's switch while A's Link stands on A's.

### Chests and doors

| File | What it does |
|---|---|
| `coop/host/chest_open.hpp`, `chest_open.cpp`, `chest_list.inc` | The chest table (every chest placement in the romfs, what it gives, the notice words, the shared flag of each find, the stand-in model over the puppet's head); `ChestLids` (ours: our Link's chest move and the chest beside it changing to an opening command is told once as `CHEST_OPEN`; theirs: play the same command on our chest of that placement, its frame carried on by the time since); `ChestFinds` (who found what, the stand-in while his puppet holds it up, the notice once) |
| `lichii/hook/coop_chest_open.cpp`, `.h`, `coop_chest_open_game.cpp` | Glue: sends and receives `CHEST_OPEN`, plays the lid, shows the find |
| `coop/host/doors.hpp`, `doors.cpp` | The door mirror rule: when a remote player's AS command (already in `PLAYER_SAMPLE`) becomes a door or chest command of Link's AS, the nearest door or chest of the right kind near his puppet plays its own opening command, then its closing one. Never on one beside our own Link, never on one that shows open already |
| `lichii/hook/coop_doors.cpp`, `.h`, `coop_doors_game.cpp` | Glue: reads the door actors and makes the calls; with `CHEST_OPEN` on, chests are left to it |
| `coop/host/passages.hpp`, `passages.cpp` | A player leaving our place is drawn on his walk into the exit, then parked out of sight, then let go; one arriving is kept out of sight until he has moved away from where he was elsewhere (no slide across the room). Called by the puppet and fairy planners |

What a find gives still travels as the shared item flags and the chest's opened flag, as upstream does.

### Talks

| File | What it does |
|---|---|
| `coop/host/npc_talk.hpp`, `npc_talk.cpp` | `NpcTalk` finds the townsperson our player talks to (from "an event runs", our Link's place and the shared actors; big or far ones such as Biggoron or King Zora through their event component or a per-name reach). The hook then claims it (`OBJECT_PRESENT` flags it targeting us, so the server hands it to us) and stops writing the old owner's pose into our copy |
| `coop/host/flag_defer.hpp`, `flag_defer.cpp` | Talk flag rules: a talk flag from another game waits while its townsperson is in view here, and is written once he is out of view, far or gone (no townsperson jumping in front of you). Rules from text: names, hashes, options |
| `lichii/hook/coop_flags.cpp` | Loads `LICHII_COOP_TALK` (the talk list: flags and their rules) |
| `tools/release/kit/shared_talk.txt` | The talk flags that are the world's |
| `coop/host/flag_burst.hpp`, `flag_burst.cpp` | Bandwidth: a scene load sets dozens of flags over a few frames; the first change goes at once, the rest of a 100 ms window together |

### Warp songs

| File | What it does |
|---|---|
| `coop/host/songs.hpp`, `songs.cpp` | The thirteen songs as data (the item that says one is known, the event that teaches it, where a warp song lands), what each song changes for good (the flag list `shared_dungeon_songs.txt`, checked against it in the tests) and the invitation: a mate who leaves our scene by a warp song can be followed for 10 s by holding a button |
| `lichii/hook/coop_songs_game.cpp`, `.h` | Glue: sees a mate land on a warp pad, offers the trip, reads the held button, starts the trip; checks that the button moves nothing of our Link (`comunicacion-aqui` checks it), with a fallback button |
| `coop/host/live_check.hpp`, `live_check.cpp` | Read-only: after a watched flag changes, counts the watched actors to say whether the flag moved them at once ("live"), only on the next load, or that a key is missing ("unexplained") |

A warp song takes only who plays it (the rule of our N64 co-op); the follower's trip starts during an event, since the game takes a scene change only while an event runs.

### Kit lists

`coop/host/dungeon_lists.hpp`, `dungeon_lists.cpp` read plain text files applied on top of upstream's built-in lists (`coop_dungeon.cpp`, `coop_chests.cpp`). A line is one flag: `Name` (shared, live), `key64:0x...` (a one-time key), `once:Name` (a 1 is never undone by another game's 0 or a load), `hold:Name Place` (held while our Link is in Place, so a played gimmick does not reach a game whose area is loaded), `unshare:Name` (taken off a built-in list: it says where one player is, not what the group did), `toggle64:0x... Place` (a key that goes both ways, held while we are in Place), `held:Name Dungeon` (a held switch's flag, see above), `enum:` (an Enum flag with its values in order). A flag has one kind across all files; a flag under two kinds is an error.

| File | What it shares |
|---|---|
| `shared_dungeon_child.txt` | The child blocks: Mido and the Kokiri blockers, the Meadow's gate, bombable rocks, walls and grotto covers of the child overworld, Goron City's torches and Darunia's door, Zora's River waterfall; one player's dungeon sensors taken off |
| `shared_dungeon_adult.txt` | The adult overworld (Gerudo carpenters, desert gate, Skulltula House rewards, Door of Time), the adult dungeons' one-player sensors taken off, and the held switches' `held:` lines |
| `shared_dungeon_minigames.txt` | Each player plays a minigame in his own game; a prize given once (heart piece, bag, quiver, scale, bottle) is given once for the group; Biggoron's trade step goes with the trade item |
| `shared_dungeon_songs.txt` | What the songs change for good: the frogs, Song of Time blocks that go for good, Pierre |
| `shared_dungeon_temples.txt` | Fire, Forest and Water temples: a Fire Temple Goron freed by one player is gone from his cell for everyone |
| `shared_dungeon_montana.txt` | Death Mountain, Goron City, Ice Cavern, Shadow Temple, Bottom of the Well: adult rocks, the big pot, the rolling Gorons, Link the Goron stopped once |
| `shared_dungeon_towns.txt` | Kakariko's ReDead grotto, the Graveyard's Poes and royal tomb, Ruto's letter, the lake's water held while we are in Hyrule Field, King Zora's ice held while we are in the Domain, the castle gate |
| `shared_dungeon_collectibles.txt` | A loose heart piece one player takes is gone for all; a bean one child plants is planted for all; the Lost Woods scrub's two upgrades are sold once |
| `shared_dungeon_trades.txt` | A bottle handed out once is handed out once for the group; bottle contents stay each player's; the Mask Shop and Biggoron chains are one for the group; the Odd Mushroom, Eyeball Frog and Eye Drops stay with who got them (their timer runs in his game) |
| `shared_items.txt` | Item rules (below), read by `LICHII_ORM_ITEM_RULES` |
| `shared_talk.txt` | Talk flags (above), read by `LICHII_COOP_TALK` |
| `shared_counts.txt` | Counted Ints for the endgame, read by `LICHII_ORM_COUNTS`: see [05-bosses](05-bosses.md) |
| `shared_story.txt` | Story cutscenes: see [07-cutscenes](07-cutscenes.md) |
| `shared_time_temple.txt` | Temple of Time rules: see [08-age](08-age.md) |

The lists were written from the romfs by our scripts (the names are checked against `GameData/GameDataList`). `make_kit.ps1` copies every `shared_*.txt` by pattern.

### Items, wallet, hearts

| File | What it does |
|---|---|
| `coop/host/item_rules.hpp`, `item_rules.cpp` | Changes to upstream's item table from text: `RULE FLAG` (max, any, mirror, count or player), `cap COUNT FLAG` (a gain stops at another Int, a full wallet), `fill FROM TO` (a shared max raised by another player fills a per-player Int: the magic meter) |
| `coop/host/src/game_items.cpp` (upstream file) | Applies the rules, the caps and the fills |
| `coop/host/wallets.hpp`, `wallets.cpp` | Each player's wallet stays his (`PlayerStatus.CurrentRupee` per player); a game's own count goes out in `PLAYER_EQUIP`'s `rupees` component; receivers only show it in the HUD, never write it into their game. The kit list has the common wallet as two commented-out lines for whoever wants it back |
| `coop/host/trades.hpp`, `trades.cpp` | The trade step guard (`LICHII_ORM_TRADE_GUARD`) |
| `coop/host/beans.hpp`, `beans.cpp` | The bean soil check (`LICHII_ORM_BEAN_CHECK`) |
| `coop/host/prizes.hpp`, `prizes.cpp` | The double-win arbiter (`LICHII_ORM_PRIZE_ARBITER`); the watcher keeps another game's equal value for it (`FlagWatcher::take_agreed`) |
| `coop/host/hearts.hpp`, `hearts.cpp` | The game draws hearts from Link's Life component, not from the `MaxLife` flag (a copy written back within a quarter second). This keeps the highest of our Link's max, the flag, and every value another player sent, and raises Link's component to it |

### Clock

| File | What it does |
|---|---|
| `coop/host/clock.hpp`, `clock.cpp` | The host owns the shared clock (or the lowest-numbered mate outdoors while the host's clock stands); it sends it in upstream's `TIME_OF_DAY` about once a second, every other game reports its own. Followers nudge their clock by a few in-game seconds per tick (at most a set skew of its rate); the game's setter is used only on joining, arriving outdoors after standing, as a last resort, and for a jump (Sun's Song, a night's sleep, a scene that sets the time) |
| `lichii/hook/coop_clock.cpp`, `.h` | Glue: the nudges through the svc cave, the test switches |
| `lichii/hook/coop_time.cpp` (upstream file) | Uses our clock instead of upstream's rules when `CoopClock::On()` |

Upstream's way set the others' clock with the game's setter, which runs the game's time change (the area's grass and objects are made again, the clock stops some seconds; upstream issue 159).

### Epona

| File | What it does |
|---|---|
| `coop/host/epona.hpp`, `epona.cpp` | `Step_Get_Epona` shared forward only and only its last value (Complete); a game whose own race or escape runs holds a Complete from elsewhere until it is over; every horse, Ingo and the horseback archery stay each game's own |
| `lichii/hook/coop_epona.cpp`, `.h` | Glue (the rider's horse drawing is part of [11-puppets](11-puppets.md)) |

### Pause and title screen

| File | What it does |
|---|---|
| `coop/host/menus.hpp`, `menus.cpp` | From `Sequence_IsPause` (set by the game's pause, map, item wheel and save screen) and whether a Link and a scene are there: our `PLAYER_STATUS` (paused, or at the title screen); for others' statuses the "(pausa)" tag and chat notices |
| `coop/host/pause_detect.hpp`, `pause_detect.cpp` | A second detector for our own pause: a frozen frame, used only while the flag cannot be read (a game run showed the flag is set 0.5 s after the menu opens, and that an idle Link looks frozen) |
| `lichii/hook/coop_menus.cpp`, `.h` | Glue |
| `server/session.cpp`, `server/enemies.cpp` | A paused owner gives his enemy to a standing player after 1 s in a fight |

### Hook points in upstream files

| File | Hook |
|---|---|
| `lichii/hook/coop_hook.cpp` | Session start: dungeon lists, water, talk list and defer rules, flag burst, Epona's step, the three guards' log line; each frame: water, held switches (after `CoopObjects::Share`), doors, chest lids, songs' blocks, menus, Epona, trade/bean/prize guards, hearts; network thread: `CoopChestOpen::OnEvent`, `CoopEpona::OnEvent` |
| `lichii/hook/coop_flags.cpp` | Loads `LICHII_ORM_ITEM_RULES` and `LICHII_COOP_TALK` |
| `lichii/hook/coop_objects.cpp`, `coop_objects_sync.cpp` | The talk claim, the actor list the held switches and doors read, chest lids kept shown open when the opened flag comes |
| `lichii/hook/coop_time.cpp` | The shared clock in place of upstream's time rules |
| `lichii/hook/coop_world.cpp` | `Stepswitch_` (lower-case s) in upstream's gimmick probe, which never matched the held switches |
| `coop/host/src/flag_watcher.cpp`, `flags.cpp`, `game_flags.cpp`, `game_items.cpp` | Kinds of list lines (`once`, `hold`, `toggle64`, `held`, `enum`), refused writes retried each frame (the guards), agreed values for the prize arbiter, item caps and fills |

## Messages and components

| Name | Number | Use |
|---|---|---|
| `CHEST_OPEN` | type `0xF009` (`orm_types.hpp`), channel 0 | Player number, chest placement hash, sender's scene, the chest's animation (command, frame, rate), server time. Relayed to everyone else, nothing kept |
| `PLAYER_EQUIP` component `rupees` | 29 (`equip_component::rupees`) | A player's own wallet, u32 |
| `PLAYER_STATUS` bit `at_title` | bit 7 (`orm_status::at_title`) | Title screen or file select; sent with upstream's `loading` bit |
| `TIME_OF_DAY` (upstream) | upstream's type, with its flag bits | The shared clock and jumps |
| `OBJECT_PRESENT` (upstream) | upstream's | The talk claim |

Everything else in this feature travels as ordinary shared flags.

## Tests

| File | What it checks |
|---|---|
| `tests/host/water_test.cpp` (24 cases) | Table hashes; the shared list gets the levels and loses the one-hot flags; the swim, event, fall and climb holds; the max wait and 0; Jabu only when asked; one-hot flags derived; live or next-load lines; a late joiner; iron boots on the bottom count as standing; a mutant without the rule |
| `tests/host/held_switch_test.cpp` (21), `tests/lichii/held_switch_test.cpp` (3) | The built-in list and every `held:` flag of the adult list; the turned area; two and three players; our Link on it too; disconnects; another scene; reloads; the flag written and let go; the words; the glue reading presence |
| `tests/host/chest_open_test.cpp` (18), `tests/lichii/chest_open_test.cpp` (7) | Every chest once with what it gives, words and stand-in for every find; the lid opened once at the sender's frame |
| `tests/host/doors_test.cpp` (10), `tests/lichii/doors_test.cpp` (11), `tests/host/passages_test.cpp` (6) | Door and chest commands from Link's AS and the door actors' AS; the mirror's push then close; no door, a gone actor, a scene change; walking out and in |
| `tests/host/npc_talk_test.cpp` (27), `tests/host/flag_defer_test.cpp` (17), `tests/host/flag_burst_test.cpp` (5) | Who counts as a townsperson, the nearest in reach, big and far ones; defer rules from text and the built-in Kokiri rules; bursts |
| `tests/host/songs_test.cpp` (18), `tests/host/live_check_test.cpp` (8) | The thirteen songs, effects and rules, the kit's song list says the same, the invitation; live, not live, unexplained |
| `tests/host/dungeon_lists_test.cpp` (13), `tests/host/kit_lists_test.cpp` (5), `tests/lichii/adult_list_test.cpp`, `collectible_list_test.cpp`, `minigame_list_test.cpp`, `montana_list_test.cpp`, `temples_list_test.cpp`, `town_list_test.cpp`, `trade_list_test.cpp` | Each kind of line; one kind per flag; every kit list has a reader and parses; no flag with two rules across lists; each list's content |
| `tests/host/item_rules_test.cpp` (9), `tests/orm/wallet_test.cpp` (11), `tests/host/trades_test.cpp` (5), `tests/host/beans_test.cpp` (4), `tests/host/prizes_test.cpp` (7), `tests/host/hearts_test.cpp` (12) | Rules, caps, fills; the kit list keeps each wallet its own, three players at once; the trade step guard; the soil cases; double wins; the heart capacity kept one way up |
| `tests/host/clock_test.cpp` (18), `tests/host/epona_test.cpp` (9) | Host and guest clocks, nudges, jumps, midnight; only Complete shared, the race hold |
| `tests/host/menus_test.cpp` (14), `tests/server/menus_test.cpp` (7), `tests/host/pause_detect_test.cpp` (7) | Status bits per state; a paused owner gives up his enemy after 1 s; the detector's modes |

Harness scenarios (they live in `tools/orm/harness`): `meca-agua-templo` (A plays the three triforce events; B gets the level, derives the one-hot Bools, and the probe says whether B's water planes moved), the other `meca-agua-*`; `meca-sostenido-fuego`, `-espiritu`, `-agua`, `-dodongo`, `-jabu`, `-deku`, `-bosque` (A stands on the switch 15 s, B watches the door from 2.5 m); `efectos-cofre` (A opens the Deku Tree's slingshot chest, B sees the lid, the slingshot over A's puppet and the notice); `puertas-*`; `charla-dos` (two players talk to the same Kokiri), `charla-mido` (Mido's blocker flag reaches B once Mido is A's copy), `charla-saria` and the other `charla-*`; `viaje-cancion` (B plays the Minuet in Kakariko, A holds L and lands beside him), `comunicacion-aqui` (L is free while playing); `cartera-cada-uno` (A and B set their own wallets, each sees both); `corazon` (a heart container raises both Links); `hora-salto`, `hora-sigue`, `hora-sin-empujon`; `epona-*`; `menu-pausa` (A's puppet held, not hidden, while A's menu is open; a shield change still shows), `menu-salir`; `pueblo-*`, `templo-*`, `montana-*`, `trueque-*`, `botella-*` for the lists.

## Log lines

| Line | Says |
|---|---|
| `coop: water: on: N level flags added, N taken off (the one-hot flags are derived here); a level waits while our Link swims or is off his feet, at most 20 s` | Water on |
| `coop: water: <flag> (<what>) waited N s: written, ...`, `coop: water: <flag>: written in <scene> after the wait, live (...)` / `...; its mover reads it at load ...` | The hold and whether it moved live |
| `coop: held switch: on: switches others stand on are pressed here with their flag (...)` / `on: switches others stand on are pressed here` / `watching only (...)` | Held switch mode |
| `coop: held switch: <name> held by another player (P2): pressed here` / `would be pressed here (watching only)`; `... no other player on it: let go here` | Each press and release |
| `coop: held switch: flag 0x........ = 1 ...` | The flag path wrote |
| `coop: chests: a chest's lid opens on every screen (CHEST_OPEN) ...`, `coop: chests: CHEST_OPEN from P2: ..., '<command>' at frame N` | Chest lids on, a lid received |
| `coop: doors and chests ...` | Door mirror |
| `coop: talk: sharing N talk flags (N new)`, `coop: flags: talk flags wait while seen, N rules` | Talk list and defer rules |
| `coop: npc: talking to <name> 0x... (...), found <how>` / `... talks with its owner P2: not claimed, our talk ...` | The talk claim |
| `coop: songs: warp-song invitation on (LICHII_COOP_WARP_INVITE=1): hold L to follow a mate who leaves by a warp song` / `off (...)` | Invitation on or off |
| `coop: songs: our Link arrives by a warp song in <scene> at x y z (event ...)` | A trip landed |
| `coop: lists: LICHII_COOP_DUNGEON_LISTS <files>: <why>; the lists are not used` | A list file failed |
| `coop: items: rules from <file>: N changed, N as built, N caps, N fills` / `coop: items: LICHII_ORM_ITEM_RULES ...; the built-in item rules are used` | Item rules |
| `coop: wallet: P1 'Ana' (you) has 87 rupees` | Each wallet a game knows |
| `coop: riesgos: trade step guard on, bean check on, prize arbiter on` | The three guards |
| `coop: hearts: the group's capacity kept one way up (LICHII_ORM_HEARTS), before the object walk's raise` | Hearts guard on |
| `coop: clock: ours ... shared ... off N s, ...` and the test lines `coop: clock: test: ...` | Clock state |
| `coop: epona: Step_Get_Epona = Complete shared forward (the group's Epona): ...` / `... not shared (LICHII_ORM_EPONA=0): each game wins its own`; `coop: epona: our race with Ingo runs: the group's Epona waits until it is over` | Epona |
| `coop: menus: on (LICHII_ORM_MENUS): our pause and title screen are sent, a paused mate stands frozen with '(pausa)' on the tag`; `pause detector: ...` | Pause |

## Status

| Part | Status |
|---|---|
| Water levels | **confirmed in game on our test9-based build (scenario `meca-agua-templo`)** |
| Held floor switches, flag path | **confirmed in game on our test9-based build (scenarios `meca-sostenido-fuego`: the flag alone opens the Fire Temple's fire wall; `meca-sostenido-espiritu`)** |
| Held switches, `press` words | not yet confirmed in game |
| Chest lid and find | **confirmed in game on our test9-based build (scenario `efectos-cofre`)** |
| Door mirror, passages | not yet confirmed in game |
| Talk flags and talk claim | **confirmed in game on our test9-based build (scenarios `charla-dos`, `charla-mido`, `charla-saria`)** |
| Warp-song invitation | **confirmed in game on our test9-based build (scenarios `viaje-cancion`, `comunicacion-aqui`)** |
| Kit lists (child, adult, minigames, songs, temples, montana, towns, collectibles, trades) | not yet confirmed in game as lists (unit-tested; the `held:` lines ran with the held switches) |
| Item rules, magic fill | not yet confirmed in game |
| Wallet per player | **confirmed in game on our test9-based build (scenario `cartera-cada-uno`)** |
| Hearts guard | **confirmed in game on our test9-based build (scenario `corazon`)** |
| Trade guard, bean check, prize arbiter | not yet confirmed in game |
| Shared clock | not yet confirmed in game |
| Epona of the group | not yet confirmed in game |
| Pause status | **confirmed in game on our test9-based build (scenario `menu-pausa`)** |

## Known gaps

- Held switches that need the actor's own state (Spirit Temple room 08: the flag does not move its bars) need the words found with the probe; candidate words were seen but are not programmed (writing a contact without its pointer could crash the game).
- Water Temple room 14's switch is pushed with the iron boots; the other game does not know which boots a mate wears. Jabu-Jabu's room 17 stone stays out.
- Whether every list flag moves its actor live or only on the next load is per flag; `LICHII_ORM_LIVE_CHECK` and the water "live" lines say it in a run.
- The Song of Time blocks: the `meca-cancion-tiempo` probe did not move the block yet.
- Bean meaning: the soil Bool read as "planted" is deduced, checked by the bean check in a run.
- Clock: the host's clock runs only outdoors; with nobody outdoors it stands.
