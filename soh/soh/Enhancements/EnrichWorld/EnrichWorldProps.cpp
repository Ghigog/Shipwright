/**
 * Enrich World - extra set dressing in the scenes that already exist.
 *
 * Rooms carry a baked-in actor list in the asset archive. We don't rewrite it: SoH fires
 * OnSceneSpawnActors right after a room's setup actors finish spawning (z_actor.c, from
 * Actor_UpdateAll), so we can append to the room from here and leave the archive untouched.
 * Same shape as Enhancements/QoL/DaytimeGS.cpp, which does this for night-time Gold Skulltulas.
 *
 * There are two sources of placements, and both spawn:
 *
 *   1. `builtInRooms` below - content that ships with the mod. Reviewed, commented, in git.
 *   2. The JSON store (EnrichWorldStore.cpp) - whatever the in-game placer has written. This is
 *      the working set: place things by eye, save, and they're live on the next scene load with
 *      no rebuild. Entries get promoted into the table above once they've been vetted.
 *
 * THE ONE RULE: only place props already native to the scene.
 *
 * Every room loads a short list of objects (asset banks) and a prop's model lives in one of
 * them. Placing a prop whose object the room doesn't carry fails *silently*, in two different
 * ways, neither of which produces an error you'd notice:
 *
 *   - Actor_Spawn won't reject it. The missing-object path in z_actor.c falls back to bank 0
 *     when !gMapLoading, and this hook fires after map loading is done. So the spawn "succeeds".
 *   - But eight actors re-check their own object in their init function and Actor_Kill
 *     themselves when it's absent. En_Kusa (bushes) is one of them.
 *
 * The placer makes this unreachable by hand - it only offers props whose object is loaded in the
 * room you're standing in (EnrichWorldPalette.cpp). For the table below, look it up in
 * ../../../../enrich-world/data/scene-props.json and read
 * ../../../../enrich-world/docs/environment-props.md first.
 *
 * Deliberately independent of Seven Sages: gated on its own CVar and nothing else, never
 * IS_RANDO, so this works in a plain playthrough and the two mods can't observe each other.
 */
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/ShipInit.hpp"

#include <vector>

// After the headers above on purpose - see EnrichWorldPlacer.cpp for why z64.h has to come last.
#include "EnrichWorld.h"

extern "C" {
#include "functions.h"
#include "macros.h"
extern PlayState* gPlayState;
}

#define CVAR_ENRICH_WORLD_NAME CVAR_ENHANCEMENT("EnrichWorld")
#define CVAR_ENRICH_WORLD_VALUE CVarGetInteger(CVAR_ENRICH_WORLD_NAME, 0)

namespace {

// One batch of props to append to a single room. Scene and room numbers, then the actors
// themselves in the same {id, pos, rot, params} form the archive's own actor lists use.
struct EnrichedRoom {
    int16_t scene;
    int16_t room;
    std::vector<ActorEntry> props;
};

// Check the prop's object is native to that room before adding an entry (see the header
// comment). If a scene ever needs props to differ by age or time of day, this struct is where
// that filter goes - vanilla varies room contents the same way, via alternate scene headers.
const std::vector<EnrichedRoom> builtInRooms = {
    // Hyrule Field - two trees flanking the Market drawbridge where it lands in the field.
    //
    // OBJECT_WOOD02 is in spot00's base object list, and params 0x0205 (WOOD_TREE_OVAL_GREEN,
    // drop table 0x02) is exactly what the three vanilla trees on the west bank already use.
    //
    // Not literally beside the gate walls: those sit at x +/-180..340, z 360..680, and the moat
    // (floor y = -140, spanning x -800..800 / z 700..1000) runs along both of their outer faces,
    // so there is no ground to stand a tree on there. z = 1400 is the first flat y = 0 ground
    // past the bridge, which frames the exit instead. Both spots verified against the scene
    // collision mesh: ground y = 0, >= 315 units from the nearest wall, >= 544 from any existing
    // actor. The west bank already has vanilla trees; the east was bare.
    //
    // rot.z must stay 0 - EnWood02_Init treats a non-zero home.rot.z as a packed drop/flag value
    // and rewrites params. rot.y is free and is only varied here so the pair doesn't look cloned.
    { SCENE_HYRULE_FIELD,
      0,
      {
          { ACTOR_EN_WOOD02, { -650, 0, 1400 }, { 0, 8000, 0 }, 517 },
          { ACTOR_EN_WOOD02, { 650, 0, 1400 }, { 0, -12000, 0 }, 517 },
      } },
};

void SpawnBuiltIns() {
    for (const auto& room : builtInRooms) {
        if (room.scene != gPlayState->sceneNum || room.room != gPlayState->roomCtx.curRoom.num) {
            continue;
        }
        for (const auto& prop : room.props) {
            Actor_Spawn(&gPlayState->actorCtx, gPlayState, prop.id, prop.pos.x, prop.pos.y, prop.pos.z, prop.rot.x,
                        prop.rot.y, prop.rot.z, prop.params);
        }
    }
}

void SpawnFromStore() {
    for (auto& p : EnrichWorld::Placements()) {
        // Every placement's cached instance belongs to the room it was spawned into. Clearing on
        // the way past means a stale Actor* can never outlive its room and get nudged by the
        // placer into a freed slot.
        if (p.sceneId != gPlayState->sceneNum || p.room != gPlayState->roomCtx.curRoom.num) {
            p.live = nullptr;
            continue;
        }
        p.live = Actor_Spawn(&gPlayState->actorCtx, gPlayState, p.actorId, p.pos.x, p.pos.y, p.pos.z, p.rot.x, p.rot.y,
                             p.rot.z, p.params);
    }
}

void EnrichWorldOnSceneSpawnActors() {
    SpawnBuiltIns();
    SpawnFromStore();
}

void RegisterEnrichWorldProps() {
    COND_HOOK(OnSceneSpawnActors, CVAR_ENRICH_WORLD_VALUE, EnrichWorldOnSceneSpawnActors);
}

} // namespace

static RegisterShipInitFunc enrichWorldPropsInitFunc(RegisterEnrichWorldProps, { CVAR_ENRICH_WORLD_NAME });
