// ORM cases moved out of upstream's tests/overlay/overlay_test.cpp (oot-remake D-R65; ADOPTAR_UPSTREAM.md rule 2:
// our tests live in tests/orm/, so a re-pin does not conflict in upstream's test files).

#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "coop/overlay/overlay.hpp"

using namespace coop::overlay;
namespace proto = coop::protocol;

namespace {

constexpr float kPi = 3.14159265358979f;

// At the origin looking down -z, 90 degree field of view, 800 x 800 pixels:
// a point at depth d with x = d sits on the right edge.
Camera straight_camera() {
  Camera c;
  c.view = look_at({0, 0, 0}, {0, 0, -1}, {0, 1, 0});
  c.projection = perspective(kPi / 2, 1.0f, 0.1f, 1000.0f);
  c.viewport_width = 800;
  c.viewport_height = 800;
  return c;
}

Shown player(std::uint8_t number, Vec3 position, float fade = 1.0f, std::string scene = "field") {
  Shown s;
  s.number = number;
  s.name = "Player " + std::to_string(number);
  s.position = position;
  s.scene = std::move(scene);
  s.fade = fade;
  return s;
}

Result build(const std::vector<Shown>& players, Settings settings = {}) {
  return Overlay(settings).build(straight_camera(), "field", players);
}

std::optional<Why> why(const Result& r, std::uint8_t number) {
  for (const auto& s : r.skipped) {
    if (s.number == number) return s.why;
  }
  return std::nullopt;
}

}  // namespace

// ORM: a downed mate's tag stays solid at any distance and carries its note,
// so the one who can revive him finds him from across the field.
TEST_CASE("overlay: an urgent player does not fade with distance and shows its note") {
  Settings s;
  s.fade_start = 20;
  s.fade_end = 40;
  Shown down = player(4, {0, 0, -50});  // beyond fade_end: skipped if not urgent
  down.urgent = true;
  down.note = "down, 50 m";
  Shown mid = player(3, {0, 0, -30}, 0.5f);
  mid.urgent = true;  // half way into the fade: solid, times the presence fade
  Result r = build({down, mid, player(2, {0, 0, -50})}, s);
  REQUIRE(r.items.size() == 2);
  for (const auto& item : r.items) {
    if (item.number == 4) {
      CHECK(item.alpha == doctest::Approx(1.0f));
      CHECK(item.tag.text == "Player 4 (down, 50 m)");
      CHECK(item.urgent);
    }
    if (item.number == 3) {
      CHECK(item.alpha == doctest::Approx(0.5f));
      CHECK(item.tag.text == "Player 3");
      CHECK(item.urgent);
    }
  }
  CHECK(why(r, 2) == Why::too_far);
  // Still nothing past far_distance, urgent or not.
  s.far_distance = 45;
  Result far = build({down}, s);
  CHECK(far.items.empty());
  CHECK(why(far, 4) == Why::too_far);
}

TEST_CASE("overlay: each player's colour, from presence or its number's default (ORM)") {
  // Six different defaults, then round again.
  for (std::size_t i = 0; i < kPalette.size(); ++i) {
    for (std::size_t j = i + 1; j < kPalette.size(); ++j) CHECK(kPalette[i] != kPalette[j]);
  }
  // The defaults are upstream's slot colours (test10: the puppet's tint for a
  // player with no colour), 5 and 6 ours: six different ones.
  const auto rgb = [](std::uint32_t c) {
    return proto::Rgb{static_cast<std::uint8_t>(c >> 16), static_cast<std::uint8_t>(c >> 8),
                      static_cast<std::uint8_t>(c)};
  };
  for (std::uint8_t n = 1; n <= proto::kMaxPlayers; ++n) {
    CHECK(default_color(n) == rgb(proto::slot_color(n)));
    for (std::uint8_t m = n + 1; m <= proto::kMaxPlayers; ++m) CHECK(default_color(n) != default_color(m));
  }
  CHECK(default_color(0) == default_color(1));
  CHECK(default_color(1) == proto::Rgb{0x30, 0x70, 0xff});  // blue
  CHECK(default_color(6) == proto::Rgb{0xf0, 0x9a, 0x28});  // orange
  CHECK(color_of(nullptr, 3) == default_color(3));

  coop::presence::Presence presence;
  proto::Welcome w;
  w.player_number = 1;
  w.players = {{2, "Ana"}, {3, "Ben"}};
  presence.on_welcome(w);
  presence.apply(proto::PlayerLook{3, {0x12, 0x34, 0x56}, 0});
  CHECK(color_of(presence.player(2), 2) == default_color(2));  // no PLAYER_LOOK
  CHECK(color_of(presence.player(3), 3) == proto::Rgb{0x12, 0x34, 0x56});
  for (std::uint8_t n : {std::uint8_t{2}, std::uint8_t{3}}) {
    proto::PlayerSample s;
    s.player_number = n;
    s.time = {1'000'000, 1};
    s.position = Vec3{0, 0, -10.0f * n};
    s.scene = "field";
    presence.on_sample(s);
  }

  const std::vector<Shown> shown = collect(presence, 1'000'000 + presence.settings().show_delay_us);
  REQUIRE(shown.size() == 2);
  CHECK_FALSE(shown[0].color);
  REQUIRE(shown[1].color);
  CHECK(*shown[1].color == proto::Rgb{0x12, 0x34, 0x56});

  // The draw list carries it: far to near, so 3 (depth 30) first.
  Result r = build(shown);
  REQUIRE(r.items.size() == 2);
  CHECK(r.items[0].number == 3);
  CHECK(r.items[0].color == proto::Rgb{0x12, 0x34, 0x56});
  CHECK(r.items[1].number == 2);
  CHECK(r.items[1].color == default_color(2));
}
