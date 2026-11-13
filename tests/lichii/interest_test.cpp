// ORM: whether our Link's pose goes, from where the others stand
// (lichii/hook/coop_interest.cpp, oot-remake docs/ANCHO_DE_BANDA.md). Decision
// (2026-11-11): the native rate for everyone in our place, whatever the
// distance; none while nobody is here.
#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <optional>
#include <string>

#include "coop/net/client.hpp"
#include "coop_interest.h"
#include "coop_pose.h"

using CoopInterest::Interest;
using CoopInterest::Tier;
using CoopInterest::Vec3;

namespace {

constexpr std::uint64_t kNow = 10'000;

// We are player 1 in the market, at the origin.
void Market(Interest& in) {
    in.SetSelf(1);
    in.SetLocal(std::string_view("Market"), Vec3{0, 0, 0});
}

}  // namespace

TEST_CASE("interest: anyone in our place gets the native pose rate, at any distance") {
    Interest in;
    Market(in);
    CHECK(in.TierAt(kNow) == Tier::alone);  // nobody in the room
    CHECK_FALSE(in.RatesAt(kNow).pose_gap_ms.has_value());
    for (const float metres : {5.0f, 15.0f, 40.0f, 60.0f, 500.0f, 5000.0f}) {
        in.OnSample(2, std::string("Market"), Vec3{metres, 0, 0}, kNow);
        CHECK(in.TierAt(kNow) == Tier::here);
        REQUIRE(in.RatesAt(kNow).pose_gap_ms.has_value());
        CHECK(*in.RatesAt(kNow).pose_gap_ms == 1000 / CoopPose::kSendHz);
        CHECK(in.AnyoneHere(kNow));
    }
    in.OnSample(3, std::string("Market"), Vec3{3, 4, 0}, kNow);  // 5 m
    REQUIRE(in.Nearest(kNow).has_value());
    CHECK(*in.Nearest(kNow) == doctest::Approx(5.0f));
    in.Forget(2);
    in.Forget(3);
    CHECK(in.TierAt(kNow) == Tier::alone);
}

TEST_CASE("interest: players in other places do not count, and either era is one place") {
    Interest in;
    in.SetSelf(1);
    in.SetLocal(std::string_view("Graveyard_Child"), Vec3{0, 0, 0});
    in.OnSample(2, std::string("Kakariko_Child"), Vec3{1, 0, 0}, kNow);
    in.OnSample(3, std::string("HyruleField"), Vec3{2, 0, 0}, kNow);
    CHECK(in.TierAt(kNow) == Tier::alone);
    CHECK_FALSE(in.AnyoneHere(kNow));
    CHECK_FALSE(CoopInterest::RatesOf(Tier::alone).pose_gap_ms.has_value());
    // An adult in the graveyard seven years on sees our puppet.
    in.OnSample(4, std::string("Graveyard_Adult"), Vec3{200, 0, 0}, kNow);
    CHECK(in.TierAt(kNow) == Tier::here);
}

TEST_CASE("interest: anything not known counts as here") {
    SUBCASE("a player who joined and has sent nothing yet") {
        Interest in;
        Market(in);
        in.Joined(2);
        CHECK(in.TierAt(kNow) == Tier::here);
    }
    SUBCASE("a player whose scene is not known") {
        Interest in;
        Market(in);
        in.OnSample(2, std::nullopt, Vec3{100, 0, 0}, kNow);
        CHECK(in.TierAt(kNow) == Tier::here);
    }
    SUBCASE("a player not heard from for a while (loading)") {
        Interest in;
        Market(in);
        in.OnSample(2, std::string("Kakariko"), Vec3{100, 0, 0}, kNow);
        CHECK(in.TierAt(kNow + CoopInterest::kStaleMs) == Tier::alone);
        CHECK(in.TierAt(kNow + CoopInterest::kStaleMs + 1) == Tier::here);
    }
    SUBCASE("our own place not known") {
        Interest in;
        in.SetSelf(1);
        in.OnSample(2, std::string("Market"), Vec3{100, 0, 0}, kNow);
        CHECK(in.TierAt(kNow) == Tier::here);
    }
    SUBCASE("in our place but no position since its scene changed") {
        Interest in;
        Market(in);
        in.OnSample(2, std::string("Kakariko"), Vec3{100, 0, 0}, kNow);
        in.OnSample(2, std::string("Market"), std::nullopt, kNow);
        CHECK(in.TierAt(kNow) == Tier::here);
    }
    SUBCASE("turned off") {
        Interest in;
        Market(in);
        in.SetEnabled(false);
        CHECK(in.TierAt(kNow) == Tier::here);
        CHECK(in.AnyoneHere(kNow));
    }
}

TEST_CASE("interest: our own number and numbers out of range are ignored") {
    Interest in;
    Market(in);
    in.OnSample(1, std::string("Market"), Vec3{1, 0, 0}, kNow);  // ourselves
    in.OnSample(0, std::string("Market"), Vec3{1, 0, 0}, kNow);
    in.OnSample(coop::protocol::kMaxPlayers + 1, std::string("Market"), Vec3{1, 0, 0}, kNow);
    CHECK(in.TierAt(kNow) == Tier::alone);
}

TEST_CASE("interest: the townsperson's pose goes with anyone in our place, at any distance") {
    Interest in;
    Market(in);
    CHECK_FALSE(in.AnyoneHere(kNow));  // alone
    in.OnSample(2, std::string("Market"), Vec3{500, 0, 0}, kNow);
    CHECK(in.AnyoneHere(kNow));
    in.OnSample(2, std::string("Kakariko"), Vec3{0, 0, 0}, kNow);
    CHECK_FALSE(in.AnyoneHere(kNow));
}

TEST_CASE("interest: our Link entering another scene") {
    Interest in;
    Market(in);
    in.OnSample(2, std::string("Kakariko"), Vec3{0, 0, 0}, kNow);
    CHECK(in.TierAt(kNow) == Tier::alone);
    in.SetLocal(std::string_view("Kakariko"), std::nullopt);  // a load: no Link yet
    CHECK(in.TierAt(kNow) == Tier::here);
    CHECK_FALSE(in.Nearest(kNow).has_value());  // our spot not known
    in.SetLocal(std::string_view("Kakariko"), Vec3{100, 0, 0});
    CHECK(in.TierAt(kNow) == Tier::here);
    REQUIRE(in.Nearest(kNow).has_value());
    CHECK(*in.Nearest(kNow) == doctest::Approx(100.0f));
}

TEST_CASE("interest: the one pose rate is the native one, under what the receivers need") {
    using namespace CoopInterest;
    const Rates r = RatesOf(Tier::here);
    REQUIRE(r.pose_gap_ms.has_value());
    CHECK(*r.pose_gap_ms == kPoseGapMs);
    CHECK(kPoseGapMs == 1000 / CoopPose::kSendHz);
    CHECK(*r.pose_gap_ms < CoopPose::kResumeGapMs);
    CHECK(*r.pose_gap_ms < CoopPose::kFreshPoseMs);
}

TEST_CASE("interest: the network events keep who is in the room") {
    Interest& in = CoopInterest::The();
    in.SetEnabled(true);
    in.SetLocal(std::string_view("Market"), Vec3{0, 0, 0});
    coop::net::Event welcome;
    welcome.kind = coop::net::EventKind::welcomed;
    welcome.welcome.player_number = 1;
    welcome.welcome.players = {{2, "Zelda"}};
    CoopInterest::OnEvent(welcome, kNow);
    CHECK(in.TierAt(kNow) == Tier::here);  // Zelda: nothing heard yet

    coop::net::Event sample;
    sample.kind = coop::net::EventKind::message;
    coop::protocol::PlayerSample s;
    s.player_number = 2;
    s.scene = "Market";
    s.position = Vec3{100, 0, 0};
    sample.message = s;
    CoopInterest::OnEvent(sample, kNow);
    CHECK(in.TierAt(kNow) == Tier::here);  // 100 m away: still the native rate

    coop::protocol::PlayerSample away = s;
    away.scene = "Kakariko";
    coop::net::Event gone;
    gone.kind = coop::net::EventKind::message;
    gone.message = away;
    CoopInterest::OnEvent(gone, kNow);
    CHECK(in.TierAt(kNow) == Tier::alone);

    coop::net::Event left;
    left.kind = coop::net::EventKind::player_left;
    left.player = 2;
    CoopInterest::OnEvent(sample, kNow);
    CoopInterest::OnEvent(left, kNow);
    CHECK(in.TierAt(kNow) == Tier::alone);

    CoopInterest::OnEvent(sample, kNow);
    coop::net::Event drop;
    drop.kind = coop::net::EventKind::disconnected;
    CoopInterest::OnEvent(drop, kNow);
    CHECK(in.TierAt(kNow) == Tier::alone);
    in.Clear();
}
