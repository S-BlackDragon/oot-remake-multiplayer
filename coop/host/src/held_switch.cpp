// ORM: held floor switches pressed in every game while any player stands on
//   them (rules in coop/host/held_switch.hpp).
// Doc: oot-remake docs/SOSTENIDOS.md.
// Switch: LICHII_ORM_HELD_SWITCHES, LICHII_ORM_HELD_SWITCH_LIST.
// Test: tests/host/held_switch_test.cpp.

#include "coop/host/held_switch.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <locale>
#include <sstream>
#include <system_error>

#include "coop/host/scene.hpp"

namespace coop::host {
namespace {

// Read from the romfs of build e0f3ccfaf50065ae (1.1.1): every Banc *_Static
// placement of a step switch whose ActorParam has Obj_Stepswitch_A_03 as its
// parent (on only while something stands on it), and the timed ones
// (Obj_StepSwitch_A_02, CountTime). Left out: Jabu-Jabu's
// Obj_InsideJabu_Jabu_Stepswitch_A_02 (SwitchStepOnTwoPeople: Link and this
// game's Ruto, each game its own), the permanent Obj_StepSwitch_A_01 (its
// pressed state is a saved flag, shared by daurosk's list), A_04 and A_05.
// The Forest Temple and the Bottom of the Well have no held step switch.
// yaw: Rotate.y, or pi - Rotate.y for the placements turned (pi, y, pi).
// Areas: CheckObjectOn (A_03: box 0.5 x 0.5, y -0.1..0.3; Jabu's A_03/A_04:
// cylinder r 1.0, y -0.5..0.5) and the timed button's Tgt (0.44, y -0.335..
// -0.015), widened down to the pressed button (feet are tested).
constexpr std::string_view kBuiltin = R"(# built-in held switches (coop/host/src/held_switch.cpp)
switch DodongoCavern 1607199957418968783 Obj_Stepswitch_A_03 78.450 99.000 -6.500 yaw=-1.5708 flag=DodongoCavern_Room02_StepSwitchOnPlayer label=room02
switch DodongoCavern 18014361144700329294 Obj_Stepswitch_A_03 0.310 90.660 -77.935 yaw=-1.5708 flag=DodongoCavern_Room19_StepSwitchOnPlayer label=room19
switch FireTemple 7865893853239028816 Obj_Stepswitch_A_03 -51.465 147.000 -18.928 yaw=-0.7854 flag=FireTemple_Room10_StepSwitchOnLink label=room10
switch FireTemple 17051710586489310321 Obj_StepSwitch_A_02 51.157 189.010 -6.757 yaw=-0.7852 kind=timed label=room08
switch FireTemple 12554840317369602718 Obj_StepSwitch_A_02 -58.934 188.782 0.435 yaw=1.5708 kind=timed label=room13
switch GerudoTrainingGround 12204023271289760717 Obj_StepSwitch_A_02 32.500 94.000 -40.013 yaw=1.5708 kind=timed label=room06
switch InsideJabuJabuBelly 6199188956081581189 Obj_InsideJabu_Jabu_StepSwitch_A_03 7.723 92.346 -141.340 yaw=3.0892 cyl=1.0,-0.6,0.5 label=room07
switch InsideJabuJabuBelly 2614356414187277000 Obj_InsideJabu_Jabu_StepSwitch_A_04 28.900 74.484 -58.486 yaw=3.1416 cyl=1.0,-0.6,0.5 label=room14a
switch InsideJabuJabuBelly 1096783339081413828 Obj_InsideJabu_Jabu_StepSwitch_A_04 29.250 77.481 -66.740 yaw=0 cyl=1.0,-0.6,0.5 label=room14b
switch InsideJabuJabuBelly 6821128215755838807 Obj_InsideJabu_Jabu_StepSwitch_A_03 8.510 102.462 -51.438 yaw=1.0468 cyl=1.0,-0.6,0.5 label=room17
switch InsideTheDekuTree 15143979767362734130 Obj_StepSwitch_A_02 -17.469 119.600 -4.899 yaw=0 kind=timed label=roomD
switch SpiritTemple 9537646758471876367 Obj_Stepswitch_A_03 -20.206 129.140 -45.470 yaw=1.5708 flag=SpiritTemple_Room08_StepSwitchOnLink label=room08
switch SpiritTemple 9971258386446661589 Obj_Stepswitch_A_03_Amos 55.811 128.967 -14.795 yaw=1.5708 flag=SpiritTemple_Room23_StepSwitchOnLink label=room23
switch WaterTemple 5998802587513005458 Obj_WaterTemple_Stepswitch_A_03 -8.300 93.795 -114.170 yaw=3.1416 label=room14
)";

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

std::vector<std::string_view> tokens(std::string_view line) {
  std::vector<std::string_view> out;
  std::size_t i = 0;
  while (i < line.size()) {
    while (i < line.size() && is_space(line[i])) ++i;
    const std::size_t start = i;
    while (i < line.size() && !is_space(line[i])) ++i;
    if (i > start) out.push_back(line.substr(start, i - start));
  }
  return out;
}

std::optional<std::uint64_t> parse_u64(std::string_view s) {
  int base = 10;
  if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
    s.remove_prefix(2);
    base = 16;
  }
  if (s.empty()) return std::nullopt;
  std::uint64_t v = 0;
  const auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v, base);
  if (ec != std::errc{} || p != s.data() + s.size()) return std::nullopt;
  return v;
}

std::optional<std::uint64_t> parse_hex(std::string_view s) {
  if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s.remove_prefix(2);
  if (s.empty() || s.size() > 16) return std::nullopt;
  std::uint64_t v = 0;
  const auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v, 16);
  if (ec != std::errc{} || p != s.data() + s.size()) return std::nullopt;
  return v;
}

std::optional<float> parse_float(std::string_view s) {
  // from_chars for float is not in every standard library this builds with.
  if (s.empty() || s.size() > 31) return std::nullopt;
  const std::string text(s);
  std::istringstream in(text);
  in.imbue(std::locale::classic());
  float v = 0.0f;
  in >> v;
  if (in.fail() || !in.eof() || !std::isfinite(v)) return std::nullopt;
  return v;
}

std::vector<std::string_view> split(std::string_view s, char sep) {
  std::vector<std::string_view> out;
  std::size_t start = 0;
  for (std::size_t i = 0; i <= s.size(); ++i) {
    if (i == s.size() || s[i] == sep) {
      out.push_back(s.substr(start, i - start));
      start = i + 1;
    }
  }
  return out;
}

std::optional<std::vector<float>> parse_floats(std::string_view s, std::size_t count) {
  const auto parts = split(s, ',');
  if (parts.size() != count) return std::nullopt;
  std::vector<float> out;
  for (const auto p : parts) {
    const auto f = parse_float(p);
    if (!f) return std::nullopt;
    out.push_back(*f);
  }
  return out;
}

// A flag token: a name (hashed) or 0x and 1 to 8 hex digits.
std::optional<std::uint32_t> parse_flag(std::string_view s) {
  if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
    if (s.size() > 10) return std::nullopt;
    const auto v = parse_hex(s);
    if (!v) return std::nullopt;
    return static_cast<std::uint32_t>(*v);
  }
  if (s.empty()) return std::nullopt;
  return flag_hash(s);
}

HeldArea default_area(HeldKind kind) {
  HeldArea a;
  if (kind == HeldKind::timed) {
    a.half_x = a.half_z = 0.44f;
    a.y_min = -0.4f;
    a.y_max = 0.25f;
  }
  return a;
}

std::string_view strip_comment(std::string_view line) {
  const auto hash = line.find('#');
  return hash == std::string_view::npos ? line : line.substr(0, hash);
}

}  // namespace

bool HeldSwitchSpec::contains(const protocol::Vec3& p, float margin) const {
  const float dx = p[0] - position[0];
  const float dy = p[1] - position[1];
  const float dz = p[2] - position[2];
  if (dy < area.y_min - margin || dy > area.y_max + margin) return false;
  // Into the switch's frame: turned by -yaw about +y.
  const float c = std::cos(yaw);
  const float s = std::sin(yaw);
  const float lx = c * dx - s * dz;
  const float lz = s * dx + c * dz;
  if (area.shape == HeldArea::Shape::cylinder) {
    const float r = area.half_x + margin;
    return lx * lx + lz * lz <= r * r;
  }
  return std::fabs(lx) <= area.half_x + margin && std::fabs(lz) <= area.half_z + margin;
}

bool parse_held_where(std::string_view text, HeldWord& out) {
  constexpr std::string_view kActor = "actor+";
  constexpr std::string_view kComponent = "c+";
  if (text.starts_with(kActor)) {
    const auto off = parse_hex(text.substr(kActor.size()));
    if (!off || text.substr(kActor.size()).size() < 3) return false;  // "0x" and a digit
    out.in_component = false;
    out.entry = 0;
    out.offset = *off;
  } else if (text.starts_with(kComponent)) {
    const auto parts = split(text.substr(kComponent.size()), '+');
    if (parts.size() != 2 || parts[0].size() < 3 || parts[1].size() < 3) return false;
    const auto entry = parse_hex(parts[0]);
    const auto off = parse_hex(parts[1]);
    if (!entry || !off || *entry % 8 != 0) return false;
    out.in_component = true;
    out.entry = *entry;
    out.offset = *off;
  } else {
    return false;
  }
  if (out.offset % 8 != 0) return false;  // 64-bit words, as the probes read them
  out.where = std::string(text);
  return true;
}

std::optional<HeldSwitchList> HeldSwitchList::parse(std::string_view text, std::string* error) {
  HeldSwitchList list;
  std::size_t line_no = 0;
  std::size_t start = 0;
  while (start <= text.size()) {
    const std::size_t end = std::min(text.find('\n', start), text.size());
    const std::string_view line = strip_comment(text.substr(start, end - start));
    start = end + 1;
    ++line_no;
    const auto t = tokens(line);
    if (t.empty()) {
      if (end == text.size()) break;
      continue;
    }
    auto fail = [&](const std::string& why) -> std::optional<HeldSwitchList> {
      if (error) *error = "line " + std::to_string(line_no) + ": " + why;
      return std::nullopt;
    };
    if (t[0] == "switch") {
      if (t.size() < 7) return fail("switch takes a place, a hash, an actor and x y z");
      HeldSwitchSpec s;
      s.place = std::string(place_of(t[1]));
      const auto hash = parse_u64(t[2]);
      if (!hash || *hash == 0) return fail("bad placement hash '" + std::string(t[2]) + "'");
      s.hash = *hash;
      s.actor = std::string(t[3]);
      for (std::size_t i = 0; i < 3; ++i) {
        const auto f = parse_float(t[4 + i]);
        if (!f) return fail("bad coordinate '" + std::string(t[4 + i]) + "'");
        s.position[i] = *f;
      }
      std::optional<HeldArea> area;
      for (std::size_t i = 7; i < t.size(); ++i) {
        const auto eq = t[i].find('=');
        if (eq == std::string_view::npos) return fail("expected key=value, got '" + std::string(t[i]) + "'");
        const std::string_view key = t[i].substr(0, eq);
        const std::string_view value = t[i].substr(eq + 1);
        if (key == "yaw") {
          const auto f = parse_float(value);
          if (!f) return fail("bad yaw");
          s.yaw = *f;
        } else if (key == "kind") {
          if (value == "held") {
            s.kind = HeldKind::held;
          } else if (value == "timed") {
            s.kind = HeldKind::timed;
          } else {
            return fail("kind is held or timed");
          }
        } else if (key == "box") {
          const auto f = parse_floats(value, 4);
          if (!f || (*f)[0] <= 0 || (*f)[1] <= 0 || (*f)[2] >= (*f)[3]) return fail("box=hx,hz,ymin,ymax");
          area = HeldArea{HeldArea::Shape::box, (*f)[0], (*f)[1], (*f)[2], (*f)[3]};
        } else if (key == "cyl") {
          const auto f = parse_floats(value, 3);
          if (!f || (*f)[0] <= 0 || (*f)[1] >= (*f)[2]) return fail("cyl=r,ymin,ymax");
          area = HeldArea{HeldArea::Shape::cylinder, (*f)[0], (*f)[0], (*f)[1], (*f)[2]};
        } else if (key == "flag") {
          const auto f = parse_flag(value);
          if (!f) return fail("flag= takes a name or 0x and 1 to 8 hex digits");
          s.flag = *f;
          s.flag_name = std::string(value);
        } else if (key == "label") {
          s.label = std::string(value);
        } else {
          return fail("unknown key '" + std::string(key) + "'");
        }
      }
      s.area = area ? *area : default_area(s.kind);
      list.put(std::move(s));
    } else if (t[0] == "word") {
      if (t.size() < 5) return fail("word takes an actor, a place in it, on= and off=");
      HeldWord w;
      w.actor = std::string(t[1]);
      if (!parse_held_where(t[2], w)) return fail("bad word place '" + std::string(t[2]) + "'");
      bool on = false;
      bool off = false;
      for (std::size_t i = 3; i < t.size(); ++i) {
        const auto eq = t[i].find('=');
        const std::string_view key = eq == std::string_view::npos ? t[i] : t[i].substr(0, eq);
        const auto value = eq == std::string_view::npos ? std::nullopt : parse_hex(t[i].substr(eq + 1));
        if (!value) return fail("expected on=, off= or mask= with a hex value");
        if (key == "on") {
          w.on = *value;
          on = true;
        } else if (key == "off") {
          w.off = *value;
          off = true;
        } else if (key == "mask") {
          if (*value == 0) return fail("mask=0 writes nothing");
          w.mask = *value;
        } else {
          return fail("unknown key '" + std::string(key) + "'");
        }
      }
      if (!on || !off) return fail("word needs on= and off=");
      if ((w.on & w.mask) == (w.off & w.mask)) return fail("on and off are the same under the mask");
      list.put(std::move(w));
    } else if (t[0] == "off") {
      if (t.size() != 2) return fail("off takes a placement hash");
      const auto hash = parse_u64(t[1]);
      if (!hash) return fail("bad placement hash");
      list.drop(*hash);
      list.drops_.push_back(*hash);  // and off the lists this one goes on top of
    } else {
      return fail("unknown line '" + std::string(t[0]) + "'");
    }
    if (end == text.size()) break;
  }
  return list;
}

std::optional<HeldSwitchList> HeldSwitchList::load(const std::string& path, std::string* error) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    if (error) *error = "cannot read " + path;
    return std::nullopt;
  }
  std::ostringstream text;
  text << in.rdbuf();
  std::string why;
  auto list = parse(text.str(), &why);
  if (!list && error) *error = path + ": " + why;
  return list;
}

std::string_view HeldSwitchList::builtin_text() { return kBuiltin; }

const HeldSwitchList& HeldSwitchList::builtin() {
  static const HeldSwitchList list = [] {
    auto parsed = parse(kBuiltin);
    return parsed ? *parsed : HeldSwitchList{};  // the test checks it parses
  }();
  return list;
}

void HeldSwitchList::merge(const HeldSwitchList& other) {
  // Its offs first: a switch it dropped and listed again after is kept.
  for (const std::uint64_t hash : other.drops_) {
    drop(hash);
    drops_.push_back(hash);
  }
  for (const auto& s : other.switches_) put(s);
  for (const auto& w : other.words_) put(w);
}

void HeldSwitchList::put(HeldSwitchSpec spec) {
  for (auto& s : switches_) {
    if (s.hash == spec.hash) {
      s = std::move(spec);
      return;
    }
  }
  switches_.push_back(std::move(spec));
}

void HeldSwitchList::put(HeldWord word) {
  for (auto& w : words_) {
    if (w.actor == word.actor && w.in_component == word.in_component && w.entry == word.entry &&
        w.offset == word.offset) {
      w = std::move(word);
      return;
    }
  }
  words_.push_back(std::move(word));
}

void HeldSwitchList::drop(std::uint64_t hash) {
  std::erase_if(switches_, [hash](const HeldSwitchSpec& s) { return s.hash == hash; });
}

std::vector<const HeldSwitchSpec*> HeldSwitchList::in_place(std::string_view scene) const {
  std::vector<const HeldSwitchSpec*> out;
  if (scene.empty()) return out;
  const std::string_view place = place_of(scene);
  for (const auto& s : switches_) {
    if (s.place == place) out.push_back(&s);
  }
  return out;
}

const HeldSwitchSpec* HeldSwitchList::find(std::uint64_t hash) const {
  for (const auto& s : switches_) {
    if (s.hash == hash) return &s;
  }
  return nullptr;
}

std::vector<const HeldWord*> HeldSwitchList::words_for(std::string_view actor) const {
  std::vector<const HeldWord*> out;
  for (const auto& w : words_) {
    if (w.actor == actor) out.push_back(&w);
  }
  return out;
}

std::vector<std::uint64_t> HeldFrame::held_by_others() const {
  std::vector<std::uint64_t> out;
  for (const auto& s : states) {
    if (s.held_by_others()) out.push_back(s.hash);
  }
  return out;
}

void HeldSwitchRule::reset() {
  scene_.reset();
  tracked_.clear();
}

HeldFrame HeldSwitchRule::frame(const HeldInput& in) {
  HeldFrame out;
  const std::optional<std::string> scene =
      in.scene && !in.scene->empty() ? in.scene : std::optional<std::string>{};
  if (scene != scene_) {
    // A dungeon is one scene, its rooms included: a change of scene (or a
    // load, the title) ends every hold of the scene before.
    if (scene_) {
      out.scene_changed = true;
      for (const auto& [hash, t] : tracked_) {
        if (t.held_by_others) out.left.push_back(hash);
      }
    }
    tracked_.clear();
    scene_ = scene;
  }
  if (!scene) return out;
  for (const HeldSwitchSpec* spec : list_->in_place(*scene)) {
    Tracked& t = tracked_[spec->hash];
    // Who is in this frame: our Link (0) and each live other in our scene.
    std::map<std::uint8_t, protocol::Vec3> present;
    if (in.link) present[0] = *in.link;
    for (const auto& o : in.others) {
      if (o.number == 0) continue;  // 0 is our Link here
      if (!o.live || !o.scene || !same_place(std::string_view(*o.scene), std::string_view(*scene))) continue;
      present[o.number] = o.position;
    }
    // Holders who are gone (not shown, another scene, faded out): at once.
    // Our Link not read this frame keeps what it was (a frame with no read
    // must not let the switch go under it).
    for (auto it = t.holders.begin(); it != t.holders.end();) {
      if (it->first == 0 && !in.link) {
        ++it;
      } else if (!present.count(it->first)) {
        out.changes.push_back({it->first == 0 ? HeldChange::Kind::stepped_off : HeldChange::Kind::gone,
                               spec->hash, it->first, false});
        it = t.holders.erase(it);
      } else {
        ++it;
      }
    }
    for (const auto& [who, at] : present) {
      const auto h = t.holders.find(who);
      if (h == t.holders.end()) {
        if (spec->contains(at)) {
          t.holders[who] = Holder{};
          out.changes.push_back({HeldChange::Kind::stepped_on, spec->hash, who, false});
        }
        continue;
      }
      if (spec->contains(at)) {
        h->second.outside_since_us = 0;
        continue;
      }
      if (h->second.outside_since_us == 0) h->second.outside_since_us = std::max<std::uint64_t>(in.now_us, 1);
      const bool far = !spec->contains(at, kLeaveMarginM);
      const bool long_out = in.now_us >= h->second.outside_since_us + kLeaveUs;
      if (far || long_out) {
        t.holders.erase(h);
        out.changes.push_back({HeldChange::Kind::stepped_off, spec->hash, who, false});
      }
    }
    HeldState state;
    state.hash = spec->hash;
    state.ours = t.holders.count(0) != 0;
    for (const auto& [who, h] : t.holders) {
      if (who != 0) state.others.push_back(who);
    }
    const bool now = state.held_by_others();
    if (now != t.held_by_others) {
      out.changes.push_back({now ? HeldChange::Kind::pressed : HeldChange::Kind::released, spec->hash,
                             now ? state.others.front() : std::uint8_t{0}, state.ours});
      t.held_by_others = now;
    }
    out.states.push_back(std::move(state));
  }
  return out;
}

std::vector<HeldFlagApply::Note> HeldFlagApply::apply(const HeldFrame& frame, FlagStore& store) {
  std::vector<Note> notes;
  auto write = [&](std::uint64_t hash, std::uint32_t flag, std::uint32_t value, bool again) {
    const bool ok =
        store.write(protocol::FlagEntry(FlagKey(flag, std::uint16_t{0}), protocol::FlagKind::boolean, value));
    // A failed write is tried each frame; it is noted the first time only.
    // A rewrite over the game's 0 is noted once a press (a room that puts
    // it back every frame would log a line a frame); the rest are counted.
    if (ok) {
      failed_.erase(flag);
      if (!again || again_noted_.insert(hash).second) notes.push_back({hash, flag, value, ok, again});
    } else if (failed_.insert(flag).second) {
      notes.push_back({hash, flag, value, ok, again});
    }
    return ok;
  };
  // The scene before ended with 1s of ours in it: put them back to 0.
  if (frame.scene_changed) {
    for (const auto& [hash, flag] : written_) pending_clear_[flag] = hash;
    written_.clear();
    again_noted_.clear();
  }
  std::set<std::uint32_t> ours_now;
  for (const auto& state : frame.states) {
    const HeldSwitchSpec* spec = list_->find(state.hash);
    if (!spec || !spec->flag) continue;
    const std::uint32_t flag = *spec->flag;
    if (state.ours) ours_now.insert(flag);
    const bool want = state.held_by_others() && !state.ours;
    const auto it = written_.find(state.hash);
    if (want) {
      pending_clear_.erase(flag);
      const auto now = store.read(FlagKey(flag, std::uint16_t{0}));
      if (now && now->value != 0) {
        if (it == written_.end()) {
          // The game's own 1 (it set it before us): ours from here on, so
          // the release puts it back to 0 when nobody stands on it.
          written_[state.hash] = flag;
        }
        continue;
      }
      const bool again = it != written_.end();
      if (write(state.hash, flag, 1, again)) {
        written_[state.hash] = flag;
        if (again) ++rewrites_;
      }
    } else if (it != written_.end()) {
      written_.erase(it);
      again_noted_.erase(state.hash);
      // Our Link on it: the game's own value is right; leave it alone.
      if (!state.ours) {
        if (!write(state.hash, flag, 0, false)) pending_clear_[flag] = state.hash;
      }
    }
  }
  // Switches of our scene no longer listed in the frame (none: out of the
  // scene) were handled by scene_changed. Retry the clears that wait.
  for (auto it = pending_clear_.begin(); it != pending_clear_.end();) {
    if (ours_now.count(it->first)) {  // our Link stands on it again: the game's
      it = pending_clear_.erase(it);
      continue;
    }
    // While loading too: a store that cannot read or write yet keeps it.
    const auto now = store.read(FlagKey(it->first, std::uint16_t{0}));
    if (now && now->value == 0) {
      it = pending_clear_.erase(it);
      continue;
    }
    if (now && write(it->second, it->first, 0, false)) {
      it = pending_clear_.erase(it);
    } else {
      ++it;
    }
  }
  return notes;
}

std::optional<std::uint64_t> HeldWordApply::address_of(const HeldWord& w, std::uint64_t actor,
                                                       const ReadWord& read) {
  if (actor < 0x1000) return std::nullopt;
  if (!w.in_component) return actor + w.offset;
  const auto table = read(actor + kActorTable);
  if (!table || *table < 0x1000) return std::nullopt;
  const auto component = read(*table + w.entry);
  if (!component || *component < 0x1000) return std::nullopt;
  return *component + w.offset;
}

std::vector<HeldWordApply::Note> HeldWordApply::apply(const HeldFrame& frame, const FindActor& find,
                                                      const ReadWord& read, const WriteWord& write) {
  std::vector<Note> notes;
  if (frame.scene_changed) {  // the actors are gone with their scene
    pressing_.clear();
    noted_.clear();
    actor_of_.clear();
  }
  for (const auto& state : frame.states) {
    const HeldSwitchSpec* spec = list_->find(state.hash);
    if (!spec) continue;
    const auto words = list_->words_for(spec->actor);
    if (words.empty()) continue;
    const bool want = state.held_by_others() && !state.ours;
    if (want) {
      const auto actor = find(state.hash);
      if (!actor) continue;  // not loaded: try again next frame
      if (actor_of_.count(state.hash) && actor_of_[state.hash] != *actor) noted_.erase(state.hash);  // reloaded
      actor_of_[state.hash] = *actor;
      pressing_.insert(state.hash);
      const bool first = noted_.insert(state.hash).second;
      for (const HeldWord* w : words) {
        const auto at = address_of(*w, *actor, read);
        if (!at) continue;
        const auto old = read(*at);
        if (!old) continue;
        if ((*old & w->mask) == (w->on & w->mask)) continue;
        const std::uint64_t value = (*old & ~w->mask) | (w->on & w->mask);
        const bool ok = write(*at, value);
        ++writes_;
        if (first || !ok) notes.push_back({state.hash, w->where, *at, *old, value, ok, false});
      }
    } else if (pressing_.erase(state.hash)) {
      noted_.erase(state.hash);
      const auto it = actor_of_.find(state.hash);
      const auto actor = find(state.hash);
      // Our Link holds it now, the actor is not the one we wrote, or a timed
      // switch: its own clock runs from here, as when a Link steps off it.
      if (state.ours || spec->kind == HeldKind::timed || !actor || it == actor_of_.end() ||
          it->second != *actor) {
        actor_of_.erase(state.hash);
        continue;
      }
      actor_of_.erase(it);
      for (const HeldWord* w : words) {
        const auto at = address_of(*w, *actor, read);
        if (!at) continue;
        const auto old = read(*at);
        if (!old || (*old & w->mask) != (w->on & w->mask)) continue;  // the game moved it already
        const std::uint64_t value = (*old & ~w->mask) | (w->off & w->mask);
        const bool ok = write(*at, value);
        ++writes_;
        notes.push_back({state.hash, w->where, *at, *old, value, ok, true});
      }
    }
  }
  return notes;
}

// ------------------------------------------------------------------ the door check

bool held_door_actor(std::string_view name) {
  constexpr std::string_view kParts[] = {"Shutter", "FlameWall", "FireWall"};
  for (const std::string_view p : kParts) {
    if (name.find(p) != std::string_view::npos) return true;
  }
  return false;
}

namespace {

float door_metres(const protocol::Vec3& a, const protocol::Vec3& b) {
  const float dx = a[0] - b[0];
  const float dy = a[1] - b[1];
  const float dz = a[2] - b[2];
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

std::string door_seconds(std::uint64_t us) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.1f", static_cast<double>(us) / 1e6);
  return buf;
}

std::string door_label(const HeldSwitchSpec& s) { return s.place + " " + (s.label.empty() ? s.actor : s.label); }

std::string door_path(const HeldDoorCheck::Path& p) {
  if (p.flag && p.words) return "the flag and the words";
  if (p.words) return "the words";
  return "the flag";
}

// What says a door near the switch opened since `base`, or nothing.
std::string door_sign(const std::vector<HeldDoorSeen>& base, const std::vector<HeldDoorSeen>& now) {
  for (const HeldDoorSeen& b : base) {
    const HeldDoorSeen* match = nullptr;
    float best = 0.0f;
    for (const HeldDoorSeen& n : now) {
      if (n.name != b.name) continue;
      const float d = door_metres(n.position, b.position);
      if (match == nullptr || d < best) {
        match = &n;
        best = d;
      }
    }
    if (match == nullptr) return b.name + " went";
    if (std::isfinite(best) && best > HeldDoorCheck::kMoveM) {
      char buf[48];
      std::snprintf(buf, sizeof buf, " moved %.2f m", static_cast<double>(best));
      return b.name + buf;
    }
    if (match->animation && match->animation != b.animation &&
        match->animation->find("Open") != std::string::npos) {
      return b.name + " plays '" + *match->animation + "'";
    }
  }
  return {};
}

}  // namespace

std::vector<std::string> HeldDoorCheck::frame(const HeldFrame& frame,
                                              const std::optional<std::vector<HeldDoorSeen>>& doors,
                                              const PathOf& path_of, std::uint64_t now_us) {
  std::vector<std::string> lines;
  if (frame.scene_changed) checks_.clear();  // the switches and doors went with the scene
  const auto near = [&doors](const HeldSwitchSpec& spec) {
    std::vector<HeldDoorSeen> out;
    if (!doors) return out;
    for (const HeldDoorSeen& d : *doors) {
      if (held_door_actor(d.name) && door_metres(d.position, spec.position) <= kNearM) out.push_back(d);
    }
    return out;
  };
  for (const HeldChange& c : frame.changes) {
    const HeldSwitchSpec* spec = list_->find(c.hash);
    if (spec == nullptr) continue;
    if (c.kind == HeldChange::Kind::pressed) {
      checks_.erase(c.hash);
      if (c.ours) continue;  // our own Link on it: the game presses it itself
      const Path path = path_of ? path_of(*spec) : Path{};
      if (!path.flag && !path.words) {
        lines.push_back("held switch: " + door_label(*spec) +
                        ": nothing to press it with here (no flag written, no word line): its door stays shut");
        continue;
      }
      Check k;
      k.since_us = now_us;
      k.path = path;
      if (doors) k.base = near(*spec);
      checks_[c.hash] = std::move(k);
    } else if (c.kind == HeldChange::Kind::released) {
      if (checks_.erase(c.hash) != 0) {
        lines.push_back("held switch: " + door_label(*spec) + ": let go before its door moved (not told)");
      }
    }
  }
  for (auto it = checks_.begin(); it != checks_.end();) {
    const HeldSwitchSpec* spec = list_->find(it->first);
    Check& k = it->second;
    if (spec == nullptr) {
      it = checks_.erase(it);
      continue;
    }
    if (k.base && k.base->empty()) {
      lines.push_back("held switch: " + door_label(*spec) + ": no door actor within " +
                      std::to_string(static_cast<int>(kNearM)) + " m to tell by: not told whether " +
                      door_path(k.path) + " opens it");
      it = checks_.erase(it);
      continue;
    }
    if (k.base && doors) {
      const std::string sign = door_sign(*k.base, near(*spec));
      if (!sign.empty()) {
        verdicts_[it->first] = HeldDoorVerdict::opened;
        lines.push_back("held switch: " + door_label(*spec) + ": door opened with " + door_path(k.path) + " (" +
                        sign + ", " + door_seconds(now_us - k.since_us) + " s after the press)");
        it = checks_.erase(it);
        continue;
      }
    }
    if (now_us >= k.since_us && now_us - k.since_us >= kWaitUs) {
      if (!k.base) {
        lines.push_back("held switch: " + door_label(*spec) +
                        ": door state not read (LICHII_COOP_OBJECTS off?): not told whether " + door_path(k.path) +
                        " opens it");
      } else {
        verdicts_[it->first] = HeldDoorVerdict::stays_shut;
        lines.push_back("held switch: " + door_label(*spec) + ": door did not move " +
                        door_seconds(now_us - k.since_us) + " s after " + door_path(k.path) +
                        (k.path.words ? ": its words do not press it either (run the probe again)"
                                      : ": needs the actor words (run the probe, docs/SOSTENIDOS.md)"));
      }
      it = checks_.erase(it);
      continue;
    }
    ++it;
  }
  return lines;
}

HeldDoorVerdict HeldDoorCheck::verdict(std::uint64_t hash) const {
  const auto it = verdicts_.find(hash);
  return it == verdicts_.end() ? HeldDoorVerdict::unknown : it->second;
}

const char* to_string(HeldChange::Kind kind) {
  switch (kind) {
    case HeldChange::Kind::stepped_on: return "stepped on";
    case HeldChange::Kind::stepped_off: return "stepped off";
    case HeldChange::Kind::gone: return "gone";
    case HeldChange::Kind::pressed: return "pressed";
    case HeldChange::Kind::released: return "released";
  }
  return "?";
}

}  // namespace coop::host
