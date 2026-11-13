// ORM cases moved out of upstream's tests/host/sampler_test.cpp (oot-remake D-R65; ADOPTAR_UPSTREAM.md rule 2:
// our tests live in tests/orm/, so a re-pin does not conflict in upstream's test files).

#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <algorithm>
#include <cstdint>
#include <variant>
#include <vector>

#include "coop/host/sampler.hpp"
#include "coop/protocol/codec.hpp"
#include "coop/protocol/orm_status.hpp"
#include "../host/fake_memory.hpp"

using namespace coop::host;
namespace proto = coop::protocol;

namespace {

constexpr std::uint64_t kFrameUs = 16'667;  // 60 frames a second
constexpr std::uint64_t kStart = 5'000'000;

PlayerReading at(float x, std::optional<bool> event = false) {
  PlayerReading r;
  r.position = {x, 0.0f, 0.0f};
  r.rotation = Quat{0, 0, 0, 1};
  r.age = proto::Age::adult;
  r.scene = "spot00";
  r.event_running = event;
  return r;
}

struct Decoded {
  std::vector<proto::PlayerSample> samples;
  std::vector<proto::PlayerStatus> statuses;
  std::vector<proto::PlayerEquip> equips;
};

// Decodes what one frame returned, checking each message's channel.
Decoded decode(const std::vector<Outgoing>& out) {
  Decoded d;
  for (const auto& o : out) {
    proto::Message m;
    REQUIRE(proto::decode(o.bytes, m) == proto::Error::none);
    CHECK(proto::check_received(m, proto::Sender::client, o.channel) == proto::Error::none);
    if (auto* s = std::get_if<proto::PlayerSample>(&m)) {
      CHECK(o.channel == proto::kChannelPlayerSamples);
      d.samples.push_back(*s);
    } else if (auto* st = std::get_if<proto::PlayerStatus>(&m)) {
      CHECK(o.channel == proto::kChannelSession);
      d.statuses.push_back(*st);
    } else if (auto* e = std::get_if<proto::PlayerEquip>(&m)) {
      CHECK(o.channel == proto::kChannelSession);
      d.equips.push_back(*e);
    } else {
      FAIL("unexpected message");
    }
  }
  return d;
}

}  // namespace

TEST_CASE("host sampler: downed sets the dead bit of PLAYER_STATUS and clears it after") {
  Sampler s;
  decode(s.frame(at(1.0f), kStart));
  s.set_downed(true);
  auto d = decode(s.frame(at(1.0f), kStart + kFrameUs));
  REQUIRE(d.statuses.size() == 1);
  CHECK(d.statuses[0].flags == proto::status::dead);
  CHECK(decode(s.frame(at(1.0f), kStart + 2 * kFrameUs)).statuses.empty());  // only on a change
  // With a cutscene too, both bits.
  d = decode(s.frame(at(1.0f, true), kStart + 3 * kFrameUs));
  REQUIRE(d.statuses.size() == 1);
  CHECK(d.statuses[0].flags == (proto::status::dead | proto::status::in_cutscene));
  s.set_downed(false);
  d = decode(s.frame(at(1.0f, false), kStart + 4 * kFrameUs));
  REQUIRE(d.statuses.size() == 1);
  CHECK(d.statuses[0].flags == 0);
}

TEST_CASE("host sampler: still down after a reconnect, the dead bit goes out again (ORM)") {
  Sampler s;
  s.set_downed(true);
  auto d = decode(s.frame(at(1.0f), kStart));
  REQUIRE(d.statuses.size() == 1);
  CHECK(d.statuses[0].flags == proto::status::dead);
  // The server forgot us (a rejoin by name starts with no status): the
  // session's reset after the WELCOME sends the status again, bit 4 kept.
  s.reset();
  d = decode(s.frame(at(1.0f), kStart + kFrameUs));
  REQUIRE(d.statuses.size() == 1);
  CHECK(d.statuses[0].flags == proto::status::dead);
}

// ORM menus (coop/host/menus.hpp, oot-remake docs/MENUS_PAUSA.md).
TEST_CASE("host sampler: the menu bits go into the status, the others kept (ORM menus)") {
  Sampler s;
  decode(s.frame(at(1.0f, true), kStart));  // in a cutscene
  s.set_menu_status(proto::status::paused | proto::status::in_menu | proto::status::dead);
  CHECK(s.menu_status() == proto::status::paused);  // only the menus' own bits are taken
  auto d = decode(s.frame(at(1.0f, true), kStart + kFrameUs));
  REQUIRE(d.statuses.size() == 1);
  CHECK(d.statuses[0].flags == (proto::status::paused | proto::status::in_cutscene));
  CHECK(decode(s.frame(at(1.0f, true), kStart + 2 * kFrameUs)).statuses.empty());  // only on a change
  // Downed while paused: both bits.
  s.set_downed(true);
  d = decode(s.frame(at(1.0f, false), kStart + 3 * kFrameUs));
  REQUIRE(d.statuses.size() == 1);
  CHECK(d.statuses[0].flags == (proto::status::paused | proto::status::dead));
  s.set_downed(false);
  s.set_menu_status(0);
  d = decode(s.frame(at(1.0f, false), kStart + 4 * kFrameUs));
  REQUIRE(d.statuses.size() == 1);
  CHECK(d.statuses[0].flags == 0);
}

// ORM the room's age (coop/host/age.hpp, oot-remake docs/EDAD.md, 2026-11-13).
TEST_CASE("host sampler: our move to the room's age waits: bit 6 in the status, with the others") {
  Sampler s;
  decode(s.frame(at(1.0f, false), kStart));
  s.set_age_waits(true);
  CHECK(s.age_waits());
  s.set_downed(true);
  auto d = decode(s.frame(at(1.0f, true), kStart + kFrameUs));
  REQUIRE(d.statuses.size() == 1);
  CHECK(d.statuses[0].flags == (proto::orm_status::age_waits | proto::status::dead | proto::status::in_cutscene));
  CHECK(proto::orm_status::age_waits == 0x40);
  CHECK((proto::orm_status::age_waits & proto::orm_status::at_title) == 0);
  CHECK(decode(s.frame(at(1.0f, true), kStart + 2 * kFrameUs)).statuses.empty());  // only on a change
  s.set_age_waits(false);
  s.set_downed(false);
  d = decode(s.frame(at(1.0f, false), kStart + 3 * kFrameUs));
  REQUIRE(d.statuses.size() == 1);
  CHECK(d.statuses[0].flags == 0);
}

TEST_CASE("host sampler: the title screen's status goes out with no reading (ORM menus)") {
  Sampler s;
  decode(s.frame(at(1.0f, true), kStart));
  // No Link: the title bits still go, once; no sample.
  s.set_menu_status(proto::status::loading | proto::orm_status::at_title);
  auto d = decode(s.frame(std::nullopt, kStart + kFrameUs));
  REQUIRE(d.statuses.size() == 1);
  CHECK(d.statuses[0].flags ==
        (proto::status::loading | proto::orm_status::at_title | proto::status::in_cutscene));
  CHECK(d.samples.empty());
  CHECK(decode(s.frame(std::nullopt, kStart + 2 * kFrameUs)).statuses.empty());
  // The downed bit is not sent without a reading (as before).
  s.set_downed(true);
  CHECK(decode(s.frame(std::nullopt, kStart + 3 * kFrameUs)).statuses.empty());
  s.set_downed(false);
  // Back in the world: the first reading clears them.
  s.set_menu_status(0);
  d = decode(s.frame(at(2.0f, false), kStart + 4 * kFrameUs));
  REQUIRE(d.statuses.size() == 1);
  CHECK(d.statuses[0].flags == 0);
  CHECK(d.samples.size() == 1);

  // After a reconnect on the title screen the status goes again with no reading.
  s.set_menu_status(proto::status::loading | proto::orm_status::at_title);
  decode(s.frame(std::nullopt, kStart + 5 * kFrameUs));
  s.reset();
  d = decode(s.frame(std::nullopt, kStart + 6 * kFrameUs));
  REQUIRE(d.statuses.size() == 1);
  CHECK(d.statuses[0].flags == (proto::status::loading | proto::orm_status::at_title));
  // Nothing to say: a reset with no reading and no menu bits sends nothing.
  Sampler quiet;
  CHECK(quiet.frame(std::nullopt, kStart).empty());
  quiet.set_menu_status(0);
  CHECK(quiet.frame(std::nullopt, kStart + kFrameUs).empty());
}
