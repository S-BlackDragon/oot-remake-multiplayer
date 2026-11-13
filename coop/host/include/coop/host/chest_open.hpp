// ORM (oot-remake docs/DETALLES_MARIONETA.md, "Cofres"; 2026-11-11:
// "cuando alguien abra un cofre todos ven cómo lo abre y cómo descubre el
// objeto; si uno descubre un objeto, todos lo descubren, como en oot-64").
//
// Three game-free pieces, tested in tests/host/chest_open_test.cpp:
//
// 1. The chest table (chest_list.inc, written from the romfs by
//    oot-remake/tools/romfs/chest_list.py): every chest placement with what
//    it gives, and what each of those things is: its words for the notice
//    (Spanish), the shared flag that brings it to every game, and the
//    model-only stand-in shown over a puppet's head (CoopGet_*, made by
//    tools/puppet_pack). So every game knows from a chest's placement hash
//    what came out of it, with nothing more on the wire.
//
// 2. ChestLids, the lid on every screen (CHEST_OPEN, type 249):
//    - ours: while our Link's AS command is a chest move (doors.hpp
//      door_move_of), the chest beside it whose AS slot 0 command changes to
//      an opening one is told once as CHEST_OPEN (its placement hash, our
//      scene, its command and frame, the server time);
//    - theirs: a CHEST_OPEN for our place plays the same command on our
//      chest of that placement, its frame carried on by the time since, so
//      the lid opens while the opener's puppet opens it. Not on a chest that
//      shows open here already, nor while our own Link opens that one too.
//    The lids we opened are remembered: the door mirror leaves them alone
//    and the objects sync keeps them (shown open) when the chest's shared
//    "opened" flag comes (coop_objects_sync.h SetLidsShown).
//
// 3. ChestFinds, the find on every screen: from each CHEST_OPEN, who found
//    what. While that player's puppet holds the find up (an item-get
//    command), the find's stand-in is shown over its head; and once, the
//    notice "<nombre> encontró <objeto>" (oot-64 D-049: the others see a
//    short line for 5 s, never the finder's text box; the HUD draws it).
//    A finder in another scene, or one whose pose never shows the item-get,
//    gets the notice kNoticeAfterMs after the CHEST_OPEN.
//
// What the find gives to each game is not here: it travels as the shared
// item flags (game_items.hpp, the kit's shared_items.txt) and the chest's
// opened flag (coop_chests, coop_dungeon), as before. chest_item_flag names
// the flag of each kind so the tests can check every kind is shared.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "coop/host/doors.hpp"
#include "coop/protocol/messages.hpp"

namespace coop::host {

// ---- 1. The chest table ----

struct ChestPlacement {
  std::uint64_t hash;      // the placement hash (the actor's, in the objects walk)
  const char* area;        // the Banc folder ("InsideTheDekuTree")
  const char* actor;       // the placement's Gyaml ("Obj_TBox_A_01")
  const char* drop;        // Drop__DropActor, "" none
  const char* check;       // Drop__CheckActor, "" none
  const char* substitute;  // Drop__DropActor_Substitute, "" none
};

// Every chest placement outside the Treasure Chest Shop (200), sorted by hash.
std::span<const ChestPlacement> chest_placements();
const ChestPlacement* find_chest(std::uint64_t hash);
// What the chest gives: its drop, or its substitute when it has no drop
// (the game gives the substitute when the player has the check actor
// already; the drop is the usual find). "" when the table has neither.
std::string_view chest_drop(const ChestPlacement& chest);

enum class ChestItemKind : std::uint8_t {
  item,         // an item or upgrade of the group (game_items.hpp: max or any)
  small_key,    // DungeonInfo.<area>.SmallKeyNum (a count)
  map,          // DungeonInfo.<area>.IsGetMap
  compass,      // DungeonInfo.<area>.IsGetCompass
  boss_key,     // DungeonInfo.<area>.IsGetBossKey
  heart_piece,  // PouchData.Obj_Heart_Piece.StockNum (a count)
  rupees,       // PlayerStatus.CurrentRupee: the kit's shared wallet (a count)
  ammo,         // each player's own (as oot-64): only the opener gets it
  own_shield,   // the Deku Shield (issue 130: shared again since test10, upstream e2132ffc)
  magic,        // a magic jar: each player's own meter
};
const char* chest_item_kind_text(ChestItemKind kind);

struct ChestItem {
  std::string_view drop;      // the drop actor ("Item_Dungeon_SmallKey")
  ChestItemKind kind;
  std::string_view words;     // for the notice: "una llave pequeña"
  // The flag that brings it to every game: an item flag's name, or for the
  // dungeon kinds the part after "DungeonInfo.<area>." ("SmallKeyNum").
  // "" for each player's own (ammo, magic).
  std::string_view flag;
  std::string_view stand_in;  // "CoopGet_Item_Dungeon_SmallKey"
};

// Every drop a chest of the table can give, sorted by drop.
std::span<const ChestItem> chest_items();
const ChestItem* chest_item(std::string_view drop);
// The full flag name of `item` from a chest in `area`
// ("DungeonInfo.ForestTemple.SmallKeyNum", "IsGet.Item_Hookshot"); "" none.
std::string chest_item_flag(const ChestItem& item, std::string_view area);
// ORM riesgos-visual: the counted flag a chest's find raises ("DungeonInfo.
// ForestTemple.SmallKeyNum", "PouchData.Obj_Heart_Piece.StockNum",
// "PlayerStatus.CurrentRupee"), for a chest our game opens second
// (FlagWatcher::expect_double_find); "" for a unique item (a second one is
// the same item), each player's own (ammo, magic), or a chest not in the list.
std::string chest_count_flag(std::uint64_t hash);

// The stand-in of a drop, as a view into the table; "" when not a chest find.
std::string_view chest_get_stand_in(std::string_view drop);
// Whether `actor` is a chest find (its stand-in is the only copy ever made:
// the game's own Item_* actor would give the item in another game).
bool is_chest_find(std::string_view actor);

// "Ana encontró una llave pequeña"; a drop not in the table: "Ana abrió un
// cofre"; an empty name: "Alguien ...".
std::string chest_found_text(std::string_view player_name, std::string_view drop);

// Whether Link's AS command holds a find up: DemoGet, DemoGetLoop,
// DemoGet_Smile, DemoGetLoop_Smile, EvtGet and their animations
// Demo_Get_Child/Adult[...]; not the chest kick (Demo_Get_Kick_TreasureBox_*)
// nor a bottle's catch (DemoGetBottleContent).
bool is_item_get_command(std::string_view command);

// ---- 2. The lid on every screen ----

struct ChestLidRules {
  float reach_m = 2.5f;                  // our Link this close to the chest it opens
  std::uint64_t open_wait_ms = 2500;     // after our chest move starts, its lid's change is looked for this long
  std::uint64_t resend_ms = 60000;       // one chest is told at most once in this time
  std::uint64_t inbox_keep_ms = 4000;    // a CHEST_OPEN whose chest is not loaded here waits this long
  std::uint64_t lid_keep_ms = 60000;     // the lids we opened are remembered this long
  // ORM riesgos-visual: two CHEST_OPENs of one chest this close in server time
  // are a tie: the lower player number keeps the find, the other leaves its out.
  std::uint64_t tie_us = 50000;
  std::uint64_t carry_max_us = 5000000;  // the frame is carried on by at most this much time
  float frames_per_second = 30.0f;       // AS frames a second (the game's calc rate)
  float min_carry_frames = 2.0f;         // a carry smaller than this plays from the frame as is
};

struct ChestLidInput {
  std::string scene;                   // ours; "" loading
  std::optional<protocol::Vec3> own;   // our Link
  std::string own_command;             // our Link's AS command ("" not read)
  std::uint64_t server_now_us = 0;     // 0: not connected
  std::uint8_t self = 0;               // ORM riesgos-visual: our player number (0 not known)
};

// One play to make on a chest (frame: set after the play), or a line only (actor 0).
struct ChestLidPlay {
  std::uint64_t actor = 0;
  std::string command;
  std::optional<float> frame;
  std::string line;
};

struct ChestLidOut {
  std::vector<ChestLidPlay> plays;
  std::vector<protocol::ChestOpen> send;  // player_number kPlayerSelf
  std::vector<std::string> lines;         // the plays' lines are in the plays
  // ORM riesgos-visual: chests (placement hashes) our game opens second:
  // another player's CHEST_OPEN came first (its server time earlier than
  // ours, or ours not sent), so our find of it must not count again.
  std::vector<std::uint64_t> second;
  // ORM riesgos-visual: how each tie of two CHEST_OPENs was settled (for the
  // self-check line "coop: check: chest tie ...").
  std::vector<std::string> ties;
};

class ChestLids {
 public:
  explicit ChestLids(ChestLidRules rules = {}) : rules_(rules) {}

  // Another player's CHEST_OPEN (relayed: player_number 1 to kMaxPlayers).
  void receive(const protocol::ChestOpen& m, std::uint64_t now_ms);
  // Once a frame, before deciding: our Link's command (a chest move starts
  // our wait for its lid).
  void update(const ChestLidInput& in, std::uint64_t now_ms);
  // Whether the chests must be read now: a CHEST_OPEN waits for its chest,
  // or our Link opens one.
  bool wants_actors(std::uint64_t now_ms) const;
  // Whether an actor at `at` is worth reading (any chest while a CHEST_OPEN
  // waits: it is found by its hash; one near our Link while ours opens).
  bool worth_reading(const protocol::Vec3& at) const;
  // With the doors and chests loaded now (DoorActor::hash and ::frame read
  // for chests): the lids to open here and the CHEST_OPEN to send. `busy`:
  // chests the door mirror is opening already (left alone).
  ChestLidOut decide(const ChestLidInput& in, const std::vector<DoorActor>& actors,
                     const std::vector<std::uint64_t>& busy, std::uint64_t now_ms);
  // Our scene changed: every chest is another one. The inbox stays (a
  // CHEST_OPEN for the place we come to still finds its chest there; one for
  // elsewhere goes after inbox_keep_ms).
  void reset();

  // A chest whose lid we opened for another player (by actor), and all of
  // them: the door mirror and the objects sync leave them as they are.
  bool lid_opened(std::uint64_t actor) const;
  std::vector<std::uint64_t> lids_opened() const;
  std::size_t waiting() const { return inbox_.size(); }
  const ChestLidRules& rules() const { return rules_; }

 private:
  struct Waiting {
    protocol::ChestOpen message;
    std::uint64_t got_ms = 0;
  };
  struct Lid {
    std::uint64_t actor = 0;
    std::uint64_t at_ms = 0;
  };

  ChestLidRules rules_;
  std::vector<Waiting> inbox_;
  std::vector<Lid> lids_;
  std::optional<protocol::Vec3> own_;
  // Our Link's chest move: since when (0 none), whether it was told, and the
  // chests' commands as first seen in it (by actor).
  DoorMove own_move_ = DoorMove::none;
  std::uint64_t own_move_ms_ = 0;
  bool own_waiting_ = false;  // a chest move started and its window is not over
  std::string own_move_command_;  // the command it started with, for the log
  bool own_told_ = false;
  bool own_said_ = false;  // the "no lid change seen" line, once a move
  std::map<std::uint64_t, std::string> before_;
  std::map<std::uint64_t, std::string> seen_;  // "<name> '<first>' -> '<last>'" of each chest in reach this move
  std::map<std::uint64_t, std::uint64_t> sent_;  // hash: when told
  std::map<std::uint64_t, std::uint64_t> sent_us_;  // hash: the server time we told it with
  std::uint8_t self_ = 0;  // our player number, from the input (0 not known)
  // Whether a CHEST_OPEN of `m` makes ours the second opening of its chest.
  bool second_to(const protocol::ChestOpen& m) const;
  // Its tie line when `m` and ours are a tie (tie_us), else "".
  std::string tie_text(const protocol::ChestOpen& m) const;
};

// ---- 3. The find on every screen ----

// Another player as the puppets have him.
struct FindMate {
  std::uint8_t number = 0;
  std::optional<std::string> scene;
  protocol::Vec3 position{};
  bool adult = false;
  std::string command;  // his AS command ("" none)
  float frame = 0.0f;   // its frame, for the log
  bool shown = true;    // his puppet is drawn here (not faded out)
};

// A find to show over a puppet's head.
struct HeldFind {
  std::uint8_t number = 0;
  std::uint32_t serial = 0;  // this find's, from 1: a new find is a new copy
  std::string drop;          // the copy's actor: chest_get_stand_in makes it the stand-in
  protocol::Vec3 position{};
};

struct FoundNotice {
  std::uint8_t number = 0;
  std::string name;   // the finder's
  std::string text;   // "Ana encontró una llave pequeña"
};

struct ChestFindRules {
  float above_child_m = 1.25f;          // the find's place over the puppet's feet
  float above_adult_m = 1.85f;
  std::uint64_t notice_after_ms = 4000; // no item-get seen by then: the notice anyway
  std::uint64_t held_max_ms = 8000;     // a find is held up at most this long
  std::uint64_t held_grace_ms = 300;    // an item-get that stops this long is over
  std::uint64_t forget_ms = 20000;      // a find never held up is forgotten after this
  // The remake (oot-remake docs/SONDAS.md, 2026-11-12): the finder's Link plays one command,
  // "DemoOpenTreasureBox", from the kick to the end of the find (no "DemoGet..." after it; the
  // event Dm_SY_Treasurebox runs it). While his command is still that one this long after his
  // CHEST_OPEN, he holds the find up (0: only an item-get command counts, as before).
  std::uint64_t chest_hold_after_ms = 2500;
};

// The chest-opening command the remake plays through the whole find ("DemoOpenTreasureBox",
// and an adult's "..._Adult" if it has one).
bool is_chest_open_command(std::string_view command);

struct ChestFindOut {
  std::vector<HeldFind> held;
  std::vector<FoundNotice> notices;
  std::vector<std::string> lines;
};

class ChestFinds {
 public:
  explicit ChestFinds(ChestFindRules rules = {}) : rules_(rules) {}

  // Another player's CHEST_OPEN (any scene): what he is about to find.
  void opened(const protocol::ChestOpen& m, std::uint64_t now_ms);
  // Once a frame: our scene, the others (as the puppets have them) and their
  // names (for the notice).
  ChestFindOut tick(const std::optional<std::string>& our_scene, const std::vector<FindMate>& mates,
                    const std::function<std::string(std::uint8_t)>& name, std::uint64_t now_ms);
  void reset() { finds_.clear(); }
  std::size_t pending() const { return finds_.size(); }
  const ChestFindRules& rules() const { return rules_; }

 private:
  struct Find {
    std::uint32_t serial = 0;
    std::string drop;
    std::string area;
    std::string scene;
    std::uint64_t got_ms = 0;
    std::uint64_t held_since_ms = 0;  // 0: not held up yet
    std::uint64_t held_last_ms = 0;
    bool said = false;
    bool by_chest_command = false;  // held up by the chest command's time (chest_hold_after_ms)
  };
  ChestFindRules rules_;
  std::map<std::uint8_t, Find> finds_;  // the newest by player
  std::uint32_t next_serial_ = 1;
};

}  // namespace coop::host
