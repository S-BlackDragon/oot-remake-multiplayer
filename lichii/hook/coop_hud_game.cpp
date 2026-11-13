// SPDX-License-Identifier: GPL-3.0-or-later
//
// The in-game HUD's model from the live hook (orm_hud.h, OrmHud::Current).
// Called by the emulator's present pass (coop_hud_vk.cpp) once per frame it
// draws, on the renderer's thread.
#include <chrono>
#include <mutex>


#include "coop_camera_probe.h"
#include "coop_hook.h"
#include "orm_hud.h"
#include "coop_notices.h"
#include "orm_commands_game.h"  // ORM: the stats corner
#include "coop_spectator.h"
#include "coop_map.h"
#include "platform_env.h"  // ORM_LOG_*

namespace OrmHud {
namespace {

using Clock = std::chrono::steady_clock;
// The player list takes the network and presence locks and the panel formats
// its lines: a few times a second is enough, as the Qt panel (200 ms).
constexpr auto kRefresh = std::chrono::milliseconds(200);

std::mutex g_mutex;
Clock::time_point g_at{};
bool g_panel_was = false;
std::vector<CoopHook::PlayerRow> g_players;
std::vector<CoopHook::OverlayLine> g_lines;
std::vector<CoopHook::ChatLine> g_chat;  // ORM: the chat lines (docs/COMUNICACION.md)

} // namespace

std::optional<Model> Current() {
    if (!On()) {
        return std::nullopt;
    }
    const bool panel = PanelShown();
    const auto markers = CoopHook::CurrentMarkers();
    const auto now = Clock::now();
    bool refresh = false;
    {
        std::lock_guard lock(g_mutex);
        refresh = now - g_at >= kRefresh || panel != g_panel_was;
    }
    if (refresh) {
        // Outside our lock: these take the hook's own.
        auto players = CoopHook::PlayerList();
        auto lines = panel ? CoopHook::OverlayLines() : std::vector<CoopHook::OverlayLine>{};
        auto chat = CoopHook::ChatLines();
        std::lock_guard lock(g_mutex);
        g_players = std::move(players);
        g_lines = std::move(lines);
        g_chat = std::move(chat);
        g_at = now;
        g_panel_was = panel;
    }
    Model model;
    std::vector<CoopHook::PlayerRow> players;  // ORM: for the rupee line (Apply)
    {
        std::lock_guard lock(g_mutex);
        model = Build(markers, g_players, g_lines, panel);
        players = g_players;
        // ORM: chat and notices in the picture, unless the Qt box shows them.
        if (!Typing()) {
            model.chat = BuildChat(g_chat);
        }
    }
    // ORM cofre: "<nombre> encontró <objeto>" (coop_notices.h), the name in
    // the colour of his tag when he has one here.
    const auto steady_ms = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
    for (const auto& line : CoopNotices::TheBoard().Current(steady_ms)) {
        Notice n;
        n.number = line.number;
        n.name = line.name;
        n.rest = line.rest;
        for (const auto& tag : model.tags) {
            if (!tag.self && line.number != 0 && tag.number == line.number) {
                n.rgb = tag.rgb;
            }
        }
        model.notices.push_back(std::move(n));
    }
    // ORM the room's age (coop_notices.h Banner): the big centred notice.
    if (auto line = CoopNotices::TheBanner().Current(steady_ms)) {
        Banner b;
        b.number = line->number;
        b.name = std::move(line->name);
        b.rest = std::move(line->rest);
        b.detail = std::move(line->detail);
        for (const auto& tag : model.tags) {
            if (!tag.self && b.number != 0 && tag.number == b.number) {
                b.rgb = tag.rgb;
            }
        }
        model.banner = std::move(b);
    }
    model.corner = CoopNotices::TheCorner().Current(steady_ms);  // ORM "Guardado"
    // ORM spectator C2: the mate a downed Link's camera follows reads "> Ana <".
    if (const std::uint8_t followed = CoopCameraProbe::Following(); followed != 0) {
        for (auto& tag : model.tags) {
            if (!tag.self && !tag.ping && tag.number == followed) {
                tag.name = CoopSpectator::FollowedTag(tag.name);
            }
        }
    }
    if (OrmCommands::MapShown()) {  // ORM /mapa off: none (COMANDOS.md)
        CoopMap::AddTo(model);  // ORM partner map: dots and elsewhere lines (on unless LICHII_COOP_MAP=0)
    }
    model.stats = OrmCommands::CornerLines();  // ORM /fps, /ping, /red, /stats (COMANDOS.md)
    // ORM (docs/HUD_AJUSTES.md): this player's settings (coop-options.ini, /hud),
    // and no tags over our own pause menu (oot-64 D-072 point 6).
    Apply(model, LiveSettings(), markers && markers->paused, players);
    if (model.Empty()) {
        return std::nullopt;
    }
    return model;
}

} // namespace OrmHud

// ORM "Guardado" (coop_notices.h): what the app calls on its helper's room_saved.
void CoopNotices::ShowSaved() {
    const auto now_ms = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                       std::chrono::steady_clock::now().time_since_epoch())
                                                       .count());
    TheCorner().Show(kSavedText, now_ms, kSavedMs);
    ORM_LOG_INFO("coop: hud: \"{}\" for {} ms (the app sent the room's save)", kSavedText, kSavedMs);
}
