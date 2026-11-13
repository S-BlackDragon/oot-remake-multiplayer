// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM efectos2 (oot-remake docs/DETALLES_MARIONETA.md, "Efectos 2"; D-R37:
// "we see each other doing anything"). What a puppet shows beyond its
// player's copied pose. No emulator code here: tested on its own.
//
// With the pose copy (the default) a puppet plays no AS command, so the
// effect and sound events of its player's animation never fire on it: no
// spell cast, no splash, no dust. Four things are added here:
//
// 1. Allowed commands. While the pose copy drives a puppet, its player's AS
//    command (PLAYER_SAMPLE field 7) is played on it as well when an
//    allow-list says it is harmless: the pose copy still writes every bone,
//    the command is there only for its events (EmitEffect, AtSound, Voice)
//    on the puppet's own effect and sound users. The list is data (Rules,
//    kDefaultRules, LICHII_ORM_EFFECTS_LIST): attacks (hitboxes), damage
//    (the heart gauge), lifting, throwing, items, demos and the horse are
//    never played. A command off the list after one on it plays the rest
//    command (Wait), so a cast's looping effect does not stay on.
// 2. The damage flash. A new damage command of its player (a `flash` line)
//    makes the puppet blink for kFlashMs: drawn kHideDrop under its spot on
//    every other kBlinkMs, as Link blinks while he cannot be hurt. The
//    knock-back itself comes with the pose and the position.
// 3. Burn, freeze and shock. While its player's command says so (a `status`
//    line, held at least the status' minimum) an effect stand-in of ours
//    (CoopFx_Burn, _Freeze, _Shock: the effect user of an effect locator,
//    no model, physics, chemistry or attack; tools/puppet_pack FX_STAND_INS)
//    is placed on the puppet by the spawn director (coop_spawn.h).
// 4. Nayru's Love. After its player's cast (a `nayru` line) the crystal
//    stand-in CoopFx_Nayru stays on the puppet for the spell's time
//    (Settings::nayru_ms), until that player changes scene, is downed or
//    takes damage that is not blocked (a `flash` command).
//
// Switches (coop_svc.cpp, coop_switch.h): LICHII_ORM_EFFECTS (1), the
// damage flash LICHII_ORM_DAMAGE_FLASH (2), the status and crystal
// stand-ins LICHII_ORM_STATUS_FX (3 and 4), all on; LICHII_ORM_NAYRU_S the
// crystal's seconds; LICHII_ORM_EFFECTS_LIST a rules file instead of the
// built-in list.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace CoopEffects {

enum class Status : std::uint8_t { none, burn, freeze, shock, nayru };
inline constexpr std::size_t kStatusCount = 5;
const char* StatusText(Status status);  // "none", "burn", "freeze", "shock", "nayru"
// "burn" -> burn ...; anything else: nothing ("none" too).
std::optional<Status> StatusNamed(std::string_view word);
// The effect stand-in that shows it (tools/puppet_pack FX_STAND_INS); ""
// for none.
std::string_view StandInFor(Status status);
// Whether `actor` is one of those stand-ins.
bool IsFxStandIn(std::string_view actor);
inline constexpr std::array<std::string_view, 4> kFxStandIns{"CoopFx_Burn", "CoopFx_Freeze",
                                                             "CoopFx_Shock", "CoopFx_Nayru"};

// A command name against a pattern whose only wildcard is `*` (any run,
// empty too). Case counts, as in the game's AS.
bool Match(std::string_view pattern, std::string_view name);

// The rules, one per line; `#` starts a comment, blank lines are skipped:
//   play <pattern>      the puppet plays it as well while posed
//   skip <pattern>      it never does
//   flash <pattern>     a damage command: the puppet blinks
//   noflash <pattern>   no blink (blocked by Nayru's Love, getting up)
//   status <burn|freeze|shock> <pattern>
//   nayru <pattern>     a cast of Nayru's Love
//   rest <command>      played after an allowed command (default Wait)
// Within play/skip, within flash/noflash, among the status lines and among
// the nayru lines, the first line that matches decides; nothing matching
// is skip, noflash, no status, no cast.
class Rules {
public:
    // Unreadable lines are left out, one text each in `errors` (may be null)
    // as "line <n>: <why>".
    static Rules Parse(std::string_view text, std::vector<std::string>* errors = nullptr);
    static const Rules& Default();  // Parse(kDefaultRules)

    bool Plays(std::string_view command) const;
    bool Flashes(std::string_view command) const;
    Status StatusOf(std::string_view command) const;  // burn, freeze, shock or none
    bool CastsNayru(std::string_view command) const;
    const std::string& rest() const { return rest_; }
    std::size_t size() const { return lines_.size(); }

private:
    enum class Kind : std::uint8_t { play, skip, flash, noflash, status, nayru };
    struct Line {
        Kind kind = Kind::skip;
        std::string pattern;
        Status status = Status::none;
    };
    const Line* First(std::string_view command, Kind a, Kind b) const;

    std::vector<Line> lines_;
    std::string rest_ = "Wait";
};

// The built-in list (oot-remake docs/DETALLES_MARIONETA.md, "Efectos 2",
// has the reasons for each line).
extern const char* const kDefaultRules;

// While the pose copy drives a puppet: the command to play on it now, or
// nothing. `wanted`: its player's command (shown time or newest); `played`:
// what was last played on the puppet ("" none). An allowed command is
// played; one that is not, after an allowed one, gives the rest command
// once; otherwise nothing is played (as before this file).
struct Command {
    std::string name;
    float frame = 0.0f;
    float rate = 1.0f;
    bool rest = false;  // the rest command, not the player's
};
std::optional<Command> PosedCommand(const Rules& rules, const std::optional<Command>& wanted,
                                    std::string_view played);

inline constexpr std::uint64_t kFlashMs = 1200;
inline constexpr std::uint64_t kBlinkMs = 50;  // three game frames
// The status minimums: a burn's or a shock's command can be shorter than
// what it shows; frozen is shown exactly while it lasts.
inline constexpr std::uint64_t kBurnMinMs = 1500;
inline constexpr std::uint64_t kShockMinMs = 800;
inline constexpr std::uint64_t kFreezeMinMs = 0;
// A remote not seen in a plan for this long is forgotten.
inline constexpr std::uint64_t kForgetMs = 10000;
// How high above the puppet's feet a stand-in is put (metres).
inline constexpr float kFxLift = 0.5f;

// Whether a puppet flashing since `since_ms` (0: not flashing) is drawn
// under the floor this frame, and whether it is still flashing (`margin`
// more ms counted as flashing).
bool FlashHidden(std::uint64_t since_ms, std::uint64_t now_ms);
bool Flashing(std::uint64_t since_ms, std::uint64_t now_ms, std::uint64_t margin = 0);

struct Settings {
    bool as_effects = false;  // 1
    bool flash = false;       // 2
    bool status = false;      // 3 and 4
    std::uint64_t nayru_ms = kNayruDefaultMs;
    Rules rules = Rules::Default();
    bool any() const { return as_effects || flash || status; }
    static constexpr std::uint64_t kNayruDefaultMs = 60000;
};

// The switches (coop_effects.h top), each on unless set off; a rules file
// that cannot be read, a line of it that cannot, or a bad LICHII_ORM_NAYRU_S
// leaves the built-in value and adds a line to `notes`, as does what was
// chosen ("coop: effects: ..."), for the log.
Settings FromSwitches(std::vector<std::string>& notes);

// ORM riesgos-visual (oot-remake docs/RIESGOS_RESUELTOS.md): the crystal's
// time read from the game. tools/puppet_pack reads NayruLoveTime from the
// player's own PlayerGameParam (34.0 in the dump of 2026-11-11) into its
// coop_puppet.json as "nayru_s"; this is that value, nothing when absent,
// null or not 1 to 600.
std::optional<double> NayruSecondsIn(std::string_view manifest);
// LICHII_ORM_NAYRU_S when set (`switch_set`), else the pack's, else the
// 60 s estimate, with one "coop: check: nayru crystal" line of the path.
void PickNayru(Settings& settings, bool switch_set, std::optional<double> pack_s);
// The same from given values (nullptr: unset), for the tests. `list_text`:
// the rules file's text when one was named and read.
Settings FromValues(const char* effects, const char* flash, const char* status, const char* nayru_s,
                    const char* list_path, const std::optional<std::string>& list_text,
                    std::vector<std::string>& notes);

// What one remote's puppet shows beyond the pose, now.
struct Look {
    std::uint64_t flash_since_ms = 0;  // 0: not flashing
    Status status = Status::none;      // the stand-in on it (one at a time)
};

// The present thread (the puppets' Planner): each remote's state from its
// commands, plan after plan.
class Tracker {
public:
    // One remote in this plan. `scene`: theirs ("" not known); `anim`: their
    // newest command, if any; `downed`: PLAYER_STATUS dead.
    Look Update(const Settings& settings, std::uint8_t number, std::string_view scene,
                std::optional<std::string_view> anim, bool downed, std::uint64_t now_ms);
    // Drops remotes not updated for kForgetMs.
    void Prune(std::uint64_t now_ms);
    std::size_t size() const { return players_.size(); }

    // For the log: what changed in the last Update of that player, or "".
    const std::string& Said(std::uint8_t number) const;

private:
    struct Player {
        std::string anim;              // the last command seen
        bool anim_known = false;
        std::string scene;
        std::uint64_t seen_ms = 0;
        std::uint64_t flash_since_ms = 0;
        Status status = Status::none;  // burn, freeze or shock
        std::uint64_t status_until_ms = 0;
        bool casting = false;          // a Nayru cast command now
        std::uint64_t nayru_until_ms = 0;
        Status shown = Status::none;   // the last Look's status (for the log)
        std::string said;
    };
    std::map<std::uint8_t, Player> players_;
    std::string none_;
};

// The stand-in copy a plan asks the spawn director for: one per shown
// puppet with a status, at its place lifted by kFxLift. The key has
// kFxKeyBit (coop_spawn.h: SetEffects keys have bit 60), the player, the
// status and `generation` (the glue counts our scene changes: a load
// deletes the stand-in, and a key once made is not made again).
struct FxPlace {
    std::uint8_t number = 0;
    Status status = Status::none;
    std::array<float, 3> position{};
    std::uint64_t key = 0;
    std::string actor;
};
inline constexpr std::uint64_t kFxKeyBit = 1ULL << 60;  // 61: the chest finds (CoopSpawn kFindKeyBit)
// `episode` (bits 32 to 47): which time this player shows this status in
// this scene (FxKeys).
std::uint64_t FxKey(std::uint8_t number, Status status, std::uint16_t generation = 0,
                    std::uint16_t episode = 0);
FxPlace FxAt(std::uint8_t number, Status status, const std::array<float, 3>& feet,
             std::uint16_t generation = 0, std::uint16_t episode = 0);

// ORM review (REVISION_FINAL.md "Visual"): the keys of each plan's stand-ins.
// The spawn director makes a key once (its started set is only emptied for
// a new game), so a status that ends deletes its stand-in for good: with one
// key per player, status and scene, a player's second burn (or second Nayru's
// Love) in the same scene showed nothing. Here a pair that shows again after
// a plan without it gets a new episode, and our scene change a new
// generation; a pair shown plan after plan keeps its key (no remake).
class FxKeys {
public:
    struct Shown {
        std::uint8_t number = 0;
        Status status = Status::none;
    };
    // One key per entry of `shown`, in order; `scene` is ours.
    std::vector<std::uint64_t> Plan(std::string_view scene, const std::vector<Shown>& shown);
    std::uint16_t generation() const { return generation_; }

private:
    std::string scene_;
    bool planned_ = false;
    std::uint16_t generation_ = 0;
    std::uint16_t episodes_ = 0;
    std::map<std::uint16_t, std::uint16_t> last_;  // (number << 8 | status) -> episode, the last plan's
};

// The glue's side (coop_effects_game.cpp, the emulator only): with the
// status stand-ins on, each plan's (CoopPuppet::OnPlan) stand-ins go to
// the spawn director. Nothing when `settings.status` is off.
void UseGame(const Settings& settings);

}  // namespace CoopEffects
