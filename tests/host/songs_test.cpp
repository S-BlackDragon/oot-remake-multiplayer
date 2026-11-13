// Tests for the ORM songs with several players (coop/host/songs.hpp, oot-remake
// docs/CANCIONES.md): the song table, the effects and the kit's shared_dungeon_songs.txt
// made from them, and the warp-song invitation (oot-64 D-067).
// Mutants these catch (each turned red by hand when written, by reading the checks):
//   no pad check in WarpInvite::tick (any scene change to a warp scene invites): "a walk to
//   the field is no warp"; the invitation never closing: "it closes after 10 s"; the hold
//   not reset on release: "a release starts the hold over"; an effect's lines left out of
//   the kit file: "the kit file says what the effects say".
#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <algorithm>
#include <set>
#include <string>

#include "coop/host/dungeon_lists.hpp"
#include "coop/host/game_items.hpp"
#include "coop/host/scene.hpp"
#include "coop/host/songs.hpp"
#include "coop/host/story.hpp"
#include "coop_chests.h"
#include "coop_dungeon.h"

namespace {

using coop::host::DungeonList;
using coop::host::FlagKey;
using coop::host::InviteInput;
using coop::host::InviteMate;
using coop::host::InviteOutput;
using coop::host::Song;
using coop::host::SongRule;
using coop::host::WarpInvite;
using coop::host::flag_hash;

constexpr const char* kKit = COOP_TEST_SOURCE_DIR "/../tools/release/kit/";

DungeonList parsed(std::string_view text) {
  std::string error;
  auto list = DungeonList::parse(text, &error);
  INFO(error);
  REQUIRE(list);
  return *list;
}

DungeonList kit_file(const char* name) {
  std::string error;
  auto list = DungeonList::load(std::string(kKit) + name, &error);
  INFO(error);
  REQUIRE(list);
  return *list;
}

bool ascii(std::string_view s) {
  return std::all_of(s.begin(), s.end(), [](char c) { return c >= 0x20 && c <= 0x7e; });
}

}  // namespace

TEST_CASE("songs: the thirteen, in the game's MelodyType order") {
  const auto all = coop::host::songs();
  REQUIRE(all.size() == coop::host::kSongCount);
  std::size_t warps = 0;
  for (std::size_t i = 0; i < all.size(); ++i) {
    const auto& s = all[i];
    CAPTURE(s.id);
    CHECK(static_cast<std::size_t>(s.song) == i);
    CHECK(coop::host::song_from_melody_type(static_cast<int>(i)) == s.song);
    CHECK(s.item == "IsGet.Item_Melody_" + std::string(s.id));
    // Knowing a song is the group's: every song's item is in the shared item list.
    CHECK(coop::host::find_item(flag_hash(s.item)) != nullptr);
    CHECK(coop::host::song_from_id(s.id) == s.song);
    CHECK(coop::host::song_from_id("Item_Melody_" + std::string(s.id)) == s.song);
    CHECK(coop::host::song_from_id(s.item) == s.song);
    CHECK(ascii(s.spanish));
    CHECK_FALSE(s.learned_by.empty());
    if (s.warp) {
      ++warps;
      CHECK(coop::host::warp_song_to(s.warp->child_scene) == s.song);
      CHECK(coop::host::warp_song_to(s.warp->adult_scene) == s.song);
      CHECK(s.warp->child_scene.ends_with("_Child"));
      CHECK(s.warp->adult_scene.ends_with("_Adult"));
      CHECK(coop::host::place_of(s.warp->child_scene) == coop::host::place_of(s.warp->adult_scene));
    }
  }
  CHECK(warps == 6);
  for (const Song s : {Song::minuet, Song::bolero, Song::serenade, Song::requiem, Song::nocturne, Song::prelude}) {
    CHECK(coop::host::song_info(s).warp.has_value());
  }
  CHECK_FALSE(coop::host::song_from_melody_type(-1));
  CHECK_FALSE(coop::host::song_from_melody_type(13));
  CHECK_FALSE(coop::host::song_from_id("Ocarina_Fairy"));
  CHECK_FALSE(coop::host::warp_song_to("KokiriForest_Child"));
  CHECK_FALSE(coop::host::warp_song_to(""));
  CHECK(coop::host::warp_song_to("Market_Adult") == Song::prelude);
  CHECK(coop::host::warp_song_to("HyruleField_Child") == Song::serenade);
}

TEST_CASE("songs: every song has its effects, every rule and status has a name") {
  std::set<Song> known;
  for (const auto& e : coop::host::song_effects()) {
    CAPTURE(e.what);
    if (e.rule == SongRule::item) known.insert(e.song);
    CHECK(std::string(coop::host::to_string(e.rule)) != "?");
    CHECK(std::string(coop::host::to_string(e.status)) != "?");
    CHECK_FALSE(e.what.empty());
    // The lines are where the rule says they are: a list rule brings lines, a song's warp,
    // its item, its story and the local ones bring none.
    switch (e.rule) {
      case SongRule::once:
      case SongRule::shared:
      case SongRule::hold:
      case SongRule::key_once:
        CHECK_FALSE(e.lines.empty());
        break;
      case SongRule::item:
      case SongRule::story:
      case SongRule::local:
      case SongRule::time:
      case SongRule::warp:
      case SongRule::built_in:
        CHECK(e.lines.empty());
        break;
      case SongRule::key_toggle:  // two-way keys: toggle64 lines, never a one-time key64 line
        CHECK(e.lines.find("toggle64:") != std::string_view::npos);
        CHECK(parsed(e.lines).pickup_keys().empty());
        CHECK_FALSE(parsed(e.lines).toggles().empty());
        break;
    }
    if (!e.lines.empty()) (void)parsed(e.lines);
  }
  CHECK(known.size() == coop::host::kSongCount);
  for (const auto& s : coop::host::songs()) {
    if (!s.warp) continue;
    const auto effects = coop::host::song_effects();
    CHECK(std::any_of(effects.begin(), effects.end(),
                      [&](const auto& e) { return e.song == s.song && e.rule == SongRule::warp; }));
  }
}

TEST_CASE("songs: the kit file says what the effects say") {
  const DungeonList made = parsed(coop::host::song_flag_list_text());
  const DungeonList kit = kit_file("shared_dungeon_songs.txt");
  CHECK(kit.share() == made.share());
  CHECK(kit.once() == made.once());
  CHECK(kit.unshare().empty());
  CHECK(made.unshare().empty());
  CHECK(kit.pickup_keys() == made.pickup_keys());
  CHECK(kit.toggles() == made.toggles());
  REQUIRE(kit.holds().size() == made.holds().size());
  for (const auto& h : made.holds()) {
    CAPTURE(h.name);
    CHECK(kit.held_in(h.hash) == std::optional<std::string_view>(h.place));
  }
  CHECK(kit.pickup_keys().size() == 4);  // the four Song of Time signs that go for good
  CHECK(kit.toggles().size() == 8);      // the eight signs whose blocks come and go
  CHECK(kit.share().size() == 16 + 4 + 8);  // Pierre's sixteen spots, those four keys and the eight
  CHECK(kit.once().size() == 10);
  CHECK(kit.holds().size() == 7);
  CHECK(kit.share_enums().empty());
}

TEST_CASE("songs: the kit file adds only flags no other list shares") {
  const DungeonList songs = kit_file("shared_dungeon_songs.txt");
  // With the other two lists, in the kit's order: no flag under two kinds.
  std::string error;
  const auto all = DungeonList::load_many(std::string(kKit) + "shared_dungeon_child.txt," + kKit +
                                              "shared_dungeon_adult.txt," + kKit + "shared_dungeon_songs.txt",
                                          &error);
  INFO(error);
  CHECK(all);

  std::set<std::uint32_t> built_in;
  for (const auto& f : CoopDungeon::Flags()) built_in.insert(f.hash);
  for (const auto& f : CoopDungeon::LiveFlags()) built_in.insert(f.hash);
  for (const auto& f : coop::host::item_flags()) built_in.insert(f.hash);
  for (const auto& k : coop::host::StoryList::defaults().shared_flags()) built_in.insert(k.hash);
  for (const char* other : {"shared_dungeon_child.txt", "shared_dungeon_adult.txt"}) {
    const DungeonList list = kit_file(other);
    for (const auto& k : list.share()) built_in.insert(k.hash);
    for (const auto& k : list.once()) built_in.insert(k.hash);
    for (const auto& h : list.holds()) built_in.insert(h.hash);
  }
  std::set<std::uint64_t> keys;
  for (const auto& k : CoopDungeon::Keys()) keys.insert(k.key);
  for (const auto& k : CoopChests::Keys()) keys.insert(k.key);

  for (const auto& k : songs.share()) {
    if (k.kind == coop::host::FlagKeyKind::key64) {
      CHECK_FALSE(keys.contains(k.key));
    } else {
      CHECK_FALSE(built_in.contains(k.hash));
    }
  }
  for (const auto& k : songs.once()) CHECK_FALSE(built_in.contains(k.hash));
  for (const auto& h : songs.holds()) {
    CAPTURE(h.name);
    CHECK_FALSE(built_in.contains(h.hash));
    CHECK(h.name.starts_with("HavePlayedEvent."));
  }
  // Two spellings are the game's own and easy to "fix" by mistake.
  CHECK(std::binary_search(songs.once().begin(), songs.once().end(),
                           FlagKey(flag_hash("InsideGanonCastele_Dungeon_Room13_Ocarina"), std::uint16_t{0})));
  CHECK(std::binary_search(songs.once().begin(), songs.once().end(),
                           FlagKey(flag_hash("DessertClossus_IsStormSongPlayed"), std::uint16_t{0})));
}

// ---- The warp-song invitation ----

namespace {

constexpr std::uint64_t kS = 1'000'000;
constexpr coop::protocol::Vec3 kKakariko{-40.0f, 108.0f, 90.0f};
constexpr coop::protocol::Vec3 kMinuetPad{-6.95f, 114.89f, -95.99f};

InviteMate mate(std::uint8_t n, std::string scene, coop::protocol::Vec3 at, std::string name = "Saria") {
  InviteMate m;
  m.number = n;
  m.name = std::move(name);
  m.scene = std::move(scene);
  m.position = at;
  return m;
}

InviteInput here(std::uint64_t t, std::vector<InviteMate> mates, bool held = false,
                 std::string scene = "KakarikoVillage_Child") {
  InviteInput in;
  in.now_us = t;
  in.scene = std::move(scene);
  in.standing = true;
  in.held = held;
  in.mates = std::move(mates);
  return in;
}

// Saria stands beside us in Kakariko, then lands on the Minuet's pad at t = 10 s.
WarpInvite opened(InviteOutput* first = nullptr) {
  WarpInvite w;
  CHECK(w.tick(here(1 * kS, {mate(2, "KakarikoVillage_Child", kKakariko)})).notices.empty());
  // Her scene changes; the shown pose still lags in Kakariko.
  auto o = w.tick(here(10 * kS, {mate(2, "SacredForestMeadow_Child", kKakariko)}));
  CHECK(o.notices.empty());
  CHECK(w.open() == 0);
  o = w.tick(here(10 * kS + 300'000, {mate(2, "SacredForestMeadow_Child", kMinuetPad)}));
  CHECK(w.open() == 0);  // not settled yet
  o = w.tick(here(10 * kS + 700'000, {mate(2, "SacredForestMeadow_Child", kMinuetPad)}));
  CHECK(w.open() == 2);
  if (first) *first = o;
  return w;
}

std::vector<InviteMate> landed() { return {mate(2, "SacredForestMeadow_Child", kMinuetPad)}; }

}  // namespace

TEST_CASE("songs: a mate who warps from our scene leaves an invitation") {
  InviteOutput o;
  WarpInvite w = opened(&o);
  REQUIRE(o.notices.size() == 1);
  CHECK(o.notices[0] == "Saria se ha ido con el Minueto del Bosque al Prado Sagrado. Manten L para ir tambien (10 s)");
  CHECK(ascii(o.notices[0]));
  REQUIRE(o.log.size() == 1);
  CHECK(o.log[0].find("P2 warped from KakarikoVillage_Child to SacredForestMeadow_Child (ForestMinuet) with the "
                      "ForestMinuet") == 0);
  CHECK_FALSE(o.follow);
}

TEST_CASE("songs: holding the button 0.75 s follows, and a release starts the hold over") {
  WarpInvite w = opened();
  const std::uint64_t t = 11 * kS;
  CHECK_FALSE(w.tick(here(t, landed(), true)).follow);
  CHECK_FALSE(w.tick(here(t + 500'000, landed(), true)).follow);
  CHECK_FALSE(w.tick(here(t + 600'000, landed(), false)).follow);  // let go
  CHECK_FALSE(w.tick(here(t + 700'000, landed(), true)).follow);
  CHECK_FALSE(w.tick(here(t + 1'300'000, landed(), true)).follow);  // 0.6 s since the new hold
  const auto o = w.tick(here(t + 1'500'000, landed(), true));
  REQUIRE(o.follow);
  CHECK(*o.follow == 2);
  CHECK(o.follow_name == "Saria");
  REQUIRE(o.notices.size() == 1);
  CHECK(o.notices[0].find("Vas con Saria al Prado Sagrado") == 0);
  CHECK(ascii(o.notices[0]));
  CHECK(w.open() == 0);
  CHECK_FALSE(w.tick(here(t + 2 * kS, landed(), true)).follow);  // once
}

TEST_CASE("songs: the hold counts only with our Link up and no event of ours") {
  WarpInvite w = opened();
  auto busy = [](std::uint64_t t) {
    InviteInput in = here(t, landed(), true);
    in.standing = false;
    return in;
  };
  CHECK_FALSE(w.tick(busy(11 * kS)).follow);
  CHECK_FALSE(w.tick(busy(12 * kS)).follow);
  CHECK(w.open() == 2);
  CHECK_FALSE(w.tick(here(12 * kS + 100'000, landed(), true)).follow);
  CHECK(w.tick(here(12 * kS + 900'000, landed(), true)).follow);
}

TEST_CASE("songs: the invitation closes after 10 s, when we leave, or when the mate goes") {
  SUBCASE("10 s") {
    WarpInvite w = opened();
    CHECK(w.tick(here(20 * kS, landed())).log.empty());
    const auto o = w.tick(here(20 * kS + 800'000, landed()));
    CHECK(w.open() == 0);
    REQUIRE(o.log.size() == 1);
    CHECK(o.log[0].find("over (10 s)") != std::string::npos);
    CHECK_FALSE(w.tick(here(21 * kS, landed(), true)).follow);
    CHECK_FALSE(w.tick(here(22 * kS, landed(), true)).follow);
  }
  SUBCASE("we leave our scene") {
    WarpInvite w = opened();
    w.tick(here(11 * kS, landed(), false, "HyruleField_Child"));
    CHECK(w.open() == 0);
  }
  SUBCASE("we are loading") {
    WarpInvite w = opened();
    InviteInput in = here(11 * kS, landed());
    in.scene.reset();
    w.tick(in);
    CHECK(w.open() == 0);
  }
  SUBCASE("the mate leaves the room") {
    WarpInvite w = opened();
    InviteInput in = here(11 * kS, {});
    in.room = std::vector<std::uint8_t>{};  // the room says he is gone
    const auto o = w.tick(in);
    CHECK(w.open() == 0);
    REQUIRE(o.log.size() == 1);
    CHECK(o.log[0].find("closed: the player left") != std::string::npos);
  }
  SUBCASE("the mate is not shown: open until he has been missing kAbsentUs") {
    WarpInvite w = opened();                        // last listed at 10.7 s
    CHECK(w.tick(here(11 * kS, {})).log.empty());  // missing, still in the room: open
    CHECK(w.open() == 2);
    CHECK(w.tick(here(20 * kS, {})).log.empty());
    CHECK(w.open() == 2);
    const auto o = w.tick(here(10 * kS + 800'000 + WarpInvite::kAbsentUs, {}));
    CHECK(w.open() == 0);
    REQUIRE(o.log.size() == 1);
    CHECK(o.log[0].find("closed: not seen for 10 s") != std::string::npos);
  }
}

TEST_CASE("songs: a mate missing from the list for a moment still leaves an invitation (REVISION_NUEVAS)") {
  // Saria stands beside us; while her warp loads she is shown nowhere (Presence gives no
  // pose), then she shows on the Minuet's pad: the landing is judged when she shows again.
  for (const bool room_known : {false, true}) {
    CAPTURE(room_known);
    auto tick = [&](WarpInvite& w, std::uint64_t t, std::vector<InviteMate> mates) {
      InviteInput in = here(t, std::move(mates));
      if (room_known) in.room = std::vector<std::uint8_t>{2};
      return w.tick(in);
    };
    WarpInvite w;
    CHECK(tick(w, 1 * kS, {mate(2, "KakarikoVillage_Child", kKakariko)}).log.empty());
    for (std::uint64_t t = 2 * kS; t <= 8 * kS; t += 100'000) {
      CHECK(tick(w, t, {}).log.empty());  // 7 s missing, within kAbsentUs
    }
    CHECK(tick(w, 8 * kS + 100'000, landed()).notices.empty());  // her scene changed: settle first
    const auto o = tick(w, 8 * kS + 700'000, landed());
    CHECK(w.open() == 2);
    REQUIRE(o.notices.size() == 1);
    CHECK(o.notices[0].find("Saria se ha ido con el Minueto del Bosque") == 0);
  }
  // Missing for longer than kAbsentUs: forgotten, so her return is a first sight (no invitation).
  WarpInvite w;
  w.tick(here(1 * kS, {mate(2, "KakarikoVillage_Child", kKakariko)}));
  w.tick(here(1 * kS + WarpInvite::kAbsentUs + 100'000, {}));
  for (std::uint64_t t = 12 * kS; t <= 14 * kS; t += 100'000) CHECK(w.tick(here(t, landed())).notices.empty());
  CHECK(w.open() == 0);
  // In the room's list no more: forgotten at once, whatever the time.
  WarpInvite x;
  x.tick(here(1 * kS, {mate(2, "KakarikoVillage_Child", kKakariko)}));
  InviteInput gone = here(1 * kS + 100'000, {});
  gone.room = std::vector<std::uint8_t>{3};
  x.tick(gone);
  for (std::uint64_t t = 2 * kS; t <= 3 * kS; t += 100'000) CHECK(x.tick(here(t, landed())).notices.empty());
  CHECK(x.open() == 0);
}

TEST_CASE("songs: a walk to the field is no warp") {
  WarpInvite w;
  w.tick(here(1 * kS, {mate(2, "KakarikoVillage_Child", kKakariko)}));
  // Saria walks out of Kakariko into Hyrule Field, far from the Serenade's pad.
  const coop::protocol::Vec3 gate{-60.0f, 120.0f, 150.0f};
  InviteOutput o;
  for (std::uint64_t t = 10 * kS; t <= 17 * kS; t += 100'000) {
    o = w.tick(here(t, {mate(2, "HyruleField_Child", gate)}));
    CHECK(o.notices.empty());
    if (!o.log.empty()) break;
  }
  CHECK(w.open() == 0);
  REQUIRE(o.log.size() == 1);
  CHECK(o.log[0].find("not seen on a warp pad") != std::string::npos);
}

TEST_CASE("songs: only a mate who leaves our own scene, to another one, is followed") {
  SUBCASE("he was elsewhere") {
    WarpInvite w;
    w.tick(here(1 * kS, {mate(2, "KokiriForest_Child", kKakariko)}));
    w.tick(here(10 * kS, {mate(2, "SacredForestMeadow_Child", kMinuetPad)}));
    const auto o = w.tick(here(11 * kS, {mate(2, "SacredForestMeadow_Child", kMinuetPad)}));
    CHECK(w.open() == 0);
    CHECK(o.notices.empty());
  }
  SUBCASE("first seen already on a pad") {
    WarpInvite w;
    w.tick(here(1 * kS, {mate(2, "SacredForestMeadow_Child", kMinuetPad)}));
    w.tick(here(2 * kS, {mate(2, "SacredForestMeadow_Child", kMinuetPad)}));
    CHECK(w.open() == 0);
  }
  SUBCASE("a scene with no warp song") {
    WarpInvite w;
    w.tick(here(1 * kS, {mate(2, "KakarikoVillage_Child", kKakariko)}));
    w.tick(here(10 * kS, {mate(2, "KokiriForest_Child", kMinuetPad)}));
    const auto o = w.tick(here(10 * kS + 600'000, {mate(2, "KokiriForest_Child", kMinuetPad)}));
    CHECK(o.log.empty());
    CHECK(w.open() == 0);
  }
  SUBCASE("we had already gone there") {
    WarpInvite w;
    w.tick(here(1 * kS, {mate(2, "KakarikoVillage_Child", kKakariko)}));
    w.tick(here(10 * kS, {mate(2, "SacredForestMeadow_Child", kKakariko)}, false, "SacredForestMeadow_Child"));
    w.tick(here(11 * kS, landed(), false, "SacredForestMeadow_Child"));
    CHECK(w.open() == 0);
  }
}

TEST_CASE("songs: the adult pads, a nameless mate, another button, a newer warp") {
  WarpInvite w("ZL");
  const coop::protocol::Vec3 nocturne{64.65f, 108.97f, 2.48f};
  w.tick(here(1 * kS, {mate(3, "KakarikoVillage_Adult", kKakariko, ""), mate(2, "KakarikoVillage_Adult", kKakariko)},
              false, "KakarikoVillage_Adult"));
  w.tick(here(5 * kS, {mate(3, "Graveyard_Adult", kKakariko, ""), mate(2, "KakarikoVillage_Adult", kKakariko)}, false,
              "KakarikoVillage_Adult"));
  auto o = w.tick(here(6 * kS, {mate(3, "Graveyard_Adult", {66.0f, 109.0f, 4.0f}, ""),
                                mate(2, "KakarikoVillage_Adult", kKakariko)},
                       false, "KakarikoVillage_Adult"));
  CHECK(w.open() == 3);
  REQUIRE(o.notices.size() == 1);
  CHECK(o.notices[0] ==
        "P3 se ha ido con el Nocturno de la Sombra al Cementerio. Manten ZL para ir tambien (10 s)");
  // Saria plays the Bolero next: the newer invitation replaces P3's.
  w.tick(here(7 * kS, {mate(3, "Graveyard_Adult", nocturne, ""), mate(2, "DeathMountainCrater_Adult", kKakariko)},
              false, "KakarikoVillage_Adult"));
  o = w.tick(here(8 * kS, {mate(3, "Graveyard_Adult", nocturne, ""),
                           mate(2, "DeathMountainCrater_Adult", {0.4f, 111.8f, -8.6f})},
                  false, "KakarikoVillage_Adult"));
  CHECK(w.open() == 2);
  REQUIRE(o.notices.size() == 1);
  CHECK(o.notices[0].find("Bolero del Fuego") != std::string::npos);
  CHECK(coop::host::mate_label(4, "") == "P4");
  CHECK(coop::host::mate_label(4, "Mido") == "Mido");
}

TEST_CASE("songs: with his animations known, the Ocarina must have been out before he left") {
  auto with_anim = [](std::string scene, coop::protocol::Vec3 at, std::string anim) {
    InviteMate m = mate(2, std::move(scene), at);
    m.anim = std::move(anim);
    return m;
  };
  SUBCASE("played: invited") {
    WarpInvite w;
    w.tick(here(1 * kS, {with_anim("KakarikoVillage_Child", kKakariko, "Wait")}));
    w.tick(here(5 * kS, {with_anim("KakarikoVillage_Child", kKakariko, "Ocarina_Child_A_lp")}));
    w.tick(here(10 * kS, {with_anim("SacredForestMeadow_Child", kKakariko, "DemoOcarinaWarp")}));
    w.tick(here(11 * kS, {with_anim("SacredForestMeadow_Child", kMinuetPad, "Wait")}));
    CHECK(w.open() == 2);
  }
  SUBCASE("never played (the Water Temple's exit lands near the Serenade's pad)") {
    WarpInvite w;
    w.tick(here(1 * kS, {with_anim("KakarikoVillage_Child", kKakariko, "Run")}));
    const auto o = w.tick(here(10 * kS, {with_anim("SacredForestMeadow_Child", kKakariko, "Run")}));
    REQUIRE(o.log.size() == 1);
    CHECK(o.log[0].find("with no Ocarina out before") != std::string::npos);
    w.tick(here(11 * kS, {with_anim("SacredForestMeadow_Child", kMinuetPad, "Wait")}));
    CHECK(w.open() == 0);
  }
  SUBCASE("played too long before") {
    WarpInvite w;
    w.tick(here(1 * kS, {with_anim("KakarikoVillage_Child", kKakariko, "OcarinaReadyStart")}));
    w.tick(here(2 * kS, {with_anim("KakarikoVillage_Child", kKakariko, "Wait")}));
    w.tick(here(40 * kS, {with_anim("SacredForestMeadow_Child", kKakariko, "Wait")}));
    w.tick(here(41 * kS, {with_anim("SacredForestMeadow_Child", kMinuetPad, "Wait")}));
    CHECK(w.open() == 0);
  }
}

TEST_CASE("songs: a landing beside the pad, not on it, is no warp") {
  WarpInvite w;
  w.tick(here(1 * kS, {mate(2, "KakarikoVillage_Child", kKakariko)}, false, "WaterTemple"));
  // From the Water Temple to the lake: its exit is 8.6 from the Serenade's pad.
  w.tick(here(2 * kS, {mate(2, "WaterTemple", kKakariko)}, false, "WaterTemple"));
  w.tick(here(10 * kS, {mate(2, "HyruleField_Adult", {-193.8f, 103.9f, 508.6f})}, false, "WaterTemple"));
  w.tick(here(11 * kS, {mate(2, "HyruleField_Adult", {-193.8f, 103.9f, 508.6f})}, false, "WaterTemple"));
  CHECK(w.open() == 0);
}

TEST_CASE("songs: a button held before the invitation opened never follows by itself") {
  WarpInvite w;
  w.tick(here(1 * kS, {mate(2, "KakarikoVillage_Child", kKakariko)}, true));  // a shield held up
  w.tick(here(10 * kS, {mate(2, "SacredForestMeadow_Child", kKakariko)}, true));
  w.tick(here(10 * kS + 700'000, {mate(2, "SacredForestMeadow_Child", kMinuetPad)}, true));
  REQUIRE(w.open() == 2);
  // Held all along: 2 s and nothing.
  for (std::uint64_t t = 11 * kS; t <= 13 * kS; t += 100'000) CHECK_FALSE(w.tick(here(t, landed(), true)).follow);
  // Let go, then a fresh hold of 0.75 s follows.
  CHECK_FALSE(w.tick(here(13 * kS + 100'000, landed(), false)).follow);
  CHECK_FALSE(w.tick(here(13 * kS + 200'000, landed(), true)).follow);
  CHECK(w.tick(here(14 * kS, landed(), true)).follow);
}

namespace {

using coop::host::InviteButton;
constexpr std::uint64_t kL = 1u << 6;
constexpr std::uint64_t kR = 1u << 7;

// Presses L `n` times from a steady pose; on a press the game's command becomes `on_press`
// (the same command: the button moves nothing).
std::vector<std::string> presses(InviteButton& b, std::uint64_t& t, int n, const std::string& on_press,
                                 bool open = false) {
  std::vector<std::string> log;
  auto frame = [&](std::uint64_t buttons, const std::string& anim) {
    InviteButton::Input in;
    in.now_us = t;
    in.buttons = buttons;
    in.animation = anim;
    in.standing = true;
    in.invitation_open = open;
    for (auto& l : b.frame(in)) log.push_back(std::move(l));
    t += 100'000;
  };
  for (int i = 0; i < n; ++i) {
    for (int k = 0; k < 5; ++k) frame(0, "Wait");  // 0.5 s steady
    frame(kL, "Wait");
    for (int k = 0; k < 4; ++k) frame(kL, on_press);  // held 0.4 s
    for (int k = 0; k < 3; ++k) frame(0, "Wait");
  }
  return log;
}

}  // namespace

TEST_CASE("songs: the invitation button is free when presses move nothing of our Link") {
  InviteButton b("L", kL, "R", kR);
  std::uint64_t t = 1 * kS;
  CHECK(presses(b, t, 2, "Wait").empty());
  CHECK_FALSE(b.decided());
  const auto log = presses(b, t, 1, "Wait");
  REQUIRE(log.size() == 1);
  CHECK(log[0] == "invitation button L: free (3 of 3 presses moved nothing of our Link): L alone");
  CHECK(b.decided());
  CHECK_FALSE(b.taken());
  CHECK(b.label() == "L");
  InviteButton::Input in;
  in.now_us = t;
  in.buttons = kL;
  in.animation = "Wait";
  in.standing = true;
  b.frame(in);
  CHECK(b.held());
}

TEST_CASE("songs: an invitation button the game uses takes its twin too (L+R)") {
  InviteButton b("L", kL, "R", kR);
  std::uint64_t t = 1 * kS;
  const auto log = presses(b, t, 3, "GuardStart");
  REQUIRE(log.size() == 1);
  CHECK(log[0] == "invitation button L: the game uses it (our Link went 'Wait' -> 'GuardStart' on 3 of 3 presses): "
                  "the invitation takes L+R held together");
  CHECK(b.taken());
  CHECK(b.label() == "L+R");
  InviteButton::Input in;
  in.now_us = t;
  in.animation = "GuardWait";
  in.standing = true;
  in.buttons = kL;
  b.frame(in);
  CHECK_FALSE(b.held());  // L alone is the game's now
  in.buttons = kL | kR;
  b.frame(in);
  CHECK(b.held());
  // The notice says the gesture.
  WarpInvite w = opened();
  w.set_button(b.label());
  CHECK(w.button() == "L+R");
}

TEST_CASE("songs: presses while an invitation is open, in an event or from a moving pose say nothing") {
  InviteButton b("L", kL, "R", kR);
  std::uint64_t t = 1 * kS;
  CHECK(presses(b, t, 5, "GuardStart", /*open=*/true).empty());  // an accepted follow is no test of L
  CHECK_FALSE(b.decided());
  // Running: the command changes all the time, so a press is not judged.
  for (int i = 0; i < 20; ++i) {
    InviteButton::Input in;
    in.now_us = t;
    in.buttons = (i % 4 == 0) ? kL : 0;
    in.animation = (i % 2 == 0) ? "Run" : "RunTurn";
    in.standing = true;
    CHECK(b.frame(in).empty());
    t += 100'000;
  }
  CHECK_FALSE(b.decided());
  // One of three presses that moved Link (a step started by chance) is still free.
  presses(b, t, 1, "Walk");
  const auto log = presses(b, t, 2, "Wait");
  REQUIRE(log.size() == 1);
  CHECK(log[0].find("free (2 of 3 presses") != std::string::npos);
}
