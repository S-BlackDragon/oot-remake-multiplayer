#pragma once

// ORM "downed, not dead" (oot-remake docs/CAIDO_REVIVIR.md): a Link whose life
// runs out stays down instead of getting the game over, until a mate gets him
// up. Only with LICHII_COOP_DOWNED=1 in the hook.
//
// How it works in the game (P0 to P2 of that document):
// - The hook's gate (lichii/hook/coop_downed.h) holds the game over event shut
//   while its word is nonzero, so a dead Link lies in DeadRagdoll with his
//   Death component's word saying "dead".
// - To get him up, a Fairy is put in a bottle and the gate opened: the game
//   over event's own fairy branch revives him (animation, life, no game over
//   screen, no reload) and uses the fairy up.
//
// This file is the portable side: reading Link's life and death and the
// bottles over GameMemory, the local state machine (Downed), and the mate's
// side, which watches for A held beside a downed player (ReviveWatch). No
// sockets, no emulator: the hook gives readings and carries out the actions.
//
// The room's rules (P4): alone in the room (or with no co-op at all) the gate
// stays open and the game's own death and game over apply. With a mate in, a
// death leaves Link down; when the last mate leaves he gets 10 s for one to
// come back, then his game over runs. Our own connection lost is not a mate
// leaving: down stays down through a reconnect (the server's rejoin by name),
// up to 60 s offline. When everyone is down the server says GROUP_DEFEAT and
// every game lets its game over run: Continue reloads the save.
//
// Doc: oot-remake docs/CAIDO_REVIVIR.md (D-R13, D-R14).
// Switch: LICHII_COOP_DOWNED (on in the kit since 0.7).
// Test: tests/host/downed_test.cpp.

#include <array>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "coop/host/flags.hpp"
#include "coop/host/game_flags.hpp"
#include "coop/host/game_memory.hpp"
#include "coop/protocol/messages.hpp"

namespace coop::host {

// ---- Link's life and death -------------------------------------------------

// The Death component's word as the game's QueryDeathIsDead reads it: dead
// when (w & 0xe) != 0 and (w & 0x6000) != 0x2000 (0x43af dead, 0x43a0 alive,
// seen in a run).
bool death_word_dead(std::uint32_t word);

struct Vitals {
  std::optional<std::int32_t> life;      // quarter hearts
  std::optional<std::int32_t> max_life;  // quarter hearts
  std::optional<std::uint32_t> death_word;

  // Known dead / known alive: the death word read and saying so. Life is not
  // asked: a life written back does not get Link up (P1).
  bool dead() const { return death_word && death_word_dead(*death_word); }
  bool up() const { return death_word && !death_word_dead(*death_word) && life && *life > 0; }
};

// Reads the vitals of the player actor (PlayerReader::find_player_actor):
// components = [actor + 0x270]; life = [components + 0x2b8], the current
// life s32 at [life + 0x1c10] + 8 and the max at [life + 0x1c18] + 8; the
// Death component = [components + 0x1c0], its u32 at +0x20 (docs/objects.md,
// oot-remake docs/CAIDO_REVIVIR.md 2.1, 2.2). A part that cannot be read, or
// a life outside 0..max (max 1 to 999), is left out.
Vitals read_vitals(GameMemory& memory, std::uint64_t player_actor);

// ---- Life after a revive (oot-64 PLAN 2.8.4, D-036) -------------------------

// PlayerStatus.Life, per player (coop/host/game_items.cpp), quarter hearts.
inline constexpr std::uint32_t kLifeFlag = 0x31ab5580;

// The life a mate's revive leaves at least: 3 hearts or 25 % of the max,
// whichever is more, the quarter rounded up to a whole heart (oot-64's
// MAX(0x30, ((cap / 4) + 0xF) & ~0xF), in quarter hearts), never above the
// max. 12 for a max of 12 or less (a max of 0 or less: 0).
std::int32_t revive_life(std::int32_t max_life);

// Writes Link's current life. `direct` false: through the game's own Int
// setter queue (GameFlags::set_int on PlayerStatus.Life), the game applies it
// with its own bookkeeping; true: straight into the Life component's word
// ([[[actor + 0x270] + 0x2b8] + 0x1c10] + 8, what the test command `life`
// writes; P1: a 0 written there kills Link as a hit does). False when the
// flag or the word is not found, the value is outside 0..max, or the write
// fails.
bool write_life(WritableGameMemory& memory, std::uint64_t player_actor, std::int32_t quarters,
                bool direct);

// ---- Bottles ---------------------------------------------------------------

inline constexpr int kBottleCount = 5;  // A to E

// The three flags of bottle `slot` (0 = A): its content (Enum, per player),
// whether it is owned (Bool, shared) and its count (Int, shared; 1 owned).
struct BottleKeys {
  std::uint32_t item = 0;   // PlayerStatus.AssignableItem.BottleInfo.Bottle<X>.BottleItem
  std::uint32_t valid = 0;  // PlayerStatus.AssignableItem.BottleInfo.Bottle<X>.IsValid
  std::uint32_t stock = 0;  // PouchData.Bottle<X>.StockNum
};
BottleKeys bottle_keys(int slot);

// BottleItem values are the Murmur3 hash of the value's name.
std::uint32_t bottle_value(std::string_view name);  // "Empty", "Fairy", ...

struct Bottle {
  std::uint32_t item = 0;  // a BottleItem value hash
  bool valid = false;      // IsValid
  std::int32_t stock = 0;  // StockNum
  bool owned() const { return valid || stock > 0; }
  bool operator==(const Bottle&) const = default;
};
using Bottles = std::array<std::optional<Bottle>, kBottleCount>;

// The live values of the five bottles; a bottle whose three flags are not
// all found is nothing.
Bottles read_bottles(GameMemory& memory);
// Writes a bottle's three live values straight (GameFlags::live_address):
// not through the game's queue, so a save keeps the real ones.
bool write_bottle(WritableGameMemory& memory, int slot, const Bottle& bottle);

// Which bottle a revive uses (P2: a fairy in a bottle and the gate opened).
struct BottlePick {
  int slot = 0;
  bool own_fairy = false;  // a real Fairy is there already: nothing is written
  bool synthetic = false;  // not owned: IsValid and StockNum are faked too
};
// A real Fairy in an owned bottle first, then an owned empty bottle, then a
// bottle not owned (faked whole). A bottle with anything else in it is never
// used: the event could use up something else and lose it. Nothing when every
// bottle is full or unreadable.
std::optional<BottlePick> pick_bottle(const Bottles& bottles);

// ---- The local player ------------------------------------------------------

enum class DownedState : std::uint8_t {
  alive,
  downed,     // dead, the gate shut: lying there, PLAYER_STATUS dead
  reviving,   // a fairy in a bottle and the gate open: the fairy branch runs
  game_over,  // a revive that did not take: the game over runs its course
  // Dead with a real Fairy in a bottle of our own (oot-64 PLAN 2.8 point 1,
  // D-R17): the gate open at once, the game over event's own fairy branch gets
  // us up, as in the original. Not down: no PLAYER_STATUS bit, no REVIVE.
  fairy,
};
const char* to_string(DownedState state);

struct DownedSettings {
  // The game over event must start this long after the gate opens, or the
  // try counts as failed and the fairy is checked and the gate opened again.
  std::uint64_t start_wait_us = 1'000'000;
  int tries = 3;
  // From the gate opening to Link up with the event over (P2: 6.5 s).
  std::uint64_t revive_wait_us = 20'000'000;
  // Down, and the last mate left: this long for one to come back.
  std::uint64_t alone_grace_us = 10'000'000;
  // Down, and our own connection lost: this long for the reconnect.
  std::uint64_t offline_grace_us = 60'000'000;
  // A mate's REVIVE progress is shown this long after it came.
  std::uint64_t progress_show_us = 1'000'000;
  // After a mate's revive (revive_life): the life is checked this long after
  // each write; the first write goes through the game's queue, the next ones
  // straight into the Life component, at most heal_tries in all.
  std::uint64_t heal_check_us = 500'000;
  int heal_tries = 3;
  // After a reload that held us down (DownedInput::hold_down) and got Link up:
  // life 0 written this often, at most redown_tries times, and the wait for
  // Link to read dead or up again at most redown_wait_us.
  std::uint64_t redown_check_us = 1'000'000;
  int redown_tries = 3;
  std::uint64_t redown_wait_us = 10'000'000;
};

struct DownedInput {
  std::uint64_t now_us = 0;  // any steady clock
  Vitals vitals;
  std::optional<bool> event_running;  // PlayerReading::event_running
  Bottles bottles;                    // read only when wants_bottles()
  // The test command `gate` has taken the gate (coop_testinput): watch and
  // log, never write.
  bool manual = false;
  // The room. `coop` false: no co-op session at all. `connected`: welcomed
  // by the server and not dropped. `mates`: the other players in the room.
  // The defaults are a room with one mate, where a death leaves Link down.
  bool coop = true;
  bool connected = true;
  int mates = 1;
  // ORM D-R62 (oot-64 D-178): our scene reloads at the room's other age while
  // we lie down (CoopAge, coop/host/age.hpp AgeFollow::reloading_downed).
  // While it lasts we stay down whatever Link reads (the load may stand him
  // up), no revive, defeat or grace starts; once it is over, a Link the load
  // got up is put down again where he stands (life 0, as a hit kills him,
  // with the gate shut) and stays down until a mate revives him.
  bool hold_down = false;
};

struct BottleWrite {
  int slot = 0;
  Bottle value;
  bool operator==(const BottleWrite&) const = default;
};

// Link's life to write now (write_life).
struct LifeWrite {
  std::int32_t quarters = 0;
  bool direct = false;
  bool operator==(const LifeWrite&) const = default;
};

struct DownedActions {
  bool gate_closed = true;                // the gate word wanted now
  bool write_gate = true;                 // false: leave the word alone (manual)
  std::vector<BottleWrite> bottles;       // live values to write, in order
  std::optional<LifeWrite> life;          // Link's life, after a revive or a reload
  std::optional<std::set<FlagKey>> quiet; // the watcher's quiet set, when it changes
  std::vector<std::string> log;           // one line each, "alive -> downed"...
};

class Downed {
 public:
  explicit Downed(DownedSettings settings = {}) : settings_(settings) {}

  // A REVIVE for us came (any thread may not call this: the hook queues it
  // and calls it on the game's thread). Taken while downed; dropped otherwise.
  void request_revive(std::uint8_t by);
  // A REVIVE with a progress (0 to 100 %): a mate holding A beside us, for the
  // screen only. Kept while downed.
  void hear_progress(std::uint8_t by, std::uint8_t percent, std::uint64_t now_us);
  // GROUP_DEFEAT came: everyone is down. Taken while downed or being revived;
  // false when dropped (alive, or the game over already running). A defeat
  // that comes anyway is everyone's, also ours while getting up (D-R66): the
  // revive is put back and the game over runs.
  bool group_defeat(std::uint8_t down);

  // Once a frame on the game's thread.
  DownedActions tick(const DownedInput& in);

  DownedState state() const { return state_; }
  // Not on our feet: down, getting up, or a game over running (we hold A
  // beside nobody, the other rules treat us as downed).
  bool dead_status() const {
    return state_ != DownedState::alive && state_ != DownedState::fairy;
  }
  // PLAYER_STATUS bit 4, the one the server's group defeat counts: down or a
  // game over running. Not while getting up (D-R66, D-R73): from the REVIVE
  // taken we count as standing, so the group never splits even if the mate
  // who revives us falls in those seconds; a revive that fails puts us down
  // again and the server's 2 s start over. While a mate holds A beside us
  // (the 3 s) the bit stays on (the server routes REVIVE only to a dead
  // player) and the server counts the hold instead (Session::check_group_defeat).
  bool status_dead() const {
    return state_ == DownedState::downed || state_ == DownedState::game_over;
  }
  // A mate holds A beside us now: his last REVIVE progress came within
  // progress_show_us (for the log and the screen; the server counts it).
  bool held(std::uint64_t now_us) const {
    return state_ == DownedState::downed && progress_at_ != 0 && now_us >= progress_at_ &&
           now_us - progress_at_ <= settings_.progress_show_us;
  }
  // Read the bottles this frame (downed or reviving).
  bool wants_bottles() const {
    return state_ == DownedState::downed || state_ == DownedState::reviving;
  }
  // The same, knowing Link's vitals: also on the frame he dies standing, for
  // a fairy of his own (the fairy state).
  bool wants_bottles(const Vitals& vitals) const {
    return wants_bottles() || (state_ == DownedState::alive && vitals.dead());
  }
  // The mate reviving us and how far (0 to 100), while the screen should say
  // so: his last progress came within progress_show_us, or his request
  // started the revive (100).
  struct Reviver {
    std::uint8_t by = 0;
    int percent = 0;
  };
  std::optional<Reviver> reviver(std::uint64_t now_us) const;
  // Down and alone (or offline): when the game over runs, if nobody comes.
  std::optional<std::uint64_t> alone_until() const;
  // A mate's revive left less than revive_life: the life is being written.
  bool healing() const { return heal_target_ != 0; }
  // A reload holds us down (DownedInput::hold_down), or the one just over is
  // putting Link down again.
  bool held_down() const { return holding_ || redown_; }

 private:
  enum class Phase : std::uint8_t { write, open, event };
  void set(DownedState next, DownedActions& out, const std::string& why);
  void start(std::uint8_t by, DownedActions& out);
  void restore(DownedActions& out);
  void reviving(const DownedInput& in, DownedActions& out);
  void own_fairy(const DownedInput& in, DownedActions& out);
  void start_heal(const DownedInput& in, DownedActions& out);
  void heal(const DownedInput& in, DownedActions& out);
  bool hold(const DownedInput& in, DownedActions& out);  // true: nothing else this tick

  DownedSettings settings_;
  DownedState state_ = DownedState::alive;
  std::optional<std::uint8_t> asked_by_;  // a REVIVE not taken yet
  // The revive under way.
  std::uint8_t by_ = 0;  // the reviver, 0 for our own fairy
  Phase phase_ = Phase::write;
  BottlePick pick_;
  Bottle before_;
  int tries_ = 0;
  std::uint64_t opened_at_ = 0;
  std::uint64_t first_open_at_ = 0;
  std::uint64_t started_at_ = 0;     // the fairy written (Phase::write)
  bool event_wait_logged_ = false;   // "another event runs" said for this revive
  bool quiet_on_ = false;
  // The fairy state: since when, and whether the event was seen running.
  std::uint64_t fairy_since_ = 0;
  bool fairy_event_ = false;
  // The room (P4).
  int room_ = -1;                       // the room as last logged (tick); -1: not yet
  std::uint64_t lonely_since_ = 0;      // down with no mate (or offline) since; 0: not
  std::uint64_t lonely_grace_us_ = 0;   // the grace that applies to it
  std::uint8_t defeat_ = 0;             // a GROUP_DEFEAT not taken yet (players down)
  std::uint8_t progress_by_ = 0;        // the last REVIVE progress
  std::uint8_t progress_ = 0;
  std::uint64_t progress_at_ = 0;
  // The life after a mate's revive (revive_life); 0: none being written.
  std::int32_t heal_target_ = 0;
  std::uint64_t heal_at_ = 0;
  int heal_tries_ = 0;
  // A reload while down (D-R62): held now, then putting Link down again.
  bool holding_ = false;
  bool redown_ = false;
  std::uint64_t redown_since_ = 0;
  std::uint64_t redown_at_ = 0;  // the last life 0 written; 0: none yet
  int redown_tries_ = 0;
};

// ---- A mate's side: A held beside a downed player --------------------------

struct ReviveSettings {
  float reach = 1.5f;                     // game units (metres, unmeasured)
  std::uint64_t hold_us = 3'000'000;      // A held this long, without a break
  std::uint64_t again_us = 5'000'000;     // still held and still down: ask again
  std::uint64_t progress_us = 250'000;    // REVIVE progress this often while held
};

struct Mate {
  std::uint8_t number = 0;
  std::optional<std::string> scene;
  std::optional<protocol::Vec3> position;  // its last sample's
  std::uint8_t status = 0;                 // PLAYER_STATUS flags
};

struct ReviveInput {
  std::uint64_t now_us = 0;
  std::optional<protocol::Vec3> position;  // ours
  std::optional<std::string> scene;        // ours
  bool self_down = false;                  // we are down ourselves
  std::optional<bool> a_held;              // the pad's A (nothing: unknown)
  std::vector<Mate> mates;
  // Our connection is up. Off: nothing can be sent, so no hold is counted
  // (the hook ticks the watch offline too, so a hold never spans the drop).
  bool connected = true;
};

struct ReviveOutput {
  std::optional<std::uint8_t> request;  // send REVIVE to this player
  // Send REVIVE with this progress (0 to 100 %) to this player: about every
  // progress_us while A is held, before the request.
  std::optional<std::uint8_t> progress_to;
  std::uint8_t progress = 0;
  // For the screen: the downed mate in reach (0: none), and how far the hold
  // is (nothing: A not held).
  std::uint8_t in_reach = 0;
  std::optional<int> holding_percent;
  // Mates whose dead bit came on or went off this tick (any scene), for the
  // chat notices (mate_down_notice, mate_up_notice). A mate first seen down
  // counts as gone down.
  std::vector<std::uint8_t> went_down;
  std::vector<std::uint8_t> got_up;
  std::vector<std::string> log;
};

class ReviveWatch {
 public:
  explicit ReviveWatch(ReviveSettings settings = {}) : settings_(settings) {}
  ReviveOutput tick(const ReviveInput& in);

 private:
  ReviveSettings settings_;
  std::array<bool, protocol::kMaxPlayers + 1> down_{};  // last seen, by number
  std::uint8_t target_ = 0;  // 0: not holding beside anyone
  std::uint64_t since_ = 0;
  std::uint64_t sent_at_ = 0;
  bool sent_ = false;
  std::uint64_t progress_at_ = 0;  // the last progress sent; 0: none yet this hold
};

// ---- The line on the screen ------------------------------------------------

// What the co-op window says about it, one line (in Spanish, for the players),
// empty for nothing. Names are the players' nicknames.
struct BannerView {
  DownedState state = DownedState::alive;
  std::optional<std::string> reviver;  // the mate reviving us (Downed::reviver)
  int reviver_percent = 0;
  std::optional<std::string> target;   // a downed mate in reach (ReviveOutput::in_reach)
  std::optional<int> holding_percent;  // our hold beside him
  std::optional<std::uint64_t> alone_left_us;  // down and alone: time to the game over
  bool group_defeat = false;  // the game over runs because everyone is down (GROUP_DEFEAT)
};
std::string banner_text(const BannerView& view);

// The chat notices for a mate going down or getting up (Spanish, like the
// line above), and the note on a downed mate's name tag: "caido" and, when
// known, how far he is from our Link in whole metres ("caido, 23 m").
std::string mate_down_notice(const std::string& name);
std::string mate_up_notice(const std::string& name);
std::string downed_tag_note(std::optional<float> metres);

// ORM: the line about our own connection at the top of the screen, which the
// player sees with the panel hidden (F9). Empty while connected, before the
// first welcome (still joining: the panel and the chat say it), and for the
// first second of a loss (a blip). Then "Sin conexion con el servidor:
// reconectando (n s)", with the seconds since it was lost.
std::string connection_text(bool welcomed_once, bool connected, std::uint64_t lost_for_ms);
// Two lines of the top of the screen as one, either may be empty.
std::string join_screen_lines(const std::string& first, const std::string& second);

}  // namespace coop::host
