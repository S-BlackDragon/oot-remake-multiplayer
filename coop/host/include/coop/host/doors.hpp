// ORM (oot-remake docs/PUERTAS.md, D-R12 one world): the doors and chests
// another player opens move in our game too. Each game runs its own door
// actors, and a door only moves for the Link that pushes it in that game; so
// a partner walked through our closed door, and a chest he opened stayed shut
// here until the shared "opened" flag removed it (docs/items.md, chests).
//
// What crosses already: the player's AS command, PLAYER_SAMPLE field 7. When
// it becomes one of the door or chest commands of Player_Child/_Adult (romfs
// AS, read 2026-11-11: DoorPushOpenL/R..., BigDoorPushOpenL/R, EvtDoorShutter,
// EvtDoorKey, EvtDoorBoss, EvtDoorJabu, DemoOpenTreasureBox..., Demo_Get_Kick_
// TreasureBox..., Demo_Front_OpenTreasureBox..., DemoOpenBox..._Mini), the
// nearest door or chest of the right kind in our game, within a reach of the
// puppet, plays its own opening command (AS Door: Push_L_ByPlayer / _R;
// DoorDungeon and DoorDungeonBig: Push_L / Push_R; the shutters: ToOpen; the
// big chest: FrontOpen, FrontOpen_Adult for an adult; the small one:
// FrontOpenSmall: AS commands, not their animations Open_Front_*), and once
// the player's command has moved on, the door's closing one (Close_L_ByPlayer / _R, Close_L / _R, ToClose) if the
// door still shows ours. A chest stays open.
//
// Never on a door or chest beside our own Link (our player may be using it;
// a chest within 3 m is the objects sync's to remove, docs/items.md), and
// never on one that already shows open here.
//
// Game-free: names, places and commands in, the plays to make out. The game
// side (lichii/hook/coop_doors.*) reads the actors and makes the calls.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "coop/protocol/messages.hpp"

namespace coop::host {

// What a player's AS command does to the door or chest in front of it.
enum class DoorMove : std::uint8_t {
  none,
  push_left,    // DoorPushOpenL...: a hinged door, left hand
  push_right,   // DoorPushOpenR...
  big_left,     // BigDoorPushOpenL: a big dungeon door
  big_right,    // BigDoorPushOpenR
  shutter,      // EvtDoorShutter, EvtDoorKey, EvtDoorBoss, EvtDoorJabu
  chest_kick,   // the big chest kicked open
  chest_front,  // a chest opened from the front
  chest_small,  // the small chest (..._Mini)
};
DoorMove door_move_of(std::string_view command);
const char* door_move_text(DoorMove move);

// What kind of door or chest an actor is, by its pack name (the lists in
// doors.cpp, read from the romfs packs' ASRef). Shop chests are not listed:
// each player plays that minigame for himself.
enum class DoorKind : std::uint8_t {
  none,
  hinge,        // AS "Door" (houses, shops) or "DoorKokiri"
  dungeon,      // AS "DoorDungeon" / "DoorDungeonBig"
  shutter,      // a SwitchShutter whose AS has ToOpen
  chest_small,  // AS Obj_TBox_A_01
  chest_big,    // AS Obj_TBox_A_02
};
DoorKind door_kind_of(std::string_view actor);
const char* door_kind_text(DoorKind kind);
inline bool is_chest(DoorKind kind) {
  return kind == DoorKind::chest_small || kind == DoorKind::chest_big;
}

// The commands a door of `kind` plays for `move`: its opening one and the
// closing one after (empty: none, a chest). Nothing: this kind does not
// answer that move (a shutter to a hand push, a door to a chest).
struct DoorPlay {
  std::string_view open;
  std::string_view close;
};
std::optional<DoorPlay> door_play(DoorKind kind, DoorMove move, bool adult);

// Whether a door already shows open with this command in slot 0.
bool door_shows_open(DoorKind kind, std::string_view command);

struct DoorRules {
  float reach_door_m = 2.0f;     // the puppet this close to a hinged or dungeon door
  float reach_shutter_m = 2.5f;  // ... to a shutter
  float reach_chest_m = 2.5f;    // ... to a chest
  float own_near_door_m = 1.5f;  // our Link this close: the door is left alone
  float own_near_chest_m = 3.0f; // as the objects sync's chest rule (docs/items.md)
  std::uint64_t close_door_ms = 300;     // after the player's command moved on
  std::uint64_t close_shutter_ms = 800;
  std::uint64_t start_wait_ms = 1000;    // a move whose door was not found by then is dropped
  std::uint64_t open_keep_ms = 20000;    // an opened door is forgotten after this
};

// A door or chest loaded in our game, as the game side read it.
struct DoorActor {
  std::uint64_t id = 0;  // the actor's address
  std::string name;
  protocol::Vec3 position{};
  std::string command;   // its AS slot 0 command now; "" not read
  // ORM cofre (chest_open.hpp): read for chests only. The placement hash
  // (0: not read, ~0: the game made it) and slot 0's frame.
  std::uint64_t hash = 0;
  float frame = 0.0f;
};

// Another player in our place.
struct DoorMover {
  std::uint8_t number = 0;
  protocol::Vec3 position{};
  std::string command;  // their AS command (PLAYER_SAMPLE field 7); "" none
  bool adult = false;
};

// One play to make (actor not 0), or a log line only (actor 0).
struct DoorWish {
  std::uint64_t actor = 0;
  std::string command;
  std::string line;
};

class DoorMirror {
 public:
  explicit DoorMirror(DoorRules rules = {}) : rules_(rules) {}

  // Once a frame: our Link (nothing: not read) and the others in our place.
  void update(const std::optional<protocol::Vec3>& own, const std::vector<DoorMover>& others,
              std::uint64_t now_ms);
  // Whether the door actors must be read now: a move waits for its door, or
  // a door is due to close.
  bool wants_actors(std::uint64_t now_ms) const;
  // Which actors to read: those within this of a waiting mover, and the
  // opened ones by id. For the game side's filter.
  bool worth_reading(const protocol::Vec3& at, std::uint64_t id) const;
  // With the door and chest actors loaded now (any kind; others ignored):
  // the plays to make, and a line for each choice.
  std::vector<DoorWish> decide(const std::vector<DoorActor>& actors, std::uint64_t now_ms);
  // Our scene changed: every door is another one.
  void reset();

  // ORM cofre: with CHEST_OPEN on (chest_open.hpp) a mate's chest move is left to his message: the
  // message carries the chest and its frame, the move only a guess by distance. Seen in a game
  // (efectos-cofre, 2026-11-12): the move opened the lid first and the message found it open.
  void set_chests_by_message(bool on) { chests_by_message_ = on; }
  std::size_t waiting() const { return starts_.size(); }
  std::size_t opened() const { return opened_.size(); }
  // The doors and chests we opened and still remember (by actor), for the
  // chest lids (chest_open.hpp), which leave them alone.
  std::vector<std::uint64_t> opened_ids() const;
  const DoorRules& rules() const { return rules_; }

 private:
  struct Start {
    std::uint8_t number = 0;
    DoorMove move = DoorMove::none;
    protocol::Vec3 position{};
    bool adult = false;
    std::string command;
    std::uint64_t at_ms = 0;
  };
  struct Opened {
    std::uint64_t id = 0;
    std::string name;
    std::uint8_t number = 0;
    DoorKind kind = DoorKind::none;
    std::string open;
    std::string close;
    std::uint64_t at_ms = 0;
    std::uint64_t done_ms = 0;  // when the player's move ended; 0 still on
  };
  struct Last {
    std::uint8_t number = 0;
    DoorMove move = DoorMove::none;
  };

  float reach(DoorMove move) const;
  bool close_due(const Opened& o, std::uint64_t now_ms) const;

  DoorRules rules_;
  bool chests_by_message_ = false;
  std::optional<protocol::Vec3> own_;
  std::vector<Last> last_;       // each mover's move at the last update
  std::vector<Start> starts_;    // moves waiting for their door
  std::vector<Opened> opened_;   // doors we opened
};

}  // namespace coop::host
