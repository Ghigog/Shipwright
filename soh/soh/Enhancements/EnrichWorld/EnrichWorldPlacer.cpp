/**
 * Enrich World - the in-game prop placer.
 *
 * Link is the cursor: stand where you want something, pick it, place it, nudge it, save.
 *
 * The reason this window has to own the spawn, rather than a "dump what's in the room" button
 * bolted onto the Actor Viewer: the values an actor was spawned with are NOT recoverable from
 * the actor afterwards. Actor_Spawn writes home.rot and params from its arguments and then
 * Actor_Init runs, which is free to rewrite both - EnWood02_Init masks params to 0xFF and
 * repacks home.rot.z. Reading a live actor back would silently record params 5 where 517 was
 * meant, losing the drop table. So the requested params and rotation are recorded here at the
 * moment of spawning, and only *position* is read back off the actor (world.pos survives, and
 * reading it is what lets you drag a prop around and have the move stick).
 *
 * The prop list drops anything that would Actor_Kill itself in this room, which makes the mod's
 * one hard rule unreachable by hand, and sorts the rest so the room's own props come first - see
 * EnrichWorldPalette.cpp.
 */
#include "soh/SohGui/UIWidgets.hpp"
#include "soh/Enhancements/game-interactor/GameInteractor.h"

#include <spdlog/fmt/fmt.h>
#include <string>
#include <vector>

// These two must come AFTER everything above, because EnrichWorld.h pulls in z64.h. OoT's macros
// break libstdc++ internals when they are live while a standard header is parsed - here <future>,
// reached via UIWidgets.hpp -> portable-file-dialogs.h. Clang tolerates it; GCC and MSVC do not,
// which is what broke the first Linux and Windows CI builds while macOS stayed green.
#include "EnrichWorldPlacer.h"
#include "EnrichWorld.h"

extern "C" {
#include "z64.h"
#include "functions.h"
#include "macros.h"
extern PlayState* gPlayState;
}

namespace {

using EnrichWorld::Placement;
using EnrichWorld::PropDef;

float GroundBelow(float x, float y, float z) {
    Vec3f probe = { x, y + 50.0f, z };
    CollisionPoly* poly = nullptr;
    float floorY = BgCheck_EntityRaycastFloor1(&gPlayState->colCtx, &poly, &probe);
    // BGCHECK_Y_MIN means the ray found nothing - keep the caller's y rather than dropping the
    // prop through the world.
    return (floorY <= BGCHECK_Y_MIN) ? y : floorY;
}

struct Choice {
    const PropDef* def;
    bool native;
};

/**
 * Everything placeable in this room, natives first.
 *
 * Only props that would kill themselves here are left out - see IsPropUsable. Non-native props
 * stay in the list because they genuinely work: SoH resolves models by OTR resource name, so
 * "the room doesn't load that object" costs nothing at draw time. Sorting them below a heading
 * keeps the vanilla-looking choice the obvious one without making the others unreachable.
 */
std::vector<Choice> PlaceableProps() {
    std::vector<Choice> out;
    const auto& all = EnrichWorld::AllProps();
    for (const auto& def : all) {
        if (EnrichWorld::IsPropUsable(def) && EnrichWorld::IsPropNative(def)) {
            out.push_back({ &def, true });
        }
    }
    for (const auto& def : all) {
        if (EnrichWorld::IsPropUsable(def) && !EnrichWorld::IsPropNative(def)) {
            out.push_back({ &def, false });
        }
    }
    return out;
}

} // namespace

void EnrichWorldPlacerWindow::InitElement() {
    EnrichWorld::LoadStore();

    // A placement's `live` pointer outlives the actor if anything else destroys it - the player
    // cutting a placed bush, an actor culling itself, a room unload. Nudging the placement after
    // that would write through a freed pointer. Drop the reference the moment the actor dies.
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnActorDestroy>([](void* refActor) {
        Actor* actor = static_cast<Actor*>(refActor);
        for (auto& p : EnrichWorld::Placements()) {
            if (p.live == actor) {
                p.live = nullptr;
            }
        }
    });
}

void EnrichWorldPlacerWindow::DrawElement() {
    if (gPlayState == nullptr) {
        ImGui::Text("Load a save first - the placer needs a scene to place into.");
        return;
    }

    if (!CVarGetInteger(CVAR_ENHANCEMENT("EnrichWorld"), 0)) {
        ImGui::TextWrapped("Extra Scenery is switched off, so nothing placed here will spawn. "
                           "Enable it under Enhancements -> Quality of Life -> Enrich World.");
        ImGui::Separator();
    }

    const int16_t scene = gPlayState->sceneNum;
    const int8_t room = gPlayState->roomCtx.curRoom.num;
    ImGui::Text("Scene %d, room %d", scene, room);

    // ---- Place a new prop ----

    const auto choices = PlaceableProps();
    if (choices.empty()) {
        ImGui::TextWrapped("No palette props can be placed in this room.");
    } else {
        if (selectedProp >= static_cast<int>(choices.size())) {
            selectedProp = 0;
        }

        if (ImGui::BeginCombo("Prop", choices[selectedProp].def->label)) {
            bool headed = false;
            for (int i = 0; i < static_cast<int>(choices.size()); i++) {
                // One heading, at the boundary between the two halves PlaceableProps sorted into.
                if (!choices[i].native && !headed) {
                    headed = true;
                    if (i > 0) {
                        ImGui::Separator();
                    }
                    ImGui::TextDisabled("Not native to this room");
                }
                if (ImGui::Selectable(choices[i].def->label, i == selectedProp)) {
                    selectedProp = i;
                    paramsEdited = false; // a new prop means a new sensible default
                }
            }
            ImGui::EndCombo();
        }

        const PropDef* def = choices[selectedProp].def;
        if (!paramsEdited) {
            paramsOverride = def->params;
        }
        if (!choices[selectedProp].native) {
            ImGui::TextWrapped("Not a vanilla prop for this room - it will work, but it won't look native.");
        }
        if (def->note[0] != '\0') {
            ImGui::TextWrapped("%s", def->note);
        }

        ImGui::PushItemWidth(ImGui::GetFontSize() * 6);
        if (ImGui::InputInt("params", &paramsOverride)) {
            paramsEdited = true;
        }
        ImGui::PopItemWidth();
        ImGui::SameLine();
        ImGui::Text("(0x%X)", static_cast<uint16_t>(paramsOverride));

        ImGui::Checkbox("Snap to ground", &snapToGround);

        if (UIWidgets::Button("Place at Link")) {
            Player* player = GET_PLAYER(gPlayState);

            Placement p;
            p.sceneId = scene;
            p.room = room;
            p.actorId = def->actorId;
            p.params = static_cast<int16_t>(paramsOverride);
            p.pos = player->actor.world.pos;
            if (snapToGround) {
                p.pos.y = GroundBelow(p.pos.x, p.pos.y, p.pos.z);
            }
            // Face the prop the way Link is facing, but never rotate on x/z: several actors
            // (En_Wood02 among them) treat a non-zero home.rot.z as packed data, not a rotation.
            p.rot = { 0, player->actor.world.rot.y, 0 };
            p.label = EnrichWorld::ActorLabel(p.actorId, p.params);
            p.live = Actor_Spawn(&gPlayState->actorCtx, gPlayState, p.actorId, p.pos.x, p.pos.y, p.pos.z, p.rot.x,
                                 p.rot.y, p.rot.z, p.params);

            EnrichWorld::Placements().push_back(p);
            EnrichWorld::MarkStoreDirty();
            selectedPlacement = static_cast<int>(EnrichWorld::Placements().size()) - 1;
        }
    }

    ImGui::Separator();

    // ---- Everything placed in this room ----

    auto& placements = EnrichWorld::Placements();
    int inRoom = 0;
    for (const auto& p : placements) {
        if (p.sceneId == scene && p.room == room) {
            inRoom++;
        }
    }
    ImGui::Text("Placed here: %d   (%zu total)", inRoom, placements.size());

    if (ImGui::BeginChild("##placed", ImVec2(0, 160), true)) {
        for (int i = 0; i < static_cast<int>(placements.size()); i++) {
            auto& p = placements[i];
            if (p.sceneId != scene || p.room != room) {
                continue;
            }
            const std::string row =
                fmt::format("{}##{}  ({:.0f}, {:.0f}, {:.0f})", p.label, i, p.pos.x, p.pos.y, p.pos.z);
            if (ImGui::Selectable(row.c_str(), i == selectedPlacement)) {
                selectedPlacement = i;
            }
        }
    }
    ImGui::EndChild();

    // ---- Nudge the selected prop ----

    if (selectedPlacement >= 0 && selectedPlacement < static_cast<int>(placements.size())) {
        auto& p = placements[selectedPlacement];
        if (p.sceneId == scene && p.room == room) {
            ImGui::Text("Selected: %s", p.label.c_str());

            bool moved = false;
            ImGui::PushItemWidth(ImGui::GetFontSize() * 14);
            moved |= ImGui::DragFloat3("Position", &p.pos.x, 1.0f);
            int rotY = p.rot.y;
            if (ImGui::DragInt("Rotation", &rotY, 128.0f, -32768, 32767)) {
                p.rot.y = static_cast<int16_t>(rotY);
                moved = true;
            }
            ImGui::PopItemWidth();

            if (UIWidgets::Button("Drop to ground")) {
                p.pos.y = GroundBelow(p.pos.x, p.pos.y, p.pos.z);
                moved = true;
            }
            ImGui::SameLine();
            if (UIWidgets::Button("Move to Link")) {
                Player* player = GET_PLAYER(gPlayState);
                p.pos = player->actor.world.pos;
                if (snapToGround) {
                    p.pos.y = GroundBelow(p.pos.x, p.pos.y, p.pos.z);
                }
                moved = true;
            }
            ImGui::SameLine();
            if (UIWidgets::Button("Go to prop")) {
                Player* player = GET_PLAYER(gPlayState);
                Math_Vec3f_Copy(&player->actor.world.pos, &p.pos);
                Math_Vec3f_Copy(&player->actor.home.pos, &player->actor.world.pos);
            }

            if (moved) {
                EnrichWorld::MarkStoreDirty();
                if (p.live != nullptr) {
                    // Rotation is applied to shape.rot as well - world.rot alone won't turn the
                    // model for most props, since drawing reads shape.rot.
                    Math_Vec3f_Copy(&p.live->world.pos, &p.pos);
                    Math_Vec3f_Copy(&p.live->home.pos, &p.pos);
                    p.live->world.rot.y = p.rot.y;
                    p.live->shape.rot.y = p.rot.y;
                }
            }

            if (UIWidgets::Button("Delete")) {
                // The spawned instance goes too, otherwise it lingers until the room reloads and
                // looks like the delete silently failed.
                if (p.live != nullptr) {
                    Actor_Kill(p.live);
                }
                placements.erase(placements.begin() + selectedPlacement);
                selectedPlacement = -1;
                EnrichWorld::MarkStoreDirty();
            }
        }
    }

    ImGui::Separator();

    // ---- Persistence ----

    if (UIWidgets::Button(EnrichWorld::StoreIsDirty() ? "Save *" : "Save")) {
        EnrichWorld::SaveStore();
    }
    ImGui::SameLine();
    if (UIWidgets::Button("Reload from file")) {
        EnrichWorld::LoadStore();
        selectedPlacement = -1;
    }
    ImGui::TextWrapped("Saved props respawn on the next scene load. File: %s", EnrichWorld::StorePath().c_str());
}
