// ORM PvP (coop/host/orm_pvp.hpp, oot-remake docs/COMANDOS.md "PvP"): the
// room's switches and their notices, the attacker's hits, the victim's rules
// (shield, Nayru, invulnerability, repeats, the last one up), the shove, the
// bodies' push-out and a mate's bomb with no copy here.
#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <cmath>
#include <string>
#include <vector>

#include "coop/host/orm_pvp.hpp"

using namespace coop::host::orm::pvp;
namespace proto = coop::protocol;

namespace {

std::string Name(std::uint8_t n) { return n == 2 ? "Saria" : n == 1 ? "Anju" : "P" + std::to_string(n); }

proto::RoomSwitch Set(std::uint8_t by, std::uint64_t at, proto::RoomSwitchKey key, bool on) {
  proto::RoomSwitch m;
  m.player_number = by;
  m.server_time_us = at;
  m.entries.push_back({static_cast<std::uint8_t>(key), on});
  return m;
}

float Length(const Vec3& v) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }

AttackFrame Frame(std::uint64_t now, std::string command, Vec3 mate_at) {
  AttackFrame f;
  f.now_us = now;
  f.pvp = true;
  f.able = true;
  f.command = std::move(command);
  f.at = {0, 0, 0};
  f.forward = {0, 0, 1};
  f.mates.push_back({2, mate_at, true, true});
  return f;
}

proto::PlayerHit Hit(std::uint32_t id, proto::HitWeapon weapon = proto::HitWeapon::sword, std::uint8_t damage = 2) {
  proto::PlayerHit h;
  h.player_number = 1;
  h.victim = 2;
  h.hit_id = id;
  h.weapon = weapon;
  h.damage = damage;
  h.from = {0, 0, -1};  // the attacker stands 1 m behind (-z)
  h.push = {0, 0, 0.8f};
  return h;
}

VictimFrame Me(std::uint64_t now, std::string command = "Wait") {
  VictimFrame f;
  f.now_us = now;
  f.pvp = true;
  f.up = true;
  f.command = std::move(command);
  f.at = {0, 0, 0};
  f.forward = {0, 0, -1};  // facing the attacker
  f.life = 12;
  f.others_up = true;
  return f;
}

}  // namespace

TEST_CASE("pvp: the room's switches, their notices and the state a joiner finds") {
  RoomSwitches s;
  s.connection_new(5'000'000);
  CHECK_FALSE(s.on(proto::RoomSwitchKey::pvp));
  CHECK(s.describe(proto::RoomSwitchKey::pvp, Name) == "PvP: desactivado");
  // Set before we came: "En esta sala ...", once.
  auto lines = s.received(Set(2, 1'000'000, proto::RoomSwitchKey::pvp, true), Name);
  REQUIRE(lines.size() == 1);
  CHECK(lines[0] == "En esta sala PvP está activado (lo activó Saria)");
  CHECK(s.on(proto::RoomSwitchKey::pvp));
  CHECK(s.describe(proto::RoomSwitchKey::pvp, Name) == "PvP: activado (lo activó Saria)");
  // A change now.
  lines = s.received(Set(1, 6'000'000, proto::RoomSwitchKey::pvp, false), Name);
  REQUIRE(lines.size() == 1);
  CHECK(lines[0] == "PvP desactivado por Anju");
  lines = s.received(Set(2, 7'000'000, proto::RoomSwitchKey::lock_mates, true), Name);
  REQUIRE(lines.size() == 1);
  CHECK(lines[0] == "Fijar aliados activado por Saria");
  CHECK(s.on(proto::RoomSwitchKey::lock_mates));
  CHECK_FALSE(s.on(proto::RoomSwitchKey::pvp));
  // The same again: nothing to tell. An unknown key: kept, not told.
  CHECK(s.received(Set(2, 8'000'000, proto::RoomSwitchKey::lock_mates, true), Name).empty());
  proto::RoomSwitch other = Set(2, 9'000'000, proto::RoomSwitchKey::pvp, false);
  other.entries[0].key = 9;
  other.entries[0].on = true;
  CHECK(s.received(other, Name).empty());
  // A new connection forgets (the server sends the room's again).
  s.connection_new(10'000'000);
  CHECK_FALSE(s.on(proto::RoomSwitchKey::lock_mates));
  const auto req = RoomSwitches::request(proto::RoomSwitchKey::lock_mates, false);
  CHECK(req.player_number == 0);
  REQUIRE(req.entries.size() == 1);
  CHECK(req.entries[0].key == 2);
  CHECK_FALSE(req.entries[0].on);
}

TEST_CASE("pvp: the bodies switch (/choque): its default until the room sets it, its notices, a joiner") {
  RoomSwitches s;
  s.connection_new(5'000'000);
  CHECK(s.on(proto::RoomSwitchKey::bodies) == kBodiesDefault);
  CHECK(default_on(proto::RoomSwitchKey::bodies) == kBodiesDefault);
  CHECK_FALSE(default_on(proto::RoomSwitchKey::pvp));
  CHECK_FALSE(default_on(proto::RoomSwitchKey::lock_mates));
  CHECK(switch_name(proto::RoomSwitchKey::bodies) == "Choque entre jugadores");
  CHECK(static_cast<int>(proto::RoomSwitchKey::bodies) == 3);
  // The room's state when we came (the server sends it after WELCOME).
  auto lines = s.received(Set(2, 1'000'000, proto::RoomSwitchKey::bodies, !kBodiesDefault), Name);
  REQUIRE(lines.size() == 1);
  CHECK(lines[0] == std::string("En esta sala Choque entre jugadores ") +
                        (kBodiesDefault ? "está desactivado (lo desactivó Saria)" : "está activado (lo activó Saria)"));
  CHECK(s.on(proto::RoomSwitchKey::bodies) == !kBodiesDefault);
  // A change now, and the same again (nothing).
  lines = s.received(Set(1, 6'000'000, proto::RoomSwitchKey::bodies, kBodiesDefault), Name);
  REQUIRE(lines.size() == 1);
  CHECK(lines[0] == std::string("Choque entre jugadores ") + (kBodiesDefault ? "activado" : "desactivado") +
                        " por Anju");
  CHECK(s.received(Set(1, 7'000'000, proto::RoomSwitchKey::bodies, kBodiesDefault), Name).empty());
  // A room that set it to its default says nothing to a joiner.
  RoomSwitches j;
  j.connection_new(9'000'000);
  CHECK(j.received(Set(1, 6'000'000, proto::RoomSwitchKey::bodies, kBodiesDefault), Name).empty());
  CHECK(j.describe(proto::RoomSwitchKey::bodies, Name).rfind("Choque entre jugadores: ", 0) == 0);
  const auto req = RoomSwitches::request(proto::RoomSwitchKey::bodies, true);
  REQUIRE(req.entries.size() == 1);
  CHECK(req.entries[0].key == 3);
  CHECK(req.entries[0].on);
}

TEST_CASE("pvp: sword swings by command name") {
  CHECK_FALSE(swing_of("Wait"));
  CHECK_FALSE(swing_of("Guard_Start"));
  const auto slash = swing_of("Sword_Attack_Horizontal_R");
  REQUIRE(slash);
  CHECK(slash->damage == 2);
  CHECK(slash->cone_cos > 0);
  const auto spin = swing_of("Sword_Attack_Spin");
  REQUIRE(spin);
  CHECK(spin->cone_cos == -1.0f);
  CHECK(swing_of("Sword_Attack_Jump")->damage == 4);
  CHECK(swing_of("LSword_Attack_Vertical")->damage == 4);
  CHECK(swing_of("CutJump_Land")->damage == 4);
  CHECK(swing_of("Hammer_Attack")->weapon == proto::HitWeapon::hammer);
  CHECK(shot_of("Shootable_Hook")->pull);
  CHECK(shot_of("Shootable_DekuSeed")->damage == 1);
  CHECK_FALSE(shot_of("Item_Bomb"));  // bombs hurt by their explosion, never as a hit
}

TEST_CASE("pvp: one swing hits a mate in front once, behind, too far or PvP off: nothing") {
  Attacker a;
  auto hits = a.step(Frame(0, "Sword_Attack_Horizontal_R", {0, 0, 1.2f}));
  REQUIRE(hits.size() == 1);
  CHECK(hits[0].victim == 2);
  CHECK(hits[0].player_number == 0);
  CHECK(hits[0].hit_id != 0);
  CHECK(hits[0].weapon == proto::HitWeapon::sword);
  CHECK(hits[0].push[2] > 0.7f);  // away from us
  CHECK(a.step(Frame(16'000, "Sword_Attack_Horizontal_R", {0, 0, 1.2f})).empty());  // the same swing
  // A new swing hits again.
  CHECK(a.step(Frame(32'000, "Sword_Attack_Horizontal_L", {0, 0, 1.2f})).size() == 1);
  a.step(Frame(48'000, "Wait", {0, 0, 1.2f}));
  CHECK(a.step(Frame(64'000, "Sword_Attack_Horizontal_L", {0, 0, -1.2f})).empty());  // behind
  CHECK(a.step(Frame(80'000, "Wait", {0, 0, 1.2f})).empty());
  CHECK(a.step(Frame(96'000, "Sword_Attack_Spin", {0, 0, -1.2f})).size() == 1);  // a spin: all round
  a.step(Frame(112'000, "Wait", {0, 0, 1.2f}));
  CHECK(a.step(Frame(128'000, "Sword_Attack_Horizontal_R", {0, 0, 4.0f})).empty());  // too far
  a.step(Frame(144'000, "Wait", {0, 0, 1.2f}));
  auto off = Frame(160'000, "Sword_Attack_Horizontal_R", {0, 0, 1.2f});
  off.pvp = false;
  CHECK(a.step(off).empty());
  a.step(Frame(176'000, "Wait", {0, 0, 1.2f}));
  auto down = Frame(192'000, "Sword_Attack_Horizontal_R", {0, 0, 1.2f});
  down.mates[0].up = false;  // a downed mate is never hit
  CHECK(a.step(down).empty());
}

TEST_CASE("pvp: a shot hits each mate once, the hookshot pulls toward us") {
  Attacker a;
  auto f = Frame(0, "Wait", {0, 0, 10});
  f.shots.push_back({7, "Shootable_DekuSeed", {0, 0.9f, 9.8f}});
  auto hits = a.step(f);
  REQUIRE(hits.size() == 1);
  CHECK(hits[0].weapon == proto::HitWeapon::seed);
  CHECK(hits[0].push[2] > 0);
  CHECK(a.step(f).empty());  // the same seed again
  f.shots[0] = {8, "Shootable_Hook", {0, 1.0f, 10.1f}};
  hits = a.step(f);
  REQUIRE(hits.size() == 1);
  CHECK((hits[0].flags & proto::player_hit::pull) != 0);
  CHECK(hits[0].push[2] < 0);  // back toward us
  CHECK(Length(hits[0].push) <= 2.5f + 1e-4f);
  f.shots[0] = {9, "NormalArrow", {3, 1.0f, 10}};  // 3 m to the side: a miss
  CHECK(a.step(f).empty());
}

TEST_CASE("pvp: a pull stops a metre short of the puller") {
  const Vec3 p = push_between({0, 0, 0}, {0, 0, 2}, 2.5f, true);
  CHECK(p[2] == doctest::Approx(-1.0f));
  const Vec3 q = push_between({0, 0, 0}, {0, 0, 0.5f}, 2.5f, true);
  CHECK(Length(q) == doctest::Approx(0.0f));
}

TEST_CASE("pvp: the victim takes a hit, then is invulnerable, never twice the same") {
  Victim v;
  const Judged first = v.judge(Hit(1), Me(1'000'000));
  CHECK(first.verdict == Verdict::take);
  CHECK(first.damage == 2);
  CHECK(first.push[2] == doctest::Approx(0.8f));
  CHECK(v.judge(Hit(1), Me(2'000'000)).verdict == Verdict::repeat);
  CHECK(v.judge(Hit(2), Me(1'500'000)).verdict == Verdict::invulnerable);
  CHECK(v.judge(Hit(3), Me(1'900'000)).verdict == Verdict::take);
  CHECK(v.judge(Hit(4), Me(3'000'000, "Damage_Small_Front")).verdict == Verdict::invulnerable);
}

TEST_CASE("pvp: the shield toward the hit blocks it and its push, from behind it does not") {
  Victim v;
  CHECK(v.judge(Hit(1), Me(1'000'000, "Guard_Wait")).verdict == Verdict::blocked);
  CHECK(v.judge(Hit(2), Me(1'000'000, "CrouchGuard_Wait")).verdict == Verdict::blocked);
  VictimFrame back = Me(1'000'000, "Guard_Wait");
  back.forward = {0, 0, 1};  // the attacker is behind us
  CHECK(v.judge(Hit(3), back).verdict == Verdict::take);
  VictimFrame nayru = Me(5'000'000, "Guard_Nayru_Loop");
  nayru.forward = {0, 0, 1};
  CHECK(v.judge(Hit(4), nayru).verdict == Verdict::blocked);  // Nayru's Love: from every side
  CHECK(shielding("Lift_ShieldGuard"));
  CHECK_FALSE(shielding("Guard_Nayru"));
}

TEST_CASE("pvp: PvP off, downed, too far, push only") {
  Victim v;
  VictimFrame off = Me(1'000'000);
  off.pvp = false;
  CHECK(v.judge(Hit(1), off).verdict == Verdict::off);
  VictimFrame down = Me(1'000'000);
  down.up = false;
  CHECK(v.judge(Hit(2), down).verdict == Verdict::down);
  proto::PlayerHit far = Hit(3);
  far.from = {0, 0, -20};
  CHECK(v.judge(far, Me(1'000'000)).verdict == Verdict::too_far);
  proto::PlayerHit arrow = Hit(4, proto::HitWeapon::arrow, 4);
  arrow.from = {0, 0, -20};
  CHECK(v.judge(arrow, Me(1'000'000)).verdict == Verdict::take);
  proto::PlayerHit shove = Hit(5);
  shove.flags = proto::player_hit::push_only;
  const Judged j = v.judge(shove, Me(3'000'000));
  CHECK(j.verdict == Verdict::push_only);
  CHECK(j.damage == 0);
  CHECK(j.push[2] > 0);
}

TEST_CASE("pvp: the last one up keeps a quarter, with a mate up the hit can down him") {
  Victim v;
  VictimFrame last = Me(1'000'000);
  last.life = 2;
  last.others_up = false;
  Judged j = v.judge(Hit(1, proto::HitWeapon::sword, 4), last);
  CHECK(j.verdict == Verdict::take);
  CHECK(j.damage == 1);
  CHECK(j.spared);
  VictimFrame with = Me(3'000'000);
  with.life = 2;
  j = v.judge(Hit(2, proto::HitWeapon::sword, 4), with);
  CHECK(j.damage == 4);
  CHECK_FALSE(j.spared);
  bool spared = true;
  CHECK(spare_last(5, 2, false, spared) == 2);
  CHECK_FALSE(spared);
}

TEST_CASE("pvp: a shove is spread over a few frames, fast first, all of it") {
  Shove s;
  s.start({1.0f, 0, 0}, 0);
  float total = 0;
  float first = 0;
  for (std::uint64_t t = 16'000; t <= 300'000; t += 16'000) {
    if (const auto part = s.step(t)) {
      if (first == 0) first = (*part)[0];
      total += (*part)[0];
    }
  }
  CHECK(total == doctest::Approx(1.0f).epsilon(0.001));
  CHECK(first > 1.0f / 14.0f);  // more than an even share
  CHECK_FALSE(s.active());
  CHECK_FALSE(s.step(400'000));
}

TEST_CASE("pvp: bodies push out by half the overlap, never stuck, downed mates left alone") {
  std::vector<Mate> mates{{2, {0.4f, 0, 0}, true, true}};
  auto out = push_out({0, 0, 0}, 1, mates);
  REQUIRE(out);
  CHECK((*out)[0] < 0);  // away from the mate
  CHECK(Length(*out) == doctest::Approx(0.12f));  // (0.7 - 0.4) / 2 = 0.15, held to 0.12 a frame
  mates[0].at = {0.6f, 0, 0};
  out = push_out({0, 0, 0}, 1, mates);
  REQUIRE(out);
  CHECK(Length(*out) == doctest::Approx(0.05f));
  mates[0].at = {0, 0, 0};  // exactly on top: the two games go opposite ways
  const auto mine = push_out({0, 0, 0}, 1, mates);
  std::vector<Mate> me{{1, {0, 0, 0}, true, true}};
  const auto theirs = push_out({0, 0, 0}, 2, me);
  REQUIRE(mine);
  REQUIRE(theirs);
  CHECK((*mine)[0] * (*theirs)[0] + (*mine)[2] * (*theirs)[2] < 0);
  mates[0].up = false;
  CHECK_FALSE(push_out({0, 0, 0}, 1, mates));
  mates[0] = {2, {0.2f, 2.0f, 0}, true, true};  // another floor
  CHECK_FALSE(push_out({0, 0, 0}, 1, mates));
  mates[0] = {2, {1.0f, 0, 0}, true, true};
  CHECK_FALSE(push_out({0, 0, 0}, 1, mates));
}

TEST_CASE("pvp: a mate's bomb with no copy here hurts us once, with a copy the game does it") {
  const auto hurt = blast_hurt({0, 0, 0}, {2, 0, 0}, true, false);
  REQUIRE(hurt);
  CHECK(hurt->damage == kBlastDamage);
  CHECK(hurt->push[0] > 0);
  CHECK_FALSE(blast_hurt({0, 0, 0}, {2, 0, 0}, true, true));
  CHECK_FALSE(blast_hurt({0, 0, 0}, {5, 0, 0}, true, false));
  CHECK_FALSE(blast_hurt({0, 0, 0}, {2, 0, 0}, false, false));
}

TEST_CASE("pvp: the way a rotation faces") {
  const Vec3 ahead = forward_of({0, 0, 0, 1});
  CHECK(ahead[2] == doctest::Approx(1.0f));
  const float s = std::sqrt(0.5f);
  const Vec3 turned = forward_of({0, s, 0, s});  // 90 degrees about y: +z to +x
  CHECK(turned[0] == doctest::Approx(1.0f));
  CHECK(turned[2] == doctest::Approx(0.0f).epsilon(1e-4));
}

TEST_CASE("pvp: Fijar aliados makes lock-on puppets, ok once one is made, back to plain when off") {
  AllyTarget a;
  auto out = a.step(false, true, 0, 0);
  CHECK_FALSE(out.lock);
  CHECK_FALSE(out.log);
  out = a.step(true, true, 5, 1);  // the director's totals so far
  CHECK(out.lock);
  CHECK(a.state() == AllyTarget::State::waiting);
  REQUIRE(out.log);
  out = a.step(true, true, 5, 1);
  CHECK(out.lock);
  CHECK_FALSE(out.log);
  out = a.step(true, true, 6, 1);  // one lock-on puppet made
  CHECK(out.lock);
  REQUIRE(out.log);
  CHECK(out.log->find("check: ally target ok") == 0);
  CHECK(a.state() == AllyTarget::State::ok);
  out = a.step(false, true, 6, 1);
  CHECK_FALSE(out.lock);
  CHECK(a.state() == AllyTarget::State::off);
  CHECK(a.available());
}

TEST_CASE("pvp: Fijar aliados falls back with no lock-on pack, or after three failed makes, for good") {
  AllyTarget no_pack;
  auto out = no_pack.step(true, false, 0, 0);
  CHECK_FALSE(out.lock);
  REQUIRE(out.log);
  CHECK(out.log->find("check: ally target fallback") == 0);
  REQUIRE(out.chat);
  CHECK_FALSE(no_pack.available());
  CHECK_FALSE(no_pack.step(true, true, 9, 0).lock);  // held for the session
  CHECK_FALSE(no_pack.step(false, true, 9, 0).log);

  AllyTarget failing;
  CHECK(failing.step(true, true, 0, 2).lock);
  CHECK(failing.step(true, true, 0, 4).lock);  // two of ours failed
  out = failing.step(true, true, 0, 5);        // the third
  CHECK_FALSE(out.lock);
  REQUIRE(out.log);
  CHECK(out.log->find("check: ally target fallback") == 0);
  CHECK(out.chat.has_value());
  CHECK(failing.state() == AllyTarget::State::fallback);
}

// ---- On upstream's PvP (test10): our rules as a layer on PVP_HIT -----------------

namespace {

proto::PvpHit UpHit(proto::DamageCause cause, std::int32_t amount) {
  proto::PvpHit h;
  h.player_number = 1;
  h.victim = 2;
  h.cause = static_cast<std::uint8_t>(cause);
  h.amount = amount;
  h.at = {0, 1, 0};
  h.from = {0, 0, -1};  // the attacker stands 1 m behind (-z), as Hit()
  return h;
}

}  // namespace

TEST_CASE("pvp on upstream: the real weapon's damage for our PVP_HIT") {
  using C = proto::DamageCause;
  CHECK(attack_amount(C::sword, 2, "Sword_Attack_Horizontal") == 2);   // half a heart
  CHECK(attack_amount(C::sword, 2, "LSword_Attack_Horizontal") == 4);  // the long sword: one heart
  CHECK(attack_amount(C::sword, 2, "Sword_Attack_Spin") == 4);
  CHECK(attack_amount(C::sword, 2, "LSword_Attack_Jump") == 8);
  CHECK(attack_amount(C::hammer, 4, "Hammer_Attack") == 8);
  CHECK(attack_amount(C::sword, 2, "Wait") == 2);  // unknown command: upstream's
  CHECK(attack_amount(C::arrow, 4, "") == 4);
  CHECK(attack_amount(C::seed, 1, "") == 1);
  CHECK(attack_amount(C::hookshot, 2, "") == 2);
  CHECK(attack_amount(C::light_arrow, 8, "") == 8);  // not in our table: upstream's
  CHECK(attack_amount(C::nut, 0, "") == 0);          // a nut stuns, no hurt
  CHECK(attack_amount(C::bomb, 200, "") == proto::kMaxPvpLife);
  CHECK(weapon_of(C::fire_arrow) == proto::HitWeapon::arrow);
  CHECK(weapon_of(C::bomb) == proto::HitWeapon::other);
  CHECK(knock_of(C::hookshot, "").pull);
  CHECK(knock_of(C::sword, "Sword_Attack_Spin").metres == doctest::Approx(1.2f));
}

TEST_CASE("pvp on upstream: shield, Nayru and invulnerability keep the hit off, its push is ours") {
  UpstreamVictim v;
  CHECK(v.judge(UpHit(proto::DamageCause::sword, 2), 2, "", Me(1'000'000, "Guard_Wait")).verdict ==
        Verdict::blocked);
  VictimFrame nayru = Me(1'000'000, "Guard_Nayru_Loop");
  nayru.forward = {0, 0, 1};  // back to the attacker: Nayru's Love still holds
  CHECK(v.judge(UpHit(proto::DamageCause::arrow, 4), 4, "", nayru).verdict == Verdict::blocked);
  const Judged j = v.judge(UpHit(proto::DamageCause::sword, 2), 2, "", Me(2'000'000));
  CHECK(j.verdict == Verdict::take);
  CHECK(j.damage == 2);
  CHECK(j.push[2] == doctest::Approx(0.8f));  // away from the attacker, 0.8 m for a sword
  CHECK(Length(j.push) == doctest::Approx(0.8f));
  CHECK(v.judge(UpHit(proto::DamageCause::sword, 2), 2, "", Me(2'300'000)).verdict == Verdict::invulnerable);
  CHECK(v.judge(UpHit(proto::DamageCause::sword, 2), 2, "", Me(4'000'000, "Damage_Front")).verdict ==
        Verdict::invulnerable);  // our own hit reaction plays
  CHECK(v.judge(UpHit(proto::DamageCause::sword, 2), 2, "", Me(6'000'000)).verdict == Verdict::take);
  // The hookshot pulls toward the attacker.
  const Judged pull = v.judge(UpHit(proto::DamageCause::hookshot, 2), 2, "", Me(9'000'000));
  CHECK(pull.verdict == Verdict::take);
  CHECK(pull.push[2] <= 0.0f);
}

TEST_CASE("pvp on upstream: kept off by upstream stays off, downed, the last one up keeps a quarter") {
  UpstreamVictim v;
  CHECK(v.judge(UpHit(proto::DamageCause::sword, 2), 0, "", Me(1'000'000)).verdict == Verdict::kept_off);
  VictimFrame down = Me(2'000'000);
  down.up = false;
  CHECK(v.judge(UpHit(proto::DamageCause::sword, 2), 2, "", down).verdict == Verdict::down);
  VictimFrame last = Me(3'000'000);
  last.life = 2;
  last.others_up = false;
  Judged j = v.judge(UpHit(proto::DamageCause::hammer, 8), 2, "", last);  // upstream already capped at our life
  CHECK(j.verdict == Verdict::take);
  CHECK(j.damage == 1);
  CHECK(j.spared);
  VictimFrame with = Me(5'000'000);
  with.life = 2;
  j = v.judge(UpHit(proto::DamageCause::hammer, 8), 2, "", with);
  CHECK(j.damage == 2);
  CHECK_FALSE(j.spared);
  CHECK(std::string(to_string(Verdict::kept_off)) == "kept off by upstream's rules");
}

// ---- The victim's shield: every slot, the equipped shield, the guard button ----------------
// (the 0.12 game check pvp-espada: B, with no shield on, held ZL with "LockOnWait"
// in the main slot and took the hit; the shield rule knew no real command name)

namespace {

GuardView View(std::vector<std::string> commands, std::optional<std::string> shield = std::string("Weapon_Shield_Deku"),
               std::optional<bool> held = false) {
  GuardView v;
  v.commands = std::move(commands);
  v.shield = std::move(shield);
  v.guard_held = held;
  return v;
}

VictimFrame Guarded(std::uint64_t now, const GuardView& view) {
  VictimFrame f = Me(now, view.commands.empty() ? std::string() : view.commands.front());
  f.guard = guard_of(view);
  return f;
}

}  // namespace

TEST_CASE("pvp shield: the real commands of Player_Child/Adult.root.asb") {
  for (const char* c : {"ShieldGuard", "ShieldGuard_Caution", "GuardHit", "GuardBreak", "CrouchGuard",
                        "CrouchGuardChild", "Guard_Hit_L", "Guard_Crouch_St", "LockOn_Wait_ShieldGuard",
                        "Pose_Sword_ShieldGuard_UL", "Hook_Aim_ShieldGuard", "Wait_To_LockOn_ShieldGuard",
                        "GuardBeam"}) {
    INFO(c);
    CHECK(shielding(c));
  }
  for (const char* c : {"LockOnWait", "Wait", "Wait_Sword_Shield", "GuardFailHit", "CrouchGuardFailHit", "GuardNayru",
                        "GuardBreakNayru", "Guard_Nayru_Air", "DamageS", ""}) {
    INFO(c);
    CHECK_FALSE(shielding(c));
  }
  for (const char* c : {"GuardNayru", "GuardBreakNayru", "Guard_Break_Nayru_B", "Magic_Nayru_St", "StartNayruLove",
                        "Damage_Land_Nayru"}) {
    INFO(c);
    CHECK(nayru(c));
  }
  CHECK_FALSE(nayru("EndNayruLove"));
  CHECK_FALSE(nayru("ShieldGuard"));
  for (const char* c : {"DamageS", "DamageM", "DamageL", "Damage", "Damage_S_F", "Damage_Though_Sword"}) {
    INFO(c);
    CHECK(reacting(c));
  }
  CHECK_FALSE(reacting("Wait"));
}

TEST_CASE("pvp shield: guard_of reads a slot, the button with a shield on, or says why not") {
  // A guard on the upper body's slot while the main one locks on.
  Guard g = guard_of(View({"LockOnWait", "", "Pose_Sword_ShieldGuard_U"}));
  CHECK(g.up);
  CHECK(g.path == GuardPath::command);
  CHECK(g.slot == 2);
  CHECK(g.command == "Pose_Sword_ShieldGuard_U");
  CHECK(g.describe() == "command 'Pose_Sword_ShieldGuard_U' in slot 2");
  // The main slot itself.
  CHECK(guard_of(View({"ShieldGuard"})).slot == 0);
  // The 0.12 run: no shield on, ZL held, LockOnWait: nothing to raise.
  g = guard_of(View({"LockOnWait", ""}, std::string(), true));
  CHECK_FALSE(g.up);
  CHECK(g.path == GuardPath::no_shield);
  CHECK(g.describe() == "no shield equipped ('LockOnWait')");
  // A shield on and ZL held, locked on or standing: raised by the button.
  g = guard_of(View({"LockOnWait"}, std::string("Weapon_Shield_Deku"), true));
  CHECK(g.up);
  CHECK(g.path == GuardPath::button);
  CHECK(g.describe() == "the guard button held with a shield on ('LockOnWait')");
  CHECK(guard_of(View({"Wait"}, std::string("Weapon_Shield_Hylian"), true)).up);
  CHECK(guard_of(View({"LockOn_Run_L"}, std::string("Weapon_Shield_Hylian"), true)).up);
  // Not while swinging, reacting or casting, nor with the button up.
  CHECK_FALSE(guard_of(View({"Sword_Attack_Horizontal"}, std::string("Weapon_Shield_Deku"), true)).up);
  CHECK_FALSE(guard_of(View({"DamageS"}, std::string("Weapon_Shield_Deku"), true)).up);
  CHECK_FALSE(guard_of(View({"Bow_Aim"}, std::string("Weapon_Shield_Deku"), true)).up);
  g = guard_of(View({"LockOnWait"}, std::string("Weapon_Shield_Deku"), false));
  CHECK_FALSE(g.up);
  CHECK(g.path == GuardPath::not_raised);
  // No pad read or no equipment read: only a guard command raises it.
  CHECK_FALSE(guard_of(View({"LockOnWait"}, std::string("Weapon_Shield_Deku"), std::nullopt)).up);
  CHECK_FALSE(guard_of(View({"LockOnWait"}, std::nullopt, true)).up);
  CHECK(guard_of(View({"ShieldGuard"}, std::nullopt, std::nullopt)).up);
  // A guard command wins over an empty shield read (the slots show it).
  CHECK(guard_of(View({"ShieldGuard"}, std::string(), false)).up);
  // Nayru's Love on any slot.
  CHECK(guard_of(View({"Wait", "Magic_Nayru"})).nayru);
  // Nothing read.
  CHECK(guard_of(GuardView{}).path == GuardPath::unread);
  CHECK(std::string(to_string(GuardPath::button)) == "guard button");
}

TEST_CASE("pvp shield: the victim's judge on what guard_of read, and the log says the path") {
  UpstreamVictim v;
  // The 0.12 case: taken, and the line says there was no shield.
  Judged j = v.judge(UpHit(proto::DamageCause::sword, 2), 2, "", Guarded(1'000'000, View({"LockOnWait"}, std::string(), true)));
  CHECK(j.verdict == Verdict::take);
  CHECK(j.shield == "shield not up: no shield equipped ('LockOnWait')");
  // The same with the Deku Shield on: the button raises it, facing the attacker: blocked.
  j = v.judge(UpHit(proto::DamageCause::sword, 2), 2, "",
              Guarded(3'000'000, View({"LockOnWait"}, std::string("Weapon_Shield_Deku"), true)));
  CHECK(j.verdict == Verdict::blocked);
  CHECK(j.damage == 0);
  CHECK(j.shield.find("shield up (the guard button held with a shield on ('LockOnWait')), facing the hit (cos 1.00)") ==
        0);
  // A guard command in the upper body's slot, back to the attacker: taken, said so.
  VictimFrame back = Guarded(5'000'000, View({"LockOnWait", "Pose_Sword_ShieldGuard"}));
  back.forward = {0, 0, 1};
  j = v.judge(UpHit(proto::DamageCause::sword, 2), 2, "", back);
  CHECK(j.verdict == Verdict::take);
  CHECK(j.shield.find("but the hit came from beside or behind it (cos -1.00)") != std::string::npos);
  // From the side (90 degrees): the shield covers 70 each way: taken.
  VictimFrame side = Guarded(7'000'000, View({"ShieldGuard"}));
  side.forward = {1, 0, 0};
  CHECK(v.judge(UpHit(proto::DamageCause::sword, 2), 2, "", side).verdict == Verdict::take);
  // An arrow at the raised shield: blocked as well.
  CHECK(v.judge(UpHit(proto::DamageCause::arrow, 4), 4, "", Guarded(9'000'000, View({"ShieldGuard"}))).verdict ==
        Verdict::blocked);
  // Nayru's Love on another slot, from behind: blocked.
  VictimFrame nayru = Guarded(11'000'000, View({"Wait", "Magic_Nayru"}));
  nayru.forward = {0, 0, 1};
  j = v.judge(UpHit(proto::DamageCause::sword, 2), 2, "", nayru);
  CHECK(j.verdict == Verdict::blocked);
  CHECK(j.shield == "Nayru's Love");
  // No guard reading at all: the main command alone, as before.
  j = v.judge(UpHit(proto::DamageCause::sword, 2), 2, "", Me(13'000'000, "ShieldGuard"));
  CHECK(j.verdict == Verdict::blocked);
  CHECK(j.shield.find("main command 'ShieldGuard'") != std::string::npos);
  // A reaction of the game's own (DamageS) keeps a hit off as our invulnerability.
  CHECK(v.judge(UpHit(proto::DamageCause::sword, 2), 2, "", Me(15'000'000, "DamageS")).verdict ==
        Verdict::invulnerable);
}
