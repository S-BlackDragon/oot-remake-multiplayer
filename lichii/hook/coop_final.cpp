// SPDX-License-Identifier: GPL-3.0-or-later
//
// The end of the game with up to six players, the hook glue (coop_final.h).
// No emulator code.

#include "coop_final.h"

#include <algorithm>
#include <charconv>
#include <system_error>
#include <utility>

#include "coop_check_visual.h"

namespace CoopFinal {
namespace {

namespace host = coop::host;
namespace proto = coop::protocol;

std::string Coop(std::string line) {
    return "coop: " + std::move(line);
}

// 1 to 3600 whole seconds.
std::optional<std::uint32_t> SecondsOf(const std::string& text) {
    std::uint32_t value = 0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc() || end != text.data() + text.size() || value == 0 ||
        value > host::CountdownWord::kMaxSeconds) {
        return std::nullopt;
    }
    return value;
}

} // namespace

Final::Final(Settings settings) : settings_(std::move(settings)) {
    if (settings_.escape_clock) {
        host::EscapeClockSettings s;
        if (settings_.clock_length) {
            if (const auto length = SecondsOf(*settings_.clock_length)) {
                s.length_s = *length;
            } else {
                pending_.push_back(Coop("escape clock: LICHII_ORM_ESCAPE_CLOCK_LENGTH_S " + *settings_.clock_length +
                                        " is not 1 to 3600: " + std::to_string(s.length_s) + " s"));
            }
        }
        if (settings_.clock_word) {
            std::string why;
            word_ = host::CountdownWord::parse(*settings_.clock_word, &why);
            if (!word_) {
                pending_.push_back(Coop("escape clock: LICHII_ORM_ESCAPE_CLOCK_WORD " + *settings_.clock_word + ": " +
                                        why + "; nothing is set"));
            }
        }
        clock_settings_ = s;
        clock_.emplace(s);
        pending_.push_back(Coop("escape clock: on (LICHII_ORM_ESCAPE_CLOCK): one countdown for the group in " +
                                s.scene + ", " + std::to_string(s.length_s) + " s from the first " + s.start_event +
                                (word_ ? std::string(", ours at ") + *settings_.clock_word
                                       : std::string(", ours not read (no LICHII_ORM_ESCAPE_CLOCK_WORD)"))));
    }
}

std::vector<std::string> Final::Begin(std::optional<host::SharedFlags>& shared) {
    std::vector<std::string> log;
    log.swap(pending_);
    if (clock_) {
        clock_->reset();
    }
    own_start_seen_ = false;
    told_no_word_ = false;
    found_taken_.reset();
    if (!settings_.clock_word) {
        word_.reset();  // a word found in an earlier session is looked for again
    }
    keep_why_.clear();
    counts_due_.Reset();
    counts_.reset();
    if (!settings_.counts) {
        return log;
    }
    std::string why;
    auto list = host::CountList::load(*settings_.counts, &why);
    if (!list) {
        log.push_back(Coop("count: LICHII_ORM_COUNTS " + why + "; no Int is counted"));
        return log;
    }
    if (!shared) {
        log.push_back(Coop("count: flags are not shared: no Int is counted"));
        return log;
    }
    std::size_t added = 0;
    for (const auto& key : list->keys()) {
        if (shared->add(key)) {
            ++added;
        }
    }
    log.push_back(Coop("count: on (LICHII_ORM_COUNTS " + *settings_.counts + "): " +
                       std::to_string(list->entries().size()) + " counted Ints, " + std::to_string(added) +
                       " flags added to the shared ones"));
    counts_ = std::move(*list);
    return log;
}

void Final::Story(const proto::StoryEvent& m) {
    if (!clock_ || (m.kind != proto::StoryKind::start && m.kind != proto::StoryKind::played) ||
        m.name != clock_settings_->start_event) {
        return;
    }
    const auto before = clock_->group_seconds(m.server_time_ms);
    clock_->started(m.name, m.server_time_ms);
    if (!before && clock_->group_seconds(m.server_time_ms)) {
        pending_.push_back(Coop("escape clock: the group's countdown started at server time " +
                                std::to_string(m.server_time_ms) + " ms (P" + std::to_string(m.player_number) +
                                "'s " + std::string(m.kind == proto::StoryKind::played ? "played" : "start") +
                                ")"));
    }
}

FrameOut Final::Frame(host::WritableGameMemory* memory, const FrameIn& in) {
    FrameOut out;
    out.log.swap(pending_);

    // Counted Ints: with our Link in the game (its flags are the save's).
    if (counts_ && memory != nullptr && in.link && counts_due_.Due(in.now_us)) {
        for (auto& line : host::apply_counts(*memory, *counts_)) {
            out.log.push_back(Coop(std::move(line)));
        }
    }

    // The escape's clock.
    if (clock_) {
        const bool in_scene = in.scene && *in.scene == clock_settings_->scene;
        if (!in_scene) {
            own_start_seen_ = false;
        } else if (in.event_name && *in.event_name == clock_settings_->start_event && !own_start_seen_ &&
                   in.server_now_us != 0) {
            own_start_seen_ = true;
            const auto now_ms = static_cast<std::uint32_t>(in.server_now_us / 1000);
            const bool had = clock_->group_seconds(now_ms).has_value();
            clock_->started(clock_settings_->start_event, now_ms);
            if (!had) {
                out.log.push_back(Coop("escape clock: the group's countdown started at server time " +
                                       std::to_string(now_ms) + " ms (our game's start)"));
            }
        }
        const auto server_ms = static_cast<std::uint32_t>(in.server_now_us / 1000);
        // No word given: the one the search found in our game (coop_escape_timer.h), once each.
        if (!settings_.clock_word && in.found_word && in.found_word != found_taken_) {
            found_taken_ = in.found_word;
            std::string why;
            if (auto found = host::CountdownWord::parse(*in.found_word, &why)) {
                word_ = std::move(found);
                out.log.push_back(Coop("escape clock: ours at " + *in.found_word +
                                       " (found in our game, no LICHII_ORM_ESCAPE_CLOCK_WORD): set from now on"));
            } else {
                out.log.push_back(Coop("escape clock: the word found in our game, " + *in.found_word + ", is no "
                                       "countdown word (" + why + "): the group's clock goes on without it"));
            }
        }
        if (in_scene && !word_ && !told_no_word_ && clock_->group_seconds(server_ms)) {
            told_no_word_ = true;
            out.log.push_back(Coop("escape clock: our countdown is not read (no LICHII_ORM_ESCAPE_CLOCK_WORD, "
                                   "none found in our game yet, docs/FINAL.md): nothing is set"));
        }
        if (word_ && memory != nullptr && in.link) {
            host::EscapeClockInput ci;
            ci.now_us = in.now_us;
            ci.scene = in.scene;
            ci.seconds = word_->read_seconds(*memory);
            ci.server_now_ms = server_ms;
            auto act = clock_->update(ci);
            const bool written = act.set_seconds && word_->write_seconds(*memory, *act.set_seconds);
            for (auto& line : act.log) {
                if (act.set_seconds && !written) {
                    line += "; NOT written";
                }
                out.log.push_back(Coop(std::move(line)));
            }
        }
    }

    // Twinrova's owner: our Link for the object sync.
    out.keep.up = in.downed == host::DownedState::alive;
    // ORM boss families (docs/JEFES.md section 9): held by a grabber, in any scene.
    out.keep.held = out.keep.up && in.link && in.animation && host::held_animation(*in.animation);
    if (out.keep.held != held_said_) {
        held_said_ = out.keep.held;
        out.log.push_back(Coop(out.keep.held ? "grab: our Link's AS command " + *in.animation +
                                                   " is a held one: the objects walk looks for what holds it"
                                             : "grab: our Link's AS command is no held one any more (" +
                                                   (in.animation ? *in.animation : std::string("not read")) +
                                                   ")"));
    }
    const bool in_temple = settings_.twinrova && in.scene && *in.scene == kTwinrovaScene;
    out.keep.frozen = in_temple && in.animation && host::frozen_animation(*in.animation);
    // The mirror shield's charge (docs/FINAL.md, "Twinrova"): our Link holding a witch's blast
    // on the shield. Path and fallback said once each (request: an anticipated problem gets a path
    // and a log line).
    out.keep.charging = in_temple && in.animation && host::absorbing_animation(*in.animation);
    if (out.keep.charging && !charge_ok_said_) {
        charge_ok_said_ = true;
        out.log.push_back(CoopVCheck::Line(
            "twinrova charge", CoopVCheck::Path::ok,
            "our Link's AS command " + *in.animation + " in " + std::string(kTwinrovaScene) +
                " (absorbing a blast with the mirror shield): our samples of Twinrova carry owner_keeps "
                "while it lasts"));
    }
    if (in_temple && in.link && !in.animation) {
        if (no_animation_since_ == 0) {
            no_animation_since_ = std::max<std::uint64_t>(in.now_us, 1);
        }
        if (!charge_fallback_said_ && in.now_us >= no_animation_since_ &&
            in.now_us - no_animation_since_ >= kNoAnimationUs) {
            charge_fallback_said_ = true;
            out.log.push_back(CoopVCheck::Line(
                "twinrova charge", CoopVCheck::Path::fallback,
                "our Link's AS command is not read in " + std::string(kTwinrovaScene) +
                    ": a charge of the mirror shield is not seen, the boss is kept only while Kotake's "
                    "ice holds us (a hit or a nearer player may take it mid-charge)"));
        }
    } else {
        no_animation_since_ = 0;
    }
    if (settings_.twinrova) {
        host::HandOverInput h;
        h.body = "Enemy_DungeonBoss_Twinrova";
        h.owner_up = out.keep.up;
        h.owner_frozen = out.keep.frozen;
        h.owner_charging = out.keep.charging;
        h.owner_charge = out.keep.charge;
        const host::OwnerKeep keep = host::owner_keep(h);
        if (keep.why != keep_why_) {
            out.log.push_back(Coop(keep.why.empty()
                                       ? std::string("twinrova: our Link lets the boss go again (a hit or a nearer "
                                                     "player takes it, issue 150)")
                                       : "twinrova: while we own it, the boss stays with us (" +
                                             std::string(keep.hit ? "owner_keeps" : "owner_keeps_near") +
                                             "): " + keep.why));
            keep_why_ = keep.why;
        }
    }
    return out;
}

} // namespace CoopFinal
