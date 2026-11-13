// ORM: Continue with the group's game (coop/host/continue_load.hpp): the rule's phases, and the flag watcher
//   taking a Continue's reload as a save load (FlagWatcher::set_continue_load): the host's reload goes out as
//   FLAG_RESET, a player's own save never changes the group, and a host and a player end with the same flags.
// Doc: oot-remake docs/CONTINUAR.md (E11).

#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "coop/host/continue_load.hpp"
#include "coop/host/flag_watcher.hpp"
#include "coop/host/flags.hpp"
#include "coop/host/game_items.hpp"
#include "coop/protocol/codec.hpp"

using namespace coop::host;
namespace proto = coop::protocol;

namespace {

constexpr FlagKey kChest{0x51000u, 0};
constexpr FlagKey kDoor{0x52000u, 0};
constexpr FlagKey kSwitch{0x53000u, 0};
constexpr FlagKey kStory{0x54000u, 0};
constexpr FlagKey kWallet{0x55000u, 0};  // per player, not shared (PlayerStatus.CurrentRupee)
constexpr std::uint64_t kS = 1'000'000;

proto::FlagEntry flag(FlagKey key, std::uint32_t value) { return {key, proto::FlagKind::boolean, value}; }

// The game's flags, as the emulator would give them.
class Store : public FlagStore {
 public:
  std::map<FlagKey, proto::FlagEntry> values;

  void set(FlagKey key, std::uint32_t value) { values[key] = flag(key, value); }
  std::uint32_t get(FlagKey key) const { return values.at(key).value; }

  std::optional<proto::FlagEntry> read(FlagKey key) override {
    const auto it = values.find(key);
    if (it == values.end()) return std::nullopt;
    return it->second;
  }
  bool write(const proto::FlagEntry& e) override {
    const FlagKey key = key_of(e);
    if (!values.contains(key)) return false;
    values[key] = e;
    return true;
  }
};

SharedFlags shared() { return SharedFlags({kChest, kDoor, kSwitch, kStory}); }

Store game() {
  Store store;
  for (FlagKey k : {kChest, kDoor, kSwitch, kStory, kWallet}) store.set(k, 0);
  return store;
}

std::vector<proto::Message> decode(const std::vector<Outgoing>& out) {
  std::vector<proto::Message> messages;
  for (const auto& o : out) {
    proto::Message m;
    REQUIRE(proto::decode(o.bytes, m) == proto::Error::none);
    messages.push_back(std::move(m));
  }
  return messages;
}

// The host's or the server's FLAG_STATE, as a player gets it.
proto::FlagState state(std::vector<proto::FlagEntry> flags) { return {1, std::move(flags)}; }

// Every shared flag and the wallet of a game.
std::map<FlagKey, std::uint32_t> flags_of(const Store& s) {
  std::map<FlagKey, std::uint32_t> out;
  for (FlagKey k : {kChest, kDoor, kSwitch, kStory, kWallet}) out[k] = s.get(k);
  return out;
}

// One frame; FLAG_RESET entries go to `resets`, anything else (FLAG_SET, FLAG_STATE) to `other`.
void frame(FlagWatcher& w, FlagStore& store, std::vector<proto::FlagEntry>* resets = nullptr,
           std::vector<proto::Message>* other = nullptr) {
  for (auto& m : decode(w.frame(store))) {
    if (const auto* r = std::get_if<proto::FlagReset>(&m)) {
      if (resets) resets->insert(resets->end(), r->flags.begin(), r->flags.end());
    } else if (other) {
      other->push_back(std::move(m));
    }
  }
}

// The game over's Continue: Link goes (a few frames with no Link, the scene
// still loaded: no title screen), `reload` writes the save's values, Link is
// back and the window settles.
template <typename Reload>
void continue_reload(FlagWatcher& w, Store& store, Reload reload, std::vector<proto::FlagEntry>* resets,
                     std::vector<proto::Message>* other) {
  w.set_loading(true);
  w.set_scene(true);
  frame(w, store, resets, other);
  reload(store);
  for (int i = 0; i < 10; ++i) frame(w, store, resets, other);
  w.set_loading(false);
  for (int i = 0; i <= FlagWatcher::kSettleFrames; ++i) frame(w, store, resets, other);
  CHECK_FALSE(w.settling());
}

ContinueInput at(DownedState state, bool hosting, bool settling, const FlagStats& stats = {}) {
  ContinueInput in;
  in.state = state;
  in.hosting = hosting;
  in.settling = settling;
  in.host_resets = stats.host_resets;
  in.load_reverts = stats.load_reverts;
  in.reset_applied = stats.reset_applied;
  in.continue_kept = stats.continue_kept;
  return in;
}

// The hook's order each frame (coop_downed_game.cpp, then the session's frame): the rule
// ticks with the watcher's state, its mode goes to the watcher, then the watcher's frame.
// 60 frames a second; `event`: the game over event runs.
class Driver {
 public:
  Driver(FlagWatcher& w, Store& store, ContinueMode mode) : rule(mode), w_(w), store_(store) {}

  void step(DownedState state, bool event, int frames = 1) {
    for (int i = 0; i < frames; ++i) {
      ContinueInput in;
      in.now_us = now;
      in.state = state;
      in.event_running = event;
      in.event_name = event ? "DmF_SY_GameOver" : "";
      in.hosting = w_.role() == FlagRole::host;
      in.settling = w_.settling();
      in.host_resets = w_.stats().host_resets;
      in.load_reverts = w_.stats().load_reverts;
      in.reset_applied = w_.stats().reset_applied;
      in.continue_kept = w_.stats().continue_kept;
      const ContinueOutput out = rule.tick(in);
      log.insert(log.end(), out.log.begin(), out.log.end());
      w_.set_continue_load(out.mode);
      frame(w_, store_, &resets, &other);
      now += 16'667;
    }
  }
  bool logged(std::string_view text) const {
    for (const auto& line : log) {
      if (line.find(text) != std::string::npos) return true;
    }
    return false;
  }

  ContinueLoad rule;
  std::uint64_t now = 1 * kS;
  std::vector<std::string> log;
  std::vector<proto::FlagEntry> resets;
  std::vector<proto::Message> other;

 private:
  FlagWatcher& w_;
  Store& store_;
};

}  // namespace

TEST_CASE("continue load, save: on from the game over until the reload's window has settled") {
  ContinueLoad rule(ContinueMode::save);
  CHECK_FALSE(rule.tick(at(DownedState::alive, true, false)).mode == ContinueMode::save);
  CHECK_FALSE(rule.tick(at(DownedState::downed, true, false)).mode == ContinueMode::save);
  auto in = at(DownedState::game_over, true, false);
  in.group_defeat = true;
  auto out = rule.tick(in);
  CHECK(out.mode == ContinueMode::save);
  CHECK(rule.phase() == ContinueLoad::Phase::game_over);
  REQUIRE(out.log.size() == 1);
  CHECK(out.log[0].find("group defeat") != std::string::npos);
  CHECK(out.log[0].find("FLAG_RESET") != std::string::npos);
  // The game over screen, then the reload with no Link (still game_over).
  CHECK(rule.tick(at(DownedState::game_over, true, false)).mode == ContinueMode::save);
  CHECK(rule.tick(at(DownedState::game_over, true, true)).mode == ContinueMode::save);
  // Link is back: the window still settles, so it stays on.
  out = rule.tick(at(DownedState::alive, true, true));
  CHECK(out.mode == ContinueMode::save);
  CHECK(rule.phase() == ContinueLoad::Phase::reloading);
  CHECK(rule.tick(at(DownedState::alive, true, true)).mode == ContinueMode::save);
  // The window is over: off, with what it did.
  FlagStats stats;
  out = rule.tick(at(DownedState::alive, true, false, stats));
  CHECK_FALSE(out.mode == ContinueMode::save);
  CHECK(rule.phase() == ContinueLoad::Phase::idle);
  CHECK(out.log.empty());  // told on the next tick: the watcher closes the window in this frame
  stats.host_resets = 1;    // its FLAG_RESET
  out = rule.tick(at(DownedState::alive, true, false, stats));
  CHECK_FALSE(out.mode == ContinueMode::save);
  REQUIRE(out.log.size() == 1);
  CHECK(out.log[0].find("FLAG_RESET sent") != std::string::npos);
  // A later door is the watcher's own business.
  CHECK_FALSE(rule.tick(at(DownedState::alive, true, true)).mode == ContinueMode::save);
}

TEST_CASE("continue load, save: a player's summary counts its own save put back and the host's reset") {
  ContinueLoad rule(ContinueMode::save);
  FlagStats before;
  before.load_reverts = 4;
  before.reset_applied = 1;
  CHECK(rule.tick(at(DownedState::game_over, false, false, before)).mode == ContinueMode::save);
  CHECK(rule.tick(at(DownedState::alive, false, true, before)).mode == ContinueMode::save);
  FlagStats after = before;
  after.load_reverts = 7;
  after.reset_applied = 3;
  CHECK_FALSE(rule.tick(at(DownedState::alive, false, false, before)).mode == ContinueMode::save);
  const auto out = rule.tick(at(DownedState::alive, false, false, after));
  CHECK_FALSE(out.mode == ContinueMode::save);
  REQUIRE(out.log.size() == 1);
  CHECK(out.log[0].find("3 write(s)") != std::string::npos);
  CHECK(out.log[0].find("2 value(s)") != std::string::npos);
}

TEST_CASE("continue load: off, or out of the game over with no reload, it does nothing") {
  ContinueLoad off(ContinueMode::off);
  CHECK_FALSE(off.tick(at(DownedState::game_over, true, true)).mode == ContinueMode::save);
  CHECK(off.tick(at(DownedState::game_over, true, true)).log.empty());

  ContinueLoad rule(ContinueMode::save);
  CHECK(rule.tick(at(DownedState::game_over, true, false)).mode == ContinueMode::save);
  const auto out = rule.tick(at(DownedState::downed, true, false));
  CHECK_FALSE(out.mode == ContinueMode::save);
  CHECK(rule.phase() == ContinueLoad::Phase::idle);
  // Back with no load window yet: it waits for one (REVISION_NUEVAS), and with none in
  // kNoWindowUs it is off.
  auto in = at(DownedState::game_over, true, false);
  in.now_us = 100 * kS;
  CHECK(rule.tick(in).mode == ContinueMode::save);
  in.state = DownedState::alive;
  CHECK(rule.tick(in).mode == ContinueMode::save);
  in.now_us += ContinueLoad::kNoWindowUs - 1;
  CHECK(rule.tick(in).mode == ContinueMode::save);
  in.now_us += 1;
  const auto gave_up = rule.tick(in);
  CHECK(gave_up.mode == ContinueMode::off);
  CHECK(rule.phase() == ContinueLoad::Phase::idle);
  REQUIRE(gave_up.log.size() == 1);
  CHECK(gave_up.log[0].find("no load window came in 10 s") != std::string::npos);
}

TEST_CASE("continue load, save, host: the Continue's reload goes out as FLAG_RESET with no title screen") {
  Store store = game();
  FlagWatcher w(shared(), FlagRole::host);
  frame(w, store);
  store.set(kDoor, 1);
  store.set(kSwitch, 1);
  store.set(kChest, 1);
  frame(w, store);
  // Without the switch a death's reload is no save load (docs/flags.md).
  {
    Store plain = store;
    FlagWatcher v(shared(), FlagRole::host);
    frame(v, plain);
    std::vector<proto::FlagEntry> resets;
    continue_reload(v, plain, [](Store& s) { s.set(kDoor, 0); s.set(kSwitch, 0); }, &resets, nullptr);
    CHECK(resets.empty());
    CHECK(v.stats().continue_loads == 0);
  }
  // The group defeat: the reload puts the door and the switch back to the
  // host's last save; the group follows it.
  w.set_continue_load(ContinueMode::save);
  std::vector<proto::FlagEntry> resets;
  std::vector<proto::Message> other;
  continue_reload(w, store, [](Store& s) { s.set(kDoor, 0); s.set(kSwitch, 0); }, &resets, &other);
  CHECK(resets == std::vector{flag(kDoor, 0), flag(kSwitch, 0)});
  CHECK(other.empty());  // no FLAG_STATE of the reload's values
  CHECK(store.get(kDoor) == 0);  // the host's reload is the truth: not put back
  CHECK(store.get(kChest) == 1);
  CHECK(w.stats().continue_loads == 1);
  CHECK(w.stats().host_resets == 1);
  CHECK(w.take_reset());
  w.set_continue_load(ContinueMode::off);
  // A Continue that moved nothing sends nothing.
  w.set_continue_load(ContinueMode::save);
  resets.clear();
  continue_reload(w, store, [](Store&) {}, &resets, nullptr);
  CHECK(resets.empty());
  CHECK(w.stats().host_resets == 1);
}

TEST_CASE("continue load, save, player: its own save, behind or ahead, never reaches the group") {
  Store store = game();
  FlagWatcher w(shared(), FlagRole::player);
  frame(w, store);
  CHECK(w.receive(state({flag(kDoor, 1), flag(kSwitch, 1)})));
  frame(w, store);
  REQUIRE(store.get(kDoor) == 1);
  w.set_continue_load(ContinueMode::save);
  // Its own save: the door shut (behind) and a chest and a story scene the
  // group has not got (its save is ahead).
  w.set_loading(true);
  frame(w, store);
  store.set(kDoor, 0);
  store.set(kChest, 1);
  store.set(kStory, 1);
  std::vector<proto::Message> other;
  frame(w, store, nullptr, &other);
  CHECK(store.get(kDoor) == 1);  // the group's value, at once
  w.set_loading(false);
  for (int i = 0; i <= FlagWatcher::kSettleFrames; ++i) frame(w, store, nullptr, &other);
  CHECK(other.empty());  // nothing sent: no FLAG_SET of the save's values
  CHECK(store.get(kChest) == 0);  // back to what the group had at the window's end
  CHECK(store.get(kStory) == 0);
  CHECK(store.get(kSwitch) == 1);
  CHECK(w.stats().continue_loads == 1);
}

TEST_CASE("continue load, save: after a group defeat the host and a player go on with the host's flags") {
  // The live state at the defeat: door, switch and chest, the same in both.
  for (const bool host_first : {true, false}) {
    CAPTURE(host_first);
    Store hs = game();
    Store ps = game();
    FlagWatcher host(shared(), FlagRole::host);
    FlagWatcher player(shared(), FlagRole::player);
    frame(host, hs);
    frame(player, ps);
    for (FlagKey k : {kDoor, kSwitch, kChest}) hs.set(k, 1);
    std::vector<proto::Message> sent;
    frame(host, hs, nullptr, &sent);
    for (const auto& m : sent) CHECK(player.receive(m));  // the host's FLAG_STATE
    frame(player, ps);
    for (FlagKey k : {kDoor, kSwitch, kChest}) REQUIRE(ps.get(k) == 1);

    host.set_continue_load(ContinueMode::save);
    player.set_continue_load(ContinueMode::save);
    // The host's save is behind on the switch; the player's own save is
    // behind on the door and ahead on the story scene.
    const auto host_reload = [](Store& s) { s.set(kSwitch, 0); };
    const auto player_reload = [](Store& s) {
      s.set(kDoor, 0);
      s.set(kStory, 1);
    };
    std::vector<proto::FlagEntry> resets;
    std::vector<proto::Message> from_player;
    if (host_first) {
      // The host continues first; its FLAG_RESET reaches the player still on
      // the game over screen, which then reloads.
      continue_reload(host, hs, host_reload, &resets, nullptr);
      REQUIRE(player.receive(proto::FlagReset{1, resets}));
      frame(player, ps, nullptr, &from_player);
      CHECK(ps.get(kSwitch) == 0);
      continue_reload(player, ps, player_reload, nullptr, &from_player);
    } else {
      // The player continues first; the host's FLAG_RESET comes after.
      continue_reload(player, ps, player_reload, nullptr, &from_player);
      continue_reload(host, hs, host_reload, &resets, nullptr);
      REQUIRE(player.receive(proto::FlagReset{1, resets}));
      frame(player, ps, nullptr, &from_player);
    }
    CHECK(resets == std::vector{flag(kSwitch, 0)});
    CHECK(from_player.empty());
    for (FlagKey k : {kChest, kDoor, kSwitch, kStory}) {
      CAPTURE(k.hash);
      CHECK(ps.get(k) == hs.get(k));
    }
    CHECK(hs.get(kSwitch) == 0);
    CHECK(hs.get(kStory) == 0);
  }
}

TEST_CASE("continue load: the switch's values, keep by default") {
  CHECK(parse_continue_mode("") == ContinueMode::keep);
  CHECK(parse_continue_mode("keep") == ContinueMode::keep);
  CHECK(parse_continue_mode("save") == ContinueMode::save);
  CHECK(parse_continue_mode("0") == ContinueMode::off);
  CHECK(parse_continue_mode("off") == ContinueMode::off);
  CHECK_FALSE(parse_continue_mode("maybe"));
  ContinueLoad rule;  // keep
  CHECK(rule.mode() == ContinueMode::keep);
  auto out = rule.tick(at(DownedState::game_over, true, false));
  CHECK(out.mode == ContinueMode::keep);
  REQUIRE(out.log.size() == 1);
  CHECK(out.log[0].find("written back") != std::string::npos);
  CHECK(rule.tick(at(DownedState::alive, true, true)).mode == ContinueMode::keep);
  FlagStats stats;
  stats.continue_kept = 5;
  CHECK(rule.tick(at(DownedState::alive, true, false)).mode == ContinueMode::off);
  out = rule.tick(at(DownedState::alive, true, false, stats));
  CHECK(out.mode == ContinueMode::off);
  REQUIRE(out.log.size() == 1);
  CHECK(out.log[0].find("5 write(s)") != std::string::npos);
}

TEST_CASE("continue load, keep, host: the reload, back or forward, goes back to the group's values and sends nothing") {
  Store store = game();
  FlagWatcher w(shared(), FlagRole::host);
  w.set_continue_keep_extra({kWallet});
  frame(w, store);
  store.set(kDoor, 1);
  store.set(kSwitch, 1);
  frame(w, store);
  store.set(kWallet, 87);  // rupees picked up since the last autosave
  frame(w, store);
  const auto before = flags_of(store);
  w.set_continue_load(ContinueMode::keep);
  frame(w, store);  // the game over screen: the wallet's value is taken
  std::vector<proto::FlagEntry> resets;
  std::vector<proto::Message> other;
  // The autosave: the door and switch shut, a story scene it has played, 20 rupees.
  continue_reload(w, store, [](Store& s) { s.set(kDoor, 0); s.set(kSwitch, 0); s.set(kStory, 1); s.set(kWallet, 20); },
                  &resets, &other);
  CHECK(resets.empty());
  CHECK(other.empty());
  CHECK(flags_of(store) == before);
  CHECK(w.stats().continue_loads == 1);
  CHECK(w.stats().continue_kept >= 4);
  CHECK(w.stats().host_resets == 0);
  CHECK_FALSE(w.take_reset());  // no reset: the session's kept state stays
  w.set_continue_load(ContinueMode::off);
  // After it, a door is a door again and the game's own changes go out.
  store.set(kChest, 1);
  frame(w, store, nullptr, &other);
  CHECK(other.size() == 1);
}

TEST_CASE("continue load, keep, player: the reload goes back, a value that comes in meanwhile is kept") {
  Store store = game();
  FlagWatcher w(shared(), FlagRole::player);
  w.set_continue_keep_extra({kWallet, kDoor});  // a shared key in it is kept with the others
  frame(w, store);
  CHECK(w.receive(state({flag(kDoor, 1), flag(kSwitch, 1)})));
  store.set(kWallet, 40);
  frame(w, store);
  w.set_continue_load(ContinueMode::keep);
  frame(w, store);
  w.set_loading(true);
  frame(w, store);
  store.set(kDoor, 0);
  store.set(kChest, 1);   // its own save is ahead
  store.set(kWallet, 5);
  std::vector<proto::Message> other;
  frame(w, store, nullptr, &other);
  CHECK(store.get(kDoor) == 1);  // at once
  CHECK(store.get(kChest) == 0);
  CHECK(store.get(kWallet) == 40);
  // The load writes its value again: written back again.
  store.set(kDoor, 0);
  frame(w, store, nullptr, &other);
  CHECK(store.get(kDoor) == 1);
  // The host's change while we reload is the group's: kept, not undone.
  CHECK(w.receive(state({flag(kStory, 1)})));
  frame(w, store, nullptr, &other);
  w.set_loading(false);
  for (int i = 0; i <= FlagWatcher::kSettleFrames; ++i) frame(w, store, nullptr, &other);
  CHECK(other.empty());
  CHECK(store.get(kStory) == 1);
  CHECK(store.get(kSwitch) == 1);
  CHECK(store.get(kWallet) == 40);
}

TEST_CASE("continue load, keep: after a group defeat the host and a player go on as they fell") {
  Store hs = game();
  Store ps = game();
  FlagWatcher host(shared(), FlagRole::host);
  FlagWatcher player(shared(), FlagRole::player);
  frame(host, hs);
  frame(player, ps);
  for (FlagKey k : {kDoor, kSwitch, kChest}) hs.set(k, 1);
  std::vector<proto::Message> sent;
  frame(host, hs, nullptr, &sent);
  for (const auto& m : sent) CHECK(player.receive(m));
  frame(player, ps);
  const auto fell = flags_of(hs);
  REQUIRE(flags_of(ps) == fell);
  host.set_continue_load(ContinueMode::keep);
  player.set_continue_load(ContinueMode::keep);
  std::vector<proto::FlagEntry> resets;
  std::vector<proto::Message> out;
  continue_reload(host, hs, [](Store& s) { s.set(kSwitch, 0); s.set(kChest, 0); }, &resets, &out);
  continue_reload(player, ps, [](Store& s) { s.set(kDoor, 0); s.set(kStory, 1); }, &resets, &out);
  CHECK(resets.empty());
  CHECK(out.empty());
  CHECK(flags_of(hs) == fell);
  CHECK(flags_of(ps) == fell);
}

// ---- REVISION_NUEVAS (continuar): life back before the reload's window ----

TEST_CASE("continue load, keep: life back before the reload's window keeps it on until that window is over") {
  ContinueLoad rule;  // keep
  ContinueInput in;
  in.now_us = 1 * kS;
  in.state = DownedState::game_over;
  in.event_running = true;
  in.event_name = "DmF_SY_GameOver";
  CHECK(rule.tick(in).mode == ContinueMode::keep);
  // The Continue fills the hearts while the game over event still runs: no window yet.
  in.state = DownedState::alive;
  in.now_us = 2 * kS;
  auto out = rule.tick(in);
  CHECK(out.mode == ContinueMode::keep);
  CHECK(rule.phase() == ContinueLoad::Phase::reloading);
  REQUIRE(out.log.size() == 1);
  CHECK(out.log[0].find("before the reload's load window: it waits for one") != std::string::npos);
  // No time limit while the game over event runs (the menu).
  in.now_us = 60 * kS;
  CHECK(rule.tick(in).mode == ContinueMode::keep);
  // An event whose name is not read yet counts as that event too.
  in.event_name = {};
  in.now_us = 80 * kS;
  CHECK(rule.tick(in).mode == ContinueMode::keep);
  // The event is over (the fade): kNoWindowUs from now.
  in.event_running = false;
  in.now_us = 81 * kS;
  CHECK(rule.tick(in).mode == ContinueMode::keep);
  in.now_us = 81 * kS + ContinueLoad::kNoWindowUs - 1;
  CHECK(rule.tick(in).mode == ContinueMode::keep);
  // The reload's window opens: on for as long as it settles.
  in.settling = true;
  out = rule.tick(in);
  CHECK(out.mode == ContinueMode::keep);
  REQUIRE(out.log.size() == 1);
  CHECK(out.log[0] == "the reload's load window opens");
  in.now_us += 30 * kS;
  CHECK(rule.tick(in).mode == ContinueMode::keep);
  // Over: off, with what it did.
  in.settling = false;
  out = rule.tick(in);
  CHECK(out.mode == ContinueMode::off);
  CHECK(rule.phase() == ContinueLoad::Phase::idle);
  CHECK(out.log.empty());
  in.continue_kept = 3;  // the watcher's last writes, at the window's end
  out = rule.tick(in);
  REQUIRE(out.log.size() == 1);
  CHECK(out.log[0].find("3 write(s)") != std::string::npos);
}

TEST_CASE("continue load: only a window opened after the game over is the reload, another event does not hold it") {
  ContinueLoad rule(ContinueMode::keep);
  ContinueInput in;
  in.now_us = 1 * kS;
  in.state = DownedState::game_over;
  in.settling = true;  // he died on a door: its window is still open
  CHECK(rule.tick(in).mode == ContinueMode::keep);
  in.state = DownedState::alive;
  CHECK(rule.tick(in).mode == ContinueMode::keep);
  in.settling = false;  // the door's window is over: not the reload
  CHECK(rule.tick(in).mode == ContinueMode::keep);
  CHECK(rule.phase() == ContinueLoad::Phase::reloading);
  // An event that is not the game over's does not stop the 10 s.
  in.event_running = true;
  in.event_name = "Npc_Talk";
  in.now_us = 1 * kS + ContinueLoad::kNoWindowUs - 1;
  CHECK(rule.tick(in).mode == ContinueMode::keep);
  in.settling = true;  // the reload's own window, just in time
  CHECK(rule.tick(in).mode == ContinueMode::keep);
  in.settling = false;
  in.event_running = false;
  CHECK(rule.tick(in).mode == ContinueMode::off);
  const auto out = rule.tick(in);
  CHECK(out.mode == ContinueMode::off);
  REQUIRE(out.log.size() == 1);
  CHECK(out.log[0].find("our Continue's reload is done") == 0);

  // The game over event still running after the window holds the end.
  ContinueLoad late(ContinueMode::keep);
  ContinueInput b;
  b.state = DownedState::game_over;
  (void)late.tick(b);
  b.settling = true;
  (void)late.tick(b);  // the window while still in the game over
  b.settling = false;
  b.state = DownedState::alive;
  b.event_running = true;
  b.event_name = "Delayed_GameOver";
  CHECK(late.tick(b).mode == ContinueMode::keep);
  b.event_running = false;
  CHECK(late.tick(b).mode == ContinueMode::off);
}

TEST_CASE("continue load, keep, host: hearts back and the save's values in before the load window: nothing leaks") {
  Store store = game();
  FlagWatcher w(shared(), FlagRole::host);
  w.set_continue_keep_extra({kWallet});
  frame(w, store);
  store.set(kDoor, 1);
  store.set(kSwitch, 1);
  frame(w, store);
  store.set(kWallet, 87);
  frame(w, store);
  const auto fell = flags_of(store);
  Driver d(w, store, ContinueMode::keep);
  d.step(DownedState::alive, false, 2);
  d.step(DownedState::game_over, true, 30);  // the game over screen: the wallet is taken
  // The Continue fills the hearts, and the game puts the autosave's values in before its
  // load window opens (the order REVISION_NUEVAS feared).
  d.step(DownedState::alive, true);
  store.set(kDoor, 0);
  store.set(kStory, 1);
  store.set(kWallet, 20);
  d.step(DownedState::alive, true, 20);
  CHECK(d.other.empty());  // none of it went out
  CHECK(d.rule.phase() == ContinueLoad::Phase::reloading);
  CHECK(w.stats().continue_held > 0);
  d.step(DownedState::alive, false, 30);  // the fade
  w.set_loading(true);
  d.step(DownedState::alive, false, 10);
  store.set(kDoor, 0);  // the load writes it again
  d.step(DownedState::alive, false, 5);
  w.set_loading(false);
  d.step(DownedState::alive, false, FlagWatcher::kSettleFrames + 2);
  CHECK(d.rule.phase() == ContinueLoad::Phase::idle);
  CHECK(flags_of(store) == fell);  // the wallet too: 87, as at the game over
  CHECK(d.resets.empty());
  CHECK(d.other.empty());
  CHECK(w.stats().continue_loads == 1);
  CHECK(d.logged("it waits for one"));
  CHECK(d.logged("the reload's load window opens"));
  CHECK(d.logged("our Continue's reload is done"));
  // After it the game's own changes go out again.
  store.set(kChest, 1);
  d.step(DownedState::alive, false);
  CHECK(d.other.size() == 1);
}

TEST_CASE("continue load, save, host: the save's values put in before the load window go out as FLAG_RESET") {
  Store store = game();
  FlagWatcher w(shared(), FlagRole::host);
  frame(w, store);
  store.set(kDoor, 1);
  frame(w, store);
  Driver d(w, store, ContinueMode::save);
  d.step(DownedState::game_over, true, 5);
  d.step(DownedState::alive, true);
  store.set(kDoor, 0);  // the reload, before its window
  d.step(DownedState::alive, true, 5);
  CHECK(d.other.empty());
  d.step(DownedState::alive, false, 2);
  w.set_loading(true);
  d.step(DownedState::alive, false, 5);
  w.set_loading(false);
  d.step(DownedState::alive, false, FlagWatcher::kSettleFrames + 2);
  CHECK(d.resets == std::vector{flag(kDoor, 0)});
  CHECK(d.other.empty());
  CHECK(d.rule.phase() == ContinueLoad::Phase::idle);
  CHECK(d.logged("FLAG_RESET sent"));  // the summary counts the window's end
}

TEST_CASE("continue load, keep: no reload at all (a fairy): our change waits, then goes out, nothing undone") {
  Store store = game();
  FlagWatcher w(shared(), FlagRole::host);
  frame(w, store);
  Driver d(w, store, ContinueMode::keep);
  d.step(DownedState::game_over, true, 10);
  d.step(DownedState::alive, false);
  store.set(kChest, 1);  // opened at once
  d.step(DownedState::alive, false, 30);
  CHECK(d.other.empty());         // held while it waits for a window
  CHECK(store.get(kChest) == 1);  // and not undone
  d.now += ContinueLoad::kNoWindowUs;
  d.step(DownedState::alive, false);
  CHECK(d.rule.phase() == ContinueLoad::Phase::idle);
  CHECK(d.logged("no load window came in 10 s"));
  CHECK(d.other.size() == 1);  // sent the frame the mode went off
  CHECK(store.get(kChest) == 1);
  CHECK(w.stats().continue_loads == 0);
}

TEST_CASE("continue load, keep: the wallet and the ammo, per player, go back through the item store") {
  for (const auto& k : continue_keep_extra()) {
    CAPTURE(k.name);
    CHECK(proto::flag_hash(k.name) == k.hash);
    const ItemFlag* item = find_item(k.hash);
    REQUIRE(item != nullptr);
    CHECK(item->name == k.name);
    CHECK(item->rule == ItemRule::per_player);
    CHECK(item->kind == proto::FlagKind::int32);
  }
  const std::vector<FlagKey> keys = continue_keep_extra_keys();
  REQUIRE(keys.size() == continue_keep_extra().size());
  REQUIRE(keys.size() == 7);  // the wallet and six ammo counts
  Store raw = game();
  for (const FlagKey k : keys) raw.values[k] = proto::FlagEntry{k, proto::FlagKind::int32, 30};
  ItemFlagStore items(raw);
  // The item store refuses a per player value (right for the group's messages);
  // write_own lets this game's own value through.
  const proto::FlagEntry wallet{keys[0], proto::FlagKind::int32, 99};
  CHECK_FALSE(items.write_exact(wallet));
  CHECK(raw.values.at(keys[0]).value == 30);
  CHECK(items.write_own(wallet));
  CHECK(raw.values.at(keys[0]).value == 99);
  // Anything else: write_own is write_exact.
  CHECK(items.write_own(flag(kChest, 1)));
  CHECK(raw.get(kChest) == 1);

  // Through the watcher, as the hook chains it (hold -> item store -> game).
  HoldFlagStore hold(items, nullptr);
  FlagWatcher w(shared(), FlagRole::player);
  w.set_continue_keep_extra(keys);
  (void)w.frame(hold);
  w.set_continue_load(ContinueMode::keep);
  (void)w.frame(hold);  // the game over: the values are taken
  w.set_loading(true);
  (void)w.frame(hold);
  for (const FlagKey k : keys) raw.values[k].value = 0;  // the autosave's
  for (int i = 0; i < 3; ++i) (void)w.frame(hold);
  for (const FlagKey k : keys) {
    CAPTURE(k.hash);
    CHECK(raw.values.at(k).value == (k == keys[0] ? 99u : 30u));
  }
  w.set_loading(false);
  for (int i = 0; i <= FlagWatcher::kSettleFrames; ++i) (void)w.frame(hold);
  CHECK(w.stats().write_failures == 0);
  CHECK(w.stats().continue_kept >= keys.size());
}

TEST_CASE("continue load: the game over's event is learnt by name, also one with no GameOver in it") {
  // The expected name: said, and it holds the Continue as before.
  ContinueLoad usual;
  ContinueInput u;
  u.now_us = 1 * kS;
  u.state = DownedState::game_over;
  u.event_running = true;
  u.event_name = "DmF_SY_GameOver";
  auto out = usual.tick(u);
  REQUIRE(out.log.size() == 2);
  CHECK(out.log[1] == "the game over event is 'DmF_SY_GameOver' (its name has GameOver): the Continue waits while it runs");

  // Another name (the remake's menu called otherwise): taken as the game over's, said so.
  ContinueLoad rule;  // keep
  ContinueInput in;
  in.now_us = 1 * kS;
  in.state = DownedState::game_over;
  in.event_running = true;
  in.event_name = "Dm_ContinueMenu";
  out = rule.tick(in);
  REQUIRE(out.log.size() == 2);
  CHECK(out.log[1].find("the game over event is 'Dm_ContinueMenu' (its name has no GameOver: taken as the game "
                        "over's") == 0);
  // Life back with that menu still running: no time limit while it runs.
  in.state = DownedState::alive;
  in.now_us = 2 * kS;
  (void)rule.tick(in);
  in.now_us = 2 * kS + 3 * ContinueLoad::kNoWindowUs;
  CHECK(rule.tick(in).mode == ContinueMode::keep);
  CHECK(rule.phase() == ContinueLoad::Phase::reloading);
  // Another event afterwards does not hold it: 10 s from when the menu ended.
  in.event_name = "Npc_Talk";
  const std::uint64_t ended = in.now_us + 1;
  in.now_us = ended;
  CHECK(rule.tick(in).mode == ContinueMode::keep);
  in.now_us = ended + ContinueLoad::kNoWindowUs;
  out = rule.tick(in);
  CHECK(out.mode == ContinueMode::off);
  REQUIRE(out.log.size() == 1);
  CHECK(out.log[0].find("no load window came in 10 s") != std::string::npos);

  // A new game over learns afresh: the old name does not hold it.
  in.state = DownedState::game_over;
  in.event_running = false;
  in.event_name = {};
  in.now_us += kS;
  (void)rule.tick(in);
  in.state = DownedState::alive;
  in.event_running = true;
  in.event_name = "Dm_ContinueMenu";
  in.now_us += kS;
  (void)rule.tick(in);  // reloading, waiting since now (the old name is no game over event now)
  in.now_us += ContinueLoad::kNoWindowUs;
  CHECK(rule.tick(in).mode == ContinueMode::off);
}

// ---- RIESGOS_RESUELTOS.md "Mundo e historia": a small key another player gets while the host's
// Continue reloads. Before, the delta went on the reload's older count and the keep rule then wrote
// the group's value from before over it: the key was lost in the host (and told so to everyone).

namespace {

constexpr FlagKey kKeys{0x56000u, 0};  // DungeonInfo.<scene>.SmallKeyNum: a count

class CountStore : public Store {
 public:
  bool is_count(FlagKey key) override { return key == kKeys; }
  bool write(const proto::FlagEntry& e) override {
    if (e.kind != proto::FlagKind::count_delta) return Store::write(e);
    const FlagKey key = key_of(e);
    if (!values.contains(key)) return false;
    proto::FlagEntry now = values[key];
    now.value = static_cast<std::uint32_t>(static_cast<std::int32_t>(now.value) + static_cast<std::int32_t>(e.value));
    return Store::write(now);
  }
  void keys(std::int32_t n) { values[kKeys] = {kKeys, proto::FlagKind::int32, static_cast<std::uint32_t>(n)}; }
  std::int32_t keys() const { return static_cast<std::int32_t>(values.at(kKeys).value); }
};

CountStore count_game() {
  CountStore store;
  for (FlagKey k : {kChest, kDoor, kSwitch, kStory, kWallet}) store.set(k, 0);
  store.keys(3);
  return store;
}

proto::FlagSet key_gained(std::int32_t n) {
  return {2, {proto::FlagEntry{kKeys, proto::FlagKind::count_delta, static_cast<std::uint32_t>(n)}}};
}

// The FLAG_STATE totals of kKeys the host sent.
std::vector<std::int32_t> totals(const std::vector<proto::Message>& sent) {
  std::vector<std::int32_t> out;
  for (const auto& m : sent) {
    if (const auto* s = std::get_if<proto::FlagState>(&m)) {
      for (const auto& e : s->flags) {
        if (key_of(e) == kKeys) out.push_back(static_cast<std::int32_t>(e.value));
      }
    }
  }
  return out;
}

// The host falls with 3 keys, its Continue's reload brings back an autosave with 2, and P2 gets a key
// (+1) at `when`: before the reload's write, during the window after it, or with the mode on and no
// window yet (the game over). Every order ends with 4 in the host's game and 4 told.
enum class When : std::uint8_t { game_over, before_write, after_write };
void host_reload_with_a_key(When when) {
  CountStore store = count_game();
  FlagWatcher w(SharedFlags({kChest, kDoor, kSwitch, kStory, kKeys}), FlagRole::host);
  frame(w, store);
  w.set_continue_load(ContinueMode::keep);
  std::vector<proto::Message> sent;
  frame(w, store, nullptr, &sent);
  if (when == When::game_over) {
    REQUIRE(w.receive(key_gained(1)));
    frame(w, store, nullptr, &sent);
    CHECK(store.keys() == 4);
  }
  w.set_loading(true);
  w.set_scene(true);
  frame(w, store, nullptr, &sent);
  if (when == When::before_write) {
    REQUIRE(w.receive(key_gained(1)));
    frame(w, store, nullptr, &sent);
  }
  store.keys(2);  // the autosave's count
  frame(w, store, nullptr, &sent);
  if (when == When::after_write) {
    REQUIRE(w.receive(key_gained(1)));
    frame(w, store, nullptr, &sent);
  }
  store.keys(2);  // the load writes its value again
  for (int i = 0; i < 10; ++i) frame(w, store, nullptr, &sent);
  w.set_loading(false);
  for (int i = 0; i <= FlagWatcher::kSettleFrames; ++i) frame(w, store, nullptr, &sent);
  w.set_continue_load(ContinueMode::off);
  for (int i = 0; i < 3; ++i) frame(w, store, nullptr, &sent);
  CHECK(store.keys() == 4);
  const auto told = totals(sent);
  REQUIRE_FALSE(told.empty());
  CHECK(told.back() == 4);  // the player who got it hears the right total
  CHECK(std::find(told.begin(), told.end(), 3) == told.end());  // never the lost key's total
  CHECK(w.stats().continue_counts == 1);
}

}  // namespace

TEST_CASE("continue load, keep, host: a key gained by another player while we reload is never lost, in any order") {
  SUBCASE("at the game over") { host_reload_with_a_key(When::game_over); }
  SUBCASE("in the window, before the reload's write") { host_reload_with_a_key(When::before_write); }
  SUBCASE("in the window, after the reload's write") { host_reload_with_a_key(When::after_write); }
}

TEST_CASE("continue load, keep, host: a key spent by another player while we reload goes on the group's value") {
  CountStore store = count_game();
  FlagWatcher w(SharedFlags({kChest, kKeys}), FlagRole::host);
  frame(w, store);
  w.set_continue_load(ContinueMode::keep);
  frame(w, store);
  w.set_loading(true);
  w.set_scene(true);
  frame(w, store);
  store.keys(1);  // the autosave's count
  frame(w, store);
  REQUIRE(w.receive(key_gained(-1)));  // P2 opens a door with one
  std::vector<proto::Message> sent;
  frame(w, store, nullptr, &sent);
  CHECK(totals(sent) == std::vector<std::int32_t>{2});
  w.set_loading(false);
  for (int i = 0; i <= FlagWatcher::kSettleFrames; ++i) frame(w, store);
  CHECK(store.keys() == 2);
}

TEST_CASE("continue load, keep: outside a Continue a key delta is added to the game's count as before") {
  CountStore store = count_game();
  FlagWatcher w(SharedFlags({kChest, kKeys}), FlagRole::host);
  frame(w, store);
  store.keys(5);  // our own game's count moved (sent as ours)
  frame(w, store);
  REQUIRE(w.receive(key_gained(1)));
  std::vector<proto::Message> sent;
  frame(w, store, nullptr, &sent);
  CHECK(store.keys() == 6);
  CHECK(w.stats().continue_counts == 0);
  CHECK(totals(sent) == std::vector<std::int32_t>{6});
}

TEST_CASE("continue load, keep, player: the host's key total while we reload is written exact") {
  CountStore store = count_game();
  FlagWatcher w(SharedFlags({kChest, kKeys}), FlagRole::player);
  frame(w, store);
  w.set_continue_load(ContinueMode::keep);
  frame(w, store);
  w.set_loading(true);
  w.set_scene(true);
  frame(w, store);
  store.keys(1);  // our reload's count
  frame(w, store);
  REQUIRE(w.receive(state({proto::FlagEntry{kKeys, proto::FlagKind::int32, 4}})));
  frame(w, store);
  CHECK(store.keys() == 4);  // never 4 + (1 - 3)
  w.set_loading(false);
  std::vector<proto::Message> sent;
  for (int i = 0; i <= FlagWatcher::kSettleFrames; ++i) frame(w, store, nullptr, &sent);
  CHECK(store.keys() == 4);
  CHECK(sent.empty());
}

TEST_CASE("continue load, keep: a held flag the reload moves once Link is back is kept, never sent") {
  // A dungeon's flag is held while we are in its area (HoldFlagStore, coop_hook.cpp). The Continue puts Link
  // back at the dungeon's entrance and the room writes the save's value again with Link there: the write back
  // waits for the hold, and the reload's value must not go out when the window ends.
  for (const FlagRole role : {FlagRole::host, FlagRole::player}) {
    const bool hosting = role == FlagRole::host;
    CAPTURE(hosting);
    Store store = game();
    FlagWatcher w(shared(), role);
    frame(w, store);
    if (hosting) {
      store.set(kChest, 1);
      frame(w, store);
    } else {
      CHECK(w.receive(state({flag(kChest, 1)})));
      frame(w, store);
    }
    REQUIRE(store.get(kChest) == 1);
    bool hold = false;
    HoldFlagStore held(store, [&hold](FlagKey key) { return hold && key == kChest; });
    std::vector<proto::Message> sent;
    const auto step = [&] {
      for (auto& m : decode(w.frame(held))) sent.push_back(std::move(m));
    };
    w.set_continue_load(ContinueMode::keep);
    step();
    w.set_loading(true);
    w.set_scene(true);
    for (int i = 0; i < 10; ++i) step();
    w.set_loading(false);  // Link is back, in the dungeon: its flags are held
    hold = true;
    step();
    store.set(kChest, 0);  // the room puts the save's value back
    for (int i = 0; i <= FlagWatcher::kSettleFrames; ++i) step();
    CHECK_FALSE(w.settling());
    CHECK(sent.empty());            // not the game's own change: nothing goes out
    CHECK(store.get(kChest) == 0);  // held: ours shows it once we leave the area
    hold = false;
    step();
    CHECK(store.get(kChest) == 1);  // the group's value from before the fall
    step();
    CHECK(sent.empty());
  }
}
