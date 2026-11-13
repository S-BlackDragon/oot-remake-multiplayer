#pragma once

// ORM "the room's age" (oot-remake D-R17, after oot-64 D-068): the age is the
// shared game's. Whoever draws or puts back the Master Sword changes with his
// own cutscene; every other game sees a mate's age change, marks it pending
// and, as soon as nothing runs in it (no event, cutscene, talk or game over,
// not being revived, not loading), reloads its scene at the other age where
// Link was. A Link lying down reloads too and stays down (D-R62, oot-64
// D-178): the downed machine holds him down through it (DownedInput::hold_down).
//
// What the remake gives (oot-remake docs/EDAD.md):
// - a player's age travels already: PLAYER_SAMPLE's age, from GameUI's IsChild
//   (PlayerReader::read_age);
// - the save's era is the Bool Sequence_IsChildhood; the era change flows
//   (Market_Child_Field_ChildToAdult, Market_Adult_Field_AdultToChild) set it
//   and ask the scene change to Market_Adult / Market_Child; scenes of the two
//   ages are two scenes (KokiriForest_Child, KokiriForest_Adult);
// - the game takes a scene change (Sequence_ReservedNextBanc) only while an
//   event runs (daurosk docs/scenes.md, docs/testing.md "warp"), so the reload
//   asks an event of our own first (the story work's request by name).
//
// Which event (oot-remake docs/EDAD.md, "El evento que el juego rechazaba"):
// the game starts by name only an event with a row in RSDB EventSetting.
// ReplaceEquipmentForEraChange has none: it is a sub flow (GameAnalyzedEventInfo
// SubFlowList of Market_Child_Field_ChildToAdult, Market_Adult_Field_AdultToChild
// and SageRoom_MeetGanondorf), so every request of it was refused (returned 0,
// edad runs sondas2, sondas3, sondas22r7: 45 of 45). The game's own era change
// at the pedestal is Market_Child_Field_ChildToAdult (back: _AdultToChild):
// preload the Temple of Time, the cutscene Dm_TimeTemple_0080 (_0070), the sub
// flow ReplaceEquipmentForEraChange (the equipment swap, run while the era is
// still the old one), Sequence_IsChildhood, then EventReserveBancChange to
// Market_Adult (Market_Child) with its own boot cutscene. Both have a row, as
// every event the game took from our requester has. So by default (no event
// named) the reload asks that event of the game's: the game swaps the
// equipment, sets the era and loads the Temple of Time itself, where the
// sword is; Link is not put back (another scene). With an event named
// (LICHII_ORM_AGE_EVENT) the old in-place reload: that event, then the scene
// change while it runs, then Link put back where he stood; the era is written
// only once that event runs.
// - The era is never left changed by a reload that did not take: nothing is
//   written before an event of ours runs, and a give-up after one ran puts
//   Sequence_IsChildhood back to our Link's age (it was left adult on a child
//   Link in every edad run before).
//
// What the players see (2026-11-13, oot-remake docs/EDAD.md, the
// decision): with the game's own era change everyone follows to the Temple of
// Time, and nobody is surprised. AgeOutput::notices are the big centred notice
// (the newest replaces the one shown) and, when `chat`, a chat line for the
// record, in Spanish:
// - whoever drew or put back the sword (our own age changed, not by a reload
//   of ours nor by the sword's story on a mate's word): "Moviendo al grupo
//   contigo...";
// - every other game, when the mate's change counts: "<Nombre> ha cambiado de
//   epoca: moviendo al grupo al Templo del Tiempo..." and, once it is safe
//   here, a countdown (3, 2, 1: AgeSettings::countdown_us) before the move;
//   it starts again whenever the move had to wait;
// - never a move in the middle of an event or cutscene, a boss room's lock
//   (AgeInput::boss_fight) or with our Link down (request: also lying, unlike the
//   in-place reload of D-R62): the move waits, the notice says until when, our
//   PLAYER_STATUS carries orm_status::age_waits (waits_unsafe) and the games
//   already at the new age show "Esperando a <Nombre> para cambiar de
//   epoca..."; one log line names the reason;
// - on arrival "Grupo reunido en el Templo del Tiempo" (or, while mates are
//   still on their way, "En el Templo del Tiempo / Esperando al resto del
//   grupo..." and the first once they are all there); the one who drew the
//   sword sees it too once everyone is his age;
// - era_fail_limit reloads that did not take in a row: no move, the era our
//   Link's ("No se pudo cambiar de epoca: intentalo en el pedestal").
// The in-place reload (an event named) keeps its old timing and shows none.
//
// This file is the portable side: the rules, over readings the hook gives. No
// memory, no emulator, no sockets.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "coop/protocol/messages.hpp"

namespace coop::host {

using protocol::Age;

const char* age_name(Age age);  // "child", "adult"
// The age a scene name says ("_Child", "_Adult" at its end), or nothing.
std::optional<Age> age_of_scene(std::string_view scene);
// The same scene at the other age: "KokiriForest_Child" -> "KokiriForest_Adult".
// A scene with no age in its name (a dungeon) is itself.
std::string scene_for_age(std::string_view scene, Age age);

struct AgeSettings {
  // A mate's age must read the same this long (and not while he loads) before
  // it counts: while a game loads, GameUI's IsChild reads adult for a moment.
  std::uint64_t settle_us = 2'000'000;
  // Ours must be calm (no event, not busy) this long before the reload.
  std::uint64_t calm_us = 1'000'000;
  // Our event must start this long after it is asked, or it is asked again.
  std::uint64_t event_wait_us = 2'000'000;
  int event_tries = 3;
  // The scene change is asked again this often while the event runs.
  std::uint64_t ask_again_us = 500'000;
  // From the first ask to Link up in the new scene at the new age.
  std::uint64_t reload_wait_us = 45'000'000;
  // The same with the game's own era change: its cutscene, the load and the
  // arrival's cutscene come first.
  std::uint64_t era_reload_wait_us = 120'000'000;
  // The game's era change hands over to its cutscene (EventTransition): no
  // event may read for a moment. Off this long before it counts as over.
  std::uint64_t event_gap_us = 1'000'000;
  // After a reload that did not take, this long before the next try.
  std::uint64_t retry_us = 10'000'000;
  // Link is put back where he stood only when the scene's start is farther,
  // and never in a scene for both ages (the Spirit Temple, gerudo.hpp
  // age_reload_keeps_spot): there he stays at the scene's start, the entrance
  // both ages reach (a child in the adult half could not walk out).
  float move_back_min = 3.0f;
  // Our own age must read the same this long (with a scene whose era agrees) before it can drop
  // a pending reload as "already" (a reading while our game loads is not our age).
  std::uint64_t own_steady_us = 300'000;
  // ORM (2026-11-13), the game's own era change only: the countdown on
  // screen before the move, whole seconds; it starts again after every wait.
  std::uint64_t countdown_us = 3'000'000;
  // Reloads that did not take in a row before the move is given up: no move,
  // the era stays our Link's, the notice "No se pudo cambiar de epoca".
  int era_fail_limit = 2;
  // How long a notice stays on screen (a countdown step: countdown_step_us).
  std::uint64_t notice_us = 5'000'000;
  std::uint64_t countdown_step_us = 1'200'000;
  // A mate who says his move waits (orm_status::age_waits) is named again this
  // often while it lasts.
  std::uint64_t wait_repeat_us = 15'000'000;
  // The sword's story asked of our game on a mate's word (AgeInput::story_follow)
  // makes our own age change in this time his, not ours to announce.
  std::uint64_t story_follow_us = 300'000'000;  // the draw, the Sacred Realm, Rauru, the way back
  // After our own change or our arrival, the group counts as gathered once
  // every mate reads our age; not looked for after this long.
  std::uint64_t gather_us = 300'000'000;
  // Reloads begun while we lay down that did not take, before a downed Link
  // waits for his revive to reload (the game may take no event of ours from a
  // dead Link: never seen yet). Counted until we are on our feet again.
  int downed_tries = 2;
};

struct AgeMate {
  std::uint8_t number = 0;
  std::optional<Age> age;
  std::optional<std::string> scene;
  std::uint8_t status = 0;  // PLAYER_STATUS flags
  std::string name;         // his nickname, for the notices ("" : "Jugador <n>")
};

struct AgeInput {
  std::uint64_t now_us = 0;  // any steady clock
  std::optional<Age> age;    // ours (GameUI)
  std::optional<std::string> scene;
  std::optional<protocol::Vec3> position;
  std::optional<bool> event_running;
  bool downed = false;  // down, being revived or in the game over
  // Down and only lying there (DownedState::downed: no revive, no game over
  // running): the reload may begin all the same and he stays down (D-R62).
  bool lying = false;
  std::vector<AgeMate> mates;
  // Why age changes are not the room's now (final_rules.hpp ending_holds_age:
  // the ending loads Link as a child for the credits, ours or a mate's), or
  // nothing. While it says something no mate's change is followed (what each
  // is when it ends is taken as what he is, not as a change), a pending
  // reload not yet begun is dropped, and one under way runs to its end.
  std::optional<std::string> held;
  // Why no reload may begin now, or nothing: our game runs the Temple of
  // Time's sword story (time_temple.hpp: our own cutscene changes our age),
  // stands in the Chamber of Sages (no age in its name), or the story's parts
  // still follow one another. Unlike `held`, a mate's change is still seen and
  // a pending reload is kept: it begins once this says nothing, or is dropped
  // then as "already <age>" when our own cutscene made us that age. A reload
  // under way runs to its end.
  std::optional<std::string> wait;
  // ORM (2026-11-13): a boss room shuts us in (its intro, fight or
  // outro): the game's era change waits, as for an event or a downed Link.
  bool boss_fight = false;
  // Our game is asked for the Temple of Time's sword story on a mate's word
  // (coop_time_temple.h): an own age change soon after is his, not ours.
  bool story_follow = false;
};

// A notice for the players (2026-11-13): the big centred one, the newest
// replacing the one shown; `chat` ones also go to the chat lines.
struct AgeNotice {
  enum class Kind : std::uint8_t {
    moving_you,     // we drew or put back the sword: "Moviendo al grupo contigo..."
    announce,       // a mate's change, our move to come (detail: why it waits, or "")
    countdown,      // detail "3", "2", "1"
    waiting_mate,   // "Esperando a <Nombre> para cambiar de epoca..."
    gathered,       // "Grupo reunido en el Templo del Tiempo"
    arrived,        // here, mates still on their way
    failed,         // "No se pudo cambiar de epoca: intentalo en el pedestal"
  };
  Kind kind = Kind::announce;
  std::uint8_t number = 0;  // the player it names (his colour); 0 none
  std::string name;         // "" none; `text` starts with it when there is one
  std::string text;
  std::string detail;       // a second line under it; "" none
  std::uint64_t show_us = 0;
  bool chat = false;
};

const char* notice_kind_name(AgeNotice::Kind kind);

// The game's own era change event to `to` (the pedestal's: it swaps the
// equipment, sets the era and loads the Temple of Time), and that scene.
const char* era_change_event(Age to);  // Market_Child_Field_ChildToAdult, Market_Adult_Field_AdultToChild
const char* era_change_scene(Age to);  // Market_Adult, Market_Child

struct AgeOutput {
  std::optional<Age> set_era;            // write Sequence_IsChildhood (child: 1) now
  std::optional<std::string> ask_event;  // ask this event of our own (by name)
  std::optional<std::string> ask_scene;  // ask the scene change to it (taken while an event runs)
  bool cancel_scene = false;             // clear an earlier scene change request
  std::optional<protocol::Vec3> move_to; // Link put back here, in the new scene
  std::vector<std::string> log;
  std::vector<AgeNotice> notices;  // in order (2026-11-13)
};

class AgeFollow {
 public:
  // `event`: empty, the game's own era change (era_change_event); else the
  // event asked so our in-place scene change is taken.
  explicit AgeFollow(AgeSettings settings = {}, std::string event = {})
      : settings_(settings), event_(std::move(event)) {}

  // The game's own era change is asked (no event named).
  bool game_era_event() const { return event_.empty(); }

  // Change to `age` now (the test command `age`, or a caller's own rule): as a
  // mate's change seen, without waiting for one.
  void change(Age age, const std::string& why);

  AgeOutput tick(const AgeInput& in);

  std::optional<Age> pending() const { return pending_; }
  bool reloading() const { return phase_ != Phase::idle; }
  // A reload under way that began while we lay down (AgeInput::lying): the
  // downed machine keeps us down until it is over (DownedInput::hold_down).
  bool reloading_downed() const { return phase_ != Phase::idle && reload_downed_; }
  // The room's age changed and our move waits because it is not safe here (an
  // event, a boss room's lock, our Link down): our PLAYER_STATUS says so
  // (orm_status::age_waits). As of the last tick; the game's era change only.
  bool waits_unsafe() const { return waits_unsafe_; }

 private:
  enum class Phase : std::uint8_t { idle, event, scene, arrive };
  void give_up(AgeOutput& out, const std::string& why, const AgeInput& in, std::optional<Age> ours);
  void notice(AgeOutput& out, AgeNotice::Kind kind, std::uint8_t number, const std::string& name,
              std::string text, std::string detail, std::uint64_t show_us, bool chat) const;
  // Every mate listed this tick reads `age` (settled); false with none listed.
  bool all_mates_at(Age age) const;
  void arrived(AgeOutput& out, Age age, std::uint64_t now);
  void forget_pending();
  std::string announce_text() const;  // "<Nombre> ha cambiado de época: ..."

  AgeSettings settings_;
  std::string event_;
  struct Seen {
    std::optional<Age> settled;  // the last age that counted
    std::optional<Age> candidate;
    std::uint64_t since = 0;
    std::string name;            // as last listed
    bool waits = false;          // his orm_status::age_waits, as last seen
    std::uint64_t told_at = 0;   // "Esperando a ..." last shown (0: not while it lasts)
  };
  std::array<Seen, protocol::kMaxPlayers + 1> mates_{};
  std::optional<Age> pending_;
  std::uint64_t calm_since_ = 0;  // 0: not calm
  std::uint64_t not_before_ = 0;  // a retry waits until then
  Phase phase_ = Phase::idle;
  std::uint64_t phase_at_ = 0;
  std::uint64_t first_at_ = 0;
  std::uint64_t asked_at_ = 0;
  int tries_ = 0;
  bool unloaded_ = false;  // the scene went unreadable since the ask
  std::string from_;
  std::string target_;
  std::string asked_event_;      // the event this reload asks
  bool wrote_era_ = false;       // we wrote Sequence_IsChildhood in this reload
  bool event_ran_ = false;       // the asked event was seen running in this reload
  std::uint64_t off_since_ = 0;  // the game's era change: no event reads since (0: one does)
  std::optional<protocol::Vec3> where_;
  std::vector<std::string> queued_;  // lines from change(), for the next tick
  bool held_ = false;                // AgeInput::held as of the last tick (its log lines)
  bool waiting_ = false;             // AgeInput::wait as of the last tick (its log lines)
  std::optional<Age> own_candidate_;  // our trusted age reading, and since when
  std::uint64_t own_since_ = 0;
  bool told_untrusted_ = false;       // "our age reads ... not trusted" said
  bool reload_downed_ = false;        // the reload under way began as we lay down
  int down_fails_ = 0;                // such reloads that did not take, since we were up
  bool told_down_wait_ = false;       // "the reload waits for the revive" said
  // ORM notices (2026-11-13), the game's era change.
  std::array<bool, protocol::kMaxPlayers + 1> listed_{};  // mates listed in the last tick
  std::optional<Age> own_settled_;    // our last steady age
  std::uint64_t story_follow_at_ = 0; // AgeInput::story_follow last seen (0: never)
  std::uint8_t leader_ = 0;           // the mate whose change we follow (0: none, the test command)
  std::string leader_name_;
  bool announce_due_ = false;         // the first notice of this change not shown yet
  std::string wait_reason_;           // why the move waits (log words), "" none
  bool waits_unsafe_ = false;         // waits_unsafe()
  std::uint64_t unsafe_since_ = 0;    // a reason to wait seen since (0: none)
  int countdown_shown_ = 0;           // the last whole second shown, 0 none
  int era_fails_ = 0;                 // reloads that did not take in a row (the game's era change)
  std::optional<Age> gather_age_;     // looking for every mate at this age
  std::uint64_t gather_since_ = 0;
};

}  // namespace coop::host
