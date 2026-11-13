// Tests for the spectator camera's portable part (lichii/hook/coop_camera_aim.cpp).
#include <doctest/doctest.h>

#include <ostream>  // doctest prints std::string_view results

#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <vector>

#include "coop_camera_aim.h"

namespace aim = CoopCameraAim;

namespace {

// Paged guest memory: only mapped pages read.
class PageGuest : public CoopPuppet::Guest {
public:
    void Map(std::uint64_t address, std::size_t size) {
        for (std::uint64_t p = address & ~0xfffULL; p < address + size; p += 0x1000) {
            pages_[p];
        }
    }
    bool Read(std::uint64_t address, void* out, std::size_t size) override {
        auto* o = static_cast<std::uint8_t*>(out);
        for (std::size_t i = 0; i < size; ++i) {
            const auto it = pages_.find((address + i) & ~0xfffULL);
            if (it == pages_.end()) return false;
            o[i] = it->second[(address + i) & 0xfff];
        }
        return true;
    }
    bool Write(std::uint64_t address, const void* in, std::size_t size) override {
        const auto* p = static_cast<const std::uint8_t*>(in);
        for (std::size_t i = 0; i < size; ++i) {
            const auto it = pages_.find((address + i) & ~0xfffULL);
            if (it == pages_.end()) return false;
            it->second[(address + i) & 0xfff] = p[i];
        }
        return true;
    }
    template <typename T>
    void Put(std::uint64_t at, T v) {
        Map(at, sizeof(T));
        REQUIRE(Write(at, &v, sizeof(T)));
    }

private:
    std::map<std::uint64_t, std::array<std::uint8_t, 0x1000>> pages_;
};

// A right-handed look-at view (camera looks down its -z) times a GL-style
// perspective (w = -z_view), row-major: clip = m * p.
aim::Mat4 LookAt(const aim::Vec3& eye, const aim::Vec3& at) {
    double f[3] = {at[0] - eye[0], at[1] - eye[1], at[2] - eye[2]};
    const double fl = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    for (double& v : f) v /= fl;
    const double up[3] = {0, 1, 0};
    double s[3] = {f[1] * up[2] - f[2] * up[1], f[2] * up[0] - f[0] * up[2], f[0] * up[1] - f[1] * up[0]};
    const double sl = std::sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
    for (double& v : s) v /= sl;
    const double u[3] = {s[1] * f[2] - s[2] * f[1], s[2] * f[0] - s[0] * f[2], s[0] * f[1] - s[1] * f[0]};
    double view[16] = {s[0], s[1], s[2], 0, u[0], u[1], u[2], 0, -f[0], -f[1], -f[2], 0, 0, 0, 0, 1};
    for (int r = 0; r < 3; ++r) {
        view[r * 4 + 3] = -(view[r * 4 + 0] * eye[0] + view[r * 4 + 1] * eye[1] + view[r * 4 + 2] * eye[2]);
    }
    const double t = 1.0 / std::tan(0.5), n = 0.1, fa = 1000.0, aspect = 16.0 / 9.0;
    const double proj[16] = {t / aspect, 0, 0, 0, 0, t, 0, 0, 0, 0, -(fa + n) / (fa - n), -2 * fa * n / (fa - n),
                             0, 0, -1, 0};
    aim::Mat4 m{};
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            double v = 0;
            for (int k = 0; k < 4; ++k) v += proj[r * 4 + k] * view[k * 4 + c];
            m[r * 4 + c] = static_cast<float>(v);
        }
    }
    return m;
}

}  // namespace

TEST_CASE("camera aim: the matrix's heading and pitch") {
    const auto ahead = aim::ViewOf(LookAt({0, 0, 0}, {0, 0, 5}));
    REQUIRE(ahead);
    CHECK(ahead->heading == doctest::Approx(0).epsilon(0.001));
    CHECK(ahead->pitch == doctest::Approx(0).epsilon(0.001));
    const auto east = aim::ViewOf(LookAt({1, 2, 3}, {6, 2, 3}));
    REQUIRE(east);
    CHECK(east->heading == doctest::Approx(90));
    const auto back = aim::ViewOf(LookAt({0, 0, 0}, {-1, -1, -1}));
    REQUIRE(back);
    CHECK(back->heading == doctest::Approx(-135));
    CHECK(back->pitch == doctest::Approx(-35.264).epsilon(0.001));
    CHECK_FALSE(aim::ViewOf(aim::Mat4{}));
}

TEST_CASE("camera aim: a point looked at lands in the middle, one behind is not drawn") {
    const aim::Vec3 eye{-80, 125, 30}, mate{-75, 122, 36};
    const auto m = LookAt(eye, mate);
    const auto mid = aim::Project(m, mate);
    REQUIRE(mid);
    CHECK(std::fabs(mid->x) < 1e-4);
    CHECK(std::fabs(mid->y) < 1e-4);
    CHECK(mid->w > 0);
    const auto right = aim::Project(LookAt(eye, {-75, 122, 30}), mate);  // looking down +x, mate to the right (+z)
    REQUIRE(right);
    CHECK(std::fabs(right->x) > 0.3f);
    CHECK_FALSE(aim::Project(m, {-85, 128, 24}));  // behind the eye
}

TEST_CASE("camera aim: heading from one point to another matches the matrix's") {
    const aim::Vec3 link{-82, 122, 33};
    for (const aim::Vec3 mate : {aim::Vec3{-77, 122, 33}, aim::Vec3{-87, 120, 33}, aim::Vec3{-82, 122, 40},
                                 aim::Vec3{-82, 130, 20}, aim::Vec3{-79, 122, 30}}) {
        const auto h = aim::HeadingTo(link, mate);
        REQUIRE(h);
        // A camera behind Link on that line, looking at Link, looks at the mate.
        const float r = *h * 3.14159265f / 180.0f;
        const aim::Vec3 eye{link[0] - 4 * std::sin(r), link[1] + 1.5f, link[2] - 4 * std::cos(r)};
        const auto v = aim::ViewOf(LookAt(eye, link));
        REQUIRE(v);
        CHECK(aim::Wrap(v->heading - *h) == doctest::Approx(0).epsilon(0.01));
        const auto ndc = aim::Project(LookAt(eye, link), mate);
        REQUIRE(ndc);
        CHECK(std::fabs(ndc->x) < 0.01f);
    }
    CHECK_FALSE(aim::HeadingTo(link, {-82, 140, 33.005f}));  // straight above: no heading
}

TEST_CASE("camera aim: wrap, yaw rule, nearest") {
    CHECK(aim::Wrap(190) == doctest::Approx(-170));
    CHECK(aim::Wrap(-190) == doctest::Approx(170));
    CHECK(aim::Wrap(180) == doctest::Approx(180));
    CHECK(aim::Wrap(-180) == doctest::Approx(180));
    CHECK(aim::Wrap(720 + 45) == doctest::Approx(45));
    CHECK(aim::YawFor(30, {1, 0}) == doctest::Approx(30));
    CHECK(aim::YawFor(30, {-1, 180}) == doctest::Approx(150));
    CHECK(aim::YawFor(-170, {1, -20}) == doctest::Approx(170));
    CHECK_FALSE(aim::Nearest({0, 0, 0}, {}));
    CHECK(*aim::Nearest({0, 0, 0}, {{5, 0, 0}, {0, 50, 2}, {-3, 0, -3}}) == 1);  // height does not count
}

TEST_CASE("camera aim: the walk finds the camera from its actor, and the way is followed again") {
    PageGuest g;
    const std::uint64_t actor = 0x2149dc0000, table = 0x2149e00000, comp = 0x2149e10000, cam = 0x2149e20000;
    const std::uint64_t ctx = 0x2149e30000, inner = 0x2149e40000, decoy = 0x2149e50000;
    g.Map(actor, 0x800);
    g.Map(table, 0x800);
    g.Map(comp, 0x800);
    g.Map(cam, 0x800);
    g.Map(decoy, 0x800);
    g.Put<std::uint64_t>(actor + 0x270, table);
    g.Put<std::uint64_t>(table + 0x358, decoy);  // a camera-like object of nobody, met first
    g.Put<float>(decoy + 0x178, -85.0f);
    g.Put<float>(decoy + 0x17c, 89.0f);
    g.Put<std::uint64_t>(table + 0x360, comp);
    g.Put<std::uint64_t>(comp + 0x18, cam);
    g.Put<float>(cam + 0x178, -85.0f);
    g.Put<float>(cam + 0x17c, 89.0f);
    g.Put<float>(cam + 0x1a4, -42.5f);
    g.Put<std::uint64_t>(cam + 0x08, ctx);
    g.Put<std::uint64_t>(ctx + 0x08, inner);
    g.Put<std::uint64_t>(inner + 0x1f0, actor);

    CHECK(aim::LooksLikeCamera(g, cam));
    CHECK(aim::LooksLikeCamera(g, decoy));
    CHECK_FALSE(aim::LooksLikeCamera(g, comp));
    CHECK(aim::ActorOfCamera(g, cam) == actor);
    CHECK(aim::ActorOfCamera(g, decoy) == 0);

    const auto any = aim::Walk(g, actor, 0x800, 4, 1000, false);
    REQUIRE(any);
    CHECK(any->cam == decoy);
    const auto f = aim::Walk(g, actor, 0x800, 4, 1000, true);
    REQUIRE(f);
    CHECK(f->cam == cam);
    CHECK(f->path.offsets == std::vector<std::uint32_t>{0x270, 0x360, 0x18});
    CHECK(f->path.Text() == "+0x270 -> +0x360 -> +0x18");
    CHECK(aim::Follow(g, actor, f->path) == cam);
    CHECK_FALSE(aim::Walk(g, actor, 0x800, 2, 1000, true));  // three pointers deep
    g.Put<std::uint64_t>(comp + 0x18, 0);
    CHECK(aim::Follow(g, actor, f->path) == 0);
    g.Put<float>(cam + 0x1a4, 400.0f);  // not an angle
    CHECK_FALSE(aim::LooksLikeCamera(g, cam));
}

TEST_CASE("camera aim: the search finds the pitch limits of 8-byte aligned objects only") {
    PageGuest g;
    const std::uint64_t base = 0x2140000000;
    g.Map(base, 0x20000);
    g.Map(base + 0x40000, 0x10000);  // a gap of unmapped blocks between
    const std::uint64_t a = base + 0x1000;
    g.Put<float>(a + 0x178, -85.0f);
    g.Put<float>(a + 0x17c, 89.0f);
    const std::uint64_t odd = base + 0x8004;  // the pair at a 4-aligned place: no camera there
    g.Put<float>(odd, -85.0f);
    g.Put<float>(odd + 4, 89.0f);
    const std::uint64_t c = base + 0x40000 + 0x200;
    g.Put<float>(c + 0x178, -85.0f);
    g.Put<float>(c + 0x17c, 89.0f);
    const auto hits = aim::SearchLimits(g, base, 0x50000, 10);
    REQUIRE(hits.size() == 2);
    CHECK(hits[0] == a);
    CHECK(hits[1] == c);
    CHECK(aim::SearchLimits(g, base, 0x50000, 1).size() == 1);
}
