// ORM: the escape from Ganon's collapsing castle taken by the group (oot-64 D-165). The rules are
//   described in coop/host/group_exit.hpp.
// Doc: oot-remake docs/CONTENIDO_ADULTO.md (sections 0 and 4).
// Switch: none yet: the hook glue is not in this tree (LICHII_ORM_GROUP_EXIT=0 is planned to turn
//   it off).
// Test: tests/host/group_exit_test.cpp.

#include "coop/host/group_exit.hpp"

#include <algorithm>

namespace coop::host {

std::vector<GroupExit> default_group_exits() {
  return {GroupExit{"CrumblingGanonCastle", "FinalBattleField"}};
}

GroupExitRule::GroupExitRule(std::vector<GroupExit> exits, GroupExitSettings settings)
    : exits_(std::move(exits)), settings_(settings) {}

void GroupExitRule::reset() {
  seen_.clear();
  asked_us_.reset();
  asked_from_.reset();
  waiting_.clear();
}

const GroupExit* GroupExitRule::exit_from(const std::string& scene) const {
  for (const auto& e : exits_) {
    if (e.from == scene) return &e;
  }
  return nullptr;
}

GroupExitAction GroupExitRule::update(const GroupExitInput& in) {
  GroupExitAction out;

  // Where each mate is, and since when (a new scene restarts the clock).
  std::vector<Seen> now;
  for (const auto& m : in.mates) {
    if (!m.scene) continue;
    Seen s{m.number, *m.scene, in.now_us};
    for (const auto& old : seen_) {
      if (old.number == m.number && old.scene == *m.scene) s.since_us = old.since_us;
    }
    now.push_back(s);
  }
  seen_ = std::move(now);

  if (!settings_.on || !in.scene) return out;
  if (asked_from_ && *asked_from_ != *in.scene) {  // our scene changed: the follow is done
    asked_us_.reset();
    asked_from_.reset();
  }
  const GroupExit* exit = exit_from(*in.scene);
  if (!exit) {
    waiting_.clear();
    return out;
  }

  // The first mate (by number) who is up and has been in the next scene long enough.
  std::optional<std::uint8_t> leader;
  for (const auto& m : in.mates) {
    if (!m.up || !m.scene || *m.scene != exit->to) continue;
    const auto it = std::find_if(seen_.begin(), seen_.end(),
                                 [&](const Seen& s) { return s.number == m.number; });
    if (it == seen_.end() || in.now_us - it->since_us < settings_.settle_us) continue;
    if (!leader || m.number < *leader) leader = m.number;
  }
  if (!leader) {
    waiting_.clear();
    return out;
  }

  const std::string who = "P" + std::to_string(*leader);
  auto wait = [&](const std::string& why) {
    const std::string line = "group exit: " + who + " is in " + exit->to + ": we follow when " + why;
    if (line != waiting_) out.log.push_back(line);
    waiting_ = line;
  };
  if (!in.up) {
    wait("our Link is up (down now: a scene change would stand him up)");
    return out;
  }
  if (in.event) {
    wait("our event ends");
    return out;
  }
  if (asked_us_ && in.now_us - *asked_us_ < settings_.retry_us) return out;

  out.follow = leader;
  out.to = exit->to;
  out.log.push_back("group exit: " + who + " is in " + exit->to + ", we are still in " + exit->from +
                    ": following (" + (asked_us_ ? "asked again" : "first ask") + ")");
  asked_us_ = in.now_us;
  asked_from_ = *in.scene;
  waiting_.clear();
  return out;
}

}  // namespace coop::host
