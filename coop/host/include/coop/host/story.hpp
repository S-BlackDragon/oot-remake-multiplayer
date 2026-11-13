#pragma once

// ORM "story cutscenes for everyone" (oot-remake docs/CINEMATICAS.md, P2): a
// listed story cutscene that one player's game starts by itself plays on every
// game in the same scene at the same moment, and a game that has played it
// does not play it again. Only with LICHII_ORM_STORY_EVENTS=1 in the hook
// (lichii/hook/orm_story_events.h; LICHII_COOP_STORY=1 still works).
//
// How it works:
// - The list (StoryList): the story events that are shared, by the name the
//   game gives them (the flow's entry point an EventRequestActor asks for,
//   such as KokiriForest_Meet_DekuTree_MeetDekuTree_01). Events off the list
//   (a chest, a talk, a sign, the game over) stay with the one who plays them.
// - Owner: when our game's event starts (the rising edge of "an event runs")
//   and its first readable name is listed, and we did not ask for it on
//   someone's word, we own it: STORY_EVENT start goes to the server, which
//   relays the first start of each (scene, name) to the others. When our
//   event is over, STORY_EVENT end.
// - The others: on a start for our scene, with no event of our own running,
//   not downed, not loading, and the event not played here (HavePlayedEvent.
//   <name> set, or it ran here already), we ask our game to start the same
//   event by name (the hook's cave call, coop_story.h), every 0.5 s up to 3 s
//   until it runs. A start for another scene is ignored.
// - Latecomers: the server sends a joiner every (scene, name) the room has
//   started (STORY_EVENT played); and the list's HavePlayedEvent flags are in
//   the shared flags, so the end state travels and the game's own trigger
//   does not fire again.
// - End state: a line also names the flags the cutscene sets at its end (the
//   Bools, such as the Deku Tree's open mouth, and the story steps, Enum
//   flags such as Step_Meet_DekuTree = Complete). They are shared flags too,
//   so a player who did not play the cutscene (a latecomer, one who had it
//   played already) gets the same world. A step only moves forward
//   (EnumFlags steps: GameFlagStore keeps a later value).
// - Skipping (ZMP D-048: only the cutscene's player skips it, for everyone):
//   the owner's game tells its skip apart from a plain end. A skip button
//   (StorySettings::skip_buttons, PLUS or MINUS: the remake's skip prompt)
//   pressed while our owned event runs, or the test script's `skip`
//   (note_skip), within skip_press_window_us before the event is over, makes
//   its end a STORY_EVENT `skipped`; anything else is a plain `end`, which
//   never cuts anyone's copy. A game whose copy of the skipper's event still
//   runs when `skipped` comes skips the rest with the game's own force skip
//   (StoryOutput::skip; the hook calls 0x3b3af30 as the boss rooms do), every
//   skip_every_us until it is over, for up to skip_for_us. A mate who skips
//   his own copy skips only his (a mate's end is never sent). A start still
//   asked for when `skipped` comes is dropped; an event our own game starts
//   once the room skipped it (a latecomer: the server sends a joiner `played`
//   and then `skipped`) is skipped at once; one the room still plays is
//   skipped if its owner's end comes as `skipped`.
//
// This file is the portable side: no sockets, no emulator. The hook gives
// readings and carries out what tick() asks (orm_story_events_game.cpp).
//
// Doc: oot-remake docs/CINEMATICAS.md, HISTORIA_COMPLETA.md (D-R15, D-R20).
// Switch: LICHII_ORM_STORY_EVENTS (on in the kit; LICHII_COOP_STORY before test9),
// LICHII_ORM_STORY_LIST.
// Test: tests/host/story_test.cpp (+ story_list_data.inc), tests/lichii/story_test.cpp.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "coop/host/flags.hpp"
#include "coop/host/game_memory.hpp"
#include "coop/protocol/messages.hpp"

namespace coop::host {

// ---- The list --------------------------------------------------------------

// 1 to protocol::kMaxStoryNameChars of A-Z, a-z, 0-9 and _ (every event name
// in the romfs is so).
bool valid_event_name(std::string_view name);

// "HavePlayedEvent.<event>": the game's own record that an event has played
// (a saved Bool, Murmur3 of the whole name).
std::string played_flag_name(std::string_view event);

// A story step (an Enum flag, Step_*) a cutscene moves: its values in the
// game's order (GameDataList), which is the order it moves forward in.
struct StoryStep {
  std::string name;                       // as written: a name or 0x hash
  std::uint32_t hash = 0;                 // flag_hash(name)
  std::vector<std::uint32_t> values;      // flag_hash of each value name
  std::vector<std::string> value_names;   // as written
  bool operator==(const StoryStep&) const = default;
};

struct StoryEntry {
  std::string name;                // the event, as the game names it
  std::uint32_t played_hash = 0;   // flag_hash(played_flag_name(name))
  std::vector<FlagKey> flags;      // more Bool flags its end state needs shared
  std::vector<std::string> flag_names;  // the same, as written (for logs)
  std::vector<StoryStep> steps;    // the story steps its end state needs shared
  // Seen only by who starts it (ZMP D-067: the cutscene after a blue warp is
  // the warper's): never offered to the others, its flags shared all the same.
  bool solo = false;
  // ORM (oot-remake docs/FINAL.md, ZMP D-188): more than 0, a start for it is
  // only taken by a game whose Link is within this many metres of where the
  // starter's Link stood (the cutscene of one room of a scene, such as a trial
  // of Ganon's Castle, is that room's); its flags shared all the same.
  float near_m = 0.0f;
};

// One end-state flag of the list (a Bool after an event's name, or a step),
// each once, for reading and logging (read_story_flags).
struct StoryFlag {
  std::string name;               // as written
  std::uint32_t hash = 0;
  std::optional<StoryStep> step;  // nothing: a Bool; else the step
};

// The shared story events. Loaded from plain text, one event a line:
//
//   # a comment; blank lines are fine
//   EventName [Flag ...]
//
// EventName is the event's name; HavePlayedEvent.EventName is always shared.
// Each Flag after it is one more saved flag its end state needs shared:
//
//   SomeBoolFlag        a Bool (the HavePlayedEvent.Dm_* of the timelines the
//                       event plays, a door the cutscene opens), by name or as
//                       a 0x hash
//   step:Step_Name=V1,V2,...   a story step (an Enum flag, by name or 0x
//                       hash) and every value it may take, names or 0x
//                       hashes, in the game's order: it only moves forward
//   solo                the event is only for who starts it: no STORY_EVENT
//                       start for it, a start for it from elsewhere is
//                       ignored; its flags are shared as any line's
//   near=M              (ORM, ZMP D-188) a start for it is only taken while
//                       our Link is within M metres (1 to 1000) of where the
//                       starter's Link stood, or of the starter as presence
//                       shows him now in our scene (a start place not known,
//                       sent as 0 0 0, counts as not known); its flags are
//                       shared as any line's
//
// An event listed twice is an error; so is a step listed on two lines with
// different values.
class StoryList {
 public:
  static constexpr std::size_t kMaxEntries = 256;

  StoryList() = default;
  explicit StoryList(std::vector<StoryEntry> entries) : entries_(std::move(entries)) {}

  // The built-in list (the kit's shared_story.txt says the same).
  static StoryList defaults();
  static std::optional<StoryList> parse(std::string_view text, std::string* error = nullptr);
  static std::optional<StoryList> load(const std::filesystem::path& path,
                                       std::string* error = nullptr);

  // The entry's index, or nothing when the event is not listed.
  std::optional<std::size_t> find(std::string_view name) const;
  bool contains(std::string_view name) const { return find(name).has_value(); }
  const std::vector<StoryEntry>& entries() const { return entries_; }
  std::size_t size() const { return entries_.size(); }

  // Every entry's HavePlayedEvent flag, its more flags and its steps, as
  // shared-flag keys (index 0), each once.
  std::vector<FlagKey> shared_flags() const;
  // Every step of the list, opted in as steps (only moving forward).
  EnumFlags step_flags() const;
  // The end-state flags (Bools after the names, then steps), each once, in
  // list order.
  std::vector<StoryFlag> end_flags() const;
  // The name of `value` among the values of the step `hash`, or nothing.
  std::optional<std::string> value_name(std::uint32_t hash, std::uint32_t value) const;

 private:
  std::vector<StoryEntry> entries_;
};

// The text of the built-in list.
extern const char* const kDefaultStoryList;

// HavePlayedEvent.<name> of each listed event as the game has it now, in list
// order (GameFlags::get_bool: nothing when the manager is not up or the flag
// is unknown).
std::vector<std::optional<bool>> read_story_played(GameMemory& memory, const StoryList& list);

// Each of list.end_flags() as the game has it now, in that order: a Bool as
// 0 or 1 (GameFlags::get_bool), a step as its value hash (get_enum).
std::vector<std::optional<std::uint32_t>> read_story_flags(GameMemory& memory,
                                                           const StoryList& list);

// ---- The rules -------------------------------------------------------------

struct StorySettings {
  std::uint64_t ask_every_us = 500'000;       // ask our game again this often
  std::uint64_t give_up_us = 3'000'000;       // not running this long after the start came: rejected
  std::uint64_t name_wait_us = 1'000'000;     // the event's first readable name, after its edge
  std::uint64_t end_quiet_us = 1'500'000;     // no event this long: our owned event is over
  // HavePlayedEvent set at least this long before a start comes means "played
  // here before": a flag set just now may be the owner's own, shared while its
  // start was on the way.
  std::uint64_t played_settle_us = 3'000'000;
  // Skipping (D-048). The owner's side: a press of one of these buttons (Npad
  // bits, PLUS 10 and MINUS 11: the skip prompt and its confirm) at most this
  // long before our owned event is over makes its end `skipped`. A (text) is
  // never one: a cutscene's natural end comes right after its last text.
  std::uint64_t skip_buttons = (1ull << 10) | (1ull << 11);
  std::uint64_t skip_press_window_us = 5'000'000;
  // The others' side: our copy of a skipped event is force skipped this often
  // (a force skip cuts one shot), for up to this long.
  std::uint64_t skip_every_us = 500'000;
  std::uint64_t skip_for_us = 8'000'000;
};

struct StoryInput {
  std::uint64_t now_us = 0;         // any steady clock
  std::uint64_t server_now_us = 0;  // the server's clock; 0: not connected
  std::optional<std::string> scene;  // nothing: loading, or no game
  std::optional<protocol::Vec3> position;
  std::optional<bool> event_running;  // PlayerReading::event_running
  std::optional<std::string> event_name;  // PlayerReader::read_event_name, while one runs
  bool downed = false;  // our Link is down (coop/host/downed.hpp)
  // The pad's held buttons (Npad bits, Platform::PadButtons); nothing: unknown.
  // Only its skip buttons are looked at, while our owned event runs.
  std::optional<std::uint64_t> pad;
  // The other players as presence shows them now (ORM, near=: a start is also taken when its
  // starter stands within M of us now, docs/RIESGOS_RESUELTOS.md "Mundo e historia").
  struct Mate {
    std::uint8_t number = 0;
    std::optional<std::string> scene;
    protocol::Vec3 position{};
  };
  std::vector<Mate> mates;
  // read_story_played: HavePlayedEvent.<name> of each listed event, in list
  // order; a missing or empty entry is "unknown".
  std::vector<std::optional<bool>> played;
  // read_story_flags: the list's end-state flags, in end_flags() order; a
  // missing or empty entry is "unknown". Only logged (each change once).
  std::vector<std::optional<std::uint32_t>> flags;
};

struct StoryOutput {
  std::vector<protocol::StoryEvent> send;  // to the server, in order
  std::optional<std::string> ask;          // ask our game to start this event now
  bool skip = false;  // force skip the event our game runs now (the game's own, 0x3b3af30)
  std::vector<std::string> log;            // one line each
};

class Story {
 public:
  explicit Story(StoryList list, StorySettings settings = {});

  // A STORY_EVENT from the server. Call on the game's thread (the hook queues
  // what the network thread gets); it is acted on at the next tick.
  void receive(const protocol::StoryEvent& message);

  // Once a frame on the game's thread.
  StoryOutput tick(const StoryInput& in);

  // Our game's own event was skipped by other means than the pad (the test
  // script's `skip`): counts as a skip press now, `how` for the log. On the
  // game's thread; looked at by the next tick.
  void note_skip(std::string how);

  // The host loaded a save (FlagWatcher's FLAG_RESET, docs/flags.md "Saves"):
  // the save's HavePlayedEvent 0s are back, so what ran here and what the
  // room played are forgotten; the next start of an event is shared again.
  // On the game's thread; logged at the next tick.
  void host_reset();

  const StoryList& list() const { return list_; }
  // The event we asked our game for on someone's word, while we wait for it.
  std::optional<std::string> asking() const;
  // The listed event our game runs as its owner, until its end is sent.
  std::optional<std::string> owned() const;
  // The listed event whose copy here we skip (or will skip) because its
  // owner's copy is over.
  std::optional<std::string> skipping() const;

 private:
  struct Played {
    bool known = false;        // read at least once
    bool on = false;
    bool old = false;          // on at the first reading: set before we came
    std::uint64_t since = 0;   // the last change (now_us)
  };
  struct Pending {
    std::string scene;
    std::string name;
    std::uint8_t from = 0;
    std::uint64_t since = 0;
    std::uint64_t next_ask = 0;
    int asks = 0;
  };
  struct Running {
    std::string scene;
    std::string name;
    std::uint8_t from = 0;  // 0: ours (owned); else the player whose copy ours ends with
  };
  struct Skip {
    std::string name;
    std::uint8_t from = 0;   // whose skip we follow
    std::uint64_t next = 0;  // the next skip
    std::uint64_t since = 0;
    int skips = 0;
  };
  struct SkipPress {
    std::uint64_t at = 0;
    std::string how;  // "PLUS", "test skip"
  };

  void update_played(const StoryInput& in, StoryOutput& out);  // logs each flip
  void update_flags(const StoryInput& in, StoryOutput& out);   // logs each change
  std::string flag_value(const StoryFlag& flag, std::uint32_t value) const;
  bool played_here(std::size_t index, std::uint64_t now_us) const;
  void on_start(const protocol::StoryEvent& m, const StoryInput& in, StoryOutput& out);
  void on_first_name(const std::string& name, const StoryInput& in, StoryOutput& out);
  void on_skipped(const protocol::StoryEvent& m, const StoryInput& in, StoryOutput& out);
  void watch_skip(const StoryInput& in, bool running, StoryOutput& out);
  void confirm(const StoryInput& in, StoryOutput& out);
  void step_skip(const StoryInput& in, bool running, StoryOutput& out);

  StoryList list_;
  StorySettings settings_;
  std::vector<Played> played_;
  std::vector<StoryFlag> end_flags_;                     // list_.end_flags()
  std::vector<std::optional<std::uint32_t>> flag_seen_;  // each end flag's last value
  std::vector<protocol::StoryEvent> inbox_;
  std::set<std::string> ran_;  // listed events that ran here this run
  // (scene, name) the room started, with who started it (0: us).
  std::map<std::pair<std::string, std::string>, std::uint8_t> room_played_;
  std::set<std::pair<std::string, std::string>> room_skipped_;  // (scene, name) whose owner skipped it
  std::optional<Pending> pending_;
  std::optional<Running> running_;   // a listed event running here (ours or followed)
  std::optional<Skip> skip_;         // our copy of an event its owner skipped
  std::optional<SkipPress> press_;   // the last skip press during our owned event
  std::optional<std::string> noted_skip_;  // note_skip() since the last tick
  std::uint64_t pad_was_ = 0;        // the skip buttons held at the last tick
  bool was_running_ = false;
  bool edge_ = false;                // waiting for the first name of the event that started
  std::uint64_t edge_at_ = 0;
  std::string current_name_;         // the running event's last name read
  std::uint64_t quiet_since_ = 0;    // no event since (0: one runs)
  bool reset_ = false;               // host_reset() since the last tick: logged
};

}  // namespace coop::host
