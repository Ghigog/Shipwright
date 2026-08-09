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
#include <algorithm>
#include <cstring>
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

/**
 * A button sized to its own label instead of the rest of the row.
 *
 * UIWidgets::ButtonOptions defaults size to Sizes::Fill, so a plain UIWidgets::Button eats all
 * remaining width and anything after an ImGui::SameLine() lands outside the window. That is not
 * a clip you can see - the row just looks like it has one button on it, which is how Delete,
 * Move to Link, Go to prop and the prop stepper's "+" all went missing at this window's width.
 * Every button that shares a row goes through this.
 */
bool RowButton(const char* label) {
    return UIWidgets::Button(label, UIWidgets::ButtonOptions().Size(UIWidgets::Sizes::Inline));
}

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
    // Then by group within each half, so the dropdown reads as sections. stable_sort keeps the
    // palette's own order inside a group, which is deliberate - variants of one prop were
    // written next to each other and should stay that way.
    std::stable_sort(out.begin(), out.end(), [](const Choice& a, const Choice& b) {
        if (a.native != b.native) {
            return a.native; // natives first, as before
        }
        return EnrichWorld::PropGroupRank(EnrichWorld::PropGroup(a.def->actorId)) <
               EnrichWorld::PropGroupRank(EnrichWorld::PropGroup(b.def->actorId));
    });
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
        // Categories present in this room, in palette order. Built per frame from `choices` so a
        // category with nothing placeable here never appears at all.
        std::vector<const char*> groups;
        for (const auto& choice : choices) {
            const char* group = EnrichWorld::PropGroup(choice.def->actorId);
            const bool seen = std::any_of(groups.begin(), groups.end(),
                                          [&](const char* g) { return std::strcmp(g, group) == 0; });
            if (!seen) {
                groups.push_back(group);
            }
        }
        selectedGroup = std::clamp(selectedGroup, 0, static_cast<int>(groups.size()) - 1);

        if (ImGui::BeginCombo("Category", groups[selectedGroup])) {
            for (int i = 0; i < static_cast<int>(groups.size()); i++) {
                if (ImGui::Selectable(groups[i], i == selectedGroup)) {
                    selectedGroup = i;
                    // Forget the prop; the resolve below adopts this category's first entry.
                    selectedActorId = -1;
                    paramsEdited = false;
                }
            }
            ImGui::EndCombo();
        }

        // Props inside the chosen category, natives first - PlaceableProps already sorted that
        // way, so filtering preserves it.
        std::vector<int> inGroup;
        for (int i = 0; i < static_cast<int>(choices.size()); i++) {
            if (std::strcmp(EnrichWorld::PropGroup(choices[i].def->actorId), groups[selectedGroup]) == 0) {
                inGroup.push_back(i);
            }
        }
        // Find the remembered prop in this frame's list. Falling back to the first entry covers
        // the three ways it can be absent: nothing chosen yet, the category just changed, or the
        // prop stopped being placeable in this room.
        int selectedProp = -1;
        for (int i = 0; i < static_cast<int>(inGroup.size()); i++) {
            const PropDef* d = choices[inGroup[i]].def;
            if (d->actorId == selectedActorId && d->params == selectedParams) {
                selectedProp = i;
                break;
            }
        }
        // Writes the identity back, so the selection survives the next re-sort.
        const auto chooseProp = [&](int index) {
            selectedProp = index;
            selectedActorId = choices[inGroup[index]].def->actorId;
            selectedParams = choices[inGroup[index]].def->params;
            paramsEdited = false;
        };
        if (selectedProp < 0) {
            chooseProp(0);
        }

        // Step through the category one prop at a time. The arrows wrap, because with a category
        // of two or three entries stepping off the end and stopping is just annoying.
        if (RowButton("-##prop")) {
            chooseProp((selectedProp + static_cast<int>(inGroup.size()) - 1) % static_cast<int>(inGroup.size()));
        }
        ImGui::SameLine();
        if (RowButton("+##prop")) {
            chooseProp((selectedProp + 1) % static_cast<int>(inGroup.size()));
        }
        ImGui::SameLine();
        ImGui::Text("%d/%d", selectedProp + 1, static_cast<int>(inGroup.size()));

        const int chosen = inGroup[selectedProp];
        if (ImGui::BeginCombo("Prop", choices[chosen].def->label)) {
            bool headed = false;
            for (int i = 0; i < static_cast<int>(inGroup.size()); i++) {
                if (!choices[inGroup[i]].native && !headed) {
                    headed = true;
                    if (i > 0) {
                        ImGui::Separator();
                    }
                    ImGui::TextDisabled("Not native to this room");
                }
                if (ImGui::Selectable(choices[inGroup[i]].def->label, i == selectedProp)) {
                    chooseProp(i); // clears paramsEdited - a new prop means a new sensible default
                }
            }
            ImGui::EndCombo();
        }

        const PropDef* def = choices[chosen].def;
        if (!paramsEdited) {
            paramsOverride = def->params;
        }
        if (!choices[chosen].native) {
            ImGui::TextWrapped("Not a vanilla prop for this room. It still renders - SoH resolves models by "
                               "resource name, not from the room's object list. It just won't look native.");
        }
        if (def->note[0] != '\0') {
            ImGui::TextWrapped("%s", def->note);
        }

        // Which prop this is lives in the same 16-bit word as its options, so the box edits only
        // the bits outside the variant mask and the palette entry keeps the rest. Editing the
        // whole word let "Butterflies" be turned into a Fish, or into an Obj_Mure type that kills
        // itself on init - the dropdown and the box were two views of one value, fighting.
        const uint16_t variantMask = EnrichWorld::VariantMask(def->actorId);
        const uint16_t optionMask = EnrichWorld::OptionMask(def->actorId);
        const uint16_t identity = static_cast<uint16_t>(def->params) & variantMask;

        if (optionMask == 0) {
            // No dead control: this prop either ignores params or reads only the bits the
            // dropdown already owns, so every value the box could offer is the same prop.
            ImGui::Text("params 0x%X - no options, the dropdown is the whole choice",
                        static_cast<uint16_t>(paramsOverride));
        } else {
            int options = static_cast<uint16_t>(paramsOverride) & optionMask;
            const auto setOptions = [&](int value) {
                paramsOverride = identity | (static_cast<uint16_t>(value) & optionMask);
                paramsEdited = true;
            };

            // Step by the mask's lowest set bit, so one press moves the field by one rather than
            // by one *bit* - En_Wood02's drop table lives in 0xFF00 and wants +0x100 a press,
            // En_Kanban's sign text lives in 0x00FF and wants +1. Masking after the add wraps the
            // field instead of carrying into bits the actor doesn't read. A mask with holes in it
            // will skip the values that fall in a hole; nothing unsafe, just a shorter walk.
            const uint16_t step = optionMask & static_cast<uint16_t>(~optionMask + 1);
            if (RowButton("-##opt")) {
                setOptions(options - step);
            }
            ImGui::SameLine();
            if (RowButton("+##opt")) {
                setOptions(options + step);
            }
            ImGui::SameLine();
            ImGui::PushItemWidth(ImGui::GetFontSize() * 6);
            if (ImGui::InputInt("options", &options, 0, 0)) {
                setOptions(options);
            }
            ImGui::PopItemWidth();
            ImGui::Text("-> params 0x%X", static_cast<uint16_t>(paramsOverride));
            ImGui::TextWrapped("Only bits 0x%X do anything on this prop - the rest are ignored by the "
                               "actor or belong to the dropdown.",
                               optionMask);
        }

        // Belt and braces: the mask above means a palette entry can no longer produce an
        // out-of-range variant, but actors with no known layout still take a free-form word.
        const bool paramsSafe = EnrichWorld::AreParamsSafe(def->actorId, static_cast<int16_t>(paramsOverride));
        if (!paramsSafe) {
            ImGui::TextWrapped("This actor has no such variant - placing it would read past the end of "
                               "its table and crash.");
        }

        ImGui::Checkbox("Snap to ground", &snapToGround);

        ImGui::BeginDisabled(!paramsSafe);
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
            // Same ordering requirement as the store's respawn - the bank slot is bound at spawn.
            EnrichWorld::EnsureObjectLoaded(def->nativeObjectId);
            p.live = Actor_Spawn(&gPlayState->actorCtx, gPlayState, p.actorId, p.pos.x, p.pos.y, p.pos.z, p.rot.x,
                                 p.rot.y, p.rot.z, p.params);

            EnrichWorld::Placements().push_back(p);
            EnrichWorld::MarkStoreDirty();
            selectedPlacement = static_cast<int>(EnrichWorld::Placements().size()) - 1;
        }
        ImGui::EndDisabled();
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
            // `live` is set at spawn and cleared by the OnActorDestroy hook, so a null pointer on
            // a placement in the room you are standing in means the actor is not there - almost
            // always because it Actor_Killed itself during Init. Dozens of actors do that on
            // conditions a hand-placed prop can't satisfy: a scene setup layer, Link's age, an
            // event flag, a switch flag, the scene number. Proving each one survivable in advance
            // is not practical, but showing which ones actually made it costs nothing and turns a
            // silent disappearance into something you can see.
            //
            // A withdrawn actor is a different thing and needs saying differently: the store
            // outlives the palette, and SpawnFromStore deliberately refuses to spawn a row whose
            // actor has been dropped. Reporting that as "killed itself on spawn" would send you
            // looking for a game condition that isn't the reason.
            const bool withdrawn = !EnrichWorld::IsInPalette(p.actorId);
            const bool alive = p.live != nullptr;
            if (!alive) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.45f, 1.0f));
            }
            const char* tag = alive ? "" : (withdrawn ? "[withdrawn] " : "[dead] ");
            const std::string row =
                fmt::format("{}{}##{}  ({:.0f}, {:.0f}, {:.0f})", tag, p.label, i, p.pos.x, p.pos.y, p.pos.z);
            if (ImGui::Selectable(row.c_str(), i == selectedPlacement)) {
                selectedPlacement = i;
            }
            if (!alive) {
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(withdrawn ? "This prop was removed from the palette because it "
                                                  "misbehaved, so it is no longer spawned. Delete the row."
                                                : "This actor killed itself on spawn. Usually it wants "
                                                  "something this scene doesn't provide - a setup layer, an "
                                                  "event flag, Link's age.");
                }
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

            if (RowButton("Drop to ground")) {
                p.pos.y = GroundBelow(p.pos.x, p.pos.y, p.pos.z);
                moved = true;
            }
            ImGui::SameLine();
            if (RowButton("Move to Link")) {
                Player* player = GET_PLAYER(gPlayState);
                p.pos = player->actor.world.pos;
                if (snapToGround) {
                    p.pos.y = GroundBelow(p.pos.x, p.pos.y, p.pos.z);
                }
                moved = true;
            }
            ImGui::SameLine();
            if (RowButton("Go to prop")) {
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

            bool duplicated = false;
            if (RowButton("Duplicate")) {
                duplicated = true;
                // Offset rather than placed exactly on top, so the copy is visible and clickable
                // straight away instead of z-fighting with its original. 30 units is roughly a
                // bush's width - close enough to read as a pair, far enough to grab.
                Placement copy = p;
                copy.pos.x += 30.0f;
                copy.pos.z += 30.0f;
                if (snapToGround) {
                    copy.pos.y = GroundBelow(copy.pos.x, copy.pos.y, copy.pos.z);
                }
                EnrichWorld::EnsureObjectLoaded(EnrichWorld::NativeObjectForActor(copy.actorId));
                copy.live = Actor_Spawn(&gPlayState->actorCtx, gPlayState, copy.actorId, copy.pos.x, copy.pos.y,
                                        copy.pos.z, copy.rot.x, copy.rot.y, copy.rot.z, copy.params);

                // push_back can reallocate, so `p` is dangling from here on - don't touch it.
                EnrichWorld::Placements().push_back(copy);
                EnrichWorld::MarkStoreDirty();
                selectedPlacement = static_cast<int>(EnrichWorld::Placements().size()) - 1;
            }
            ImGui::SameLine();
            // `p` is a reference into Placements() and Duplicate just push_back'd, so it is
            // dangling now. Skipping the rest of the block is what makes that safe rather than
            // relying on ImGui not reporting two buttons pressed in one frame.
            if (!duplicated &&
                UIWidgets::Button("Delete", UIWidgets::ButtonOptions()
                                                .Size(UIWidgets::Sizes::Inline)
                                                .Color(UIWidgets::Colors::DarkRed))) {
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

    if (RowButton(EnrichWorld::StoreIsDirty() ? "Save *" : "Save")) {
        EnrichWorld::SaveStore();
    }
    ImGui::SameLine();
    if (RowButton("Reload from file")) {
        EnrichWorld::LoadStore();
        selectedPlacement = -1;
    }
    ImGui::TextWrapped("Saved props respawn on the next scene load. File: %s", EnrichWorld::StorePath().c_str());
}
