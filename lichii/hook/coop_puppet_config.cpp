// SPDX-License-Identifier: GPL-3.0-or-later
#include "coop_puppet_config.h"

#include <utility>

namespace CoopPuppetConfig {
namespace {

bool On(const std::string& v) {
    return !v.empty() && v != "0";
}

}  // namespace

CoopPuppet::PlannerSettings Settings(const EnvFn& env, const std::function<bool()>& pack_green,
                                     std::vector<std::string>* log, const PackTunicsFn& pack_tunics) {
    CoopPuppet::PlannerSettings settings;
    // The test puppet has its own switch: the logs alone must not put a
    // second Link in the game (Caleb's run of 2026-11-09 with the logs on).
    settings.debug = On(env("LICHII_COOP_TEST_PUPPET"));
    settings.debug_anim = On(env("LICHII_COOP_DEBUG_ANIM"));
    if (auto actor = env("LICHII_COOP_PUPPET_ACTOR"); !actor.empty()) {
        settings.actor = std::move(actor);
    }
    bool pack_plain = false;
    if (const auto tunics = env("LICHII_COOP_TUNICS"); tunics == "0" || tunics == "off") {
        settings.tunics = false;
    } else if (pack_tunics) {
        // A pack without the tunic puppets: the game refuses CoopPuppet_<Age>_<Colour>
        // (not in its ActorInfo), so every puppet is the plain one it has.
        if (const auto has = pack_tunics(); has && !*has) {
            settings.tunics = false;
            pack_plain = true;
            if (log != nullptr) {
                log->push_back("coop: the puppet pack has no tunic colours (made before them): every puppet is "
                               "the plain one; run make-puppet-pack again for the colours");
            }
        }
    }
    // The kit's tools/release/kit/scripts/kit-paths.ps1 Set-KitSelfTunic. Only
    // with a pack that has the green puppets (a hand-set switch and an older
    // pack must not leave green players without a puppet).
    if (const auto self = env("LICHII_COOP_SELF_TUNIC"); !self.empty() && self != "green") {
        settings.green_puppet = pack_green();
        if (log != nullptr) {
            log->push_back("coop: your Link wears " + self + "; green players " +
                           (settings.green_puppet
                                ? "get CoopPuppet_<Age>_Green"
                                : "get the plain puppet (the pack has no green puppet; make it again)"));
        }
    }
    if (log != nullptr) {
        log->push_back(std::string("coop: puppets on, actor ") +
                       (settings.actor.empty() ? "by age" : settings.actor) +
                       (settings.tunics  ? ""
                        : pack_plain     ? ", tunics off (the pack has none)"
                                         : ", tunics off (LICHII_COOP_TUNICS=0)") +
                       (settings.debug ? ", test puppet beside you" : ""));
    }
    return settings;
}

}  // namespace CoopPuppetConfig

#ifdef LICHII_COOP

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "common/fs/path_util.h"
#include "common/logging.h"
#include "coop_bodies.h"         // ORM choque
#include "coop_effects.h"        // ORM efectos2
#include "coop_epona.h"          // ORM D-R19
#include "coop_switch.h"         // ORM switches (LICHII_ORM_*)
#include "orm_ocarina_sound.h"   // ORM: the others' notes and songs

namespace CoopPuppetConfig {
namespace {

std::string Env(const char* name) {
#ifdef _MSC_VER
    char* value = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&value, &size, name) != 0 || value == nullptr) {
        return {};
    }
    std::string out(value);
    std::free(value);
    return out;
#else
    const char* value = std::getenv(name);
    return value != nullptr ? std::string(value) : std::string();
#endif
}

// Every mod folder's coop_puppet.json under LoadDir/<title id>, as
// coop_spawn's PackStandIns looks. LoadDir is the emulator's load/ or
// Hookshot's mods\contents (hookshot/compat/common/fs/path_util.h).
std::vector<std::string> PackManifests() {
    std::vector<std::string> out;
    std::error_code ec;
    const auto title = Common::FS::GetEdenPath(Common::FS::EdenPath::LoadDir) / "0400486024DEC000";
    for (const auto& mod : std::filesystem::directory_iterator(title, ec)) {
        std::ifstream in(mod.path() / "coop_puppet.json", std::ios::binary);
        if (!in) {
            continue;
        }
        out.emplace_back((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }
    return out;
}

bool PackGreenPuppets() {
    for (const auto& text : PackManifests()) {
        if (CoopPuppet::PackHasGreenPuppets(text)) {
            return true;
        }
    }
    return false;
}

// Any manifest with tunic puppets; std::nullopt with no manifest at all.
std::optional<bool> PackTunicPuppets() {
    const auto manifests = PackManifests();
    if (manifests.empty()) {
        return std::nullopt;
    }
    for (const auto& text : manifests) {
        if (CoopPuppet::PackHasTunicPuppets(text)) {
            return true;
        }
    }
    return false;
}

// ORM: our settings on top of upstream's (they lived in coop_svc.cpp's ConfigurePuppets up to
// test9). Each has its own switch; the lines say which path runs.
void OrmSettings(CoopPuppet::PlannerSettings& settings) {
    // Puppet smoothing (oot-remake docs/SUAVIZADO.md), on unless LICHII_ORM_SMOOTH
    // (LICHII_COOP_SMOOTH before test9) says off.
    settings.smooth = CoopSwitch::On("LICHII_ORM_SMOOTH", "LICHII_COOP_SMOOTH", true);
    // oot-remake docs/PUERTAS.md: leaving and arriving players.
    settings.passages = Env("LICHII_COOP_PASSAGES") != "0";
    // Bodies (oot-remake docs/CUERPOS.md): a puppet on our Link drawn aside; our Link is never moved.
    settings.bodies = CoopSwitch::On("LICHII_ORM_BODIES", true);
    // Issue 152: the piece a player's Ocarina is carried as while they play
    // (CoopPuppet::OcarinaInHand): sub (default), bow, or an off word (belt only).
    // LICHII_ORM_OCARINA_HAND, LICHII_COOP_OCARINA_HAND before test9.
    settings.ocarina_hand = CoopPuppet::OcarinaHandPiece(
        CoopSwitch::Value("LICHII_ORM_OCARINA_HAND", "LICHII_COOP_OCARINA_HAND").value_or(""));
    settings.riders = CoopEpona::HorsesOn();  // D-R19: a rider shown on a horse, not parked
    // efectos2 (coop_effects.h): commands played under the copied pose for their
    // effects, the damage flash, burn/freeze/shock and Nayru's crystal; each on
    // unless LICHII_ORM_EFFECTS, _DAMAGE_FLASH or _STATUS_FX says off.
    std::vector<std::string> effect_notes;
    settings.effects = CoopEffects::FromSwitches(effect_notes);
    for (const auto& note : effect_notes) {
        LOG_INFO(Core, "{}", note);
    }
    {
        // riesgos-visual: the crystal's time from the game (coop_puppet.json "nayru_s").
        std::optional<double> pack_s;
        for (const auto& text : PackManifests()) {
            if (!pack_s) {
                pack_s = CoopEffects::NayruSecondsIn(text);
            }
        }
        const auto nayru_switch = CoopSwitch::Value("LICHII_ORM_NAYRU_S");
        const bool switch_set = nayru_switch && (settings.effects.nayru_ms != CoopEffects::Settings::kNayruDefaultMs ||
                                                 *nayru_switch == "60");
        CoopEffects::PickNayru(settings.effects, switch_set, pack_s);
    }
    LOG_INFO(Core, "coop: puppets: {} (LICHII_ORM_SMOOTH)", settings.smooth ? "smoothing on" : "smoothing off");
    LOG_INFO(Core, "coop: puppets: the Ocarina in the hand while its player plays: {}",
             settings.ocarina_hand < CoopPuppet::kGearCount ? CoopPuppet::kGearNames[settings.ocarina_hand]
                                                            : "off (on the belt only)");
}

}  // namespace

void Configure() {
    std::vector<std::string> lines;
    auto settings = Settings(Env, PackGreenPuppets, &lines, PackTunicPuppets);
    for (const auto& line : lines) {
        LOG_INFO(Core, "{}", line);
    }
    OrmSettings(settings);  // ORM
    const CoopEffects::Settings effects = settings.effects;  // ORM
    const auto verbose = Env("LICHII_COOP_DEBUG_PUPPET");
    CoopPuppet::Configure(std::move(settings), On(verbose),
                          [](const std::string& line) { LOG_INFO(Core, "{}", line); });
    CoopEffects::UseGame(effects);  // ORM efectos2: the status stand-ins
    CoopBodies::Use();              // ORM choque: the mates' bodies (coop_bodies.h, docs/CHOQUE.md)
    OrmOcarinaSound::Configure();   // ORM: the others' notes and songs (LICHII_COOP_OCARINA_SOUND)
}

}  // namespace CoopPuppetConfig

#endif  // LICHII_COOP
