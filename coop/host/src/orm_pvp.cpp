#include "coop/host/orm_pvp.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace coop::host::orm::pvp {

namespace proto = coop::protocol;

namespace {

bool has(std::string_view text, std::string_view part) { return text.find(part) != std::string_view::npos; }
bool begins(std::string_view text, std::string_view part) { return text.substr(0, part.size()) == part; }

float flat_distance(const Vec3& a, const Vec3& b) {
  const float x = a[0] - b[0], z = a[2] - b[2];
  return std::sqrt(x * x + z * z);
}

float distance(const Vec3& a, const Vec3& b) {
  const float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
  return std::sqrt(x * x + y * y + z * z);
}

bool finite(const Vec3& v) { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }

Vec3 flat_unit(const Vec3& v) {
  const float n = std::sqrt(v[0] * v[0] + v[2] * v[2]);
  if (!(n > 1e-4f)) return {0, 0, 0};
  return {v[0] / n, 0, v[2] / n};
}

float dot_flat(const Vec3& a, const Vec3& b) { return a[0] * b[0] + a[2] * b[2]; }

}  // namespace

// ---- RoomSwitches ----------------------------------------------------------------

std::string_view switch_name(proto::RoomSwitchKey key) {
  switch (key) {
    case proto::RoomSwitchKey::pvp: return "PvP";
    case proto::RoomSwitchKey::lock_mates: return "Fijar aliados";
    case proto::RoomSwitchKey::bodies: return "Choque entre jugadores";
  }
  return "?";
}

bool default_on(proto::RoomSwitchKey key) { return key == proto::RoomSwitchKey::bodies && kBodiesDefault; }

namespace {
bool known(std::uint8_t key) {
  return key == static_cast<std::uint8_t>(proto::RoomSwitchKey::pvp) ||
         key == static_cast<std::uint8_t>(proto::RoomSwitchKey::lock_mates) ||
         key == static_cast<std::uint8_t>(proto::RoomSwitchKey::bodies);
}
}  // namespace

void RoomSwitches::connection_new(std::uint64_t welcome_us) {
  states_.clear();
  welcome_us_ = welcome_us;
}

std::vector<std::string> RoomSwitches::received(const proto::RoomSwitch& m, const Names& names) {
  std::vector<std::string> out;
  const std::string who = m.player_number != 0 && names ? names(m.player_number) : std::string();
  // A switch set before our WELCOME is the room's state we came into.
  const bool before = m.server_time_us != 0 && m.server_time_us < welcome_us_;
  for (const proto::RoomSwitchEntry& e : m.entries) {
    const bool was = state(static_cast<proto::RoomSwitchKey>(e.key)).on;  // its default when never set
    states_[e.key] = SwitchState{e.on, m.player_number, m.server_time_us};
    if (was == e.on || !known(e.key)) {
      continue;  // already so, or a key this build does not know (kept, not told)
    }
    const std::string name(switch_name(static_cast<proto::RoomSwitchKey>(e.key)));
    if (before) {
      out.push_back("En esta sala " + name + (e.on ? " está activado" : " está desactivado") +
                    (who.empty() ? "" : std::string(e.on ? " (lo activó " : " (lo desactivó ") + who + ")"));
    } else {
      out.push_back(name + (e.on ? " activado" : " desactivado") + (who.empty() ? "" : " por " + who));
    }
  }
  return out;
}

bool RoomSwitches::on(proto::RoomSwitchKey key) const { return state(key).on; }

SwitchState RoomSwitches::state(proto::RoomSwitchKey key) const {
  const auto it = states_.find(static_cast<std::uint8_t>(key));
  return it == states_.end() ? SwitchState{default_on(key), 0, 0} : it->second;
}

std::string RoomSwitches::describe(proto::RoomSwitchKey key, const Names& names) const {
  const SwitchState s = state(key);
  std::string line = std::string(switch_name(key)) + (s.on ? ": activado" : ": desactivado");
  if (s.by != 0 && names) line += std::string(s.on ? " (lo activó " : " (lo desactivó ") + names(s.by) + ")";
  return line;
}

proto::RoomSwitch RoomSwitches::request(proto::RoomSwitchKey key, bool on) {
  proto::RoomSwitch m;
  m.entries.push_back({static_cast<std::uint8_t>(key), on});
  return m;
}

// ---- Weapons -----------------------------------------------------------------------

std::optional<Swing> swing_of(std::string_view c) {
  using W = proto::HitWeapon;
  if (begins(c, "Hammer_") && (has(c, "Attack") || has(c, "Swing") || has(c, "Hit"))) {
    return Swing{W::hammer, 8, 2.2f, 0.5f, 2.0f};
  }
  const bool sword = begins(c, "Sword_Attack") || begins(c, "LSword_Attack") || begins(c, "CutJump");
  if (!sword) return std::nullopt;
  const bool spin = has(c, "Spin") || has(c, "Rotate") || has(c, "Kaiten");
  const bool jump = has(c, "Jump");
  const bool longsword = begins(c, "LSword");
  if (spin) return Swing{W::sword, std::uint8_t(longsword ? 8 : 4), 2.4f, -1.0f, 1.2f};
  if (jump) return Swing{W::sword, std::uint8_t(longsword ? 8 : 4), 2.2f, 0.5f, 1.0f};
  return Swing{W::sword, std::uint8_t(longsword ? 4 : 2), longsword ? 2.2f : 1.8f, 0.5f, 0.8f};
}

std::optional<Shot> shot_of(std::string_view actor) {
  using W = proto::HitWeapon;
  if (actor == "NormalArrow" || begins(actor, "CoopShot_Arrow")) return Shot{W::arrow, 4, 0.7f, 0.6f, false};
  if (actor == "Shootable_DekuSeed") return Shot{W::seed, 1, 0.6f, 0.4f, false};
  if (actor == "Shootable_Hook" || actor == "Shootable_HookLong") return Shot{W::hookshot, 2, 0.7f, 2.5f, true};
  if (actor == "Weapon_Boomerang") return Shot{W::boomerang, 1, 0.8f, 0.3f, false};
  if (actor == "ExpandFireField") return Shot{W::magic, 8, 5.0f, 1.5f, false};
  return std::nullopt;
}

Vec3 push_between(const Vec3& from, const Vec3& to, float metres, bool pull) {
  Vec3 way = flat_unit({to[0] - from[0], 0, to[2] - from[2]});
  if (way[0] == 0 && way[2] == 0) way = {0, 0, 1};
  if (pull) {
    const float room = std::max(0.0f, flat_distance(from, to) - 1.0f);
    metres = std::min(metres, room);
    way = {-way[0], 0, -way[2]};
  }
  metres = std::clamp(metres, 0.0f, proto::kMaxHitPush);
  return {way[0] * metres, 0, way[2] * metres};
}

Vec3 forward_of(const proto::Quat& q) {
  const float x = q[0], y = q[1], z = q[2], w = q[3];
  const Vec3 f{2 * (x * z + w * y), 2 * (y * z - w * x), 1 - 2 * (x * x + y * y)};
  const Vec3 flat = flat_unit(f);
  if (flat[0] == 0 && flat[2] == 0) return {0, 0, 1};
  return flat;
}

// ---- Attacker ------------------------------------------------------------------------

std::uint32_t Attacker::next() {
  if (++next_id_ == 0) next_id_ = 1;
  return next_id_;
}

void Attacker::reset() {
  swing_.clear();
  swing_hit_.clear();
  shot_hit_.clear();
}

std::vector<proto::PlayerHit> Attacker::step(const AttackFrame& f) {
  std::vector<proto::PlayerHit> out;
  // A new swing is a new command name; the same one held is one swing.
  const std::string command = f.command.value_or("");
  const auto swing = swing_of(command);
  if (!swing || command != swing_) {
    swing_hit_.clear();
    swing_ = swing ? command : std::string();
  }
  // Shots gone: forget them.
  std::set<std::uint64_t> alive;
  for (const OwnShot& s : f.shots) alive.insert(s.id);
  std::erase_if(shot_hit_, [&](const auto& e) { return alive.count(e.first) == 0; });
  if (!f.pvp || !f.able || !finite(f.at)) return out;
  const Vec3 facing = flat_unit(f.forward);
  for (const Mate& m : f.mates) {
    if (!m.up || !m.here || m.number == 0 || !finite(m.at)) continue;
    if (swing && swing_hit_.count(m.number) == 0) {
      const Vec3 to{m.at[0] - f.at[0], 0, m.at[2] - f.at[2]};
      const float d = flat_distance(f.at, m.at);
      const bool tall = std::fabs(m.at[1] - f.at[1]) <= kTallM;
      const bool ahead = swing->cone_cos <= -1.0f || d < 0.3f || dot_flat(flat_unit(to), facing) >= swing->cone_cos;
      if (d <= swing->reach && tall && ahead) {
        proto::PlayerHit h;
        h.victim = m.number;
        h.hit_id = next();
        h.weapon = swing->weapon;
        h.damage = swing->damage;
        h.from = f.at;
        h.push = push_between(f.at, m.at, swing->push, false);
        out.push_back(h);
        swing_hit_.insert(m.number);
      }
    }
    const Vec3 middle{m.at[0], m.at[1] + kBodyHalfHeight, m.at[2]};
    for (const OwnShot& s : f.shots) {
      const auto shot = shot_of(s.actor);
      if (!shot || !finite(s.at)) continue;
      auto& hit = shot_hit_[s.id];
      if (hit.count(m.number) != 0) continue;
      if (distance(s.at, middle) > shot->radius + kBodyRadius) continue;
      proto::PlayerHit h;
      h.victim = m.number;
      h.hit_id = next();
      h.weapon = shot->weapon;
      h.damage = shot->damage;
      if (shot->pull) h.flags |= proto::player_hit::pull;
      h.from = f.at;
      // A shot pushes along its own way: from us for an arrow, back to us
      // for the hookshot.
      h.push = push_between(f.at, m.at, shot->push, shot->pull);
      out.push_back(h);
      hit.insert(m.number);
    }
  }
  return out;
}

// ---- Victim ------------------------------------------------------------------------------

const char* to_string(Verdict v) {
  switch (v) {
    case Verdict::take: return "taken";
    case Verdict::push_only: return "push only";
    case Verdict::blocked: return "blocked";
    case Verdict::invulnerable: return "invulnerable";
    case Verdict::repeat: return "repeat";
    case Verdict::off: return "PvP off";
    case Verdict::down: return "not up";
    case Verdict::too_far: return "too far";
    case Verdict::kept_off: return "kept off by upstream's rules";
  }
  return "?";
}

bool nayru(std::string_view c) { return has(c, "Nayru") && !begins(c, "EndNayru"); }

bool shielding(std::string_view c) {
  if (nayru(c) || has(c, "FailHit")) return false;
  return begins(c, "Guard") || begins(c, "CrouchGuard") || has(c, "ShieldGuard");
}

bool reacting(std::string_view c) { return begins(c, "Damage"); }

// ---- The victim's shield ----------------------------------------------------------------

const char* to_string(GuardPath p) {
  switch (p) {
    case GuardPath::command: return "command";
    case GuardPath::button: return "guard button";
    case GuardPath::no_shield: return "no shield";
    case GuardPath::not_raised: return "not raised";
    case GuardPath::unread: return "unread";
  }
  return "?";
}

std::string Guard::describe() const {
  switch (path) {
    case GuardPath::command:
      return "command '" + command + "' in slot " + std::to_string(slot);
    case GuardPath::button:
      return "the guard button held with a shield on ('" + command + "')";
    case GuardPath::no_shield:
      return "no shield equipped ('" + command + "')";
    case GuardPath::not_raised:
      return "a shield on but not raised ('" + command + "')";
    case GuardPath::unread:
      return "our Link's slots not read";
  }
  return "?";
}

bool guards_by_button(std::string_view c) {
  if (c.empty() || swing_of(c) || reacting(c) || nayru(c)) return false;
  return begins(c, "Wait") || begins(c, "Walk") || begins(c, "Run") || begins(c, "Crouch") || begins(c, "Turn") ||
         begins(c, "Brake") || has(c, "LockOn");
}

Guard guard_of(const GuardView& v) {
  Guard g;
  if (v.commands.empty()) {
    g.path = GuardPath::unread;
    return g;
  }
  g.command = v.commands.front();
  g.slot = 0;
  for (std::size_t i = 0; i < v.commands.size(); ++i) {
    if (nayru(v.commands[i])) g.nayru = true;
  }
  for (std::size_t i = 0; i < v.commands.size(); ++i) {
    if (shielding(v.commands[i])) {
      g.up = true;
      g.path = GuardPath::command;
      g.command = v.commands[i];
      g.slot = static_cast<int>(i);
      return g;
    }
  }
  if (v.shield && v.shield->empty()) {
    g.path = GuardPath::no_shield;
    return g;
  }
  if (v.shield && v.guard_held.value_or(false) && guards_by_button(g.command)) {
    g.up = true;
    g.path = GuardPath::button;
    return g;
  }
  g.path = GuardPath::not_raised;
  return g;
}

void Victim::reset() {
  last_taken_us_ = 0;
  seen_.clear();
}

std::int32_t spare_last(std::int32_t life, std::int32_t damage, bool others_up, bool& spared) {
  spared = false;
  if (damage <= 0) return 0;
  if (!others_up && life > 0 && damage >= life) {
    spared = true;
    return life - 1;
  }
  return damage;
}

Judged Victim::judge(const proto::PlayerHit& hit, const VictimFrame& f) {
  Judged out;
  const std::pair<std::uint8_t, std::uint32_t> key{hit.player_number, hit.hit_id};
  if (std::find(seen_.begin(), seen_.end(), key) != seen_.end()) {
    out.verdict = Verdict::repeat;
    return out;
  }
  seen_.push_back(key);
  if (seen_.size() > kKeep) seen_.pop_front();
  if (!f.pvp) {
    out.verdict = Verdict::off;
    return out;
  }
  if (!f.up) {
    out.verdict = Verdict::down;
    return out;
  }
  const bool melee = hit.weapon == proto::HitWeapon::sword || hit.weapon == proto::HitWeapon::hammer ||
                     hit.weapon == proto::HitWeapon::stick;
  if (distance(hit.from, f.at) > (melee ? kMeleeFarM : kShotFarM)) {
    out.verdict = Verdict::too_far;
    return out;
  }
  const std::string command = f.command.value_or("");
  if (nayru(command) || (f.guard && f.guard->nayru)) {
    out.verdict = Verdict::blocked;
    out.shield = "Nayru's Love";
    return out;
  }
  // The shield: as guard_of read every slot, the pad and the equipment, or
  // (no reading of those) the main command alone.
  const bool raised = f.guard ? f.guard->up : shielding(command);
  const std::string how = f.guard ? f.guard->describe() : "main command '" + command + "'";
  if (raised) {
    const Vec3 toward = flat_unit({hit.from[0] - f.at[0], 0, hit.from[2] - f.at[2]});
    const float facing = dot_flat(toward, flat_unit(f.forward));
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(facing));
    if (facing >= kShieldCos) {
      out.verdict = Verdict::blocked;
      out.shield = "shield up (" + how + "), facing the hit (cos " + buf + ")";
      return out;
    }
    out.shield = "shield up (" + how + ") but the hit came from beside or behind it (cos " + buf + ")";
  } else {
    out.shield = "shield not up: " + how;
  }
  if ((last_taken_us_ != 0 && f.now_us - std::min(f.now_us, last_taken_us_) < kInvulnerableUs) ||
      reacting(command)) {
    out.verdict = Verdict::invulnerable;
    return out;
  }
  last_taken_us_ = std::max<std::uint64_t>(f.now_us, 1);
  out.push = hit.push;
  if ((hit.flags & proto::player_hit::push_only) != 0 || hit.damage == 0) {
    out.verdict = Verdict::push_only;
    return out;
  }
  out.verdict = Verdict::take;
  out.damage = f.life ? spare_last(*f.life, hit.damage, f.others_up, out.spared) : hit.damage;
  return out;
}

// ---- On upstream's PvP (test10) -------------------------------------------------------------

proto::HitWeapon weapon_of(proto::DamageCause cause) {
  using C = proto::DamageCause;
  using W = proto::HitWeapon;
  switch (cause) {
    case C::sword: return W::sword;
    case C::hammer: return W::hammer;
    case C::arrow:
    case C::fire_arrow:
    case C::ice_arrow:
    case C::light_arrow: return W::arrow;
    case C::seed: return W::seed;
    case C::hookshot: return W::hookshot;
    case C::boomerang: return W::boomerang;
    case C::magic: return W::magic;
    case C::nut: return W::nut;
    case C::bomb:
    case C::unknown: return W::other;
  }
  return W::other;
}

Knock knock_of(proto::DamageCause cause, std::string_view attacker_command) {
  using C = proto::DamageCause;
  if (cause == C::sword || cause == C::hammer) {
    if (const auto s = swing_of(attacker_command)) return {s->push, false};
    return {cause == C::hammer ? 2.0f : 0.8f, false};
  }
  switch (cause) {
    case C::arrow:
    case C::fire_arrow:
    case C::ice_arrow:
    case C::light_arrow: return {0.6f, false};
    case C::seed: return {0.4f, false};
    case C::hookshot: return {2.5f, true};
    case C::boomerang: return {0.3f, false};
    case C::magic: return {1.5f, false};
    case C::bomb: return {kBlastPushM, false};
    default: return {0.0f, false};
  }
}

std::int32_t attack_amount(proto::DamageCause cause, std::int32_t upstream_amount, std::string_view our_command) {
  using C = proto::DamageCause;
  std::int32_t amount = upstream_amount;
  if (cause == C::sword || cause == C::hammer) {
    if (const auto s = swing_of(our_command)) amount = s->damage;
  } else if (cause == C::arrow || cause == C::fire_arrow || cause == C::ice_arrow) {
    amount = shot_of("NormalArrow")->damage;
  } else if (cause == C::seed) {
    amount = shot_of("Shootable_DekuSeed")->damage;
  } else if (cause == C::hookshot) {
    amount = shot_of("Shootable_Hook")->damage;
  } else if (cause == C::boomerang) {
    amount = shot_of("Weapon_Boomerang")->damage;
  }
  // 0 stays 0 (a nut stuns, upstream sends no hit for it); never past PVP_HIT's limit.
  return std::clamp<std::int32_t>(amount, 0, proto::kMaxPvpLife);
}

Judged UpstreamVictim::judge(const proto::PvpHit& hit, std::int32_t upstream_take, std::string_view attacker_command,
                             const VictimFrame& f) {
  if (upstream_take <= 0) {
    Judged out;
    out.verdict = Verdict::kept_off;
    return out;
  }
  proto::PlayerHit p;
  p.player_number = hit.player_number;
  p.victim = hit.victim;
  if (++next_id_ == 0) next_id_ = 1;  // upstream's hits are reliable and never repeat: a fresh id each
  p.hit_id = next_id_;
  p.weapon = weapon_of(static_cast<proto::DamageCause>(hit.cause));
  p.damage = static_cast<std::uint8_t>(std::clamp<std::int32_t>(upstream_take, 0, proto::kMaxHitDamage));
  p.from = hit.from;
  const Knock k = knock_of(static_cast<proto::DamageCause>(hit.cause), attacker_command);
  p.push = push_between(hit.from, f.at, k.metres, k.pull);
  VictimFrame g = f;
  g.pvp = true;  // upstream's victim already checked the session's PvP
  return victim_.judge(p, g);
}

// ---- Shove ---------------------------------------------------------------------------------

void Shove::start(const Vec3& push, std::uint64_t now_us) {
  if (!finite(push)) return;
  push_ = push;
  since_us_ = now_us;
  done_ = 0.0f;
  active_ = true;
}

std::optional<Vec3> Shove::step(std::uint64_t now_us) {
  if (!active_) return std::nullopt;
  const float t = std::clamp(static_cast<float>(now_us - std::min(now_us, since_us_)) / float(kShoveUs), 0.0f, 1.0f);
  const float eased = 1.0f - (1.0f - t) * (1.0f - t);  // fast first
  const float part = eased - done_;
  done_ = eased;
  if (t >= 1.0f) active_ = false;
  if (part <= 0.0f) return std::nullopt;
  return Vec3{push_[0] * part, push_[1] * part, push_[2] * part};
}

// ---- Bodies --------------------------------------------------------------------------------

std::optional<Vec3> push_out(const Vec3& self, std::uint8_t self_number, std::span<const Mate> mates) {
  if (!finite(self)) return std::nullopt;
  Vec3 sum{0, 0, 0};
  bool any = false;
  for (const Mate& m : mates) {
    if (!m.up || !m.here || !finite(m.at) || m.number == self_number) continue;
    if (std::fabs(m.at[1] - self[1]) >= kBodyHeight) continue;
    const float d = flat_distance(self, m.at);
    const float overlap = 2 * kBodyRadius - d;
    if (overlap <= 0) continue;
    Vec3 away = flat_unit({self[0] - m.at[0], 0, self[2] - m.at[2]});
    if (away[0] == 0 && away[2] == 0) {
      // On top of each other: each game goes its own way, by the numbers
      // (the lower one one way, the higher the other), so they part.
      const float sign = self_number < m.number ? 1.0f : -1.0f;
      const float angle = 1.0472f * float(std::min(self_number, m.number));  // 60 degrees a number
      away = {sign * std::cos(angle), 0, sign * std::sin(angle)};
    }
    sum[0] += away[0] * overlap * 0.5f;
    sum[2] += away[2] * overlap * 0.5f;
    any = true;
  }
  if (!any) return std::nullopt;
  const float n = std::sqrt(sum[0] * sum[0] + sum[2] * sum[2]);
  if (n > kMaxPushOutM) {
    sum[0] *= kMaxPushOutM / n;
    sum[2] *= kMaxPushOutM / n;
  }
  if (!(n > 1e-5f)) return std::nullopt;
  return sum;
}

// ---- Fijar aliados ---------------------------------------------------------------------------

AllyTarget::Out AllyTarget::step(bool wanted, bool pack_has, std::uint32_t made, std::uint32_t failed) {
  Out out;
  if (state_ == State::fallback) return out;  // the plain puppet for the rest of the session
  if (!wanted) {
    if (state_ != State::off) out.log = "ally target off: the plain puppet again";
    state_ = State::off;
    return out;
  }
  if (!pack_has) {
    state_ = State::fallback;
    out.log = "check: ally target fallback (the puppet pack has no CoopPuppet_*_Lock: build it again with this kit)";
    out.chat = "Fijar aliados no está disponible en tu juego: vuelve a preparar el paquete de marioneta";
    return out;
  }
  if (state_ == State::off) {
    state_ = State::waiting;
    made_at_start_ = made;
    failed_at_start_ = failed;
    out.log = "ally target on: the puppets are made as the lock-on variant";
  }
  if (state_ == State::waiting) {
    if (made > made_at_start_) {
      state_ = State::ok;
      out.log = "check: ally target ok (a lock-on puppet was made)";
    } else if (failed - std::min(failed, failed_at_start_) >= kFailures) {
      state_ = State::fallback;
      out.log = "check: ally target fallback (the lock-on puppet could not be made " + std::to_string(kFailures) +
                " times: the plain puppet again)";
      out.chat = "Fijar aliados no está disponible en tu juego: la marioneta fijable no se pudo crear";
      return out;
    }
  }
  out.lock = true;
  return out;
}

// ---- Bombs ---------------------------------------------------------------------------------

std::optional<BlastHurt> blast_hurt(const Vec3& blast, const Vec3& self, bool up, bool copy_live) {
  if (copy_live || !up || !finite(blast) || !finite(self)) return std::nullopt;
  if (distance(blast, self) > kBlastReachM) return std::nullopt;
  BlastHurt out;
  out.damage = kBlastDamage;
  out.push = push_between(blast, self, kBlastPushM, false);
  return out;
}

}  // namespace coop::host::orm::pvp
