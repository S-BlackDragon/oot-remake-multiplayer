// Tests for the townsperson-talk rule (coop/host/npc_talk.hpp, ORM D-R12).
#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "coop/host/npc_talk.hpp"
#include "coop/host/player_reader.hpp"
#include "coop/host/sampler.hpp"
#include "coop/host/scene.hpp"
#include "coop/protocol/codec.hpp"
#include "fake_memory.hpp"

using coop::host::is_talker;
using coop::host::NpcSeen;
using coop::host::NpcTalk;
using coop::protocol::Vec3;

namespace {

constexpr std::uint64_t kMs = 1000;

NpcSeen Npc(std::uint64_t address, float x, std::string animation = "Wait",
            std::string name = "Npc_Kokiri_005_Child") {
  NpcSeen s;
  s.address = address;
  s.name = std::move(name);
  s.position = {x, 0.0f, 0.0f};
  s.animation = std::move(animation);
  return s;
}

const Vec3 kLink{0.0f, 0.0f, 0.0f};

}  // namespace

TEST_CASE("npc talk: who counts as a townsperson to talk to") {
  // Names from the game's Pack/Actor list and a Kokiri Forest log (2026-11-10).
  CHECK(is_talker("Npc_Kokiri_Mido_Child"));
  CHECK(is_talker("Npc_Kokiri_001_Shop_Child_ShortStock"));
  CHECK(is_talker("Npc_Kokiri_Saria_Child_OcarinaHolder"));
  CHECK(is_talker("Npc_DekuTreeChild"));
  CHECK(is_talker("Npc_Hylia_051"));
  CHECK_FALSE(is_talker("Npc_Navi"));
  CHECK_FALSE(is_talker("Npc_Equipment_002"));
  CHECK_FALSE(is_talker("Npc_Invisible_Kokiri_006"));
  CHECK_FALSE(is_talker("Npc_Fairy"));
  CHECK_FALSE(is_talker("Npc_Fairy_Saria"));
  CHECK_FALSE(is_talker("Npc_Kokiri_Saria_Voice"));
  CHECK_FALSE(is_talker("Dm_Npc_Navi"));
  CHECK_FALSE(is_talker("Enemy_Stalchild"));
  CHECK_FALSE(is_talker("Animal_Cucco"));
}

TEST_CASE("npc talk: no event no partner, and an event picks the nearest within reach") {
  NpcTalk talk;
  const std::vector<NpcSeen> walk{Npc(0x100, 3.0f), Npc(0x200, 2.0f), Npc(0x300, 9.0f)};
  talk.update(false, kLink, walk, 0);
  CHECK_FALSE(talk.partner());
  talk.update(true, kLink, walk, 100 * kMs);
  REQUIRE(talk.partner());
  CHECK(*talk.partner() == 0x200);
  CHECK(talk.claims(0x200));
  CHECK_FALSE(talk.claims(0x100));
  CHECK_FALSE(talk.lingering());
}

TEST_CASE("npc talk: an event that is no talk's takes nobody by reach (c3: a fall-down beside Saria)") {
  using coop::host::talk_event_name;
  CHECK(talk_event_name("Npc_Kokiri_Mido_Child_DC0_TT"));
  CHECK(talk_event_name("Npc_Kokiri_004_Child_DC0_TT"));
  CHECK_FALSE(talk_event_name("DmF_SY_FallDown"));
  CHECK_FALSE(talk_event_name("DefaultBootEvent"));
  CHECK_FALSE(talk_event_name(""));

  // charla-saria, round 7: a fall-down respawn put Link 2.9 m from Saria; the radius took her.
  const std::vector<NpcSeen> walk{Npc(0x100, 2.9f, "Act_SitChair_Wait", "Npc_Kokiri_Saria_Child_OcarinaHolder")};
  NpcTalk talk;
  talk.update(false, kLink, walk, 0);
  talk.set_event_name(std::string("DmF_SY_FallDown"));
  talk.update(true, kLink, walk, 100 * kMs);
  CHECK_FALSE(talk.partner());
  const auto refused = talk.take_refused();
  REQUIRE(refused);
  CHECK(*refused == "DmF_SY_FallDown");
  talk.update(true, kLink, walk, 200 * kMs);
  CHECK_FALSE(talk.take_refused());  // one line per event
  talk.set_event_name(std::nullopt);
  talk.update(false, kLink, walk, 300 * kMs);

  // The talk itself (its own name) and a name not readable take her by reach, as before.
  talk.set_event_name(std::string("Npc_Kokiri_Saria_Child_OcarinaHolder_DC0_TT"));
  talk.update(true, kLink, walk, 400 * kMs);
  REQUIRE(talk.partner());
  CHECK(*talk.partner() == 0x100);
  CHECK_FALSE(talk.take_refused());
  NpcTalk unnamed;
  unnamed.update(false, kLink, walk, 0);
  unnamed.update(true, kLink, walk, 100 * kMs);
  REQUIRE(unnamed.partner());

  // The game's own word still counts in an event of another name.
  NpcSeen saria = walk[0];
  saria.event_phase = 0;
  NpcTalk by_game;
  by_game.update(false, kLink, {saria}, 0);
  saria.event_phase = 3;
  by_game.set_event_name(std::string("DefaultBootEvent"));
  by_game.update(true, kLink, {saria}, 100 * kMs);
  REQUIRE(by_game.partner());
  CHECK(by_game.found() == NpcTalk::Found::game);
}

TEST_CASE("npc talk: an event with nobody in reach claims nothing (a chest, a door)") {
  NpcTalk talk;
  const std::vector<NpcSeen> walk{Npc(0x100, 6.0f), Npc(0x200, 0.5f, "Fly", "Npc_Fairy")};
  talk.update(false, kLink, walk, 0);
  talk.update(true, kLink, walk, 100 * kMs);
  CHECK_FALSE(talk.partner());
  // No Link on the walk: nothing either.
  NpcTalk blind;
  blind.update(true, std::nullopt, {Npc(0x100, 1.0f)}, 0);
  CHECK_FALSE(blind.partner());
}

TEST_CASE("npc talk: the one that turned to us wins over a nearer one, in the first second") {
  NpcTalk talk;
  talk.update(false, kLink, {Npc(0x100, 1.0f, "Act_Bored"), Npc(0x200, 3.0f, "Act_Sway")}, 0);
  // The event starts; nobody has changed yet: the nearest for now.
  talk.update(true, kLink, {Npc(0x100, 1.0f, "Act_Bored"), Npc(0x200, 3.0f, "Act_Sway")},
              100 * kMs);
  REQUIRE(talk.partner());
  CHECK(*talk.partner() == 0x100);
  // The farther one starts its talk: it is the partner.
  talk.update(true, kLink, {Npc(0x100, 1.0f, "Act_Bored"), Npc(0x200, 3.0f, "Talk_Start")},
              400 * kMs);
  CHECK(*talk.partner() == 0x200);
  // Once a changed one is chosen it stays, even if the other changes too.
  talk.update(true, kLink, {Npc(0x100, 1.0f, "Look"), Npc(0x200, 3.0f, "Talk")}, 600 * kMs);
  CHECK(*talk.partner() == 0x200);
}

TEST_CASE("npc talk: after the first second the partner stays while the event runs") {
  NpcTalk talk;
  talk.update(false, kLink, {Npc(0x100, 1.0f), Npc(0x200, 3.0f)}, 0);
  talk.update(true, kLink, {Npc(0x100, 1.0f), Npc(0x200, 3.0f)}, 100 * kMs);
  CHECK(*talk.partner() == 0x100);
  // It walks off during the talk (Mido steps aside): still the partner.
  talk.update(true, kLink, {Npc(0x100, 12.0f, "Walk"), Npc(0x200, 3.0f, "Talk")}, 2000 * kMs);
  REQUIRE(talk.partner());
  CHECK(*talk.partner() == 0x100);
}

TEST_CASE("npc talk: the claim lingers 1.5 s after the event, then goes") {
  NpcTalk talk;
  const std::vector<NpcSeen> walk{Npc(0x100, 1.0f)};
  talk.update(false, kLink, walk, 0);
  talk.update(true, kLink, walk, 100 * kMs);
  talk.update(true, kLink, walk, 3000 * kMs);
  talk.update(false, kLink, walk, 3100 * kMs);
  CHECK(talk.claims(0x100));
  CHECK(talk.lingering());
  talk.update(false, kLink, walk, 4500 * kMs);
  CHECK(talk.claims(0x100));
  talk.update(false, kLink, walk, 4600 * kMs);
  CHECK_FALSE(talk.partner());
  CHECK_FALSE(talk.lingering());
}

TEST_CASE("npc talk: a partner that leaves the walk is dropped and reset forgets all") {
  NpcTalk talk;
  talk.update(false, kLink, {Npc(0x100, 1.0f)}, 0);
  talk.update(true, kLink, {Npc(0x100, 1.0f)}, 100 * kMs);
  REQUIRE(talk.partner());
  talk.update(true, kLink, {}, 2000 * kMs);  // despawned by its event
  CHECK_FALSE(talk.partner());
  talk.update(true, kLink, {Npc(0x100, 1.0f)}, 2100 * kMs);
  CHECK_FALSE(talk.partner());  // after the first second of the event: nothing new
  talk.reset();
  talk.update(true, kLink, {Npc(0x100, 1.0f)}, 2200 * kMs);
  CHECK(talk.partner());
}

TEST_CASE("npc talk: a new event while the last claim lingers starts afresh") {
  NpcTalk talk;
  talk.update(false, kLink, {Npc(0x100, 1.0f), Npc(0x200, 4.0f)}, 0);
  talk.update(true, kLink, {Npc(0x100, 1.0f), Npc(0x200, 4.0f)}, 100 * kMs);
  talk.update(false, kLink, {Npc(0x100, 1.0f), Npc(0x200, 4.0f)}, 2000 * kMs);
  REQUIRE(talk.lingering());
  // We walked to the other one and talk to it.
  const Vec3 there{4.0f, 0.0f, 0.0f};
  talk.update(true, there, {Npc(0x100, 1.0f), Npc(0x200, 4.0f)}, 2500 * kMs);
  REQUIRE(talk.partner());
  CHECK(*talk.partner() == 0x200);
  CHECK_FALSE(talk.lingering());
}

TEST_CASE("npc talk: the event comes from guest memory through the player reader") {
  // The hook's whole chain without the emulator: the event bit the reader
  // takes from the event manager (fake memory), the Ocarina rule of the
  // scene filter, then the talk rule.
  using namespace coop::host;
  using namespace coop::host::test;
  FakeMemory m;
  const Layout l = make_game(m);  // our Link at (1, 2, 3), no event
  const auto event_now = [&] {
    PlayerReader reader(m);
    const auto r = reader.read();
    REQUIRE(r);
    const auto anim = r->animation ? std::optional<std::string_view>(r->animation->name)
                                   : std::optional<std::string_view>{};
    return std::pair{local_event_hides(r->event_running.value_or(false), anim), r->position};
  };
  NpcSeen mido = Npc(0x500, 0.0f, "Act_Arrogant", "Npc_Kokiri_Mido_Child");
  mido.position = {2.0f, 2.0f, 3.0f};
  NpcTalk talk;
  auto [event, link] = event_now();
  CHECK_FALSE(event);
  talk.update(event, link, {mido}, 0);
  CHECK_FALSE(talk.partner());
  m.put_u64(l.events + addr::kEventRunning, 0x10000);  // a talk starts
  std::tie(event, link) = event_now();
  CHECK(event);
  talk.update(event, link, {mido}, 100 * kMs);
  REQUIRE(talk.partner());
  CHECK(*talk.partner() == 0x500);
}

// ORM "Dos jugadores con el mismo PNJ" (coop::host::NpcClaim): a townsperson
// its owner is talking to is not claimed from it.
namespace {

using coop::host::NpcClaim;
using coop::host::NpcOwner;
using coop::host::owner_talks;

constexpr std::uint64_t kS = 1'000'000;
const Vec3 kMidoAt{2.0f, 0.0f, 0.0f};

NpcOwner Owner(std::uint8_t player, bool in_event, std::optional<Vec3> link = Vec3{3.0f, 0, 0},
               bool posing = false) {
  NpcOwner o;
  o.player = player;
  o.in_event = in_event;
  o.link = link;
  o.posing = posing;
  return o;
}

}  // namespace

TEST_CASE("npc claim: the owner talks to it when in an event beside it, or sending its pose") {
  CHECK(owner_talks(Owner(2, true), kMidoAt));                           // 1 m away
  CHECK_FALSE(owner_talks(Owner(2, false), kMidoAt));                    // no event
  CHECK_FALSE(owner_talks(Owner(2, true, Vec3{20.0f, 0, 0}), kMidoAt));  // a chest 18 m away
  CHECK_FALSE(owner_talks(Owner(2, true, std::nullopt), kMidoAt));       // place unknown
  CHECK(owner_talks(Owner(2, false, std::nullopt, true), kMidoAt));      // its OBJECT_POSE
  CHECK_FALSE(owner_talks(Owner(0, true), kMidoAt));                     // no owner
  CHECK(owner_talks(Owner(2, true, Vec3{1.9f + NpcClaim::kOwnerReachM, 0, 0}), kMidoAt));
  CHECK_FALSE(owner_talks(Owner(2, true, Vec3{2.1f + NpcClaim::kOwnerReachM, 0, 0}), kMidoAt));
}

TEST_CASE("npc claim: ours or nobody's is claimed, another's that nobody talks to is claimed") {
  NpcClaim c;
  CHECK(c.update(std::nullopt, "", kMidoAt, 1, Owner(2, true), 0) == NpcClaim::State::none);
  CHECK(c.update(0x500, "Npc_Kokiri_Mido_Child", kMidoAt, 1, Owner(1, false), kS) ==
        NpcClaim::State::claimed);
  c.reset();
  CHECK(c.update(0x500, "Npc_Kokiri_Mido_Child", kMidoAt, 1, Owner(0, false), kS) ==
        NpcClaim::State::claimed);
  c.reset();
  CHECK(c.update(0x500, "Npc_Kokiri_Mido_Child", kMidoAt, 1, Owner(2, false), kS) ==
        NpcClaim::State::claimed);
  CHECK(c.claims());
  // We own it and talk: the owner's event beside it is ours, nothing to yield to.
  c.reset();
  CHECK(c.update(0x500, "Npc_Kokiri_Mido_Child", kMidoAt, 1, Owner(1, true), kS) ==
        NpcClaim::State::claimed);
}

TEST_CASE("npc claim: the second talker yields for its whole talk, and claims a new one afresh") {
  NpcClaim c;
  // P2 owns Mido and talks to him; we start ours 2 s later.
  c.see_status(2, true, 0);
  CHECK(c.update(0x500, "Npc_Kokiri_Mido_Child", kMidoAt, 1, Owner(2, true), 2 * kS) ==
        NpcClaim::State::yielded);
  CHECK_FALSE(c.claims());
  // P2's talk is over while ours goes on: still not claimed (no hand-over in
  // the middle of ours, and none back after it).
  c.see_status(2, false, 10 * kS);
  CHECK(c.update(0x500, "Npc_Kokiri_Mido_Child", kMidoAt, 1, Owner(2, false), 10 * kS) ==
        NpcClaim::State::yielded);
  // A hand-over already on its way when we yielded made it ours: ours to keep.
  CHECK(c.update(0x500, "Npc_Kokiri_Mido_Child", kMidoAt, 1, Owner(1, false), 11 * kS) ==
        NpcClaim::State::claimed);
  // Our talk is over; the next one, nobody else talking, claims.
  CHECK(c.update(std::nullopt, "", kMidoAt, 1, Owner(2, false), 12 * kS) ==
        NpcClaim::State::none);
  CHECK(c.update(0x500, "Npc_Kokiri_Mido_Child", kMidoAt, 1, Owner(2, false), 20 * kS) ==
        NpcClaim::State::claimed);
}

TEST_CASE("npc claim: we claimed first, then its owner talks to it too: we stop claiming") {
  // P2 owns it (the hand-over to us not made yet) and starts its own talk:
  // P2 keeps it, and we no longer ask for it.
  NpcClaim c;
  CHECK(c.update(0x500, "Npc_Kokiri_Mido_Child", kMidoAt, 1, Owner(2, false), kS) ==
        NpcClaim::State::claimed);
  c.see_status(2, true, kS + 300'000);
  CHECK(c.update(0x500, "Npc_Kokiri_Mido_Child", kMidoAt, 1, Owner(2, true), kS + 300'000) ==
        NpcClaim::State::yielded);
}

TEST_CASE("npc claim: a stuck claim is let go, later for a shopkeeper") {
  NpcClaim c;
  CHECK(c.update(0x500, "Npc_Kokiri_005_Child", kMidoAt, 1, Owner(1, false), kS) ==
        NpcClaim::State::claimed);
  CHECK(c.update(0x500, "Npc_Kokiri_005_Child", kMidoAt, 1, Owner(1, false),
                 kS + NpcClaim::kClaimMaxUs - 1) == NpcClaim::State::claimed);
  CHECK(c.update(0x500, "Npc_Kokiri_005_Child", kMidoAt, 1, Owner(1, false),
                 kS + NpcClaim::kClaimMaxUs) == NpcClaim::State::timed_out);
  CHECK_FALSE(c.claims());
  // It stays let go for the rest of that talk.
  CHECK(c.update(0x500, "Npc_Kokiri_005_Child", kMidoAt, 1, Owner(1, false), 200 * kS) ==
        NpcClaim::State::timed_out);
  // A new talk claims again.
  c.update(std::nullopt, "", kMidoAt, 1, Owner(1, false), 201 * kS);
  CHECK(c.update(0x500, "Npc_Kokiri_005_Child", kMidoAt, 1, Owner(1, false), 202 * kS) ==
        NpcClaim::State::claimed);

  // The shopkeeper: browsing the shelf takes minutes.
  NpcClaim shop;
  const std::string_view keeper = "Npc_Kokiri_001_Shop_Child_ShortStock";
  CHECK(NpcClaim::claim_max_us(keeper) == NpcClaim::kShopClaimMaxUs);
  CHECK(NpcClaim::claim_max_us("Npc_Kokiri_Mido_Child") == NpcClaim::kClaimMaxUs);
  CHECK(shop.update(0x600, keeper, kMidoAt, 1, Owner(1, false), kS) == NpcClaim::State::claimed);
  CHECK(shop.update(0x600, keeper, kMidoAt, 1, Owner(1, false), kS + NpcClaim::kClaimMaxUs) ==
        NpcClaim::State::claimed);
  CHECK(shop.update(0x600, keeper, kMidoAt, 1, Owner(1, false),
                    kS + NpcClaim::kShopClaimMaxUs) == NpcClaim::State::timed_out);
}

TEST_CASE("npc claim: an owner whose event has run past the limit is not yielded to") {
  NpcClaim c;
  c.see_status(2, true, kS);  // P2's event bit stuck on beside Mido
  CHECK(c.event_since(2) == kS);
  c.see_status(2, true, 30 * kS);
  CHECK(c.event_since(2) == kS);  // the start is kept
  CHECK(c.update(0x500, "Npc_Kokiri_Mido_Child", kMidoAt, 1, Owner(2, true),
                 kS + NpcClaim::kClaimMaxUs) == NpcClaim::State::claimed);
  CHECK(c.owner_stuck());
  // Its OBJECT_POSE of Mido still comes: it does talk to him (a long talk), we yield.
  NpcClaim d;
  d.see_status(2, true, kS);
  CHECK(d.update(0x500, "Npc_Kokiri_Mido_Child", kMidoAt, 1, Owner(2, true, Vec3{3, 0, 0}, true),
                 kS + NpcClaim::kClaimMaxUs) == NpcClaim::State::yielded);
  // Its event ended: the start is forgotten.
  c.see_status(2, false, 70 * kS);
  CHECK(c.event_since(2) == 0);
}

TEST_CASE("npc claim: both games from guest memory, the owner's status through the sampler") {
  // The chain without the emulator, for two games: the owner's event bit
  // (fake memory) goes out in its PLAYER_STATUS (Sampler, codec) and is
  // relayed with its number; ours comes from our own fake memory through the
  // player reader, the Ocarina rule and NpcTalk; NpcClaim then yields.
  using namespace coop::host;
  using namespace coop::host::test;
  namespace proto = coop::protocol;
  FakeMemory owner_mem, our_mem;
  const Layout ol = make_game(owner_mem);
  const Layout l = make_game(our_mem);  // our Link at (1, 2, 3)
  Sampler owner_sampler;
  std::uint8_t owner_flags = 0;
  const auto owner_frame = [&](std::uint64_t now) {
    PlayerReader reader(owner_mem);
    for (const Outgoing& o : owner_sampler.frame(reader, now)) {
      proto::Message m;
      REQUIRE(proto::decode(o.bytes, m) == proto::Error::none);
      if (auto* st = std::get_if<proto::PlayerStatus>(&m)) {
        st->player_number = 2;  // as the server relays it
        owner_flags = st->flags;
      }
    }
  };
  const auto our_event = [&] {
    PlayerReader reader(our_mem);
    const auto r = reader.read();
    REQUIRE(r);
    const auto anim = r->animation ? std::optional<std::string_view>(r->animation->name)
                                   : std::optional<std::string_view>{};
    return std::pair{local_event_hides(r->event_running.value_or(false), anim), r->position};
  };
  NpcSeen mido = Npc(0x500, 0.0f, "Act_Arrogant", "Npc_Kokiri_Mido_Child");
  mido.position = {2.0f, 2.0f, 3.0f};
  NpcTalk talk;
  NpcClaim claim;

  // The owner (P2, beside Mido) starts its talk.
  owner_frame(1 * kS);
  CHECK_FALSE(in_cutscene(owner_flags));
  owner_mem.put_u64(ol.events + addr::kEventRunning, 0x10000);
  owner_frame(1 * kS + 20'000);
  REQUIRE(in_cutscene(owner_flags));
  claim.see_status(2, in_cutscene(owner_flags), 1 * kS);

  // Two seconds later our player talks to Mido too.
  auto [event, link] = our_event();
  talk.update(event, link, {mido}, 2 * kS);
  our_mem.put_u64(l.events + addr::kEventRunning, 0x10000);
  std::tie(event, link) = our_event();
  REQUIRE(event);
  talk.update(event, link, {mido}, 3 * kS);
  REQUIRE(talk.partner() == std::optional<std::uint64_t>{0x500});
  claim.see_status(2, in_cutscene(owner_flags), 3 * kS);
  NpcOwner o;
  o.player = 2;
  o.in_event = in_cutscene(owner_flags);
  o.link = Vec3{2.5f, 2.0f, 3.0f};  // its PLAYER_SAMPLE: half a metre from Mido
  CHECK(claim.update(talk.partner(), mido.name, mido.position, 1, o, 3 * kS) ==
        NpcClaim::State::yielded);
  // Had the owner's talk been over before ours, we would claim as before.
  owner_mem.put_u64(ol.events + addr::kEventRunning, 0);
  owner_frame(3 * kS + 20'000);
  CHECK_FALSE(in_cutscene(owner_flags));
  NpcClaim fresh;
  o.in_event = in_cutscene(owner_flags);
  CHECK(fresh.update(talk.partner(), mido.name, mido.position, 1, o, 3 * kS) ==
        NpcClaim::State::claimed);
}

// ORM charla-radio: big or far townspeople, found from the game's own event
// component, from their invisible stand-in, or by each name's reach.
namespace {

using coop::host::read_event_phase;
using coop::host::stand_in_body;
using coop::host::talk_reach_m;

NpcSeen At(std::uint64_t address, std::string name, Vec3 position,
           std::optional<std::uint32_t> phase = std::nullopt, std::string animation = "Wait") {
  NpcSeen s;
  s.address = address;
  s.name = std::move(name);
  s.position = position;
  s.animation = std::move(animation);
  s.event_phase = phase;
  return s;
}

// The charla-* placements (CHARLAS.md): Link where the scenario puts it.
const Vec3 kBiggoronAt{25.75f, 200.46f, -128.5f};
const Vec3 kBiggoronLedge{16.0f, 206.5f, -133.0f};  // 12.3 m from him
const Vec3 kKaeporaAt{-73.19f, 103.88f, 7.65f};
const Vec3 kKaeporaLedge{-74.0f, 98.4f, 9.6f};  // 2.1 m across, 5.5 m below: 5.9 m

}  // namespace

TEST_CASE("npc talk: stand-ins and the reach of each name") {
  CHECK(stand_in_body("Npc_Invisible_Goron_Biggoron_Adult") ==
        std::optional<std::string>{"Npc_Goron_Biggoron_Adult"});
  CHECK(stand_in_body("Npc_Invisible_Kaepora") == std::optional<std::string>{"Npc_Kaepora"});
  CHECK(stand_in_body("Npc_Invisible_Zora_005") == std::optional<std::string>{"Npc_Zora_005"});
  CHECK_FALSE(stand_in_body("Npc_Invisible_Base"));
  CHECK_FALSE(stand_in_body("Npc_Invisible_"));
  CHECK_FALSE(stand_in_body("Npc_Goron_Biggoron_Adult"));
  CHECK_FALSE(stand_in_body("Npc_Navi"));

  CHECK(talk_reach_m("Npc_Goron_Biggoron_Adult") == NpcTalk::kMaxReachM);
  CHECK(talk_reach_m("Npc_JabuJabu") == NpcTalk::kMaxReachM);
  CHECK(talk_reach_m("Npc_Kaepora") == 15.0f);
  CHECK(talk_reach_m("Npc_Kaepora_LakeHylia") == 15.0f);
  CHECK(talk_reach_m("Npc_Zora_KingZora_child") == 15.0f);
  CHECK(talk_reach_m("Npc_Zora_KingZora_adult") == 15.0f);
  CHECK(talk_reach_m("Npc_GreatFairy_DefaultActor") == 15.0f);
  CHECK(talk_reach_m("Npc_Goron_Darunia_Child") == 8.0f);
  CHECK(talk_reach_m("Npc_Goron_DaruniaJr_Adult") == NpcTalk::kReachM);  // his son is Link's size
  CHECK(talk_reach_m("Npc_Kokiri_Mido_Child") == NpcTalk::kReachM);
  CHECK(talk_reach_m("") == NpcTalk::kReachM);
  for (std::string_view name : {"Npc_Goron_Biggoron_Adult", "Npc_JabuJabu", "Npc_Kaepora",
                                "Npc_Goron_Medigoron_Adult", "Npc_DekuTreeChild"}) {
    CHECK(talk_reach_m(name) <= NpcTalk::kMaxReachM);
    CHECK(talk_reach_m(name) >= NpcTalk::kReachM);
  }
}

TEST_CASE("npc talk: the event component's phase from guest memory") {
  using namespace coop::host;
  using namespace coop::host::test;
  FakeMemory m;
  constexpr std::uint64_t actor = 0x81000000, table = 0x81100000, component = 0x81200000;
  CHECK_FALSE(read_event_phase(m, actor));  // nothing there
  CHECK_FALSE(read_event_phase(m, 0));
  m.put_u64(actor + addr::kActorComponents, table);
  m.put_u64(table + addr::kComponentsEvent, 0);  // no event component
  CHECK_FALSE(read_event_phase(m, actor));
  m.put_u64(table + addr::kComponentsEvent, component);
  CHECK_FALSE(read_event_phase(m, actor));  // the phase not mapped
  m.put_u32(component + addr::kEventComponentPhase, 0);
  CHECK(read_event_phase(m, actor) == std::optional<std::uint32_t>{0});
  m.put_u32(component + addr::kEventComponentPhase, 1);
  CHECK(read_event_phase(m, actor) == std::optional<std::uint32_t>{1});
  CHECK(addr::kActorComponents == 0x270);
  CHECK(addr::kComponentsEvent == 0x90);
  CHECK(addr::kEventComponentPhase == 0x28);
}

TEST_CASE("npc talk: Biggoron named by his event component, 12 m away, over a nearer Goron") {
  NpcTalk talk;
  const auto walk = [](std::uint32_t phase) {
    return std::vector<NpcSeen>{
        At(0x100, "Npc_Goron_Biggoron_Adult", kBiggoronAt, phase),
        At(0x200, "Npc_Goron_003_Adult", Vec3{18.0f, 206.5f, -133.0f}, 0),  // 2 m from Link
    };
  };
  talk.update(false, kBiggoronLedge, walk(0), 0);
  talk.update(true, kBiggoronLedge, walk(1), 100 * kMs);
  REQUIRE(talk.partner());
  CHECK(*talk.partner() == 0x100);
  CHECK(talk.found() == NpcTalk::Found::game);
  // It stays for the whole event, even when the phase moves on.
  talk.update(true, kBiggoronLedge, walk(3), 5000 * kMs);
  CHECK(*talk.partner() == 0x100);
  talk.update(false, kBiggoronLedge, walk(0), 6000 * kMs);
  CHECK(talk.lingering());
  talk.update(false, kBiggoronLedge, walk(0), 8000 * kMs);
  CHECK_FALSE(talk.partner());
  CHECK(talk.found() == NpcTalk::Found::none);
}

TEST_CASE("npc talk: Biggoron's invisible stand-in on the ledge speaks for him") {
  const auto walk = [](std::optional<std::uint32_t> phase) {
    return std::vector<NpcSeen>{
        At(0x100, "Npc_Goron_Biggoron_Adult", kBiggoronAt, 0),
        At(0x300, "Npc_Invisible_Goron_Biggoron_Adult", Vec3{17.5f, 206.5f, -132.0f}, phase),
    };
  };
  // Its event component moved: found by it.
  NpcTalk talk;
  talk.update(false, kBiggoronLedge, walk(0), 0);
  talk.update(true, kBiggoronLedge, walk(1), 100 * kMs);
  REQUIRE(talk.partner());
  CHECK(*talk.partner() == 0x100);  // the body, never the stand-in
  CHECK(talk.found() == NpcTalk::Found::stand_in);
  // No phase read at all: the stand-in within kReachM of Link still gives him.
  NpcTalk blind;
  blind.update(false, kBiggoronLedge, walk(std::nullopt), 0);
  blind.update(true, kBiggoronLedge, walk(std::nullopt), 100 * kMs);
  REQUIRE(blind.partner());
  CHECK(*blind.partner() == 0x100);
  CHECK(blind.found() == NpcTalk::Found::by_reach);
  // A stand-in with no body on the walk gives nobody.
  NpcTalk lone;
  const std::vector<NpcSeen> only{
      At(0x300, "Npc_Invisible_Goron_Biggoron_Adult", Vec3{17.5f, 206.5f, -132.0f}, 0)};
  lone.update(false, kBiggoronLedge, only, 0);
  lone.update(true, kBiggoronLedge, only, 100 * kMs);
  CHECK_FALSE(lone.partner());
}

TEST_CASE("npc talk: with no word from the game, each name's own reach") {
  // Kaepora 5.9 m away (above the ledge): his reach, 15 m; a Kokiri as far is out.
  NpcTalk owl;
  const std::vector<NpcSeen> river{At(0x100, "Npc_Kaepora", kKaeporaAt)};
  owl.update(false, kKaeporaLedge, river, 0);
  owl.update(true, kKaeporaLedge, river, 100 * kMs);
  REQUIRE(owl.partner());
  CHECK(*owl.partner() == 0x100);
  CHECK(owl.found() == NpcTalk::Found::by_reach);
  NpcTalk kokiri;
  const std::vector<NpcSeen> forest{At(0x100, "Npc_Kokiri_005_Child", kKaeporaAt)};
  kokiri.update(false, kKaeporaLedge, forest, 0);
  kokiri.update(true, kKaeporaLedge, forest, 100 * kMs);
  CHECK_FALSE(kokiri.partner());
  // Biggoron 12.3 m away is within his 25 m; 26 m away he is not.
  NpcTalk big;
  const std::vector<NpcSeen> summit{At(0x100, "Npc_Goron_Biggoron_Adult", kBiggoronAt)};
  big.update(false, kBiggoronLedge, summit, 0);
  big.update(true, kBiggoronLedge, summit, 100 * kMs);
  CHECK(big.partner() == std::optional<std::uint64_t>{0x100});
  NpcTalk beyond;
  const Vec3 away{kBiggoronAt[0] + 26.0f, kBiggoronAt[1], kBiggoronAt[2]};
  beyond.update(false, away, summit, 0);
  beyond.update(true, away, summit, 100 * kMs);
  CHECK_FALSE(beyond.partner());
}

TEST_CASE("npc talk: a big one's long reach never takes a talk from a townsperson beside us") {
  // King Zora 10 m away on his throne, a Zora 4.5 m away: the Zora.
  const Vec3 king{10.0f, 0.0f, 0.0f};
  NpcTalk talk;
  const std::vector<NpcSeen> rest{At(0x100, "Npc_Zora_KingZora_child", king, std::nullopt, "Sit"),
                                  At(0x200, "Npc_Zora_005", Vec3{0.0f, 0.0f, 4.5f})};
  talk.update(false, kLink, rest, 0);
  talk.update(true, kLink, rest, 100 * kMs);
  REQUIRE(talk.partner());
  CHECK(*talk.partner() == 0x200);
  // But the king starting his talk (his animation changed) wins, as before.
  const std::vector<NpcSeen> turned{
      At(0x100, "Npc_Zora_KingZora_child", king, std::nullopt, "Talk_Start"),
      At(0x200, "Npc_Zora_005", Vec3{0.0f, 0.0f, 4.5f})};
  talk.update(true, kLink, turned, 400 * kMs);
  CHECK(*talk.partner() == 0x100);
}

TEST_CASE("npc talk: the game's word wins in the first second, and only a phase that moved counts") {
  // A phase already 1 at rest (left over from an earlier talk) says nothing;
  // the nearest is taken, then the one whose phase moves wins.
  NpcTalk talk;
  const auto walk = [](std::uint32_t a, std::uint32_t b) {
    return std::vector<NpcSeen>{At(0x100, "Npc_Kokiri_005_Child", Vec3{1.0f, 0, 0}, a),
                                At(0x200, "Npc_Kokiri_Mido_Child", Vec3{4.0f, 0, 0}, b)};
  };
  talk.update(false, kLink, walk(1, 0), 0);
  talk.update(true, kLink, walk(1, 0), 100 * kMs);
  REQUIRE(talk.partner());
  CHECK(*talk.partner() == 0x100);
  CHECK(talk.found() == NpcTalk::Found::by_reach);
  talk.update(true, kLink, walk(1, 1), 300 * kMs);
  CHECK(*talk.partner() == 0x200);
  CHECK(talk.found() == NpcTalk::Found::game);
  // A later move of the other one changes nothing.
  talk.update(true, kLink, walk(3, 1), 500 * kMs);
  CHECK(*talk.partner() == 0x200);

  // After the first second the game's word comes too late: the partner stays.
  NpcTalk late;
  late.update(false, kLink, walk(0, 0), 0);
  late.update(true, kLink, walk(0, 0), 100 * kMs);
  CHECK(*late.partner() == 0x100);
  late.update(true, kLink, walk(0, 1), 2000 * kMs);
  CHECK(*late.partner() == 0x100);

  // One first seen during the event with a phase other than 0 counts.
  NpcTalk fresh;
  fresh.update(false, kLink, {At(0x100, "Npc_Kokiri_005_Child", Vec3{1.0f, 0, 0}, 0)}, 0);
  fresh.update(true, kLink,
               {At(0x100, "Npc_Kokiri_005_Child", Vec3{1.0f, 0, 0}, 0),
                At(0x400, "Npc_Kaepora", Vec3{0, 6.0f, 2.0f}, 1)},
               100 * kMs);
  CHECK(fresh.partner() == std::optional<std::uint64_t>{0x400});
  // One 41 m away is past kGameReachM, whatever its phase says.
  NpcTalk distant;
  distant.update(false, kLink, {At(0x100, "Npc_JabuJabu", Vec3{41.0f, 0, 0}, 0)}, 0);
  distant.update(true, kLink, {At(0x100, "Npc_JabuJabu", Vec3{41.0f, 0, 0}, 1)}, 100 * kMs);
  CHECK_FALSE(distant.partner());
}

TEST_CASE("npc claim: an owner talking to Biggoron from the ledge is yielded to") {
  const NpcOwner o = Owner(2, true, kBiggoronLedge);
  CHECK(owner_talks(o, kBiggoronAt, "Npc_Goron_Biggoron_Adult"));
  CHECK_FALSE(owner_talks(o, kBiggoronAt));  // with no name: the 6 m of before
  CHECK_FALSE(owner_talks(o, kBiggoronAt, "Npc_Kokiri_005_Child"));
  NpcClaim c;
  c.see_status(2, true, kS);
  CHECK(c.update(0x100, "Npc_Goron_Biggoron_Adult", kBiggoronAt, 1, o, 2 * kS) ==
        NpcClaim::State::yielded);
}

TEST_CASE("npc talk: Biggoron's talk from guest memory, the event bit and his stand-in's phase") {
  // The hook's chain without the emulator: our event bit (player reader and
  // the Ocarina rule), each actor's event component phase (read_event_phase),
  // then the talk rule and the claim.
  using namespace coop::host;
  using namespace coop::host::test;
  FakeMemory m;
  const Layout l = make_game(m);
  m.put_vec3(l.game_ui + addr::kGameUiPosition, kBiggoronLedge);
  m.put_vec3(l.actor1 + addr::kActorPosition, kBiggoronLedge);
  constexpr std::uint64_t biggoron = 0x81000000, stand_in = 0x82000000;
  const auto put_component = [&](std::uint64_t actor, std::uint32_t phase) {
    m.put_u64(actor + addr::kActorComponents, actor + 0x10000);
    m.put_u64(actor + 0x10000 + addr::kComponentsEvent, actor + 0x20000);
    m.put_u32(actor + 0x20000 + addr::kEventComponentPhase, phase);
  };
  put_component(biggoron, 0);
  put_component(stand_in, 0);
  const auto walk = [&] {
    return std::vector<NpcSeen>{
        At(biggoron, "Npc_Goron_Biggoron_Adult", kBiggoronAt, read_event_phase(m, biggoron)),
        At(stand_in, "Npc_Invisible_Goron_Biggoron_Adult", Vec3{17.5f, 206.5f, -132.0f},
           read_event_phase(m, stand_in))};
  };
  const auto event_now = [&] {
    PlayerReader reader(m);
    const auto r = reader.read();
    REQUIRE(r);
    const auto anim = r->animation ? std::optional<std::string_view>(r->animation->name)
                                   : std::optional<std::string_view>{};
    return std::pair{local_event_hides(r->event_running.value_or(false), anim), r->position};
  };
  NpcTalk talk;
  auto [event, link] = event_now();
  REQUIRE_FALSE(event);
  talk.update(event, link, walk(), 0);
  CHECK_FALSE(talk.partner());
  // Link talks on the ledge: the stand-in asks its event, the manager runs it.
  put_component(stand_in, 1);
  m.put_u64(l.events + addr::kEventRunning, 0x10000);
  std::tie(event, link) = event_now();
  REQUIRE(event);
  talk.update(event, link, walk(), 100 * kMs);
  REQUIRE(talk.partner() == std::optional<std::uint64_t>{biggoron});
  CHECK(talk.found() == NpcTalk::Found::stand_in);
  NpcClaim claim;
  CHECK(claim.update(talk.partner(), "Npc_Goron_Biggoron_Adult", kBiggoronAt, 1, Owner(2, false),
                     100 * kMs) == NpcClaim::State::claimed);
}

TEST_CASE("npc talk: the self-check line says which path took the townsperson") {
  using coop::host::talk_check_line;
  // The scenarios' pattern (charla-biggoron and the others).
  const std::regex pattern(
      R"re(check: talk target Npc_Goron_Biggoron_Adult\w* by (event-component|radius [0-9.]+ m \(event component did not change))re");
  // By its own component.
  NpcTalk talk;
  const auto walk = [](std::uint32_t phase) {
    return std::vector<NpcSeen>{At(0x100, "Npc_Goron_Biggoron_Adult", kBiggoronAt, phase)};
  };
  talk.update(false, kBiggoronLedge, walk(0), 0);
  talk.update(true, kBiggoronLedge, walk(1), 100 * kMs);
  REQUIRE(talk.found() == NpcTalk::Found::game);
  CHECK(talk.stand_in().empty());
  const std::string by_game = talk_check_line("Npc_Goron_Biggoron_Adult", talk.found(),
                                              talk.stand_in(), talk.distance_m());
  CHECK(by_game == "check: talk target Npc_Goron_Biggoron_Adult by event-component");
  CHECK(std::regex_search(by_game, pattern));
  // By its stand-in's.
  NpcTalk ledge;
  const auto walk2 = [](std::uint32_t phase) {
    return std::vector<NpcSeen>{
        At(0x100, "Npc_Goron_Biggoron_Adult", kBiggoronAt, 0),
        At(0x300, "Npc_Invisible_Goron_Biggoron_Adult", Vec3{17.5f, 206.5f, -132.0f}, phase)};
  };
  ledge.update(false, kBiggoronLedge, walk2(0), 0);
  ledge.update(true, kBiggoronLedge, walk2(1), 100 * kMs);
  REQUIRE(ledge.found() == NpcTalk::Found::stand_in);
  CHECK(ledge.stand_in() == "Npc_Invisible_Goron_Biggoron_Adult");
  const std::string by_stand_in = talk_check_line("Npc_Goron_Biggoron_Adult", ledge.found(),
                                                  ledge.stand_in(), ledge.distance_m());
  CHECK(by_stand_in == "check: talk target Npc_Goron_Biggoron_Adult by event-component "
                       "(stand-in Npc_Invisible_Goron_Biggoron_Adult)");
  CHECK(std::regex_search(by_stand_in, pattern));
  // By radius: no phase read, 12.3 m away within his 25 m.
  NpcTalk blind;
  const std::vector<NpcSeen> summit{At(0x100, "Npc_Goron_Biggoron_Adult", kBiggoronAt)};
  blind.update(false, kBiggoronLedge, summit, 0);
  blind.update(true, kBiggoronLedge, summit, 100 * kMs);
  REQUIRE(blind.found() == NpcTalk::Found::by_reach);
  CHECK(blind.distance_m() == doctest::Approx(12.32f).epsilon(0.01));
  const std::string by_radius = talk_check_line("Npc_Goron_Biggoron_Adult", blind.found(),
                                                blind.stand_in(), blind.distance_m());
  CHECK(by_radius == "check: talk target Npc_Goron_Biggoron_Adult by radius 25.0 m (event "
                     "component did not change; 12.3 m away)");
  CHECK(std::regex_search(by_radius, pattern));
  // Saria at the house: the 5 m of every townsperson.
  CHECK(talk_check_line("Npc_Kokiri_Saria_Child", NpcTalk::Found::by_reach, "", 2.5f) ==
        "check: talk target Npc_Kokiri_Saria_Child by radius 5.0 m (event component did not "
        "change; 2.5 m away)");
}
