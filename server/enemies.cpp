// ORM enemigos: oot-64's rules for enemies with up to six players, on top of
// daurosk's ownership rules (docs/objects.md "Enemies"). The why of each one
// is in oot-remake/docs/ENEMIGOS.md; the switches are in SessionConfig
// (session.hpp, "ORM enemigos"). Tested in tests/server/enemies_test.cpp.
//
// - A downed player is no target (oot-64 PLAN 2.8.2: "los enemigos lo
//   ignoran ... se excluye del cálculo de jugador más cercano"). An enemy's
//   AI chases only its owner's Link, so a downed owner gives its enemies up
//   after owner_downed_us, fight or not.
// - An owner that leaves the scene, unloads the enemy or disconnects hands it
//   to the nearest player standing: the nearest living player, as oot-64's
//   "el jugador vivo más cercano" for an actor's update.
// - In a fight the enemy goes to the player clearly nearer it (the boss rule
//   of issue 150, with an enemy's numbers), so with six players it fights the
//   one beside it, not a far owner.
// - A kill stays dead for a game that reloads the room while another player
//   stayed in it (oot-64: a room stays loaded while anyone is in it).

#include <cmath>
#include <set>
#include <string>

#include "session.hpp"

namespace coop::server {

namespace {

constexpr std::uint8_t kNotStanding =
    protocol::status::loading | protocol::status::paused | protocol::status::dead;

bool finite_place(const protocol::Vec3& v) {
  return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}

// The protocol's "not known" place (an OBJECT_REMOVE with no position).
bool known_place(const protocol::Vec3& v) {
  return finite_place(v) && !(v[0] == 0.0f && v[1] == 0.0f && v[2] == 0.0f);
}

double metres_between(const protocol::Vec3& a, const protocol::Vec3& b) {
  double sum = 0;
  for (std::size_t i = 0; i < 3; ++i) {
    const double d = static_cast<double>(a[i]) - static_cast<double>(b[i]);
    sum += d * d;
  }
  return std::sqrt(sum);
}

}  // namespace

std::uint64_t Session::dead_owner_after() const {
  return config_.owner_downed_us != 0 ? config_.owner_downed_us : config_.owner_dead_us;
}

std::optional<ConnectionId> Session::next_owner(const Object& o, ConnectionId leaving,
                                                std::uint64_t now_us) const {
  if (auto standing = best_other(o, leaving, true, now_us)) return standing;
  return best_other(o, leaving, false, now_us);
}

bool Session::fight_near_rule(const protocol::ObjectId& oid, Object& o, ConnectionId owner,
                              const Connection& oc, std::uint64_t now_us) {
  if (config_.fight_near_hold_us == 0 || o.boss || o.root || o.projectile ||
      !in_fight(o, now_us)) {
    return false;
  }
  std::optional<ConnectionId> want;
  const bool may = (o.changed_at == 0 || now_us - o.changed_at >= config_.fight_near_cooldown_us) &&
                   !owner_hit_lately(o, now_us);
  const auto theirs = may ? distance_to(oc, o, now_us) : std::nullopt;
  if (theirs) {
    double best = 0;
    std::uint8_t best_number = 0;
    for (ConnectionId other : o.present) {
      if (other == owner || !has_loaded(other, o)) continue;
      const Connection& pc = connections_.at(other);
      if (pc.status & kNotStanding) continue;
      const auto mine = distance_to(pc, o, now_us);
      if (!mine || *mine > config_.fight_near_m || *mine + config_.fight_near_margin_m > *theirs) {
        continue;
      }
      // The nearest, then the lower player number, so every run picks the same.
      if (!want || *mine < best || (*mine == best && pc.number < best_number)) {
        want = other;
        best = *mine;
        best_number = pc.number;
      }
    }
  }
  if (!want) {
    o.candidate.reset();
    return true;  // in a fight: the distance rule out of a fight does not apply
  }
  if (o.candidate != want) {
    o.candidate = want;
    o.candidate_since = now_us;
  }
  if (now_us - o.candidate_since >= config_.fight_near_hold_us) {
    // ORM boss families (oot-remake docs/JEFES.md section 9): an enemy that holds its owner's
    // Link (owner_keeps, from the owner's samples) is not moved to a nearer player; asked again
    // once the owner's game lets it go.
    if (owner_keeps(o, false, now_us)) {
      ++stats_.owner_kept;
    } else {
      ++stats_.fight_near_moved;
      ask_handover(oid, o, *want, now_us);
    }
  }
  return true;
}

void Session::note_far_kills(ConnectionId id, const Connection& c, const std::string& scene,
                             std::uint64_t now_us) {
  if (!c.position) return;
  if (now_us > c.position_at && now_us - c.position_at > config_.position_stale_us) return;
  auto ds = dead_.find(scene);
  if (ds == dead_.end()) return;
  for (auto& [oid, d] : ds->second) {
    auto w = d.witnesses.find(id);
    // Still listing its dying copy, or no place known: nothing to tell.
    if (w == d.witnesses.end() || w->second || !known_place(d.position)) continue;
    if (metres_between(*c.position, d.position) > config_.dead_left_m) d.went_away.insert(id);
  }
}

bool Session::kill_held_inside(ConnectionId id, const std::string& scene, const Dead& d,
                               const std::set<protocol::ObjectId>& loaded_with, std::uint64_t now_us,
                               std::string* why) const {
  if (config_.dead_room_m <= 0 || !d.went_away.count(id) || !known_place(d.position)) {
    return false;
  }
  // The room itself (oot-remake docs/RIESGOS_RESUELTOS.md, "Mundo e historia"):
  // the other shared objects this game just loaded with the kill are that
  // room's. Another game that has listed any of them all along has the room
  // loaded: walls and floors do not fool this, a radius can be.
  // Kills (this one too) tell nothing: no game lists a dead enemy.
  const auto kills = dead_.find(scene);
  std::size_t room_objects = 0;
  for (const protocol::ObjectId& oid : loaded_with) {
    if (oid.kind != protocol::ObjectKind::placed || (kills != dead_.end() && kills->second.count(oid))) continue;
    const auto it = objects_.find(oid);
    if (it == objects_.end()) {
      ++room_objects;  // forgotten: no other game has it loaded
      continue;
    }
    if (it->second.scene != scene || it->second.removed || it->second.projectile) continue;
    ++room_objects;
    for (const auto& [other, c] : connections_) {
      if (other == id || c.state != State::joined || c.scene != scene) continue;
      if (c.present.count(oid)) {
        if (why) {
          *why = "player " + std::to_string(c.number) +
                 " has its room loaded (it lists an object loaded with it)";
        }
        return true;
      }
    }
  }
  // Fallback: nothing shared in that room to tell by, or no other game lists
  // it (a game may list only part of a big room: a radius error that keeps a
  // kill dead is the lesser one; a kill alive in one game only is unseen by
  // the others). A player within dead_room_m of the place, on the same floor
  // (dead_room_up_m), is taken to be in the room.
  const std::string lead = room_objects == 0 ? std::string("no other object in its room to tell by")
                                             : "no other game lists the " + std::to_string(room_objects) +
                                                   " objects loaded with it";
  for (const auto& [other, c] : connections_) {
    if (other == id || c.state != State::joined || c.scene != scene || !c.position) continue;
    if (now_us > c.position_at && now_us - c.position_at > config_.position_stale_us) continue;
    // Downed or paused, that player is still in the room: it stays loaded.
    if (metres_between(*c.position, d.position) > config_.dead_room_m) continue;
    if (config_.dead_room_up_m > 0 &&
        std::fabs(static_cast<double>((*c.position)[1]) - static_cast<double>(d.position[1])) >
            config_.dead_room_up_m) {
      continue;  // another floor: another room
    }
    if (why) {
      *why = lead + ": player " + std::to_string(c.number) + " stands " +
             std::to_string(static_cast<int>(std::lround(metres_between(*c.position, d.position)))) +
             " m from it, on its floor";
    }
    return true;
  }
  if (why) {
    *why = lead + ", and nobody within " +
           std::to_string(static_cast<int>(config_.dead_room_m)) + " m on its floor";
  }
  return false;
}

}  // namespace coop::server
