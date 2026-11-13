// SPDX-License-Identifier: GPL-3.0-or-later
//
// ORM partner map from the live hook (coop_map.h, CoopMap::AddTo). Called by
// the HUD's model (coop_hud_game.cpp) once per frame it draws, on the
// renderer's thread. The table is LICHII_COOP_MAP_FILE (the kit's
// coop_map.txt, made on each PC by oot-remake tools/romfs/map_table.py),
// read once; without it only the elsewhere lines show.
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>

#include "platform_env.h"  // Env, ORM_LOG_*
#include "coop_hook.h"
#include "orm_hud.h"
#include "coop_map.h"
#include "coop_check_visual.h"

namespace CoopMap {
namespace {

const Table& LoadedTable() {
    static std::once_flag once;
    static Table table;
    std::call_once(once, [] {
        const auto value = CoopPlatform::Env("LICHII_COOP_MAP_FILE");
        if (!value || value->empty()) {
            ORM_LOG_INFO("coop: map: no LICHII_COOP_MAP_FILE, no dots on the game's maps");
            return;
        }
        const std::string& path = *value;
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            ORM_LOG_ERROR("coop: map: cannot read {}", path);
            return;
        }
        std::ostringstream text;
        text << file.rdbuf();
        auto parsed = Parse(text.str());
        for (const auto& e : parsed.errors) {
            ORM_LOG_ERROR("coop: map: {}: {}", path, e);
        }
        table = std::move(parsed.table);
        ORM_LOG_INFO("coop: map: {}: {} dungeons with floors, {} minimap and {} pause map views, "
                     "minimap box {}, pause map box {}",
                     path, table.floors.size(), table.mini.size() + (table.mini_any ? 1 : 0), table.pause.size(),
                     table.mini_screen ? "set" : "not set", table.pause_screen ? "set" : "not set");
    });
    return table;
}

} // namespace

void AddTo(OrmHud::Model& model) {
    if (!On()) {
        return;
    }
    const auto mates = CoopHook::CurrentMates();
    if (!mates) {
        return;
    }
    const Table& table = LoadedTable();
    Input input;
    input.scene = mates->scene;
    input.own = mates->position;
    input.mates = mates->others;
    input.picture_aspect = mates->aspect > 0.0f ? mates->aspect : 16.0f / 9.0f;
    input.self = Debug();
    const bool pause = PauseShown();
    input.which = pause ? Which::pause : Which::mini;
    if (input.scene && !input.scene->empty()) {
        // ORM riesgos-visual: once per map and scene, which path the dots take.
        const MapPath path = PathFor(table, input.which, *input.scene);
        const std::string thing = std::string(pause ? "map pause " : "map mini ") + *input.scene;
        if (path.ok) {
            CoopVCheck::Ok(thing, path.detail);
        } else {
            CoopVCheck::Fallback(thing, path.detail);
        }
    }
    if (pause) {
        model.tags.clear();  // over the pause menu they would cover it
        model.map_dots = Dots(table, input);
    } else if (mates->camera) {
        model.map_dots = Dots(table, input);  // the minimap: only while a scene is drawn
    }
    model.notes = Elsewhere(input);
    model.notes_left = table.notes_left;
    model.notes_bottom = table.notes_bottom;
}

} // namespace CoopMap
