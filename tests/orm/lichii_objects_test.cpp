// ORM cases moved out of upstream's tests/lichii/objects_test.cpp (oot-remake D-R65; ADOPTAR_UPSTREAM.md rule 2:
// our tests live in tests/orm/, so a re-pin does not conflict in upstream's test files).

// Tests for the shared enemies' hook side (lichii/hook/coop_objects_sync.cpp).
// No emulator code: each walk is a list of actors.
#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <cstring>
#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "coop/protocol/codec.hpp"
#include "coop_hold.h"
#include "coop_objects_sync.h"

namespace proto = coop::protocol;
using CoopObjects::LifeWrite;
using CoopObjects::Seen;
using CoopObjects::Sync;

namespace {

constexpr std::uint64_t kMs = 1000;
const std::string kScene = "Spot_GraveYard";

Seen Enemy(std::uint64_t address, std::uint64_t hash, std::int32_t life, float x = 0.0f,
           std::string name = "Enemy_Stalchild") {
    Seen s;
    s.address = address;
    s.name = std::move(name);
    s.hash = hash;
    s.life = proto::ObjectLife{life, 2};
    s.position = {x, 0.0f, 0.0f};
    return s;
}

struct Step {
    std::vector<proto::Message> send;
    std::vector<LifeWrite> writes;
};

Step Tick(Sync& sync, std::uint64_t now, const std::vector<Seen>& seen, std::uint8_t self = 1,
          const std::string& scene = kScene) {
    Step step;
    sync.Tick(scene, self, now, seen, step.send, step.writes);
    return step;
}

template <typename T>
std::vector<T> All(const Step& step) {
    std::vector<T> out;
    for (const auto& m : step.send) {
        if (const auto* t = std::get_if<T>(&m)) {
            out.push_back(*t);
        }
    }
    return out;
}

void Own(Sync& sync, const proto::ObjectId& id, std::uint8_t owner, std::uint16_t epoch = 0) {
    sync.Receive(proto::ObjectOwner{id, owner, epoch, 0});
}

Seen Chest(std::uint64_t address, std::uint64_t hash = 0x3c5bb811b4cf54dd) {
    Seen chest;
    chest.address = address;
    chest.name = "Obj_TBox_A_01";
    chest.hash = hash;
    chest.position = {1.0f, 0.0f, 1.0f};
    return chest;
}

}  // namespace

// ORM: between an owner change and the new owner's first sample (a round
// trip), the copy was left to its own AI and then snapped (NPCs flipping
// owners 45 m away, issue 133). Now it holds where it was shown.
TEST_CASE("objects: a copy holds where it was shown until the new owner's first sample") {
    Sync sync;
    Tick(sync, 0, {Enemy(0x100, 0x11, 4, 50.0f)});
    const proto::ObjectId id = *sync.IdOf(0x100);
    Own(sync, id, 2);
    const auto sample = [&](std::uint8_t from, std::uint16_t epoch, std::uint64_t t, float x) {
        proto::ObjectSample s;
        s.player_number = from;
        s.id = id;
        s.epoch = epoch;
        s.state.time = {t, 0};
        s.state.position = proto::Vec3{x, 0.0f, 0.0f};
        s.state.velocity = proto::Vec3{20.0f, 0.0f, 0.0f};
        sync.Receive(s);
    };
    sample(2, 0, 1000 * kMs, 0.0f);
    sample(2, 0, 1050 * kMs, 1.0f);
    Tick(sync, 1100 * kMs, {Enemy(0x100, 0x11, 4, 50.0f)});

    // Player 3 takes it at 1125: the copy shows player 2's place at 1025.
    Own(sync, id, 3, 1);
    Tick(sync, 1125 * kMs, {Enemy(0x100, 0x11, 4, 50.0f)});
    std::vector<CoopObjects::PoseWrite> poses;
    sync.Poses(1150 * kMs, poses);
    REQUIRE(poses.size() == 1);
    CHECK(poses[0].position[0] == doctest::Approx(0.5f));
    poses.clear();
    sync.Poses(1400 * kMs, poses);  // still waiting: held, not carried on by the velocity
    REQUIRE(poses.size() == 1);
    CHECK(poses[0].position[0] == doctest::Approx(0.5f));
    CHECK(sync.Holds(1400 * kMs) == std::vector<std::uint64_t>{0x100});  // its AI stays held

    // The new owner's first sample (stamped a little before the change here:
    // clocks) ends the hold and places it, nothing of the hold blended in.
    sample(3, 1, 1120 * kMs, 30.0f);
    Tick(sync, 1450 * kMs, {Enemy(0x100, 0x11, 4, 50.0f)});
    poses.clear();
    sync.Poses(1450 * kMs, poses);
    REQUIRE(poses.size() == 1);
    CHECK(poses[0].position[0] == doctest::Approx(33.0f));  // 30 + 150 ms at 20 m/s

    // Ours, then player 2's: held at our own place until player 2 speaks.
    Own(sync, id, 1, 2);
    Tick(sync, 1500 * kMs, {Enemy(0x100, 0x11, 4, 12.0f)});
    Own(sync, id, 2, 3);
    Tick(sync, 1600 * kMs, {Enemy(0x100, 0x11, 4, 12.0f)});
    poses.clear();
    sync.Poses(1650 * kMs, poses);
    REQUIRE(poses.size() == 1);
    CHECK(poses[0].position[0] == doctest::Approx(12.0f));
    // Nobody speaks for kPoseStaleUs: left to itself, as before.
    poses.clear();
    sync.Poses(1600 * kMs + Sync::kPoseStaleUs + kMs, poses);
    CHECK(poses.empty());
}

namespace {

constexpr std::uint8_t kTargeting = proto::object_presence::targeting;

proto::ObjectSample OwnerSample(const proto::ObjectId& id, std::uint8_t owner, std::uint64_t t,
                                float x, std::int32_t life, const std::string& animation) {
    proto::ObjectSample s;
    s.player_number = owner;
    s.id = id;
    s.state.time = {t, 1};
    s.state.position = proto::Vec3{x, 0.0f, 0.0f};
    s.state.life = proto::ObjectLife{life, 2};
    s.state.animation = proto::Animation{animation, 0.0f, 1.0f};
    return s;
}

}  // namespace

// ORM D-R12: townspeople our player talks to (Sync::SetTalk, coop/host/npc_talk.hpp).
namespace {

Seen Townsperson(std::uint64_t address, std::uint64_t hash, float x = 0.0f) {
    Seen s;
    s.address = address;
    s.name = "Npc_Kokiri_Mido_Child";
    s.hash = hash;
    // Townspeople have a Life component (life 100 in a Kokiri Forest log); left
    // out here, so the owner's samples below only move it.
    s.position = {x, 0.0f, 0.0f};
    s.animation = proto::Animation{"Act_Arrogant", 0.0f, 1.0f};
    return s;
}

std::uint8_t PresentFlags(const Step& step, const proto::ObjectId& id) {
    for (const auto& p : All<proto::ObjectPresent>(step)) {
        for (const auto& e : p.objects) {
            if (e.id == id) return e.flags;
        }
    }
    return 0xff;  // not sent
}

}  // namespace

TEST_CASE("objects: a townsperson our player talks to is flagged targeting, a copy or ours") {
    Sync sync;
    const Seen mido = Townsperson(0x100, 0x21);
    Tick(sync, 0, {mido});
    const proto::ObjectId id = *sync.IdOf(0x100);
    Own(sync, id, 2);  // another game owns it
    auto step = Tick(sync, 2100 * kMs, {mido});
    REQUIRE(PresentFlags(step, id) != 0xff);
    CHECK((PresentFlags(step, id) & proto::object_presence::targeting) == 0);

    // We talk to it: the next OBJECT_PRESENT goes at once, flagged, so the
    // server hands it to us.
    sync.SetTalk({0x100}, {0x100});
    step = Tick(sync, 2200 * kMs, {mido});
    CHECK((PresentFlags(step, id) & proto::object_presence::targeting) != 0);

    // Ours now, still talking: still flagged (a fight to the server, it stays).
    Own(sync, id, 1, 1);
    step = Tick(sync, 4300 * kMs, {mido});
    CHECK((PresentFlags(step, id) & proto::object_presence::targeting) != 0);

    // The talk is over: no flag, at once.
    sync.SetTalk({}, {});
    step = Tick(sync, 4400 * kMs, {mido});
    REQUIRE(PresentFlags(step, id) != 0xff);
    CHECK((PresentFlags(step, id) & proto::object_presence::targeting) == 0);
}

TEST_CASE("objects: a freed copy gets no pose or animation from its owner until the talk ends") {
    Sync sync;
    const Seen mido = Townsperson(0x100, 0x21, 5.0f);
    Tick(sync, 0, {mido});
    const proto::ObjectId id = *sync.IdOf(0x100);
    Own(sync, id, 2);
    sync.Receive(OwnerSample(id, 2, 1000 * kMs, 1.0f, 2, "Act_Arrogant"));
    Tick(sync, 1010 * kMs, {mido});
    std::vector<CoopObjects::PoseWrite> poses;
    std::vector<CoopObjects::PlayWish> plays;
    sync.Poses(1100 * kMs, poses);
    sync.Plays(1100 * kMs, plays);
    REQUIRE(poses.size() == 1);
    REQUIRE(plays.size() == 1);

    // Our player talks to it: our own game turns it and plays the talk.
    sync.SetTalk({0x100}, {0x100});
    CHECK(sync.Freed(0x100));
    sync.Receive(OwnerSample(id, 2, 1100 * kMs, 1.0f, 2, "Act_Arrogant"));
    Tick(sync, 1110 * kMs, {mido});
    poses.clear();
    plays.clear();
    sync.Poses(1200 * kMs, poses);
    sync.Plays(1200 * kMs, plays);
    CHECK(poses.empty());
    CHECK(plays.empty());

    // The hand-over did not come (the owner talks to it too): when the talk
    // is over the owner's pose and animation come back.
    sync.SetTalk({}, {});
    CHECK_FALSE(sync.Freed(0x100));
    sync.Receive(OwnerSample(id, 2, 1300 * kMs, 1.0f, 2, "Talk"));
    Tick(sync, 1310 * kMs, {mido});
    poses.clear();
    plays.clear();
    sync.Poses(1400 * kMs, poses);
    sync.Plays(1400 * kMs, plays);
    REQUIRE(poses.size() == 1);
    REQUIRE(plays.size() == 1);
    CHECK(plays[0].name == "Talk");
}

TEST_CASE("objects: the talker's townsperson, once ours, sends its talk to the others") {
    // The whole hand-over seen from the talker: a copy it talks to becomes
    // its own, and its own game's turn and talk go out as samples at once.
    Sync sync;
    Seen mido = Townsperson(0x100, 0x21);
    Tick(sync, 0, {mido});
    const proto::ObjectId id = *sync.IdOf(0x100);
    Own(sync, id, 2);
    sync.SetTalk({0x100}, {0x100});
    mido.animation = proto::Animation{"Talk_Start", 0.0f, 1.0f};
    mido.rotation = CoopObjects::Mat3{0, 0, 1, 0, 1, 0, -1, 0, 0};  // turned to us
    CHECK(All<proto::ObjectSample>(Tick(sync, 100 * kMs, {mido})).empty());  // not ours yet
    Own(sync, id, 1, 1);
    const auto samples = All<proto::ObjectSample>(Tick(sync, 700 * kMs, {mido}));
    REQUIRE(samples.size() == 1);
    CHECK(samples[0].epoch == 1);
    REQUIRE(samples[0].state.animation);
    CHECK(samples[0].state.animation->name == "Talk_Start");
    CHECK(samples[0].state.rotation);
}

// ORM "Dos jugadores con el mismo PNJ" (oot-remake docs/NPC_COMPARTIDOS.md):
// our talk on a copy another player owns and talks to; when it is over the
// copy blends back to the owner's pose instead of snapping.
TEST_CASE("objects: a copy our talk lets go blends back to its owner's pose over kRejoinUs") {
    Sync sync;
    Seen mido = Townsperson(0x100, 0x21, 5.0f);  // where our own talk has it
    Tick(sync, 0, {mido});
    const proto::ObjectId id = *sync.IdOf(0x100);
    Own(sync, id, 2);
    const auto owner_at = [&](std::uint64_t t) {
        sync.Receive(OwnerSample(id, 2, t * kMs, 1.0f, 2, "Talk"));  // the owner's Mido, at x 1
    };
    // We yielded: freed, not claimed.
    sync.SetTalk({}, {0x100}, 1000 * kMs);
    owner_at(1000);
    const auto step = Tick(sync, 1010 * kMs, {mido});
    const std::uint8_t flags = PresentFlags(step, id);  // 0xff: unchanged, not sent again
    CHECK((flags == 0xff || (flags & proto::object_presence::targeting) == 0));
    std::vector<CoopObjects::PoseWrite> poses;
    sync.Poses(1100 * kMs, poses);
    CHECK(poses.empty());

    // Our talk is over at 2000 ms (the owner's goes on): from x 5 to x 1.
    owner_at(1900);
    Tick(sync, 1950 * kMs, {mido});
    sync.SetTalk({}, {}, 2000 * kMs);
    CHECK_FALSE(sync.Freed(0x100));
    const auto x_at = [&](std::uint64_t t) {
        std::vector<CoopObjects::PoseWrite> out;
        sync.Poses(t * kMs, out);
        REQUIRE(out.size() == 1);
        return out[0].position[0];
    };
    CHECK(sync.RejoinWeight(0x100, 2000 * kMs) == std::optional<float>{0.0f});
    CHECK(x_at(2000) == doctest::Approx(5.0f));
    const float mid = x_at(2300);
    CHECK(mid == doctest::Approx(3.0f));  // half way (eased: 0.5 at the middle)
    CHECK(*sync.RejoinWeight(0x100, 2300 * kMs) == doctest::Approx(0.5f));
    CHECK(x_at(2100) > mid);  // a step at a time
    CHECK(x_at(2100) < 5.0f);
    CHECK(sync.Rejoining(2300 * kMs) == std::vector<std::uint64_t>{0x100});
    owner_at(2500);
    CHECK(x_at(2000 + Sync::kRejoinUs / kMs) == doctest::Approx(1.0f));  // the owner's
    CHECK_FALSE(sync.RejoinWeight(0x100, 2000 * kMs + Sync::kRejoinUs));
    CHECK(sync.Rejoining(2000 * kMs + Sync::kRejoinUs).empty());

    // Talked to again mid-blend: freed again, the blend is dropped.
    sync.SetTalk({}, {}, 3000 * kMs);  // (nothing to blend: it was not freed)
    CHECK_FALSE(sync.RejoinWeight(0x100, 3000 * kMs));
    sync.SetTalk({}, {0x100}, 3000 * kMs);
    sync.SetTalk({}, {}, 3100 * kMs);
    REQUIRE(sync.RejoinWeight(0x100, 3200 * kMs));
    sync.SetTalk({}, {0x100}, 3200 * kMs);
    CHECK_FALSE(sync.RejoinWeight(0x100, 3300 * kMs));
    // No time given (as before this rule): no blend, the owner's pose at once.
    sync.SetTalk({}, {});
    CHECK_FALSE(sync.RejoinWeight(0x100, 3300 * kMs));
}

TEST_CASE("objects: the blend back turns the copy from its own facing to the owner's") {
    Sync sync;
    Seen mido = Townsperson(0x100, 0x21, 1.0f);
    mido.rotation = CoopObjects::Mat3{0, 0, 1, 0, 1, 0, -1, 0, 0};  // our talk turned it 90 degrees
    Tick(sync, 0, {mido});
    const proto::ObjectId id = *sync.IdOf(0x100);
    Own(sync, id, 2);
    sync.SetTalk({}, {0x100}, 100 * kMs);
    proto::ObjectSample s = OwnerSample(id, 2, 1000 * kMs, 1.0f, 2, "Wait");
    s.state.rotation = proto::Quat{0, 0, 0, 1};  // the owner's faces ahead
    sync.Receive(s);
    Tick(sync, 1010 * kMs, {mido});
    sync.SetTalk({}, {}, 1050 * kMs);
    std::vector<CoopObjects::PoseWrite> out;
    sync.Poses(1050 * kMs + Sync::kRejoinUs / 2, out);
    REQUIRE(out.size() == 1);
    REQUIRE(out[0].rotation);
    const proto::Quat q = CoopObjects::QuatOf(*out[0].rotation);
    const proto::Quat own = CoopObjects::QuatOf(*mido.rotation);
    // Half way: 45 degrees about the vertical, between its own and the owner's.
    const float dot_own = std::fabs(q[0] * own[0] + q[1] * own[1] + q[2] * own[2] + q[3] * own[3]);
    const float dot_owner = std::fabs(q[3]);
    CHECK(dot_own == doctest::Approx(dot_owner).epsilon(1e-3));
    CHECK(dot_owner == doctest::Approx(std::cos(3.14159265f / 8.0f)).epsilon(1e-3));
}

// The others' statuses for the talk rule come from presence (presence_test,
// "a status goes with its player"); Sync keeps only their places.
TEST_CASE("objects: other players' place for the talk rule") {
    Sync sync;
    sync.Receive(proto::PlayerStatus{2, proto::status::in_cutscene});  // not Sync's
    proto::PlayerSample p;
    p.player_number = 2;
    p.time = {1000 * kMs, 1};
    p.position = proto::Vec3{3.0f, 0.0f, 4.0f};
    sync.Receive(p);
    REQUIRE(sync.PlaceOf(2, 1500 * kMs));
    CHECK((*sync.PlaceOf(2, 1500 * kMs))[2] == doctest::Approx(4.0f));
    CHECK_FALSE(sync.PlaceOf(2, 1000 * kMs + Sync::kPlaceStaleUs));  // too old
    CHECK_FALSE(sync.PlaceOf(3, 1500 * kMs));
}

// ORM steps 5 to 7 (LICHII_COOP_NPC_POSE, oot-remake docs/NPC_COMPARTIDOS.md):
// townspeople copies held, their partial slots, and where an OBJECT_POSE goes.
namespace {

// Mido (a copy of player 2's) and a Stalchild (player 2's too), both placed by
// the owner's samples at 1000 ms.
struct NpcPoseScene {
    Sync sync;
    std::vector<Seen> seen{Townsperson(0x100, 0x21, 1.0f), Enemy(0x200, 0x22, 4, 3.0f)};
    proto::ObjectId mido{};
    proto::ObjectId enemy{};
    NpcPoseScene() {
        Tick(sync, 0, seen);
        mido = *sync.IdOf(0x100);
        enemy = *sync.IdOf(0x200);
        Own(sync, mido, 2);
        Own(sync, enemy, 2);
        sync.Receive(OwnerSample(mido, 2, 1000 * kMs, 1.0f, 2, "Act_Arrogant"));
        sync.Receive(OwnerSample(enemy, 2, 1000 * kMs, 3.0f, 4, "Wait"));
        Tick(sync, 1010 * kMs, seen);
    }
};

}  // namespace

TEST_CASE("objects: the townspeople pose holds townspeople copies' AI too (ORM step 5)") {
    NpcPoseScene s;
    // Off (the default): only the enemy copy, as before.
    CHECK(s.sync.Holds(1100 * kMs) == std::vector<std::uint64_t>{0x200});
    s.sync.SetNpcPose(true);
    CHECK(s.sync.Holds(1100 * kMs) == std::vector<std::uint64_t>{0x100, 0x200});
    // LICHII_COOP_OBJECTS_AI_HOLD unset: the townspeople only.
    s.sync.SetHoldEnemies(false);
    CHECK(s.sync.Holds(1100 * kMs) == std::vector<std::uint64_t>{0x100});
    // Our player talks to Mido: our game moves it, nothing holds it.
    s.sync.SetTalk({0x100}, {0x100});
    CHECK(s.sync.Holds(1100 * kMs).empty());
    s.sync.SetTalk({}, {});
    CHECK(s.sync.Holds(1100 * kMs) == std::vector<std::uint64_t>{0x100});
    // The owner fell silent: let go, as an enemy copy.
    CHECK(s.sync.Holds(1000 * kMs + Sync::kPoseStaleUs + kMs).empty());
    // Ours now: its own AI runs.
    Own(s.sync, s.mido, 1, 1);
    Tick(s.sync, 1150 * kMs, s.seen);
    CHECK(s.sync.Holds(1160 * kMs).empty());
}

TEST_CASE("objects: townspeople copies' plays fill their partial slots with the pose on (ORM step 6)") {
    NpcPoseScene s;
    std::vector<CoopObjects::PlayWish> plays;
    s.sync.Plays(1100 * kMs, plays);
    REQUIRE(plays.size() == 2);
    CHECK_FALSE(plays[0].partial);
    CHECK_FALSE(plays[1].partial);
    s.sync.SetNpcPose(true);
    plays.clear();
    s.sync.Plays(1100 * kMs, plays);
    REQUIRE(plays.size() == 2);
    CHECK(plays[0].address == 0x100);
    CHECK(plays[0].partial);         // Mido's face and upper body follow slot 0
    CHECK_FALSE(plays[1].partial);   // the enemy plays as before
}

TEST_CASE("objects: an OBJECT_POSE goes onto our copy of its owner's townsperson only (ORM step 7)") {
    NpcPoseScene s;
    CHECK(s.sync.CopyFor(s.mido, 2, 0) == std::optional<std::uint64_t>{0x100});
    CHECK(s.sync.EpochOf(s.mido) == std::optional<std::uint16_t>{0});
    CHECK_FALSE(s.sync.CopyFor(s.mido, 3, 0));   // not its owner
    CHECK_FALSE(s.sync.CopyFor(s.mido, 2, 1));   // another epoch
    CHECK_FALSE(s.sync.CopyFor(s.mido, 0, 0));
    CHECK_FALSE(s.sync.CopyFor(s.enemy, 2, 0));  // an enemy: not a townsperson
    CHECK_FALSE(s.sync.CopyFor(proto::ObjectId{proto::ObjectKind::placed, 0, 0x99}, 2, 0));
    // Our own talk freed it: our game shows it.
    s.sync.SetTalk({0x100}, {0x100});
    CHECK_FALSE(s.sync.CopyFor(s.mido, 2, 0));
    s.sync.SetTalk({}, {});
    // Ours: our own game's pose is the one, nothing is written on it.
    Own(s.sync, s.mido, 1, 1);
    Tick(s.sync, 1150 * kMs, s.seen);
    CHECK(s.sync.EpochOf(s.mido) == std::optional<std::uint16_t>{1});
    CHECK_FALSE(s.sync.CopyFor(s.mido, 1, 1));
}

// ORM cofre (coop/host/chest_open.hpp): a chest whose lid our door mirror
// opened for another player's CHEST_OPEN shows open here when its key comes:
// it stays, beside our Link or away from it, and is never removed.
TEST_CASE("objects: a chest whose lid opened for a CHEST_OPEN stays when its key comes (cofre)") {
    for (const bool beside : {true, false}) {
        Sync sync;
        sync.SetPlayer(beside ? proto::Vec3{1.5f, 0.0f, 1.0f} : proto::Vec3{20.0f, 0.0f, 1.0f});
        Seen chest = Chest(0x500);
        chest.animation = proto::Animation{"CloseWait", 0.0f, 1.0f};
        Tick(sync, 0, {chest, Enemy(0x100, 0x11, 2)});
        // The lid plays (ours, for the opener), seconds before his find writes the key.
        chest.animation = proto::Animation{"FrontOpenSmall", 0.0f, 1.0f};
        sync.SetLidsShown({0x500});
        Tick(sync, 500 * kMs, {chest, Enemy(0x100, 0x11, 2)});
        chest.animation = proto::Animation{"OpenWait", 0.0f, 1.0f};
        Tick(sync, 4000 * kMs, {chest, Enemy(0x100, 0x11, 2)});
        sync.Take(chest.hash);
        Tick(sync, 4010 * kMs, {chest, Enemy(0x100, 0x11, 2)});
        Tick(sync, 4020 * kMs, {chest, Enemy(0x100, 0x11, 2)});
        Tick(sync, 9000 * kMs, {chest, Enemy(0x100, 0x11, 2)});
        CHECK(sync.Removes().empty());
        bool said = false;
        for (const auto& n : sync.TakeNotes()) {
            said |= n.find("its lid opened here with it (CHEST_OPEN)") != std::string::npos;
            CHECK(n.find("at once") == std::string::npos);
        }
        CHECK(said);
    }
    // Without the lid, the same chest beside our Link is not removed either since test10: issue
    // 195 plays its own open idle on it (removal only if that play does not take, after 3 s).
    Sync sync;
    sync.SetPlayer(proto::Vec3{1.5f, 0.0f, 1.0f});
    Seen chest = Chest(0x500);
    chest.animation = proto::Animation{"CloseWait", 0.0f, 1.0f};
    Tick(sync, 0, {chest, Enemy(0x100, 0x11, 2)});
    sync.SetLidsShown({0x777});  // another chest's
    sync.Take(chest.hash);
    Tick(sync, 10 * kMs, {chest, Enemy(0x100, 0x11, 2)});
    Tick(sync, 20 * kMs, {chest, Enemy(0x100, 0x11, 2)});
    CHECK(sync.Removes().empty());
    bool shown = false;
    for (const auto& n : sync.TakeNotes()) {
        shown |= n.find("shown open here") != std::string::npos;
        CHECK(n.find("its lid opened here") == std::string::npos);  // not the CHEST_OPEN path
    }
    CHECK(shown);
}
