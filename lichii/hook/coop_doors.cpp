// ORM (oot-remake docs/PUERTAS.md): see coop_doors.h.
#include "coop_doors.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <utility>

#include "coop/host/scene.hpp"
#include "coop/host/session.hpp"
#include "coop/net/client.hpp"
#include "coop_check_visual.h"

namespace CoopDoors {

namespace {

template <typename T>
bool ReadValue(CoopPuppet::Guest& guest, std::uint64_t address, T& out) {
    return guest.Read(address, &out, sizeof(T));
}

// The AS controller of an actor and its slot 0, as CoopPuppet::Director
// reads them (addr::kControllerPath, kSlots, kSlotName).
std::uint64_t Controller(CoopPuppet::Guest& guest, std::uint64_t actor) {
    std::uint64_t p = actor;
    for (const std::uint64_t offset : CoopPuppet::addr::kControllerPath) {
        if (p == 0 || !ReadValue(guest, p + offset, p)) {
            return 0;
        }
    }
    return p;
}

std::string Text(CoopPuppet::Guest& guest, std::uint64_t at) {
    std::array<char, 64> text{};
    if (at == 0 || !guest.Read(at, text.data(), text.size() - 1)) {
        return {};
    }
    return std::string(text.data(), strnlen(text.data(), text.size()));
}

std::string Hex(std::uint64_t v) {
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%#llx", static_cast<unsigned long long>(v));
    return buf;
}

// Whether the actor has an AS controller with a slot 0 to play on: what
// CoopSpawn's NextPlay checks before it calls the AS play on a copy.
bool HasSlot0(CoopPuppet::Guest& guest, std::uint64_t actor) {
    const std::uint64_t controller = Controller(guest, actor);
    std::int32_t slots = 0;
    std::uint64_t array = 0;
    std::uint64_t slot0 = 0;
    return controller != 0 && ReadValue(guest, controller + CoopPuppet::addr::kSlotCount, slots) &&
           slots >= 1 && ReadValue(guest, controller + CoopPuppet::addr::kSlots, array) && array != 0 &&
           ReadValue(guest, array, slot0) && slot0 != 0;
}

// The actor's pack name now ("" not read). ORM review (REVISION_FINAL "Visual"):
// a play queued or watched names the actor it was chosen for; by the frame it
// is made the game may have freed that actor (a room unloaded behind a door)
// and put another one at the same address, still in state 4.
std::string ActorName(CoopPuppet::Guest& guest, std::uint64_t actor) {
    std::uint64_t name_at = 0;
    if (!ReadValue(guest, actor + addr::kActorName, name_at)) {
        return {};
    }
    return Text(guest, name_at);
}

// Whether `actor` is present and still the one named `name` (no name kept: present only).
bool StillThe(CoopPuppet::Guest& guest, std::uint64_t actor, const std::string& name) {
    std::uint32_t state = 0;
    return ReadValue(guest, actor + addr::kActorState, state) && state == addr::kStatePresent &&
           (name.empty() || ActorName(guest, actor) == name);
}

}  // namespace

Input Plan(std::uint64_t main_base, const std::optional<std::string>& scene,
           const std::optional<Vec3>& own, const std::vector<CoopPuppet::Remote>& remotes) {
    Input in;
    in.main_base = main_base;
    in.scene = scene.value_or("");
    in.own = own;
    if (main_base == 0 || in.scene.empty()) {
        return in;
    }
    for (const auto& r : remotes) {
        if (r.number == 0 || !(r.fade > 0.0f) || !r.anim) {
            continue;
        }
        const std::optional<std::string_view> theirs =
            r.scene ? std::optional<std::string_view>(*r.scene) : std::nullopt;
        if (!coop::host::same_place(in.scene, theirs)) {
            continue;
        }
        coop::host::DoorMover m;
        m.number = r.number;
        m.position = r.position;
        m.command = r.anim->name;
        m.adult = r.adult;
        in.others.push_back(std::move(m));
    }
    return in;
}

Mirror::Mirror(Log log, bool verbose, coop::host::DoorRules rules)
    : log_(std::move(log)), verbose_(verbose), mirror_(rules) {}

void Mirror::SetInput(Input input) {
    std::lock_guard lock(input_mutex_);
    next_input_ = std::move(input);
}

void Mirror::Say(const std::string& line) const {
    if (log_) {
        log_(line);
    }
}

std::optional<CoopPuppet::Call> Mirror::Step(CoopPuppet::Guest& guest, bool after_call,
                                             std::uint64_t result, std::uint64_t now_ms) {
    if (after_call) {
        if (pending_ == Pending::malloc) {
            block_ = result;
            if (block_ == 0) {
                malloc_retry_ms_ = now_ms + kMallocRetryMs;
                Say("doors: the game gave no memory block; asked again in 5 s");
            }
        }
        const bool played = pending_ == Pending::play;
        pending_ = Pending::none;
        if (played && just_played_) {
            // ORM riesgos-visual: watch its slot 0 from now (CheckWatches).
            const std::uint64_t actor = just_played_->actor;
            std::erase_if(watches_, [actor](const Watch& w) { return w.play.actor == actor; });
            watches_.push_back(Watch{*just_played_, std::max<std::uint64_t>(now_ms, 1), 0});
        }
        just_played_.reset();
        // ORM cofre: a lid played for a CHEST_OPEN goes on from the opener's
        // frame (0x37b5d70(controller, slot 0, s0 = frame), as the puppet's).
        if (played && played_ && played_->frame) {
            const Play p = *played_;
            played_.reset();
            std::uint64_t controller = 0;
            if (StillThe(guest, p.actor, p.name) && (controller = Controller(guest, p.actor)) != 0) {
                CoopPuppet::Call c;
                c.function = input_.main_base + CoopPuppet::addr::kSetAsFrame;
                c.x[0] = controller;
                c.x[1] = 0;  // slot 0
                c.s0 = *p.frame;
                pending_ = Pending::frame;
                return c;
            }
        }
        played_.reset();
        return NextPlay(guest);
    }
    pending_ = Pending::none;
    played_.reset();
    just_played_.reset();
    {
        std::lock_guard lock(input_mutex_);
        input_ = next_input_;
    }
    if (input_.main_base != base_) {
        // A new game (or none), as CoopSpawn's director: the block, its names
        // and every door were the old game's; writing there now would land in
        // whatever the new game keeps at that address.
        base_ = input_.main_base;
        block_ = 0;
        malloc_retry_ms_ = 0;
        cells_.clear();
        queue_.clear();
        mirror_.reset();
        scene_.clear();
        walked_ms_ = 0;
        watches_.clear();
        lid_times_.clear();
    }
    if (input_.main_base == 0) {
        queue_.clear();
        return std::nullopt;
    }
    if (input_.scene != scene_) {
        scene_ = input_.scene;
        mirror_.reset();  // every door is another one now
        lids_.reset();
        queue_.clear();
        played_.reset();
        watches_.clear();  // the old place's doors are gone
        lid_times_.clear();
    }
    // ORM cofre: the others' CHEST_OPENs that came since the last frame.
    if (chest_lids_) {
        std::vector<coop::protocol::ChestOpen> in;
        {
            std::lock_guard lock(chest_mutex_);
            in.swap(chest_in_);
        }
        for (const auto& m : in) {
            lids_.receive(m, now_ms);
        }
    }
    if (input_.scene.empty()) {
        return std::nullopt;  // loading
    }
    CheckWatches(guest, now_ms);
    if (chest_lids_) {
        CheckReopens(now_ms);
    }
    mirror_.update(input_.own, input_.others, now_ms);
    coop::host::ChestLidInput lid_in;
    if (chest_lids_) {
        lid_in.scene = input_.scene;
        lid_in.own = input_.own;
        lid_in.own_command = input_.own_command;
        lid_in.server_now_us = input_.server_now_us;
        lid_in.self = input_.self;
        lids_.update(lid_in, now_ms);
    }
    const bool lids_want = chest_lids_ && lids_.wants_actors(now_ms);
    if ((mirror_.wants_actors(now_ms) || lids_want) &&
        now_ms - std::min(now_ms, walked_ms_) >= kWalkEveryMs) {
        if (block_ == 0) {
            if (now_ms < malloc_retry_ms_) {
                return std::nullopt;
            }
            CoopPuppet::Call c;
            c.function = input_.main_base + CoopPuppet::addr::kMalloc;
            c.x[0] = block::kSize;
            pending_ = Pending::malloc;
            return c;
        }
        walked_ms_ = now_ms;
        std::vector<coop::host::DoorActor> actors;
        if (Walk(guest, actors)) {
            ++walks_;
            const auto name_of = [&actors](std::uint64_t actor) {
                for (const auto& a : actors) {
                    if (a.id == actor) {
                        return a.name;
                    }
                }
                return std::string();
            };
            if (verbose_) {
                Say("doors: walk: " + std::to_string(actors.size()) + " doors or chests near, " +
                    std::to_string(mirror_.waiting()) + " waiting, " +
                    std::to_string(mirror_.opened()) + " opened, " +
                    std::to_string(lids_.waiting()) + " chest opens waiting");
            }
            // The lids first: a CHEST_OPEN names its chest and frame, where
            // the door rule only guesses the nearest one to the puppet.
            if (chest_lids_) {
                const auto out = lids_.decide(lid_in, actors, mirror_.opened_ids(), now_ms);
                for (const auto& line : out.lines) {
                    Say("chests: " + line);
                }
                for (const auto& p : out.plays) {
                    Say("chests: " + p.line);
                    if (p.actor != 0) {
                        ++lids_played_;
                        queue_.push_back(Play{p.actor, p.command, p.frame, name_of(p.actor), true, 0});
                        for (const auto& a : actors) {
                            if (a.id == p.actor) {
                                std::erase_if(lid_times_, [&](const LidTime& l) { return l.actor == p.actor; });
                                lid_times_.push_back(
                                    LidTime{a.id, a.name, a.position, std::max<std::uint64_t>(now_ms, 1), a.hash});
                            }
                        }
                    }
                }
                std::lock_guard lock(chest_mutex_);
                for (const auto& tie : out.ties) {
                    CoopVCheck::Ok("chest tie", tie);  // settled by player number, the same in both games
                }
                for (const std::uint64_t hash : out.second) {
                    if (second_finds_.size() < 32) {
                        second_finds_.push_back(hash);
                    }
                }
                for (const auto& m : out.send) {
                    if (chest_out_.size() < 32) {
                        chest_out_.push_back(m);
                    }
                }
            }
            for (auto& wish : mirror_.decide(actors, now_ms)) {
                if (wish.actor != 0 && chest_lids_ && lids_.lid_opened(wish.actor)) {
                    Say("doors: " + wish.line + "; left to its CHEST_OPEN, which opened it");
                    continue;
                }
                Say("doors: " + wish.line);
                if (wish.actor != 0) {
                    queue_.push_back(
                        Play{wish.actor, std::move(wish.command), std::nullopt, name_of(wish.actor), false, 0});
                }
            }
        } else if (verbose_) {
            Say("doors: the actor list changed under the walk; again next frame");
        }
    }
    if (chest_lids_) {
        std::lock_guard lock(chest_mutex_);
        lids_shown_ = lids_.lids_opened();  // kept for lid_keep_ms (ChestLids::update)
    }
    return NextPlay(guest);
}

void Mirror::CheckWatches(CoopPuppet::Guest& guest, std::uint64_t now_ms) {
    for (auto it = watches_.begin(); it != watches_.end();) {
        Watch& w = *it;
        const Play& p = w.play;
        const char* thing = p.lid ? "chest lid" : "door mirror";
        const std::string who = p.name.empty() ? Hex(p.actor) : p.name;
        if (!StillThe(guest, p.actor, p.name)) {
            it = watches_.erase(it);  // gone (a room unloaded), or another actor there now: nothing to judge
            continue;
        }
        const std::string now = Command(guest, p.actor);
        bool again = false;
        std::string why;
        // ORM review: an opening of ours that the door or chest carries on with
        // its own open command (a lid set near its end by the opener's frame goes
        // on to OpenWait) is ours kept: played again it would start from closed.
        const coop::host::DoorKind kind = coop::host::door_kind_of(p.name);
        if (now != p.command && coop::host::door_shows_open(kind, p.command) &&
            coop::host::door_shows_open(kind, now)) {
            CoopVCheck::Ok(thing, who + " went on from our '" + p.command + "' to its own '" + now +
                                      "' (open)" +
                                      (p.replays > 0 ? " (after " + std::to_string(p.replays) + " replay(s))"
                                                     : std::string()));
            it = watches_.erase(it);
            continue;
        }
        if (now == p.command) {
            if (w.seen_ms == 0) {
                w.seen_ms = std::max<std::uint64_t>(now_ms, 1);
            }
            if (now_ms - std::min(now_ms, w.seen_ms) >= kHoldMs) {
                CoopVCheck::Ok(thing, who + " kept our '" + p.command + "' " + std::to_string(kHoldMs) +
                                          " ms in its slot 0" +
                                          (p.replays > 0 ? " (after " + std::to_string(p.replays) + " replay(s))"
                                                         : std::string()));
                it = watches_.erase(it);
                continue;
            }
        } else if (w.seen_ms != 0 && !now.empty()) {
            again = true;
            why = "its own '" + now + "' took over our '" + p.command + "' " +
                  std::to_string(now_ms - std::min(now_ms, w.seen_ms)) + " ms after it showed";
        } else if (now_ms - std::min(now_ms, w.played_ms) >= kSeenMs) {
            again = true;
            why = "our '" + p.command + "' never showed in its slot 0 in " + std::to_string(kSeenMs) +
                  " ms (it shows '" + now + "')";
        }
        if (!again) {
            ++it;
            continue;
        }
        if (p.replays >= kReplays) {
            CoopVCheck::Fallback(thing, who + ": " + why + " after " + std::to_string(kReplays) +
                                            " replays; left as its game has it");
            Say(std::string(p.lid ? "chests: " : "doors: ") + who + ": " + why + "; left as the game has it");
            it = watches_.erase(it);
            continue;
        }
        Play again_play = p;
        ++again_play.replays;
        again_play.frame.reset();  // from its start: the opener's frame is long past
        ++replays_;
        Say(std::string(p.lid ? "chests: " : "doors: ") + who + ": " + why + "; played again (" +
            std::to_string(again_play.replays) + " of " + std::to_string(kReplays) + ")");
        it = watches_.erase(it);
        queue_.push_back(std::move(again_play));
    }
}

void Mirror::CheckReopens(std::uint64_t now_ms) {
    const coop::host::DoorMove move = coop::host::door_move_of(input_.own_command);
    const auto chest = [](coop::host::DoorMove m) {
        return m == coop::host::DoorMove::chest_kick || m == coop::host::DoorMove::chest_front ||
               m == coop::host::DoorMove::chest_small;
    };
    const bool started = chest(move) && !chest(own_move_);
    own_move_ = move;
    for (auto it = lid_times_.begin(); it != lid_times_.end();) {
        const std::uint64_t since = now_ms - std::min(now_ms, it->at_ms);
        const bool near = input_.own && std::hypot((*input_.own)[0] - it->at[0], (*input_.own)[1] - it->at[1],
                                                   (*input_.own)[2] - it->at[2]) <= kReopenReachM;
        if (started && near && since <= kReopenWindowMs) {
            ++reopens_;
            const std::string line = it->name + ": our Link opens it " + std::to_string(since) +
                                     " ms after its lid opened here for another player, before its opened flag "
                                     "came: its find is his, ours is not counted again";
            if (it->hash != 0) {
                std::lock_guard lock(chest_mutex_);
                if (second_finds_.size() < 32) {
                    second_finds_.push_back(it->hash);
                }
            }
            CoopVCheck::Fallback("chest reopen", line);
            Say("chests: " + line);
            it = lid_times_.erase(it);
        } else if (since > kReopenWindowMs) {
            CoopVCheck::Ok("chest reopen", it->name + ": not opened again here in the " +
                                               std::to_string(kReopenWindowMs / 1000) +
                                               " s after its lid opened for another player");
            it = lid_times_.erase(it);
        } else {
            ++it;
        }
    }
}

bool Mirror::Walk(CoopPuppet::Guest& guest, std::vector<coop::host::DoorActor>& out) {
    const std::uint64_t main = input_.main_base;
    std::uint64_t manager = 0;
    std::uint64_t lists = 0;
    std::uint32_t count = 0;
    if (!ReadValue(guest, main + addr::kActorManagerPtr, manager) || manager == 0 ||
        !ReadValue(guest, manager + addr::kManagerLists, lists) || lists == 0) {
        return false;
    }
    const std::uint64_t head = lists + addr::kListHead;
    if (!ReadValue(guest, head + addr::kListCount, count) || count == 0) {
        return false;
    }
    std::uint64_t node = 0;
    if (!ReadValue(guest, head, node)) {
        return false;
    }
    std::size_t walked = 0;
    while (node != 0 && node != head && walked < addr::kMaxWalk) {
        ++walked;
        std::uint64_t owner = 0;
        if (ReadValue(guest, node + addr::kNodeOwner, owner) && owner != 0 &&
            owner + addr::kActorNode == node) {
            std::uint32_t state = 0;
            Vec3 at{};
            const bool present = [&] {
                return ReadValue(guest, owner + addr::kActorState, state) &&
                       state == addr::kStatePresent && ReadValue(guest, owner + addr::kActorPosition, at) &&
                       std::isfinite(at[0]) && std::isfinite(at[1]) && std::isfinite(at[2]);
            }();
            const bool door_near = present && mirror_.worth_reading(at, owner);
            const bool lid_near = present && chest_lids_ && lids_.worth_reading(at);
            if (door_near || lid_near) {
                std::uint64_t name_at = 0;
                if (ReadValue(guest, owner + addr::kActorName, name_at)) {
                    std::string name = Text(guest, name_at);
                    const coop::host::DoorKind kind = coop::host::door_kind_of(name);
                    // A chest read for the lids alone; a door only for the door rule.
                    if (kind != coop::host::DoorKind::none && (door_near || coop::host::is_chest(kind))) {
                        coop::host::DoorActor a;
                        a.id = owner;
                        a.name = std::move(name);
                        a.position = at;
                        a.command = Command(guest, owner);
                        if (coop::host::is_chest(kind)) {
                            a.hash = Hash(guest, owner);
                            a.frame = SlotFrame(guest, owner).value_or(0.0f);
                        }
                        out.push_back(std::move(a));
                    }
                }
            }
        }
        if (!ReadValue(guest, node, node)) {
            return false;
        }
    }
    return node == head && walked == count;
}

std::string Mirror::Command(CoopPuppet::Guest& guest, std::uint64_t actor) {
    const std::uint64_t controller = Controller(guest, actor);
    std::int32_t slots = 0;
    std::uint64_t array = 0;
    std::uint64_t slot0 = 0;
    std::uint64_t name = 0;
    if (controller == 0 || !ReadValue(guest, controller + CoopPuppet::addr::kSlotCount, slots) ||
        slots < 1 || !ReadValue(guest, controller + CoopPuppet::addr::kSlots, array) ||
        array == 0 || !ReadValue(guest, array, slot0) || slot0 == 0 ||
        !ReadValue(guest, slot0 + CoopPuppet::addr::kSlotName, name)) {
        return {};
    }
    return Text(guest, name);
}

std::optional<float> Mirror::SlotFrame(CoopPuppet::Guest& guest, std::uint64_t actor) {
    const std::uint64_t controller = Controller(guest, actor);
    std::int32_t slots = 0;
    std::uint64_t array = 0;
    std::uint64_t slot0 = 0;
    float frame = 0.0f;
    if (controller == 0 || !ReadValue(guest, controller + CoopPuppet::addr::kSlotCount, slots) ||
        slots < 1 || !ReadValue(guest, controller + CoopPuppet::addr::kSlots, array) ||
        array == 0 || !ReadValue(guest, array, slot0) || slot0 == 0 ||
        !ReadValue(guest, slot0 + CoopPuppet::addr::kSlotFrame, frame) || !std::isfinite(frame)) {
        return std::nullopt;
    }
    return frame;
}

std::uint64_t Mirror::Hash(CoopPuppet::Guest& guest, std::uint64_t actor) {
    std::uint64_t placement = 0;
    std::uint64_t hash = 0;
    if (!ReadValue(guest, actor + addr::kActorPlacement, placement) || placement == 0 ||
        !ReadValue(guest, placement + addr::kPlacementHash, hash)) {
        return 0;
    }
    return hash;
}

void Mirror::ReceiveChestOpen(const coop::protocol::ChestOpen& m) {
    std::lock_guard lock(chest_mutex_);
    if (chest_in_.size() < 64) {
        chest_in_.push_back(m);
    }
}

std::vector<coop::protocol::ChestOpen> Mirror::TakeChestOpens() {
    std::lock_guard lock(chest_mutex_);
    return std::exchange(chest_out_, {});
}

std::vector<std::uint64_t> Mirror::TakeSecondFinds() {
    std::lock_guard lock(chest_mutex_);
    return std::exchange(second_finds_, {});
}

std::vector<std::uint64_t> Mirror::LidsShown() const {
    std::lock_guard lock(chest_mutex_);
    return lids_shown_;
}

std::uint64_t Mirror::Cell(CoopPuppet::Guest& guest, const std::string& name) {
    if (name.empty() || name.size() >= block::kCell) {
        return 0;
    }
    const auto it = std::find(cells_.begin(), cells_.end(), name);
    if (it != cells_.end()) {
        return block_ + block::kCells + block::kCell * static_cast<std::uint64_t>(it - cells_.begin());
    }
    if (cells_.size() >= block::kCellCount) {
        return 0;
    }
    const std::uint64_t at = block_ + block::kCells + block::kCell * cells_.size();
    std::array<char, block::kCell> text{};
    std::memcpy(text.data(), name.data(), name.size());
    if (!guest.Write(at, text.data(), text.size())) {
        return 0;
    }
    cells_.push_back(name);
    return at;
}

std::optional<CoopPuppet::Call> Mirror::NextPlay(CoopPuppet::Guest& guest) {
    while (!queue_.empty() && block_ != 0) {
        const Play wish = queue_.front();
        queue_.erase(queue_.begin());
        if (!StillThe(guest, wish.actor, wish.name)) {
            Say("doors: " + wish.command + " not played: the actor is gone");
            continue;
        }
        if (!HasSlot0(guest, wish.actor)) {
            Say("doors: " + wish.command + " not played: the actor has no animation slot");
            continue;
        }
        const std::uint64_t text = Cell(guest, wish.command);
        if (text == 0) {
            Say("doors: " + wish.command + " not played: no room for its name");
            continue;
        }
        std::array<std::uint8_t, 0x30> request{};
        const float rate = 1.0f;
        const float unset = -1.0f;
        const std::int32_t slot = 0;
        std::memcpy(request.data() + 0x00, &wish.actor, 8);
        std::memcpy(request.data() + 0x08, &text, 8);
        std::memcpy(request.data() + 0x10, &slot, 4);
        std::memcpy(request.data() + 0x14, &rate, 4);
        std::memcpy(request.data() + 0x18, &unset, 4);
        std::memcpy(request.data() + 0x1c, &unset, 4);
        // +0x20 skip-if-same 0 and +0x28 node 0, as the puppet's.
        if (!guest.Write(block_ + block::kRequest, request.data(), request.size())) {
            continue;
        }
        ++plays_;
        if (wish.frame) {
            played_ = wish;  // its frame at the next call
        }
        just_played_ = wish;
        CoopPuppet::Call c;
        c.function = input_.main_base + CoopPuppet::addr::kPlayAs;
        c.x[0] = block_ + block::kRequest;
        pending_ = Pending::play;
        return c;
    }
    return std::nullopt;
}

namespace {
std::once_flag g_once;
std::unique_ptr<Mirror> g_mirror;
}  // namespace

void Configure(Mirror::Log log, bool verbose, bool chest_lids) {
    std::call_once(g_once, [&] {
        g_mirror = std::make_unique<Mirror>(std::move(log), verbose);
        g_mirror->SetChestLids(chest_lids);
    });
}

Mirror* TheMirror() {
    return g_mirror.get();
}

void Publish(std::uint64_t main_base, const std::optional<std::string>& scene,
             const std::optional<Vec3>& own, std::string_view own_command,
             coop::host::Session* session, std::uint64_t server_now_us) {
    Mirror* mirror = TheMirror();
    if (mirror == nullptr) {
        return;
    }
    Input in = Plan(main_base, scene, own, CoopPuppet::Collect(session, server_now_us));
    in.own_command = std::string(own_command);
    in.server_now_us = session != nullptr ? server_now_us : 0;
    if (session != nullptr) {
        in.self = session->with_client([](coop::net::Client& c) { return c.player_number(); });
    }
    mirror->SetInput(std::move(in));
}

std::vector<std::uint64_t> LidsShown() {
    Mirror* mirror = TheMirror();
    return mirror != nullptr ? mirror->LidsShown() : std::vector<std::uint64_t>{};
}

}  // namespace CoopDoors
