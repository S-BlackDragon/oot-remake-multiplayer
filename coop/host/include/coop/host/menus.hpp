#pragma once

// ORM pause, menus, saving and leaving with partners (oot-remake
// docs/MENUS_PAUSA.md; the rules of oot-64 D-030, D-037, D-058). In the remake
// each game pauses itself: the game's own pause menu (PLUS), map (MINUS), item
// wheel and save screen all run Sequence/ChangeGamePause.module, which sets
// the Bool Sequence_IsPause while the game is paused. From that and from
// whether a Link and a scene are there, this decides what our PLAYER_STATUS
// says (paused, or at the title screen), and for the others' statuses what
// we show (a "pausa" tag, chat notices). No game memory and no sockets here:
// lichii/hook/coop_menus.cpp reads the game and sends.
//
// Switch: LICHII_ORM_MENUS (on by default; 0 off: no bit is sent, nothing
// is shown, as before).

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "coop/protocol/messages.hpp"
#include "coop/protocol/orm_status.hpp"

namespace coop::host::menus {

// Sequence_IsPause (Bool, reset type 0, not saved; GameDataList hash,
// Murmur3 of the name): set by ChangeGamePause.module (its IsPause input)
// while the game is paused by a menu.
inline constexpr std::uint32_t kIsPauseFlag = 0x604aebe1;

// The PLAYER_STATUS bits the menus decide. The sampler puts these in and
// leaves the others (cutscene, dead) to their owners.
inline constexpr std::uint8_t kStatusMask = static_cast<std::uint8_t>(
    protocol::status::paused | protocol::status::loading | protocol::orm_status::at_title);

enum class State : std::uint8_t {
  playing,  // in the world (also while a door or a cutscene loads)
  paused,   // the game is paused by one of its menus
  title,    // the title screen or file select: no world
};

const char* to_string(State state);

// The PLAYER_STATUS bits for `state`: paused -> status::paused; title ->
// status::loading | orm_status::at_title (the server treats a loading player
// as one who cannot own an object); playing -> 0.
std::uint8_t status_bits(State state);

struct Settings {
  // No Link and no scene this long, in a row, is the title screen (a door or
  // a save load is shorter; the flag watcher's own sign is 30 frames).
  std::uint64_t title_after_us = 3'000'000;
  // Sequence_IsPause unreadable this long while paused (a frame the manager
  // moved): the pause is taken as over.
  std::uint64_t unknown_keeps_us = 500'000;
};

// One frame of our own game.
struct Input {
  std::uint64_t now_us = 0;       // any steady clock
  bool link = false;              // PlayerReader read our Link this frame
  bool scene = false;             // a scene (Banc) is loaded (PlayerReader::read_scene)
  std::optional<bool> is_pause;   // Sequence_IsPause; nothing when unreadable
  // Sequence_IsLoadingBanc: a scene loads (a door, a warp). While it is set
  // the title clock does not run, whatever the reader sees.
  std::optional<bool> loading_scene;
  // Our Link plays the Ocarina freely (coop/host/scene.hpp, Activity::ocarina).
  // The game may pause for its song staff (OcarinaPauseOn.module): that is
  // not a menu, the others must see the Ocarina played (D-R37), so it is not
  // sent as a pause.
  bool ocarina = false;
};

// Our own game: playing, paused or at the title.
class Local {
 public:
  explicit Local(Settings settings = {}) : settings_(settings) {}

  // One frame; the new state when it changed this frame.
  std::optional<State> frame(const Input& in);

  State state() const { return state_; }
  std::uint8_t status_bits() const { return menus::status_bits(state_); }

 private:
  Settings settings_;
  State state_ = State::playing;
  std::uint64_t gone_since_us_ = 0;      // first frame with no Link and no scene; 0: not gone
  std::uint64_t unknown_since_us_ = 0;   // first paused frame the flag was unreadable; 0: read
};

// What a mate's PLAYER_STATUS says. at_title wins over paused.
State mate_state(std::uint8_t status_flags);

// A mate on the title screen is in the room but not in the world: no partner
// who can revive, and not one of the players a group defeat waits for.
inline bool at_title(std::uint8_t status_flags) {
  return (status_flags & protocol::orm_status::at_title) != 0;
}

// The note after a mate's name tag ("Ana (pausa)"), or "" for none. The
// sender leaves the Ocarina out of its pause (Input::ocarina).
std::string tag_note(std::uint8_t status_flags);

// One remote player as presence has it this frame.
struct Seen {
  std::uint8_t number = 0;
  std::string name;  // empty: "P<number>"
  std::uint8_t status_flags = 0;
  // Presence knows their scene (a sample carried one): they have been in the
  // world. A player who joined on the title screen has none yet.
  bool in_world = false;
};

// One change of a mate, for the log and the chat.
struct MateChange {
  std::uint8_t number = 0;
  State from = State::playing;
  State to = State::playing;
  std::string notice;  // the chat line, Spanish, UTF-8; empty: log only
};

// The others: a chat notice when one goes to the title screen ("Ana salió al
// menú principal") and when one is back ("Ana volvió a la partida", or "Ana
// entró en la partida" for one never seen in the world before). A pause has
// no notice (its tag says it) but is reported for the log, and so is a title
// screen of one who was never in the world (a game that joined on its title
// screen). The first time a player is seen nothing is said (we just
// arrived, or they did: the join line says it). A player no longer listed is
// forgotten (the leave line says it, leave_notice).
class Mates {
 public:
  std::vector<MateChange> frame(std::span<const Seen> seen);
  void clear() { known_.clear(); }

 private:
  struct Known {
    std::uint8_t number = 0;
    State state = State::playing;
    bool been_in = false;  // seen in the world (Seen::in_world, not at the title)
  };
  std::vector<Known> known_;
};

// The chat line for PLAYER_LEFT: "Ana salió de la sesión" (left), "Ana perdió
// la conexión" (timed out), "Ana salió de la sala (expulsado)" (kicked).
std::string leave_notice(std::string_view name, protocol::LeaveReason reason);

// A save written from our own menu (the game's Save screen, which runs while
// the game is paused). `newest_write` is the newest file time of our save
// folder (coop::host::save::newest_write, as a tick count), nothing when it is
// not known. The first value seen is where it starts. A newer one counts when
// it comes while paused or within after_pause_us of the pause's end (the
// write may land as the menu closes); a newer one at any other time (the
// game's own save at a door) is only taken as the new mark.
class SaveWatch {
 public:
  static constexpr std::uint64_t kAfterPauseUs = 2'000'000;

  // True once for each menu save.
  bool frame(std::uint64_t now_us, bool paused, std::optional<std::int64_t> newest_write);

  // A write that is not ours (the host's save the co-op folder was given,
  // coop_save.cpp): taken as the new mark, never a menu save.
  void take_mark(std::int64_t newest_write) {
    if (!mark_ || newest_write > *mark_) mark_ = newest_write;
  }

 private:
  std::optional<std::int64_t> mark_;
  bool was_paused_ = false;
  std::uint64_t pause_end_us_ = 0;  // 0: no pause ended yet
};

// What a guest is told after its menu save (oot-64 D-058 point 3: only the
// host writes the group's progress). `coop_folder`: the save went into the
// co-op folder (the host's save, coop_save.h), not the player's own.
std::string guest_save_notice(std::string_view host_name, bool coop_folder);

}  // namespace coop::host::menus
