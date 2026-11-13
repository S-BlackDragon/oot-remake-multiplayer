// SPDX-License-Identifier: GPL-3.0-or-later
// ORM efectos2: see coop_effects.h.
#include "coop_effects.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include "coop_check_visual.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <utility>

#include "coop_switch.h"

namespace CoopEffects {

// Why each line is there: oot-remake docs/DETALLES_MARIONETA.md, "Efectos 2".
// The command names are those of AS/Player_Child.root.asb (the adult's is the
// same file), read from the romfs on 2026-11-11.
const char* const kDefaultRules = R"(# ORM efectos2: the puppets' effect rules (coop_effects.h).
#
# play/skip: what a puppet plays as well as copying its player's pose, for the
# command's effect and sound events. The first matching line wins; a command
# no line matches is skipped. Never: hitboxes (attacks), the heart gauge
# (damage, death), what pieces and objects do (items, lifting, throwing,
# equipping), demos, talks, warps, the horse, and the heavy landings of the
# iron boots (a pad rumble would shake our own controller). oot-64 D-053, as
# oot-remake docs/SACUDIDAS.md decides: a camera shake a player's own move
# makes (iron boots, hammer, a hit or a fall on him) is his alone, so no such
# move is ever played here and a puppet never shakes our camera.
# The Ocarina played freely (oot-remake docs/SONDAS.md, 2026-11-12): the remake's d-pad Ocarina is
# "DemoOcarina", and it is that command that takes Link's own Ocarina from the belt to his hand
# (the player's Ocarina piece rises from 0.49 to 0.78 of his height while it plays). Played on
# the puppet, its own Ocarina goes to its hand the same way; the notes come by OCARINA_NOTE.
# Before the Demo skip below; a song taught (DemoOcarinaMelodyGet) stays skipped.
play DemoOcarina
# Farore's Wind as the remake plays it (oot-remake docs/SONDAS.md round 7, sondas22r7, in a dungeon): the cast
# is "DemoStartFaroreWind" and the point's setting "DemoEndFaroreWindSetting" (no Magic_Farore_* command ran).
# Exact names: the warp back to the point stays skipped.
play DemoStartFaroreWind
play DemoEndFaroreWindSetting
skip *Attack*
skip *Damage*
skip *Guard_Break*
skip Die*
skip *Dead*
skip *Death*
skip *Lift*
skip *Throw*
skip *Put*
skip *Grab*
skip *Demo*
skip Dm_*
skip Evt*
skip *Horse*
skip *Ride*
skip *Equip*
skip *Draw*
skip *Store*
skip *Boots*
skip *Get*
skip *TreasureBox*
skip *Ocarina*
skip *Bottle*
skip *Fishing*
skip *Bomb*
skip *Bow*
skip *Hook*
skip *Boomerang*
skip *Sling*
skip *Hammer*
skip *DekuStick*
skip *Talk*
skip *Warp*
skip *Cancel*
skip Cliff*
skip *Push*
skip Heavy_*
skip *Pull*
play Magic_Din*
play Magic_Farore*
play Magic_Nayru*
play Guard_Nayru*
play Swim*
play Wait*
play Move_*
play Rolling*
play Land*
play Fall*
play Jump*
play Climb*
play Ladder*
play Tunnel*
rest Wait

# flash/noflash: a new hit makes the puppet blink (a hit Nayru's Love blocks,
# and getting up after a fall, do not).
noflash *_Nayru*
noflash *StandUp*
flash Damage_*
flash LandDamage*

# status: the stand-in shown on the puppet while the command lasts.
status burn Damage_Burning*
status burn Burning*
status freeze Freeze*
status shock Damage_Electric*

# nayru: the cast; the crystal shows once it ends.
nayru Magic_Nayru*
)";

const char* StatusText(Status status) {
    switch (status) {
    case Status::none: return "none";
    case Status::burn: return "burn";
    case Status::freeze: return "freeze";
    case Status::shock: return "shock";
    case Status::nayru: return "nayru";
    }
    return "?";
}

std::optional<Status> StatusNamed(std::string_view word) {
    for (const Status s : {Status::burn, Status::freeze, Status::shock, Status::nayru}) {
        if (word == StatusText(s)) {
            return s;
        }
    }
    return std::nullopt;
}

std::string_view StandInFor(Status status) {
    switch (status) {
    case Status::burn: return kFxStandIns[0];
    case Status::freeze: return kFxStandIns[1];
    case Status::shock: return kFxStandIns[2];
    case Status::nayru: return kFxStandIns[3];
    case Status::none: break;
    }
    return {};
}

bool IsFxStandIn(std::string_view actor) {
    return std::find(kFxStandIns.begin(), kFxStandIns.end(), actor) != kFxStandIns.end();
}

// Greedy match with one back-track point (the last `*`): linear enough for
// command names of under 64 characters.
bool Match(std::string_view pattern, std::string_view name) {
    std::size_t p = 0, n = 0;
    std::size_t star = std::string_view::npos, from = 0;
    while (n < name.size()) {
        if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            from = n;
        } else if (p < pattern.size() && pattern[p] == name[n]) {
            ++p;
            ++n;
        } else if (star != std::string_view::npos) {
            p = star + 1;
            n = ++from;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') {
        ++p;
    }
    return p == pattern.size();
}

namespace {

std::vector<std::string_view> Words(std::string_view line) {
    std::vector<std::string_view> words;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
            ++i;
        }
        const std::size_t start = i;
        while (i < line.size() && line[i] != ' ' && line[i] != '\t') {
            ++i;
        }
        if (i > start) {
            words.push_back(line.substr(start, i - start));
        }
    }
    return words;
}

}  // namespace

Rules Rules::Parse(std::string_view text, std::vector<std::string>* errors) {
    Rules rules;
    std::size_t number = 0;
    auto complain = [&](const std::string& why) {
        if (errors != nullptr) {
            errors->push_back("line " + std::to_string(number) + ": " + why);
        }
    };
    while (!text.empty()) {
        const std::size_t end = text.find('\n');
        std::string_view line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        ++number;
        if (const std::size_t hash = line.find('#'); hash != std::string_view::npos) {
            line = line.substr(0, hash);
        }
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        const auto w = Words(line);
        if (w.empty()) {
            continue;
        }
        const std::string_view key = w[0];
        Line l;
        std::size_t want = 2;
        if (key == "play") {
            l.kind = Kind::play;
        } else if (key == "skip") {
            l.kind = Kind::skip;
        } else if (key == "flash") {
            l.kind = Kind::flash;
        } else if (key == "noflash") {
            l.kind = Kind::noflash;
        } else if (key == "nayru") {
            l.kind = Kind::nayru;
        } else if (key == "status") {
            l.kind = Kind::status;
            want = 3;
        } else if (key == "rest") {
            if (w.size() != 2) {
                complain("rest wants one command");
                continue;
            }
            rules.rest_ = std::string(w[1]);
            continue;
        } else {
            complain("'" + std::string(key) + "' is not play, skip, flash, noflash, status, nayru or rest");
            continue;
        }
        if (w.size() != want) {
            complain(std::string(key) + (want == 3 ? " wants a status and a pattern" : " wants one pattern"));
            continue;
        }
        if (l.kind == Kind::status) {
            const auto s = StatusNamed(w[1]);
            if (!s || *s == Status::nayru) {
                complain("'" + std::string(w[1]) + "' is not burn, freeze or shock");
                continue;
            }
            l.status = *s;
        }
        l.pattern = std::string(w[want - 1]);
        rules.lines_.push_back(std::move(l));
    }
    return rules;
}

const Rules& Rules::Default() {
    static const Rules rules = Parse(kDefaultRules);
    return rules;
}

const Rules::Line* Rules::First(std::string_view command, Kind a, Kind b) const {
    for (const Line& l : lines_) {
        if ((l.kind == a || l.kind == b) && Match(l.pattern, command)) {
            return &l;
        }
    }
    return nullptr;
}

bool Rules::Plays(std::string_view command) const {
    const Line* l = command.empty() ? nullptr : First(command, Kind::play, Kind::skip);
    return l != nullptr && l->kind == Kind::play;
}

bool Rules::Flashes(std::string_view command) const {
    const Line* l = command.empty() ? nullptr : First(command, Kind::flash, Kind::noflash);
    return l != nullptr && l->kind == Kind::flash;
}

Status Rules::StatusOf(std::string_view command) const {
    const Line* l = command.empty() ? nullptr : First(command, Kind::status, Kind::status);
    return l != nullptr ? l->status : Status::none;
}

bool Rules::CastsNayru(std::string_view command) const {
    return !command.empty() && First(command, Kind::nayru, Kind::nayru) != nullptr;
}

std::optional<Command> PosedCommand(const Rules& rules, const std::optional<Command>& wanted,
                                    std::string_view played) {
    if (wanted && rules.Plays(wanted->name)) {
        Command c = *wanted;
        c.rest = false;
        return c;
    }
    if (!played.empty() && !rules.rest().empty() && played != rules.rest() && rules.Plays(played)) {
        Command c;
        c.name = rules.rest();
        c.rest = true;
        return c;
    }
    return std::nullopt;
}

bool Flashing(std::uint64_t since_ms, std::uint64_t now_ms, std::uint64_t margin) {
    return since_ms != 0 && now_ms >= since_ms && now_ms - since_ms < kFlashMs + margin;
}

bool FlashHidden(std::uint64_t since_ms, std::uint64_t now_ms) {
    return Flashing(since_ms, now_ms) && ((now_ms - since_ms) / kBlinkMs) % 2 == 1;
}

namespace {

std::uint64_t MinMs(Status s) {
    switch (s) {
    case Status::burn: return kBurnMinMs;
    case Status::shock: return kShockMinMs;
    case Status::freeze: return kFreezeMinMs;
    default: return 0;
    }
}

}  // namespace

Look Tracker::Update(const Settings& settings, std::uint8_t number, std::string_view scene,
                     std::optional<std::string_view> anim, bool downed, std::uint64_t now_ms) {
    Player& p = players_[number];
    p.said.clear();
    p.seen_ms = now_ms;
    const Rules& rules = settings.rules;
    auto say = [&](const std::string& text) { p.said += (p.said.empty() ? "" : "; ") + text; };
    auto crystal_off = [&](const char* why) {
        if (p.nayru_until_ms != 0) {
            say(std::string("crystal off (") + why + ")");
        }
        p.nayru_until_ms = 0;
    };

    const std::string before = p.anim;
    const bool before_known = p.anim_known;
    const std::string_view name = anim ? *anim : std::string_view{};
    const bool changed = anim && (!before_known || name != before);
    if (anim) {
        p.anim = std::string(name);
        p.anim_known = true;
    }

    // Another scene: what they had on is gone (a load ends a spell, a burn).
    if (!scene.empty()) {
        if (!p.scene.empty() && scene != p.scene) {
            crystal_off("scene");
            p.status = Status::none;
            p.status_until_ms = 0;
            p.casting = false;
        }
        p.scene = std::string(scene);
    }
    if (downed) {
        crystal_off("downed");
        p.status = Status::none;
        p.status_until_ms = 0;
        p.casting = false;
        p.flash_since_ms = 0;
    }

    // A new hit: the blink, and Nayru's Love is not on any more (a hit it
    // blocks is a noflash command).
    if (!downed && changed && rules.Flashes(name) && !(before_known && rules.Flashes(before))) {
        if (settings.flash && !Flashing(p.flash_since_ms, now_ms)) {
            p.flash_since_ms = std::max<std::uint64_t>(now_ms, 1);
            say("flash ('" + std::string(name) + "')");
        }
        crystal_off("hurt");
    }

    // Burn, freeze, shock: while the command lasts, at least its minimum.
    const Status now_status = !downed && anim ? rules.StatusOf(name) : Status::none;
    if (now_status != Status::none) {
        p.status = now_status;
        p.status_until_ms = now_ms + MinMs(now_status);
    } else if (p.status != Status::none && now_ms >= p.status_until_ms) {
        p.status = Status::none;
    }

    // Nayru's Love: on when the cast ends, off after its time.
    if (anim && !downed) {
        const bool cast = rules.CastsNayru(name);
        if (p.casting && !cast) {
            p.nayru_until_ms = now_ms + settings.nayru_ms;
            say("crystal on for " + std::to_string(settings.nayru_ms / 1000) + " s");
        }
        p.casting = cast;
    }
    if (p.nayru_until_ms != 0 && now_ms >= p.nayru_until_ms) {
        crystal_off("time");
    }

    Look look;
    if (settings.flash && Flashing(p.flash_since_ms, now_ms)) {
        look.flash_since_ms = p.flash_since_ms;
    }
    if (settings.status && !downed) {
        look.status = p.status != Status::none ? p.status
                      : p.nayru_until_ms != 0  ? Status::nayru
                                               : Status::none;
    }
    if (look.status != p.shown) {
        say(std::string("shows ") + StatusText(look.status) + " (was " + StatusText(p.shown) + ")");
        p.shown = look.status;
    }
    return look;
}

void Tracker::Prune(std::uint64_t now_ms) {
    for (auto it = players_.begin(); it != players_.end();) {
        if (now_ms >= it->second.seen_ms && now_ms - it->second.seen_ms >= kForgetMs) {
            it = players_.erase(it);
        } else {
            ++it;
        }
    }
}

const std::string& Tracker::Said(std::uint8_t number) const {
    const auto it = players_.find(number);
    return it != players_.end() ? it->second.said : none_;
}

std::uint64_t FxKey(std::uint8_t number, Status status, std::uint16_t generation,
                    std::uint16_t episode) {
    return kFxKeyBit | (std::uint64_t{episode} << 32) | (std::uint64_t{generation} << 16) |
           (std::uint64_t{number} << 8) | static_cast<std::uint64_t>(status);
}

FxPlace FxAt(std::uint8_t number, Status status, const std::array<float, 3>& feet,
             std::uint16_t generation, std::uint16_t episode) {
    FxPlace f;
    f.number = number;
    f.status = status;
    f.position = {feet[0], feet[1] + kFxLift, feet[2]};
    f.key = FxKey(number, status, generation, episode);
    f.actor = std::string(StandInFor(status));
    return f;
}

std::vector<std::uint64_t> FxKeys::Plan(std::string_view scene, const std::vector<Shown>& shown) {
    if (!planned_ || scene != scene_) {
        planned_ = true;
        scene_ = std::string(scene);
        ++generation_;  // a load deleted what we had: new keys, made again
        last_.clear();
    }
    std::map<std::uint16_t, std::uint16_t> now;
    std::vector<std::uint64_t> keys;
    keys.reserve(shown.size());
    for (const Shown& s : shown) {
        const auto pair = static_cast<std::uint16_t>((std::uint16_t{s.number} << 8) |
                                                     static_cast<std::uint16_t>(s.status));
        std::uint16_t episode = 0;
        if (const auto kept = last_.find(pair); kept != last_.end()) {
            episode = kept->second;  // shown in the last plan too: the same copy
        } else if (const auto twice = now.find(pair); twice != now.end()) {
            episode = twice->second;
        } else {
            episode = ++episodes_;  // shown again after a plan without it: a new copy
        }
        now[pair] = episode;
        keys.push_back(FxKey(s.number, s.status, generation_, episode));
    }
    last_ = std::move(now);
    return keys;
}

Settings FromValues(const char* effects, const char* flash, const char* status, const char* nayru_s,
                    const char* list_path, const std::optional<std::string>& list_text,
                    std::vector<std::string>& notes) {
    Settings s;
    s.as_effects = CoopSwitch::From(effects, true);
    s.flash = CoopSwitch::From(flash, true);
    s.status = CoopSwitch::From(status, true);
    for (const auto& [name, value] : {std::pair{"LICHII_ORM_EFFECTS", effects},
                                      std::pair{"LICHII_ORM_DAMAGE_FLASH", flash},
                                      std::pair{"LICHII_ORM_STATUS_FX", status}}) {
        if (value != nullptr) {
            if (const auto line = CoopSwitch::Complaint(name, value, true)) {
                notes.push_back(*line);
            }
        }
    }
    if (nayru_s != nullptr && *nayru_s != '\0') {
        const std::string text(nayru_s);
        std::size_t used = 0;
        unsigned long seconds = 0;
        bool ok = text.find_first_not_of("0123456789") == std::string::npos && text.size() <= 6;
        if (ok) {
            seconds = std::stoul(text, &used);
            ok = used == text.size() && seconds >= 1 && seconds <= 600;
        }
        if (ok) {
            s.nayru_ms = std::uint64_t{seconds} * 1000;
        } else {
            notes.push_back("coop: effects: LICHII_ORM_NAYRU_S=" + text +
                            " is not 1 to 600 seconds: " + std::to_string(s.nayru_ms / 1000) + " s");
        }
    }
    if (list_path != nullptr && *list_path != '\0') {
        if (!list_text) {
            notes.push_back(std::string("coop: effects: LICHII_ORM_EFFECTS_LIST=") + list_path +
                            " cannot be read: the built-in rules are used");
        } else {
            std::vector<std::string> errors;
            Rules rules = Rules::Parse(*list_text, &errors);
            for (const auto& e : errors) {
                notes.push_back(std::string("coop: effects: ") + list_path + " " + e + " (left out)");
            }
            notes.push_back(std::string("coop: effects: rules from ") + list_path + " (" +
                            std::to_string(rules.size()) + " lines, rest '" + rules.rest() + "')");
            s.rules = std::move(rules);
        }
    }
    notes.push_back(std::string("coop: effects: commands under the pose ") + (s.as_effects ? "on" : "off") +
                    ", damage flash " + (s.flash ? "on" : "off") + ", burn/freeze/shock and Nayru's crystal " +
                    (s.status ? "on (" + std::to_string(s.nayru_ms / 1000) + " s crystal)" : std::string("off")));
    return s;
}

std::optional<double> NayruSecondsIn(std::string_view manifest) {
    const std::string_view key = "\"nayru_s\"";
    const auto at = manifest.find(key);
    if (at == std::string_view::npos) {
        return std::nullopt;
    }
    std::size_t i = manifest.find(':', at + key.size());
    if (i == std::string_view::npos) {
        return std::nullopt;
    }
    ++i;
    while (i < manifest.size() && (manifest[i] == ' ' || manifest[i] == '\t')) {
        ++i;
    }
    std::size_t end = i;
    while (end < manifest.size() && (std::isdigit(static_cast<unsigned char>(manifest[end])) || manifest[end] == '.')) {
        ++end;
    }
    if (end == i) {
        return std::nullopt;  // null, or not a number
    }
    const std::string text(manifest.substr(i, end - i));
    char* stop = nullptr;
    const double v = std::strtod(text.c_str(), &stop);
    if (stop == nullptr || *stop != '\0' || !(v >= 1.0) || !(v <= 600.0)) {
        return std::nullopt;
    }
    return v;
}

void PickNayru(Settings& s, bool switch_set, std::optional<double> pack_s) {
    char buf[32];
    if (switch_set) {
        CoopVCheck::Ok("nayru crystal", std::to_string(s.nayru_ms / 1000) + " s as LICHII_ORM_NAYRU_S sets it");
    } else if (pack_s) {
        s.nayru_ms = static_cast<std::uint64_t>(*pack_s * 1000.0 + 0.5);
        std::snprintf(buf, sizeof(buf), "%.1f", *pack_s);
        CoopVCheck::Ok("nayru crystal", std::string(buf) + " s, the game's NayruLoveTime (PlayerGameParam, read by the "
                                                         "puppet pack)");
    } else {
        CoopVCheck::Fallback("nayru crystal", std::to_string(s.nayru_ms / 1000) +
                                                  " s, the estimate: the puppet pack has no nayru_s (run "
                                                  "make-puppet-pack again)");
    }
}

Settings FromSwitches(std::vector<std::string>& notes) {
    const auto effects = CoopSwitch::Value("LICHII_ORM_EFFECTS");
    const auto flash = CoopSwitch::Value("LICHII_ORM_DAMAGE_FLASH");
    const auto status = CoopSwitch::Value("LICHII_ORM_STATUS_FX");
    const auto nayru = CoopSwitch::Value("LICHII_ORM_NAYRU_S");
    const auto list = CoopSwitch::Value("LICHII_ORM_EFFECTS_LIST");
    std::optional<std::string> text;
    if (list) {
        std::ifstream in(*list, std::ios::binary);
        if (in) {
            text = std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        }
    }
    auto c = [](const std::optional<std::string>& v) { return v ? v->c_str() : nullptr; };
    return FromValues(c(effects), c(flash), c(status), c(nayru), c(list), text, notes);
}

}  // namespace CoopEffects
