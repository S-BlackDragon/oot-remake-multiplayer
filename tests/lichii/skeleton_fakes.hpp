// Fake skeletons and equipment for the tests of coop_mascaras (ORM): an
// actor's model chain as CoopPose walks it, its local transforms, and the
// relocated FSKL with the bones' names and parents. Works with any of the
// tests' fake guests (Map, Set, Get).
#pragma once

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "coop_mascaras.h"
#include "coop_pose.h"
#include "coop_puppet.h"

namespace SkeletonFakes {

struct Bone {
    std::string name;
    std::int16_t parent = -1;
    CoopMascaras::Local local{};
};

inline Bone B(std::string name, std::int16_t parent, CoopMascaras::Vec3 t = {}) {
    Bone b;
    b.name = std::move(name);
    b.parent = parent;
    b.local.translation[0] = t[0];
    b.local.translation[1] = t[1];
    b.local.translation[2] = t[2];
    return b;
}

// The space one skeleton takes from `base`.
inline constexpr std::uint64_t kSpan = 0x20000;

// `actor`'s skeleton laid out in [base, base + kSpan): returns its local
// transform array. `actor` must be mapped (its +0x270 is set: the parts,
// reused when already set).
template <class G>
std::uint64_t MakeSkeleton(G& g, std::uint64_t actor, std::uint64_t base, std::string_view model,
                           const std::vector<Bone>& bones) {
    namespace pa = CoopPose::addr;
    namespace ma = CoopMascaras::addr;
    g.Map(base, kSpan);
    std::uint64_t parts = g.template Get<std::uint64_t>(actor + pa::kSkeletonPath[0]);
    if (parts == 0) {
        parts = base;
        g.Set(actor + pa::kSkeletonPath[0], parts);
    }
    const std::uint64_t first = base + 0x1000, second = base + 0x2000, holder = base + 0x3000,
                        object = base + 0x4000, fmdl = base + 0x5000, fskl = base + 0x6000,
                        local = base + 0x10000;
    g.Map(parts, 0x400);
    g.Set(parts + pa::kSkeletonPath[1], first);
    g.Set(first + pa::kSkeletonPath[2], second);
    g.Set(second + pa::kSkeletonPath[3], holder);
    g.Set(holder + pa::kSkeletonPath[4], object);
    g.Set(holder + pa::kModelResource, fmdl);
    g.Set(fmdl, std::array<char, 4>{'F', 'M', 'D', 'L'});
    const std::uint64_t name = fmdl + 0x100;
    g.Set(fmdl + pa::kModelName, name);
    g.Set(name, static_cast<std::uint16_t>(model.size()));
    for (std::size_t i = 0; i < model.size(); ++i) {
        g.Set(name + 2 + i, model[i]);
    }
    g.Set(object + ma::kSkeletonResource, fskl);
    g.Set(object + pa::kBoneCount, static_cast<std::uint16_t>(bones.size()));
    g.Set(object + pa::kLocalArray, local);
    g.Set(fskl, std::array<char, 4>{'F', 'S', 'K', 'L'});
    const std::uint64_t array = fskl + 0x100;
    std::uint64_t text = fskl + 0x100 + bones.size() * ma::kBoneStride + 0x10;
    g.Set(fskl + ma::kFsklBones, array);
    g.Set(fskl + ma::kFsklBoneCount, static_cast<std::uint16_t>(bones.size()));
    for (std::size_t i = 0; i < bones.size(); ++i) {
        const std::uint64_t b = array + i * ma::kBoneStride;
        g.Set(b + ma::kBoneName, text);
        g.Set(b + ma::kBoneParent, bones[i].parent);
        g.Set(text, static_cast<std::uint16_t>(bones[i].name.size()));
        for (std::size_t c = 0; c < bones[i].name.size(); ++c) {
            g.Set(text + 2 + c, bones[i].name[c]);
        }
        text += 2 + bones[i].name.size() + 2;
        g.Set(local + i * sizeof(CoopMascaras::Local), bones[i].local);
    }
    return local;
}

// One bone's local transform of a skeleton made above.
template <class G>
CoopMascaras::Local LocalOf(G& g, std::uint64_t local, std::size_t bone) {
    return g.template Get<CoopMascaras::Local>(local + bone * sizeof(CoopMascaras::Local));
}

// An actor present (state 4) at `at`, turned by `rot` (rows, column vectors).
template <class G>
void MakeActor(G& g, std::uint64_t actor, CoopMascaras::Vec3 at,
               CoopMascaras::Mat3 rot = CoopMascaras::kIdentity) {
    namespace pa = CoopPuppet::addr;
    g.Map(actor, 0x400);
    g.Set(actor + pa::kActorState, std::uint32_t{pa::kStatePresent});
    g.Set(actor + pa::kActorPosition, at);
    g.Set(actor + pa::kActorRotation, rot);
}

// `wearer`'s equipment user at `eu` and `piece` in its link at `link_offset`
// (ArmorLink, HandSlot) with id `id`, through a holder at `holder`.
template <class G>
void Link(G& g, std::uint64_t wearer, std::uint64_t eu, std::uint64_t link_offset,
          std::uint64_t holder, std::uint64_t piece, std::uint32_t id) {
    namespace pa = CoopPuppet::addr;
    const std::uint64_t parts = g.template Get<std::uint64_t>(wearer + pa::kActorParts);
    REQUIRE(parts != 0);
    g.Map(eu, 0x2000);
    g.Map(holder, 0x100);
    g.Set(parts + pa::kPartsEquipmentUser, eu);
    g.Set(eu + link_offset + pa::kEquipSlotId, id);
    g.Set(eu + link_offset + pa::kEquipSlotHolder, holder);
    g.Set(holder + pa::kHolderActor, piece);
}

inline std::uint64_t ArmorLink(std::uint32_t index) {
    return CoopPuppet::addr::kArmorLinks + CoopPuppet::addr::kEquipSlotStride * index;
}
inline std::uint64_t HandSlot(std::uint32_t slot) {
    return CoopPuppet::addr::kEquipSlots + CoopPuppet::addr::kEquipSlotStride * slot;
}

}  // namespace SkeletonFakes
