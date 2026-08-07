/**
 * Enrich World - placement persistence.
 *
 * Placements live in a JSON file beside the Save folder rather than in a compiled table, so the
 * in-game placer can write them and the next scene load picks them up with no rebuild. The file
 * is also meant to be read and hand-edited outside the game, which drives two choices:
 *
 *   - Numeric ids are authoritative; `label` is regenerated on every save and is only there so
 *     the file reads sensibly. Editing a label does nothing.
 *   - `note` is free text and is preserved untouched, so a hand-written "why" survives a
 *     round-trip through the placer.
 *
 * Positions are floats, not the s16 the vanilla scene tables use - the placer reads them back
 * off a live actor, and rounding that to s16 on every save would make a prop creep as it was
 * re-saved.
 */
#include "EnrichWorld.h"

#include <ship/Context.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

#include <spdlog/spdlog.h>

namespace EnrichWorld {

namespace {

constexpr int kStoreVersion = 1;
constexpr const char* kStoreFile = "enrich-world-props.json";

std::vector<Placement> gPlacements;
bool gDirty = false;

nlohmann::json ToJson(const Placement& p) {
    nlohmann::json j;
    j["scene"] = p.sceneId;
    j["room"] = p.room;
    j["actor"] = p.actorId;
    j["params"] = p.params;
    j["pos"] = { p.pos.x, p.pos.y, p.pos.z };
    j["rot"] = { p.rot.x, p.rot.y, p.rot.z };
    j["label"] = ActorLabel(p.actorId, p.params);
    if (!p.note.empty()) {
        j["note"] = p.note;
    }
    return j;
}

bool FromJson(const nlohmann::json& j, Placement& out) {
    // Anything missing an id is unusable; skip rather than spawning actor 0 at the origin.
    if (!j.contains("scene") || !j.contains("actor") || !j.contains("pos")) {
        return false;
    }
    out.sceneId = j.value("scene", 0);
    out.room = j.value("room", 0);
    out.actorId = j.value("actor", 0);
    out.params = j.value("params", 0);

    const auto& pos = j["pos"];
    if (!pos.is_array() || pos.size() != 3) {
        return false;
    }
    out.pos = { pos[0].get<float>(), pos[1].get<float>(), pos[2].get<float>() };

    if (j.contains("rot") && j["rot"].is_array() && j["rot"].size() == 3) {
        const auto& rot = j["rot"];
        out.rot = { rot[0].get<int16_t>(), rot[1].get<int16_t>(), rot[2].get<int16_t>() };
    } else {
        out.rot = { 0, 0, 0 };
    }

    out.note = j.value("note", "");
    out.label = ActorLabel(out.actorId, out.params);
    out.live = nullptr;
    return true;
}

} // namespace

std::vector<Placement>& Placements() {
    return gPlacements;
}

bool StoreIsDirty() {
    return gDirty;
}

void MarkStoreDirty() {
    gDirty = true;
}

std::string StorePath() {
    return Ship::Context::GetPathRelativeToAppDirectory(kStoreFile);
}

void LoadStore() {
    gPlacements.clear();
    gDirty = false;

    const std::string path = StorePath();
    if (!std::filesystem::exists(path)) {
        // No file yet is the normal first-run state, not an error.
        return;
    }

    try {
        std::ifstream in(path);
        nlohmann::json root;
        in >> root;

        if (!root.contains("placements") || !root["placements"].is_array()) {
            SPDLOG_WARN("Enrich World: {} has no \"placements\" array; ignoring it", path);
            return;
        }

        size_t skipped = 0;
        for (const auto& entry : root["placements"]) {
            Placement p;
            if (FromJson(entry, p)) {
                gPlacements.push_back(p);
            } else {
                skipped++;
            }
        }
        if (skipped > 0) {
            SPDLOG_WARN("Enrich World: skipped {} malformed placement(s) in {}", skipped, path);
        }
        SPDLOG_INFO("Enrich World: loaded {} placement(s) from {}", gPlacements.size(), path);
    } catch (const std::exception& e) {
        // A broken file must not take the game down, and must not silently look empty either.
        SPDLOG_ERROR("Enrich World: failed to parse {}: {}", path, e.what());
        gPlacements.clear();
    }
}

bool SaveStore() {
    nlohmann::json root;
    root["version"] = kStoreVersion;
    root["_comment"] = "Enrich World prop placements. Numeric ids are authoritative; \"label\" is "
                       "regenerated on save. \"note\" is free text and is preserved.";

    auto arr = nlohmann::json::array();
    for (const auto& p : gPlacements) {
        arr.push_back(ToJson(p));
    }
    root["placements"] = arr;

    const std::string path = StorePath();
    try {
        std::ofstream out(path);
        if (!out) {
            SPDLOG_ERROR("Enrich World: could not open {} for writing", path);
            return false;
        }
        out << root.dump(2) << std::endl;
        if (!out) {
            SPDLOG_ERROR("Enrich World: write to {} failed", path);
            return false;
        }
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Enrich World: failed to write {}: {}", path, e.what());
        return false;
    }

    gDirty = false;
    SPDLOG_INFO("Enrich World: saved {} placement(s) to {}", gPlacements.size(), path);
    return true;
}

} // namespace EnrichWorld
