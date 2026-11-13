#pragma once
// ORM PvP and the room's switches (oot-remake docs/COMANDOS.md, "PvP"), the
// portable side: no emulator, no clock of its own (times in), no network.
//
// - RoomSwitches: the room's PvP and "Fijar aliados" switches as the server
//   last said them (ROOM_SWITCH), with who set them and the notices.
// - Attacker: our Link's sword swings and our shots near a mate's puppet,
//   turned into PLAYER_HITs (only while PvP is on; each swing or shot hits a
//   mate once).
// - Victim: a PLAYER_HIT for us judged by our own game (shield toward it,
//   Nayru's Love, our invulnerability after a hit, a repeat, too far, downed,
//   the last one up), and what to apply.
// - Shove: a push spread over a few frames (knockback without the game's
//   physics: our Link is moved, the game's gravity drops him off a ledge).
// - push_out: our Link put out of a mate's puppet (they are not solid in our
//   game), both ways since every game moves its own Link.
//   Not used by the hook any more: a written position skips the game's
//   collision, so the puppet is drawn aside instead (orm_bodies.hpp).
// - blast_hurt: a mate's bomb that went off with no copy here, our Link in
//   its reach: the blast our game never saw (no double damage: with a copy,
//   the game's own explosion hurts us and this gives nothing).
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "coop/protocol/messages.hpp"

namespace coop::host::orm::pvp {

using protocol::Vec3;

// ---- The room's switches ----------------------------------------------------

struct SwitchState {
  bool on = false;
  std::uint8_t by = 0;      // who set it (0: never set in this room)
  std::uint64_t at_us = 0;  // server clock
};

// "PvP", "Fijar aliados" and "Choque entre jugadores".
std::string_view switch_name(protocol::RoomSwitchKey key);
// A switch the room never set reads this (the server keeps no value for it).
// ORM choque: bodies on (oot-remake docs/CHOQUE.md): confirmed in game on
// test10 (choque-bloqueo, choque-encima, choque-continuar, cartel, charla;
// 2026-11-13). A room's /choque off still turns them off for everyone.
inline constexpr bool kBodiesDefault = true;
bool default_on(protocol::RoomSwitchKey key);

class RoomSwitches {
 public:
  using Names = std::function<std::string(std::uint8_t)>;  // a player's shown name
  // A new connection (WELCOME at `welcome_us`, server clock): every switch is
  // off until the server sends the room's (it does right after WELCOME).
  void connection_new(std::uint64_t welcome_us);
  // The server's ROOM_SWITCH: the notices to show ("PvP activado por Ana";
  // for the state a room already had when we came, "En esta sala el PvP está
  // activado (lo activó Ana)"; nothing for one already so, its default
  // included: default_on).
  std::vector<std::string> received(const protocol::RoomSwitch& m, const Names& names);
  bool on(protocol::RoomSwitchKey key) const;
  SwitchState state(protocol::RoomSwitchKey key) const;
  // "PvP: activado (lo activó Ana)", "Fijar aliados: desactivado".
  std::string describe(protocol::RoomSwitchKey key, const Names& names) const;
  // What a client sends to set `key` to `on`.
  static protocol::RoomSwitch request(protocol::RoomSwitchKey key, bool on);

 private:
  std::map<std::uint8_t, SwitchState> states_;
  std::uint64_t welcome_us_ = 0;
};

// ---- Weapons -----------------------------------------------------------------

// A sword (or hammer) swing, from our Link's AS command in the main slot.
struct Swing {
  protocol::HitWeapon weapon = protocol::HitWeapon::sword;
  std::uint8_t damage = 0;  // quarter hearts
  float reach = 0.0f;       // metres from our Link, sideways
  float cone_cos = 0.0f;    // in front: cos of the half angle (-1: all round, a spin)
  float push = 0.0f;        // metres
};
// Sword_Attack_*, LSword_Attack_* (the long sword), Sword_Attack_Jump and
// CutJump* (the jump attack), *Spin* / *Rotate* / *Kaiten* (the spin),
// Hammer_* (the Megaton Hammer); nothing for anything else (Guard_*, a walk).
std::optional<Swing> swing_of(std::string_view command);

// Our projectile, by its actor name.
struct Shot {
  protocol::HitWeapon weapon = protocol::HitWeapon::arrow;
  std::uint8_t damage = 0;
  float radius = 0.0f;  // metres from the puppet's middle
  float push = 0.0f;
  bool pull = false;    // the hookshot: toward us
};
// NormalArrow, Shootable_DekuSeed, Shootable_Hook, Shootable_HookLong,
// Weapon_Boomerang, ExpandFireField (Din's Fire); nothing for the others (a
// bomb hurts through its explosion in every game, never as a hit).
std::optional<Shot> shot_of(std::string_view actor);

// ---- Attacker ------------------------------------------------------------------

struct Mate {
  std::uint8_t number = 0;
  Vec3 at{};            // the puppet's feet, as drawn here
  bool up = true;       // not downed, not reviving
  bool here = true;     // in our scene, its samples fresh
};

struct OwnShot {
  std::uint64_t id = 0;  // stable while it flies (the spawn sync's)
  std::string actor;
  Vec3 at{};
};

struct AttackFrame {
  std::uint64_t now_us = 0;
  bool pvp = false;          // the room's switch
  bool able = false;         // our Link up and playing (no event, not downed)
  std::optional<std::string> command;  // our Link's AS command
  Vec3 at{};                 // our Link's feet
  Vec3 forward{0, 0, 1};     // our Link's facing, flat
  std::vector<Mate> mates;
  std::vector<OwnShot> shots;
};

class Attacker {
 public:
  static constexpr float kBodyHalfHeight = 0.9f;  // a puppet's middle above its feet
  static constexpr float kTallM = 1.8f;           // sword reach up and down
  // PLAYER_HITs for this frame (player and hit_id filled but player 0: the
  // server writes ours).
  std::vector<protocol::PlayerHit> step(const AttackFrame& f);
  void reset();

 private:
  std::uint32_t next_id_ = 0;
  std::string swing_;                 // the command of the swing in progress ("" none)
  std::set<std::uint8_t> swing_hit_;  // mates this swing has hit
  std::map<std::uint64_t, std::set<std::uint8_t>> shot_hit_;  // by shot id
  std::uint32_t next();
};

// The flat push from `from` to `to` (pull: from `to` back toward `from`),
// `metres` long, never past `from` for a pull (it stops 1 m short).
Vec3 push_between(const Vec3& from, const Vec3& to, float metres, bool pull);

// ---- Victim --------------------------------------------------------------------

// ---- The victim's shield (what our game shows of it) ---------------------------
//
// The 0.12 game check (pvp-espada, 2026-11-12) took a hit B meant to block:
// B wore no shield (PLAYER_EQUIP shield ''), held ZL, and the main slot played
// "LockOnWait", which no shield rule knew. The real commands of
// Player_Child/Adult.root.asb (romfs AS, read 2026-11-13) are "ShieldGuard",
// "ShieldGuard_Caution", "GuardHit", "GuardBreak", "CrouchGuard",
// "CrouchGuardChild" and the animations Guard_*, *_ShieldGuard*
// (LockOn_Wait_ShieldGuard, Pose_Sword_ShieldGuard_*: the upper body's pose
// while the legs lock on or walk). So a raised shield is read three ways:
// - command: a guard command in any of our Link's AS slots (slot 0 first);
// - button: the guard button held (ZL, the game's guard; LICHII_ORM_PVP_GUARD)
//   with a shield equipped, our Link standing, walking or locked on (not in a
//   swing, a hit reaction or Nayru's Love), for a guard the slots do not name;
// - none: no shield equipped (EquipShield ""), or none of the above.
struct GuardView {
  std::vector<std::string> commands;  // our Link's AS commands, slot 0 (main) first; "" an empty slot
  std::optional<std::string> shield;  // the equipped shield's actor ("" none); nothing: not read
  std::optional<bool> guard_held;     // the guard button held now; nothing: no pad read
};
enum class GuardPath : std::uint8_t {
  command,     // a guard command plays in a slot
  button,      // the guard button held with a shield on, in a pose that guards
  no_shield,   // no shield equipped: nothing to raise
  not_raised,  // a shield on, but neither a guard command nor the button
  unread,      // no slot read at all (no Link)
};
const char* to_string(GuardPath p);
struct Guard {
  bool up = false;
  GuardPath path = GuardPath::unread;
  std::string command;  // the command that showed it (command), else the main one
  int slot = -1;        // the slot of `command`
  bool nayru = false;   // a slot plays Nayru's Love (it blocks from every side)
  // "command 'ShieldGuard' in slot 2", "the guard button with Weapon_Shield_Deku
  // ('LockOnWait')", "no shield equipped", "not raised ('Wait', Weapon_Shield_Deku)".
  std::string describe() const;
};
// The main command's poses in which the held guard button raises the shield:
// waiting, walking, running, crouching, turning or locked on (Wait*, Walk*,
// Run*, Crouch*, Turn*, Brake*, *LockOn*); never a swing (swing_of), a hit
// reaction (reacting) or Nayru's Love (nayru).
bool guards_by_button(std::string_view main_command);
Guard guard_of(const GuardView& view);

struct VictimFrame {
  std::uint64_t now_us = 0;
  bool pvp = false;                    // the room's switch, as we have it
  bool up = false;                     // our Link up and playing
  std::optional<std::string> command;  // our Link's AS command
  Vec3 at{};
  Vec3 forward{0, 0, 1};
  std::optional<std::int32_t> life;    // quarter hearts
  bool others_up = true;               // another player of the room is up
  // The shield as guard_of read it; nothing: the main command alone (shielding).
  std::optional<Guard> guard;
};

enum class Verdict : std::uint8_t {
  take,          // damage and push
  push_only,     // the push, no damage (a push_only hit)
  blocked,       // the shield toward it, or Nayru's Love: nothing
  invulnerable,  // just hit: nothing
  repeat,        // the same hit again: nothing
  off,           // PvP off here: nothing
  down,          // our Link is not up: nothing
  too_far,       // the attacker is too far for that weapon: nothing
  kept_off,      // upstream's victim rules kept it off (cutscene, menu, load, mercy): nothing
};
const char* to_string(Verdict v);

struct Judged {
  Verdict verdict = Verdict::off;
  std::int32_t damage = 0;  // quarter hearts to take (take)
  Vec3 push{};              // to apply (take, push_only)
  bool spared = false;      // the last quarter left: we were the last one up
  // The shield's part, for the log, once the judge reached it: "shield up
  // (command 'ShieldGuard' in slot 0), facing the hit (cos 0.97)", "shield up
  // (...) but the hit came from behind it (cos -0.80)", "no shield equipped";
  // "" when it was decided before (repeat, off, down, too far, Nayru).
  std::string shield;
};

// Shield commands (the real ones above): Guard* (Guard_*, GuardHit,
// GuardBreak, GuardBeam*), CrouchGuard*, *ShieldGuard*; not the Nayru ones
// (nayru) nor a failed guard (*FailHit: the hit got through).
bool shielding(std::string_view command);
// Nayru's Love: GuardNayru, GuardBreakNayru, Guard_Nayru*, Magic_Nayru*,
// StartNayruLove, Damage_Land_Nayru...: any "Nayru" but EndNayruLove.
bool nayru(std::string_view command);
// Our own hit reaction plays (Damage*: DamageS, DamageM, Damage_*): the
// game's own invulnerability.
bool reacting(std::string_view command);

class Victim {
 public:
  static constexpr std::uint64_t kInvulnerableUs = 800'000;  // after a hit taken
  static constexpr float kShieldCos = 0.34f;                 // the shield covers 70 degrees each way
  static constexpr float kMeleeFarM = 6.0f;                  // sword or hammer: from at most here
  static constexpr float kShotFarM = 80.0f;
  static constexpr std::size_t kKeep = 64;                   // hits remembered for repeats
  Judged judge(const protocol::PlayerHit& hit, const VictimFrame& f);
  void reset();

 private:
  std::uint64_t last_taken_us_ = 0;
  std::deque<std::pair<std::uint8_t, std::uint32_t>> seen_;
};

// ---- On upstream's PvP (test10, D-R21) ------------------------------------------
//
// Since test10 the session's PvP is upstream's (PVP_MODE, PVP_HIT, PVP_HURT,
// lichii/hook/coop_pvp*): its switch, its attacker (the game's own foliage cut
// and shot ends) and its first victim rules (cutscene, menu, load, the 0.8 s
// mercy). Ours is a thin layer on it, what we asked for and upstream lacks:
// - attacker: the real weapon's damage (long sword, spin and jump attacks,
//   the hammer, our shot table) instead of one amount per cause;
// - victim: the shield toward the attacker, Nayru's Love, our invulnerability
//   (after a hit, and while our own hit reaction plays), the last one up never
//   left at 0, and the knockback (a Shove, applied by the hook).
// The 15 s "downed by PvP counts as up" grace is the server's (session.cpp,
// pvp_down_hold_us), armed by upstream's PVP_HIT too.

// The weapon our PLAYER_HIT rules know for an upstream cause.
protocol::HitWeapon weapon_of(protocol::DamageCause cause);
// The knockback of a cause: metres, and whether it pulls (the hookshot).
struct Knock {
  float metres = 0.0f;
  bool pull = false;
};
Knock knock_of(protocol::DamageCause cause, std::string_view attacker_command);
// The damage our weapon really does, for the PVP_HIT our game sends: for a
// sword or hammer cause, from our Link's AS command (swing_of: 1/2 heart a
// swing, 1 heart with the long sword, 1 or 2 for spin and jump attacks, 2 for
// the hammer); for shots, from shot_of's table by cause; upstream's amount
// when we know nothing better (an unknown command). 0 to kMaxPvpLife.
std::int32_t attack_amount(protocol::DamageCause cause, std::int32_t upstream_amount,
                           std::string_view our_command);

// Our victim rules on upstream's PVP_HIT. upstream_take is what upstream's
// CoopPvp::Victim decided (0: it kept the hit off; then nothing more).
class UpstreamVictim {
 public:
  Judged judge(const protocol::PvpHit& hit, std::int32_t upstream_take, std::string_view attacker_command,
               const VictimFrame& f);
  void reset() { victim_.reset(); }

 private:
  Victim victim_;
  std::uint32_t next_id_ = 0;
};

// ---- Shove ---------------------------------------------------------------------

// A push spread over kShoveUs, fast first (ease out). step() gives the part
// of it for this frame, to add to where our Link is now.
class Shove {
 public:
  static constexpr std::uint64_t kShoveUs = 220'000;
  void start(const Vec3& push, std::uint64_t now_us);
  std::optional<Vec3> step(std::uint64_t now_us);
  bool active() const { return active_; }
  void stop() { active_ = false; }

 private:
  Vec3 push_{};
  std::uint64_t since_us_ = 0;
  float done_ = 0.0f;
  bool active_ = false;
};

// ---- Bodies ----------------------------------------------------------------------

inline constexpr float kBodyRadius = 0.35f;     // a Link, metres (two: 0.7 apart)
inline constexpr float kBodyHeight = 1.5f;      // feet this close in height touch
inline constexpr float kMaxPushOutM = 0.12f;    // a frame, at most: never a jump
// What to add to our Link to stand out of every mate's body: half the overlap
// of each (the mate's game does the other half), away from it; a mate exactly
// on top of us is left by a direction from our numbers (never stuck). Down
// mates are left out (a reviver kneels by them). Nothing when none touches.
std::optional<Vec3> push_out(const Vec3& self, std::uint8_t self_number, std::span<const Mate> mates);

// ---- Bombs -----------------------------------------------------------------------

inline constexpr float kBlastReachM = 4.0f;           // a bomb's blast, about
inline constexpr std::uint8_t kBlastDamage = 4;       // quarter hearts (1 heart), as one's own bomb
inline constexpr float kBlastPushM = 1.5f;
struct BlastHurt {
  std::int32_t damage = 0;
  Vec3 push{};
};
// A mate's bomb at `blast` went off and our game had no copy of it live, so
// no explosion of its own: our Link at `self`, if up and in reach, gets it
// here. With `copy_live` nothing (the game's explosion already hurt us).
std::optional<BlastHurt> blast_hurt(const Vec3& blast, const Vec3& self, bool up, bool copy_live);

// The flat way a rotation faces (its local +z, the actor's column 2, height
// dropped, unit length); (0, 0, 1) for a rotation that faces straight up or down.
Vec3 forward_of(const protocol::Quat& q);

// ---- Fijar aliados: the lock-on puppet ---------------------------------------------

// With the room's "Fijar aliados" on, the puppets are made as the pack's
// lock-on variant (CoopPuppet_<age>_Lock: the puppet plus the enemies'
// lock-on AttentionSender, tools/puppet_pack build.py), so Z-targeting takes
// a mate as it takes an enemy; off, the plain puppet again (the director
// remakes a live puppet whose actor changed). The check, once a session:
// ok when a lock-on puppet was made; fallback when the pack has no variant
// or one failed kFailures times (the plain puppet stays, the toggle is said
// unavailable). Fallback holds for the rest of the session.
class AllyTarget {
 public:
  static constexpr std::uint32_t kFailures = 3;
  enum class State : std::uint8_t { off, waiting, ok, fallback };
  struct Out {
    bool lock = false;                 // make the puppets as the lock-on variant
    std::optional<std::string> log;    // "check: ally target ok (...)", "check: ally target fallback (...)"
    std::optional<std::string> chat;   // Spanish, for the player, on a fallback
  };
  // Once a frame: the room's switch, whether the installed pack has the
  // variants, and the director's running totals of lock-on puppets made and
  // failed.
  Out step(bool wanted, bool pack_has, std::uint32_t made, std::uint32_t failed);
  State state() const { return state_; }
  bool available() const { return state_ != State::fallback; }

 private:
  State state_ = State::off;
  std::uint32_t made_at_start_ = 0;
  std::uint32_t failed_at_start_ = 0;
};

// ---- The group defeat ------------------------------------------------------------

// The victim's rule (COMANDOS.md "Derrota y PvP"): damage that would leave us
// at 0 while nobody else is up leaves one quarter (a friend's hit never ends
// the room's game; the server holds a fall to PvP as up for a while too).
std::int32_t spare_last(std::int32_t life, std::int32_t damage, bool others_up, bool& spared);

}  // namespace coop::host::orm::pvp
