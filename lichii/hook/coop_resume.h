// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM resume (oot-remake docs/PARTIDA_SALA.md "Volver a tu sitio"; oot-64 D-054
// "Reanudar sesión"): a player who closes the game and comes back returns to
// his own last scene and place, not to where the room's save puts everyone.
// No emulator code: coop_resume_game.cpp reads the game, writes the file and
// asks the trip; this decides what is kept and where to go. Tested in
// tests/lichii/resume_test.cpp.
//
// - Our game keeps its last place (where our Link last stood on his feet,
//   CoopTeleport::Grounded) in one line of coop-resume.txt beside coop.ini,
//   with the room, the player's name, the age and whether it was a safe place
//   (Kind). The app's helper takes that line to the server after the game,
//   under the room and the name (devchannel "site"), and hands the newest one
//   (this PC's or the server's) to the next game as LICHII_ORM_RESUME_PLACE.
// - Never inside a cutscene or a boss fight: a place kept while an event ran
//   (a cutscene, a talk) or in a boss room (CoopBoss::RoomAt, or our fight
//   ran) is not a place to put Link back in. Then he goes to the entrance of
//   that scene (the start point nearest the place, as /teleport loads it) and
//   stays there; a boss fight that still runs pulls him in as it does for a
//   late player (coop_boss.h). In the same scene: he stays where the load put
//   him.
// - Once after the load: when our Link has stood with no event, no load and
//   no scene change asked by anyone for Returner::kSteadyUs. Not when the age
//   differs from the kept one, nor for a scene of the other age. Nothing is
//   written over the kept place before that decision and its trip are done
//   (a crash on the way keeps the old place).
// - Inside a dungeon (issue 197 of upstream, commit 1fdddda8): a dungeon is
//   one scene loaded one room at a time, and the game's scene change can
//   only land at a start point; every dungeon has start points in its
//   entrance room only (the others start cutscenes: boss, sage, stone), and
//   the game's own room change (a door) is not something we can ask. Writing
//   Link's place into a room that is not loaded left him held with a black
//   picture (upstream's bigrun4), so upstream refuses any move out of the
//   room we are in. So a last place in another room than the entrance's is
//   not reached; Link goes to the nearest safe point upstream allows: the
//   spot of the entrance room where he last stood before leaving it (Door,
//   the way out toward the rest of the dungeon), kept beside the place; with
//   none kept (another PC, an old file) he stays at the entrance. The move
//   is checked again against where the load really put him (CheckMove).
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "coop/protocol/messages.hpp"
#include "coop_rooms.h"
#include "coop_teleport.h"

namespace CoopResume {

using CoopTeleport::Vec;

// What the kept place is.
enum class Kind : std::uint8_t {
    ok,     // stood there in free play: put Link back there
    boss,   // a boss room or our boss fight: the scene's entrance
    event,  // an event ran when it was kept (a cutscene, a talk): the entrance
};

struct Place {
    std::string room;   // the room (LICHII_ORM_ROOM, else the server's address)
    std::string name;   // the player's name (coop.ini)
    std::string scene;  // the Banc
    Vec at;
    std::optional<coop::protocol::Age> age;
    Kind kind = Kind::ok;
    std::int64_t unix_s = 0;  // when it was kept (the PC's clock)
};

// One line, fields split by ';': "v1;room;name;scene;x;y;z;age;kind;unix_s",
// age "child", "adult" or "-", kind "ok", "boss" or "event". Room, name and
// scene pass through Clean.
std::string Format(const Place& place);
// Nothing for anything that is not such a line (another version, a field
// missing or bad, a number that is not finite).
std::optional<Place> Parse(std::string_view line);

// A field as written: ';', '#' and control characters become '_', spaces at
// the ends go, at most 64 bytes.
std::string Clean(std::string_view text);

// The spot of a dungeon's entrance room where our Link last stood on his feet
// (issue 197: where he goes back to when his last place is in another room).
struct Door {
    std::string scene;   // the dungeon
    std::string start;   // the entrance start point whose room it is in
    Vec at;
    std::optional<coop::protocol::Age> age;
    std::int64_t unix_s = 0;
};
// "d1;room;name;scene;start;x;y;z;age;unix_s": a line of its own, after the
// place's (the helper and the channel read only v1 lines, so it stays on this
// PC). DoorFromFile: the first such line of that room and name.
std::string FormatDoor(std::string_view room, std::string_view name, const Door& door);
std::optional<Door> DoorFromFile(std::string_view text, std::string_view room, std::string_view name);

// The text of coop-resume.txt for that place: two comment lines, the line,
// and the door's line when one is kept.
std::string FileText(const Place& place, const std::optional<Door>& door = std::nullopt);
// The place of the file whose room and name are these (the first such line);
// nothing when none.
std::optional<Place> FromFile(std::string_view text, std::string_view room, std::string_view name);
// LICHII_ORM_RESUME_PLACE: one line (the app's: it is ours whatever its room
// and name), or several split by '|' (a harness pair, one per game): the one
// of our name. Nothing when no line is good, or none is ours.
std::optional<Place> FromGiven(std::string_view given, std::string_view name);

// What our game is now, for both sides.
struct Now {
    CoopTeleport::Local local;
    // Where our Link last stood on his feet (CoopTeleport::Grounded::Last and
    // Scene); nothing until he has.
    std::optional<Vec> grounded;
    std::string grounded_scene;
    bool boss_room = false;  // the grounded place is in a boss room (CoopBoss::RoomAt)
    std::int64_t unix_s = 0;
    // The dungeon rooms (the puppet pack's coop_rooms.txt; empty without one:
    // then CheckMove allows only kNearMove); nothing: no dungeon rule at all.
    const CoopRooms::Table* rooms = nullptr;
};

// The door to keep for our Link standing at `at` in `scene`: in a dungeon,
// in the room of the entrance start point nearest him (CheckMove from that
// start is ok); nothing elsewhere.
std::optional<Door> DoorAt(const CoopRooms::Table& rooms, std::string_view scene, const Vec& at,
                           std::optional<coop::protocol::Age> age, std::int64_t unix_s);

// The kept place, written again when it changed: another scene or kind at
// once (at most once a second), a move of more than kMoveUnits at most every
// kEveryUs. Never before Start (the way back is decided first).
class Recorder {
public:
    static constexpr std::uint64_t kEveryUs = 5'000'000;
    static constexpr std::uint64_t kSoonUs = 1'000'000;
    static constexpr float kMoveUnits = 1.0f;

    Recorder(std::string room, std::string name) : room_(Clean(room)), name_(Clean(name)) {}
    void Start() { started_ = true; }
    bool Started() const { return started_; }
    // The place to write now, if any.
    std::optional<Place> Frame(const Now& now, std::uint64_t now_us);
    // The last door (DoorAt of a grounded place in free play), for the file.
    const std::optional<Door>& LastDoor() const { return door_; }
    // A door read from the file before this session (kept until a new one).
    void KeepDoor(std::optional<Door> door) { door_ = std::move(door); }

private:
    std::string room_;
    std::string name_;
    bool started_ = false;
    std::optional<Door> door_;
    std::optional<Vec> door_probe_;  // the grounded place DoorAt last looked at (once per spot)
    std::string door_probe_scene_;
    std::optional<Place> last_;
    std::uint64_t last_us_ = 0;
};

// The way back, decided once.
struct Decision {
    enum class What : std::uint8_t { none, move, travel };
    What what = What::none;
    std::string scene;   // travel: where to
    std::string start;   // travel: the start point there (never empty)
    Vec at;              // move, travel with place: where
    bool place = false;  // travel: move Link to `at` once there (false: the entrance)
    bool door = false;   // move or travel: `at` is the entrance room's Door, not the place (issue 197)
    std::string log;     // for the log: what was decided and why
    std::string notice;  // for the HUD (Spanish); "" none
};

class Returner {
public:
    // Our Link up, no event, no load and no scene change asked for this long
    // after the load (the room's save, a harness warp) before the decision.
    static constexpr std::uint64_t kSteadyUs = 3'000'000;
    // Given up when that never comes this long after our first read Link.
    static constexpr std::uint64_t kWindowUs = 180'000'000;
    // Closer than this to the place in the same scene: already there.
    static constexpr float kThereUnits = 2.0f;

    explicit Returner(std::optional<Place> place, std::optional<Door> door = std::nullopt)
        : place_(std::move(place)), door_(std::move(door)) {}
    // The decision, once (then Done); nothing while waiting.
    std::optional<Decision> Frame(const Now& now, std::uint64_t now_us);
    bool Done() const { return done_; }

private:
    std::optional<Place> place_;
    std::optional<Door> door_;
    bool done_ = false;
    std::uint64_t first_us_ = 0;
    std::uint64_t steady_us_ = 0;
    bool seen_ = false;
    bool steady_ = false;
};

// The decision for `place` with our game as in `now` (Returner, once steady).
// `door`: the entrance room's Door kept beside it, for a place in another
// room of a dungeon (issue 197; with now.rooms set).
Decision Decide(const Place& place, const Now& now, const std::optional<Door>& door = std::nullopt);

} // namespace CoopResume
