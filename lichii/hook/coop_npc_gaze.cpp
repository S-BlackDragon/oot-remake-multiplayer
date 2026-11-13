// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM "PNJ que mira a la marioneta": the rules and the slot writes (coop_npc_gaze.h).

#include "coop_npc_gaze.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <set>
#include <utility>

namespace CoopNpcGaze {
namespace {

constexpr std::uint32_t kNoId = 0xffffffffu;
// A place read back within this of what was written is ours (floats written
// and read as they are; the margin only absorbs nothing at all).
constexpr float kSameM = 1e-3f;

template <typename T>
std::optional<T> Get(CoopPuppet::Guest& guest, std::uint64_t address) {
    T value{};
    if (address == 0 || !guest.Read(address, &value, sizeof(T))) {
        return std::nullopt;
    }
    return value;
}

std::optional<Vec3> GetVec(CoopPuppet::Guest& guest, std::uint64_t address) {
    Vec3 v{};
    if (address == 0 || !guest.Read(address, v.data(), sizeof(float) * 3)) {
        return std::nullopt;
    }
    for (const float f : v) {
        if (!std::isfinite(f)) {
            return std::nullopt;
        }
    }
    return v;
}

float Distance(const Vec3& a, const Vec3& b) {
    const float dx = a[0] - b[0];
    const float dy = a[1] - b[1];
    const float dz = a[2] - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

bool Same(const Vec3& a, const Vec3& b) {
    return std::fabs(a[0] - b[0]) <= kSameM && std::fabs(a[1] - b[1]) <= kSameM &&
           std::fabs(a[2] - b[2]) <= kSameM;
}

std::string P(std::uint8_t player) {
    return player == 0 ? std::string("our Link") : "P" + std::to_string(player);
}

std::string Hex(std::uint64_t v) {
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%#llx", static_cast<unsigned long long>(v));
    return buf;
}

std::string M(float metres) {
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%.1f", metres);
    return buf;
}

} // namespace

std::optional<std::uint64_t> TargetComponent(CoopPuppet::Guest& guest, std::uint64_t actor) {
    const auto table = Get<std::uint64_t>(guest, actor == 0 ? 0 : actor + kComponentTable);
    if (!table || *table == 0) {
        return std::nullopt;
    }
    const auto component = Get<std::uint64_t>(guest, *table + kTargetComponent);
    if (!component || *component == 0) {
        return std::nullopt;
    }
    return component;
}

std::vector<Slot> ReadSlots(CoopPuppet::Guest& guest, std::uint64_t actor) {
    std::vector<Slot> out;
    const auto component = TargetComponent(guest, actor);
    if (!component) {
        return out;
    }
    for (unsigned i = 0; i < kSlotCount; ++i) {
        Slot s;
        s.index = i;
        s.at = *component + kSlotsAt + kSlotSize * i;
        const auto kind = Get<std::uint8_t>(guest, s.at + kSlotKind);
        if (!kind) {
            break;  // past the end of what is mapped: no more slots
        }
        if (*kind == kKindNone) {
            continue;
        }
        s.kind = *kind;
        s.flags = Get<std::uint8_t>(guest, s.at + kSlotFlags).value_or(0);
        s.key = Get<std::uint32_t>(guest, s.at + kSlotActorKey).value_or(kNoId);
        s.link_data = Get<std::uint64_t>(guest, s.at + kSlotLinkData).value_or(0);
        s.link_id = Get<std::uint32_t>(guest, s.at + kSlotLinkId).value_or(kNoId);
        if (s.link_data != 0 && s.link_id != kNoId &&
            Get<std::uint32_t>(guest, s.link_data + kLinkDataId) == s.link_id) {
            s.link_actor = Get<std::uint64_t>(guest, s.link_data + kLinkDataActor);
        }
        s.place = GetVec(guest, s.at + kSlotPlace).value_or(Vec3{});
        out.push_back(s);
    }
    return out;
}

const char* ToString(Names names) {
    switch (names) {
    case Names::no:
        return "no";
    case Names::by_link:
        return "by its link";
    case Names::by_key:
        return "by its key";
    case Names::both:
        return "by its link and its key";
    }
    return "?";
}

Names SlotNames(const Slot& slot, std::uint64_t actor, std::optional<std::uint32_t> key) {
    if (slot.kind != kKindActor || actor == 0) {
        return Names::no;
    }
    const bool link = slot.link_actor && *slot.link_actor == actor;
    const bool by_key = key && *key != 0 && *key != kNoId && slot.key == *key;
    return link && by_key ? Names::both : link ? Names::by_link : by_key ? Names::by_key : Names::no;
}

const char* ToString(Why why) {
    switch (why) {
    case Why::none:
        return "none";
    case Why::talks:
        return "talks to it";
    case Why::nearest:
        return "nearest";
    }
    return "?";
}

// ---- Rule ----

std::uint8_t Rule::LookOf(std::uint64_t npc) const {
    const auto it = state_.find(npc);
    return it == state_.end() ? 0 : it->second.player;
}

std::vector<Order> Rule::Plan(const std::vector<Npc>& npcs, const std::optional<Vec3>& self,
                              const std::vector<Partner>& partners, std::uint64_t now_us,
                              std::vector<std::string>& log) {
    constexpr float kFar = std::numeric_limits<float>::infinity();
    std::map<std::uint64_t, State> next;
    struct Turned {
        float from_self = 0.0f;
        Order order;
    };
    std::vector<Turned> turned;
    for (const Npc& n : npcs) {
        if (n.actor == 0) {
            continue;
        }
        const auto old = state_.find(n.actor);
        State was = old != state_.end() ? old->second : State{};
        State now = was;
        const float d_self = self ? Distance(*self, n.at) : kFar;
        const auto partner_at = [&](std::uint8_t player) -> std::optional<Vec3> {
            for (const Partner& p : partners) {
                if (p.player == player) {
                    return p.at;
                }
            }
            return std::nullopt;
        };
        if (!self || n.ours_talk) {
            now = {0, Why::none, now_us};  // our talk, or our Link not known: the game's own
        } else if (n.talked_by != 0 && partner_at(n.talked_by)) {
            now = {n.talked_by, Why::talks, was.player == n.talked_by && was.why == Why::talks ? was.since : now_us};
        } else {
            // By distance. The one looked at now, and how near it is.
            const auto current_at = was.player == 0 ? self : partner_at(was.player);
            const bool current_gone = was.player != 0 && !current_at;
            const float d_current = current_at ? Distance(*current_at, n.at) : kFar;
            // The nearest partner.
            std::uint8_t best = 0;
            float d_best = kFar;
            for (const Partner& p : partners) {
                const float d = Distance(p.at, n.at);
                if (p.player != 0 && (d < d_best || (d == d_best && p.player < best))) {
                    best = p.player;
                    d_best = d;
                }
            }
            const bool held = was.why == Why::nearest && now_us - was.since < s_.hold_us;
            std::uint8_t want = was.why == Why::talks ? 0 : was.player;  // a talk that ended: the distances decide
            if (was.why == Why::talks || current_gone || d_current > s_.look_m + s_.margin_m) {
                // Free to choose: the nearest partner when within reach and nearer than our Link.
                want = best != 0 && d_best <= s_.look_m && d_best + s_.margin_m < d_self ? best : 0;
            } else if (!held) {
                // Taken from the one looked at only by one nearer by the margin.
                if (was.player == 0) {
                    if (best != 0 && d_best <= s_.look_m && d_best + s_.margin_m < d_self) {
                        want = best;
                    }
                } else if (d_self + s_.margin_m < d_current && d_self <= d_best) {
                    want = 0;
                } else if (best != was.player && d_best + s_.margin_m < d_current) {
                    want = best;
                }
            }
            now = want == was.player && was.why != Why::talks ? was
                  : want == 0                                 ? State{0, Why::none, now_us}
                                                              : State{want, Why::nearest, now_us};
        }
        if (now.player != was.player || now.why != was.why) {
            if (now.player != 0) {
                const auto at = partner_at(now.player);
                log.push_back("npc gaze: " + n.name + " " + Hex(n.actor) + " looks at " + P(now.player) + " (" +
                              ToString(now.why) + (at ? ", " + M(Distance(*at, n.at)) + " m" : std::string()) +
                              (self ? "; our Link " + M(d_self) + " m" : std::string()) + ")");
            } else if (was.player != 0) {
                log.push_back("npc gaze: " + n.name + " " + Hex(n.actor) + " looks as the game has it again (" +
                              (n.ours_talk ? std::string("our talk")
                               : !self     ? std::string("our Link not known")
                                           : "our Link " + M(d_self) + " m") +
                              ")");
            }
        }
        if (now.player != 0 || now.why != Why::none) {
            next[n.actor] = now;
        }
        if (now.player != 0) {
            turned.push_back({d_self, {n.actor, n.name, now.player}});
        }
    }
    state_ = std::move(next);
    std::stable_sort(turned.begin(), turned.end(),
                     [](const Turned& a, const Turned& b) { return a.from_self < b.from_self; });
    std::vector<Order> orders;
    for (const Turned& t : turned) {
        if (orders.size() >= s_.max_npcs) {
            break;
        }
        orders.push_back(t.order);
    }
    return orders;
}

// ---- Writer ----

void Writer::Say(std::string line) {
    if (log_.size() < 256) {
        log_.push_back(std::move(line));
    }
}

void Writer::SetOrders(std::vector<Order> orders, std::uint64_t link_actor) {
    std::lock_guard lock(mutex_);
    orders_ = std::move(orders);
    link_ = link_actor;
}

std::vector<std::string> Writer::TakeLog() {
    std::lock_guard lock(mutex_);
    return std::exchange(log_, {});
}

Counts Writer::TakeCounts() {
    std::lock_guard lock(mutex_);
    return std::exchange(counts_, {});
}

bool Writer::fell_back() const {
    std::lock_guard lock(mutex_);
    return fell_back_;
}

void Writer::Reset() {
    std::lock_guard lock(mutex_);
    held_.clear();
    orders_.clear();
    named_.clear();
    link_ = 0;
}

bool Writer::StillOurs(CoopPuppet::Guest& guest, const Held& h, std::uint64_t slot) const {
    const auto kind = Get<std::uint8_t>(guest, slot + kSlotKind);
    const auto place = GetVec(guest, slot + kSlotPlace);
    const auto data = Get<std::uint64_t>(guest, slot + kSlotLinkData);
    const auto id = Get<std::uint32_t>(guest, slot + kSlotLinkId);
    // Our place in a place slot, on the link we found (a re-target clears and
    // sets the link again, f_808b1ca0 / f_808aefe0).
    return kind == kKindPlace && place && Same(*place, h.wrote) && data == h.link_data && id == h.link_id;
}

void Writer::GiveBack(CoopPuppet::Guest& guest, const Held& h) {
    const auto component = TargetComponent(guest, h.npc);
    if (!component) {
        return;
    }
    const std::uint64_t slot = *component + kSlotsAt + kSlotSize * h.index;
    if (!StillOurs(guest, h, slot)) {
        return;  // the game set it since: its own now
    }
    const auto flags = Get<std::uint8_t>(guest, slot + kSlotFlags);
    if (flags) {
        const std::uint8_t restored = static_cast<std::uint8_t>((*flags & ~kPlaceFlags) | (h.flags & kPlaceFlags));
        guest.Write(slot + kSlotFlags, &restored, 1);
    }
    guest.Write(slot + kSlotKind, &h.kind, 1);
    ++counts_.given_back;
}

void Writer::OnCalcFrame(CoopPuppet::Guest& guest, const std::vector<PuppetAt>& puppets, const Alive& alive) {
    std::lock_guard lock(mutex_);
    const auto live = [&](std::uint64_t a) { return a != 0 && (!alive || alive(guest, a)); };
    const auto puppet_of = [&](std::uint8_t player) -> const PuppetAt* {
        for (const PuppetAt& p : puppets) {
            if (p.player == player && live(p.actor)) {
                return &p;
            }
        }
        return nullptr;
    };
    const auto order_of = [&](std::uint64_t npc) -> const Order* {
        for (const Order& o : orders_) {
            if (o.npc == npc) {
                return &o;
            }
        }
        return nullptr;
    };

    // 1. The slots written last frame: still as written (the check), or the
    //    game's again; and those no longer wanted given back.
    bool wrote_last = false;
    std::vector<Held> keep;
    for (const Held& h : held_) {
        if (!live(h.npc)) {
            continue;  // the townsperson is gone: nothing to read or give back
        }
        const auto component = TargetComponent(guest, h.npc);
        const std::uint64_t slot = component ? *component + kSlotsAt + kSlotSize * h.index : 0;
        if (slot == 0 || !StillOurs(guest, h, slot)) {
            ++counts_.overwritten;
            ++judge_over_;
            wrote_last = true;
            continue;  // the game's own now; not written back
        }
        ++counts_.kept;
        ++judge_kept_;
        wrote_last = true;
        const Order* o = order_of(h.npc);
        if (fell_back_ || link_ == 0 || o == nullptr || o->player != h.player || puppet_of(h.player) == nullptr) {
            GiveBack(guest, h);
            continue;
        }
        keep.push_back(h);
    }
    held_ = std::move(keep);
    if (wrote_last && !judged_) {
        ++judged_frames_;
        if (judged_frames_ >= s_.judge_frames) {
            judged_ = true;
            const unsigned total = judge_kept_ + judge_over_;
            const bool ok = total > 0 && static_cast<float>(judge_kept_) >= s_.judge_kept * static_cast<float>(total);
            if (ok) {
                Say("check: npc gaze ok (" + std::to_string(judge_kept_) + " of " + std::to_string(total) +
                    " slot-frames kept as written over " + std::to_string(judged_frames_) + " frames)");
            } else {
                Say("check: npc gaze fallback (the game set the slots again in " + std::to_string(judge_over_) +
                    " of " + std::to_string(total) + " slot-frames): the townspeople look at our Link as before");
                fell_back_ = true;
                for (const Held& h : held_) {
                    GiveBack(guest, h);
                }
                held_.clear();
            }
        }
    }
    if (fell_back_ || link_ == 0) {
        return;
    }
    const auto link_key = Get<std::uint32_t>(guest, link_ + kActorKey);

    // 2. Each order: its townsperson's slots that link our Link, at the puppet's eyes.
    for (const Order& o : orders_) {
        const PuppetAt* puppet = puppet_of(o.player);
        if (puppet == nullptr || !live(o.npc)) {
            continue;
        }
        const auto feet = GetVec(guest, puppet->actor + kActorPosition);
        if (!feet) {
            continue;
        }
        const Vec3 eyes{(*feet)[0], (*feet)[1] + (puppet->adult ? s_.eye_adult_m : s_.eye_child_m), (*feet)[2]};
        const auto puppet_key = Get<std::uint32_t>(guest, puppet->actor + kActorKey);
        const std::vector<Slot> slots = ReadSlots(guest, o.npc);
        bool any = false;
        std::string seen;
        for (const Slot& s : slots) {
            seen += (seen.empty() ? "" : ", ") + std::to_string(s.index) + ":" + std::to_string(s.kind);
            const auto held = std::find_if(held_.begin(), held_.end(),
                                           [&](const Held& h) { return h.npc == o.npc && h.index == s.index; });
            if (held != held_.end()) {
                // Ours since last frame: the puppet's eyes again (the puppet moves).
                if (guest.Write(s.at + kSlotPlace, eyes.data(), sizeof(float) * 3)) {
                    held->wrote = eyes;
                    ++counts_.written;
                }
                any = true;
                continue;
            }
            const Names names = SlotNames(s, link_, link_key);
            if (names == Names::no) {
                continue;
            }
            if (SlotNames(s, puppet->actor, puppet_key) != Names::no) {
                ++counts_.ambiguous;  // a key or link that is the puppet's too: not ours to tell apart
                continue;
            }
            if (named_.emplace(o.npc, true).second) {
                Say("npc gaze: " + o.name + " " + Hex(o.npc) + " slot " + std::to_string(s.index) +
                    " links our Link (" + ToString(names) + "): turned to " + P(o.player) + "'s puppet");
            }
            Held h;
            h.npc = o.npc;
            h.index = s.index;
            h.kind = s.kind;
            h.flags = s.flags;
            h.link_data = s.link_data;
            h.link_id = s.link_id;
            h.player = o.player;
            h.wrote = eyes;
            const std::uint8_t flags = static_cast<std::uint8_t>(s.flags & ~kPlaceFlags);
            const std::uint8_t place = kKindPlace;
            // The point first, then the flags, the kind last: a reader between
            // two writes sees a link slot, or a place slot with our point.
            if (guest.Write(s.at + kSlotPlace, eyes.data(), sizeof(float) * 3) &&
                guest.Write(s.at + kSlotFlags, &flags, 1) && guest.Write(s.at + kSlotKind, &place, 1)) {
                held_.push_back(h);
                ++counts_.written;
                any = true;
            }
        }
        if (!any) {
            ++counts_.no_slot;
            if (named_.emplace(o.npc, false).second) {
                Say("npc gaze: " + o.name + " " + Hex(o.npc) + ": no slot links our Link (slots in use: " +
                    (seen.empty() ? std::string("none") : seen) + "; kind 1 place, 2 actor)");
            }
        }
    }
}

Writer& TheWriter() {
    static Writer writer;
    return writer;
}

} // namespace CoopNpcGaze
