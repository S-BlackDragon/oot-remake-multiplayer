#pragma once

// ORM songs with several players (oot-remake docs/CANCIONES.md, PLAN_100.md F10 and F15):
// the thirteen songs as data (the item that says a song is known, the event that teaches
// it, where a warp song lands), what each song does to the world and the rule it follows
// with several players, and the warp-song invitation of oot-64 D-067: a mate who leaves
// our scene by a warp song can be followed for 10 s by holding a button.
//
// The rules are oot-64's (oot-64/docs/DECISIONES.md): a song is known by the group (an
// item, shared by game_items); what a song changes for good in the world is the world's
// (a flag list, kit file shared_dungeon_songs.txt, made from song_effects() and checked
// against it in the tests); what it changes for a moment is the game of who plays it
// (the windmill's speed, a fairy, a frozen ReDead); the clock is the time-of-day sync's
// (lichii/hook/coop_time_sync.h: the Sun's Song is a jump); a warp song takes only who
// plays it, and whoever was in that scene may follow (D-067).
//
// No game data beyond names, hashes and positions read from the romfs (Banc StartPos
// placements, Event/EventFlow/Dm_SY_Ocarina, GameData/GameDataList).
// Switch: LICHII_COOP_WARP_INVITE (off by default: not run in a game yet; the hook side
//   is lichii/hook/coop_songs_game.cpp). The flag list: LICHII_COOP_DUNGEON_LISTS.
// Test: tests/host/songs_test.cpp.

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "coop/host/live_check.hpp"
#include "coop/protocol/messages.hpp"

namespace coop::host {

// The game's MelodyType numbers (GameSystemActor.EventQueryMelodyTypeForNormal's cases in
// Dm_SY_Ocarina, matched with the items each case gives).
enum class Song : std::uint8_t {
  zelda_lullaby = 0,
  epona = 1,
  saria = 2,
  sun = 3,
  time = 4,
  storm = 5,
  minuet = 6,
  bolero = 7,
  serenade = 8,
  requiem = 9,
  nocturne = 10,
  prelude = 11,
  scarecrow = 12,
};
inline constexpr std::size_t kSongCount = 13;

// Where a warp song lands (Dm_SY_Ocarina, entry OcarinaWarp: ChildBanc, AdultBanc and
// NextStartPos; the StartPos placement of that name in the scene's Banc).
struct WarpPad {
  std::string_view child_scene;  // "SacredForestMeadow_Child"
  std::string_view adult_scene;  // "SacredForestMeadow_Adult"
  std::string_view start_pos;    // "ForestMinuet"
  protocol::Vec3 position{};     // the StartPos's Translate, in both eras (x, y up, z)
};

struct SongInfo {
  Song song = Song::zelda_lullaby;
  std::string_view id;           // the game's: Item_Melody_<id>
  std::string_view item;         // "IsGet.Item_Melody_ZeldaLullaby": the group's (game_items)
  std::string_view spanish;      // "la Nana de Zelda", for the notices
  std::string_view learned_by;   // the event that teaches it (the story list's), or a note
  std::optional<WarpPad> warp;   // warp songs only
};

// The thirteen, in Song order (songs()[i].song == Song(i)).
std::span<const SongInfo> songs();
const SongInfo& song_info(Song song);
// MelodyType 0 to 12; nothing for any other number.
std::optional<Song> song_from_melody_type(int type);
// "ZeldaLullaby", "Item_Melody_ZeldaLullaby" or "IsGet.Item_Melody_ZeldaLullaby".
std::optional<Song> song_from_id(std::string_view id);
// The warp song that lands in `scene` (either era), or nothing.
std::optional<Song> warp_song_to(std::string_view scene);

// How a song's effect is shared with several players.
enum class SongRule : std::uint8_t {
  item,        // knowing the song: the group's (game_items.cpp, IsGet.Item_Melody_*)
  story,       // a story cutscene and its flags (the story list, shared_story.txt)
  once,        // a world flag set for good: once: in shared_dungeon_songs.txt
  shared,      // a world flag both ways (Pierre moves): a plain line there
  hold,        // a room gimmick's played event: hold: there (issue 129's rule)
  key_once,    // a placement's one-time 64-bit key: a key64: line there
  key_toggle,  // a placement's 64-bit key that goes both ways: not listed yet (see notes)
  built_in,    // already shared by a built-in list (coop_dungeon.cpp, the child/adult kit)
  local,       // only the game of who plays (a passing effect)
  time,        // the clock: the time-of-day sync (coop_time_sync.h), a jump for everyone
  warp,        // only who plays travels; the mates in that scene may follow (D-067)
};
const char* to_string(SongRule rule);

// How far it is checked.
enum class SongStatus : std::uint8_t {
  done,       // in a list or in code already, checked without the game
  to_run,     // written here (list line or code) and tested without the game; not seen in a game
  to_find,    // needs a run first: what the game writes is not known
  each_game,  // left to each game on purpose (see the note)
};
const char* to_string(SongStatus status);

// One effect of one song in one place.
struct SongEffect {
  Song song = Song::zelda_lullaby;
  std::string_view place;   // a scene or dungeon ("SpiritTemple", "KakarikoVillage_Child")
  std::string_view what;    // what the player sees
  SongRule rule = SongRule::local;
  SongStatus status = SongStatus::to_find;
  // The flag-list lines this effect brings (shared_dungeon_songs.txt), "" when none:
  // "once:Name", "Name", "hold:Name Place" or "key64:0x... # comment".
  std::string_view lines;
  std::string_view note;    // where it comes from, what is not known
};

// Every effect the work knows of, by song then place.
std::span<const SongEffect> song_effects();

// The flag list the effects bring, as the kit's shared_dungeon_songs.txt holds it
// (a DungeonList file: coop/host/dungeon_lists.hpp). The test checks they are the same.
std::string song_flag_list_text();

// The Song of Time blocks to watch (LiveCheck, coop/host/live_check.hpp): one watch per
// place of a key_once or key_toggle effect, with its key64 / toggle64 keys and the named
// Bools the built-in list shares for that place's blocks; the actor is Obj_SongOfTimeBlock,
// the cause our Ocarina. Whether a block answers its key live, on the next load, or a block
// goes with no listed key (the list misses it) is then one log line ("coop: live: ...").
std::vector<LiveWatch> song_block_watches();

// ---- The warp-song invitation (oot-64 D-067, adapted to games that each own their Link) --
//
// oot-64: the one who plays a warp song leaves alone; for 10 s the others of the zone may
// hold L 0.75 s to leave with him. Here the warper's own game runs its warp as always; the
// others' games see it from his samples: a mate who was in our scene shows up, within
// kLandWindowUs of his scene change, standing within kPadRadius of the song's pad in the
// song's scene (the arrival holds him there; a walk-in lands elsewhere: the Water Temple's
// exit is 8.6 from the Serenade's pad), and, when his animations reach us (puppets on), his
// Ocarina was out in our scene within kSongUs before he left. Then our game offers the trip for kInviteUs: holding the button kHoldUs
// (our Link up, no event of ours: the trip refuses to start in one) follows him with
// /teleport's trip (CoopTeleport::Trip): the same scene as his, beside where he stands.
// The game takes the trip's scene change only while an event runs (docs/scenes.md), so the
// trip asks again for 60 s and the notice says to take the Ocarina out (an event) to go.
struct InviteMate {
  std::uint8_t number = 0;
  std::string name;                       // the nickname ("" shows "P<n>")
  std::optional<std::string> scene;       // his newest sample's scene
  std::optional<protocol::Vec3> position; // where he is shown now
  std::optional<std::string> anim;        // his newest AS command (PLAYER_SAMPLE field 7), if any
};

struct InviteInput {
  std::uint64_t now_us = 0;
  std::optional<std::string> scene;  // ours; nothing while loading or no game
  bool standing = false;             // our Link is up and no event of ours runs
  bool held = false;                 // the invitation's button is held now
  std::vector<InviteMate> mates;
  // The other players in the room (Presence::players), when known. A mate missing from
  // `mates` (no pose shown for a moment, his warp's load) is kept for kAbsentUs while he is
  // still in the room; one that left the room is forgotten at once. Nothing: only the time.
  std::optional<std::vector<std::uint8_t>> room;
};

struct InviteOutput {
  std::vector<std::string> notices;   // for the chat box (Spanish, for the player)
  std::vector<std::string> log;       // for the log ("coop: songs: " is added)
  std::optional<std::uint8_t> follow; // start the trip to this player now
  std::string follow_name;            // as shown
};

class WarpInvite {
 public:
  static constexpr std::uint64_t kInviteUs = 10'000'000;    // D-067: 10 s
  static constexpr std::uint64_t kHoldUs = 750'000;         // D-067: 0.75 s
  static constexpr std::uint64_t kLandWindowUs = 4'000'000; // his landing is looked for this long
  static constexpr std::uint64_t kLandSettleUs = 500'000;   // the shown pose catches up first
  static constexpr std::uint64_t kSongUs = 30'000'000;      // the Ocarina out at most this long before
  static constexpr float kPadRadius = 3.0f;                 // game units (about metres), on the ground plane
  static constexpr std::uint64_t kAbsentUs = 10'000'000;    // a mate missing from the list is kept this long

  // `button`: the name the notice gives the button ("L", LICHII_COOP_WARP_INVITE_BUTTON).
  explicit WarpInvite(std::string button = "L") : button_(std::move(button)) {}

  // The gesture's name changed (InviteButton: "L" to "L+R"): for the next notices.
  void set_button(std::string button) { button_ = std::move(button); }
  const std::string& button() const { return button_; }

  // Once a frame (a few times a second is enough).
  InviteOutput tick(const InviteInput& in);

  // The open invitation's player, 0 for none.
  std::uint8_t open() const { return invite_.number; }

 private:
  struct Seen {
    std::uint8_t number = 0;
    std::string scene;              // the last scene his samples carried
    std::string left;               // the scene he left, while it was ours
    std::uint64_t changed_us = 0;   // when his scene last changed (to `scene`)
    bool looked = false;            // this landing is decided
    bool anim_known = false;        // an animation of his has reached us
    std::optional<std::uint64_t> ocarina_us;  // his Ocarina last seen out, in our scene
    std::uint64_t listed_us = 0;    // when he was last in the mates' list
  };
  struct Invite {
    std::uint8_t number = 0;
    std::string name;
    Song song = Song::minuet;
    std::string to;                 // his scene
    std::string from;               // ours, when it was offered
    std::uint64_t until_us = 0;
  };
  Seen& seen(std::uint8_t number);

  std::string button_;
  std::vector<Seen> seen_;
  Invite invite_;
  std::optional<std::uint64_t> held_since_us_;  // since when the button is held, while open
  // The button was seen up since the invitation opened: a hold that began before it (a
  // shield held up, a camera button) never follows by itself.
  bool released_ = false;
};

// Is the invitation's button free in the remake? (CANCIONES.md, "Por ver" 4.) The remake may
// give L (or the button chosen) to the game: then a player who holds it to play (a shield, a
// camera) would follow a mate by accident inside the 10 s. So the button is watched as the
// player plays: on each press with our Link up, no invitation open and his AS command steady
// for kReactUs before, does the command change within kReactUs? After kPresses presses, if
// kTakenAfter or more changed it the button is the game's ("taken") and the invitation's
// gesture becomes the button and its partner held together (L+R, ZL+ZR, PLUS+MINUS,
// LCLICK+RCLICK), which play never holds both of for 0.75 s; otherwise it is "free" and the
// button alone stays. Until then the button alone counts (and only a fresh press, WarpInvite).
// One log line says the verdict and the gesture.
class InviteButton {
 public:
  static constexpr std::uint64_t kReactUs = 300'000;
  static constexpr int kPresses = 3;
  static constexpr int kTakenAfter = 2;

  InviteButton(std::string name, std::uint64_t bit, std::string partner, std::uint64_t partner_bit)
      : name_(std::move(name)), bit_(bit), partner_(std::move(partner)), partner_bit_(partner_bit) {}

  struct Input {
    std::uint64_t now_us = 0;
    std::optional<std::uint64_t> buttons;  // the pad's; nothing: no pad
    std::optional<std::string> animation;  // our Link's AS command
    bool standing = false;                 // our Link up, no event of ours
    bool invitation_open = false;
  };
  // Every frame (or a few times a second). Returns the log lines (verdict).
  std::vector<std::string> frame(const Input& in);

  bool held() const { return held_; }       // the invitation's gesture is held now
  bool decided() const { return decided_; }
  bool taken() const { return taken_; }
  // "L" or "L+R": what the notice tells the player to hold.
  std::string label() const { return taken_ ? name_ + "+" + partner_ : name_; }
  // The buttons the invitation's hold needs, all down (ShoulderArbiter::set_invite).
  std::uint64_t bits() const { return taken_ ? bit_ | partner_bit_ : bit_; }

 private:
  std::string name_;
  std::uint64_t bit_;
  std::string partner_;
  std::uint64_t partner_bit_;
  bool held_ = false;
  bool was_down_ = false;
  bool decided_ = false;
  bool taken_ = false;
  int presses_ = 0;
  int moved_ = 0;
  std::string example_;  // "'Wait' -> 'GuardStart'"
  std::string last_anim_;
  std::uint64_t anim_since_us_ = 0;
  struct Probe {
    std::uint64_t since_us = 0;
    std::string before;
    std::string moved_to;  // "" while unchanged
  };
  std::optional<Probe> probe_;
};

// "Saria" or "P2".
std::string mate_label(std::uint8_t number, std::string_view name);

}  // namespace coop::host
