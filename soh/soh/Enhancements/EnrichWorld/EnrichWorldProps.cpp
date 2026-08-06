/**
 * Enrich World - extra set dressing in the scenes that already exist.
 *
 * Rooms carry a baked-in actor list in the asset archive. We don't rewrite it: SoH fires
 * OnSceneSpawnActors right after a room's setup actors finish spawning (z_actor.c, from
 * Actor_UpdateAll), so we can append to the room from here and leave the archive untouched.
 * Same shape as Enhancements/QoL/DaytimeGS.cpp, which does this for night-time Gold Skulltulas.
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
 * So a bad entry either quietly doesn't exist or renders as garbage. Which props are native to
 * which room is not guessable - look it up in ../../../../enrich-world/data/scene-props.json
 * (every vanilla prop in 388 rooms, with coordinates), and read
 * ../../../../enrich-world/docs/environment-props.md before adding anything.
 *
 * Deliberately independent of Seven Sages: gated on its own CVar and nothing else, never
 * IS_RANDO, so this works in a plain playthrough and the two mods can't observe each other.
 */
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/ShipInit.hpp"

#include <vector>

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

// Nothing placed yet - this is the scaffold. Entries look like:
//
//     { SCENE_HYRULE_FIELD, 0, {
//         { ACTOR_OBJ_HANA, { 1234, 0, -5678 }, { 0, 0, 0 }, 0 },
//     } },
//
// Check the prop's object is native to that room first (see the header comment). If a scene
// ever needs props to differ by age or time of day, this struct is where that filter goes -
// vanilla varies room contents the same way, via alternate scene headers.
const std::vector<EnrichedRoom> enrichedRooms = {};

void EnrichWorldOnSceneSpawnActors() {
    for (const auto& room : enrichedRooms) {
        if (room.scene != gPlayState->sceneNum || room.room != gPlayState->roomCtx.curRoom.num) {
            continue;
        }
        for (const auto& prop : room.props) {
            Actor_Spawn(&gPlayState->actorCtx, gPlayState, prop.id, prop.pos.x, prop.pos.y, prop.pos.z, prop.rot.x,
                        prop.rot.y, prop.rot.z, prop.params);
        }
    }
}

void RegisterEnrichWorldProps() {
    COND_HOOK(OnSceneSpawnActors, CVAR_ENRICH_WORLD_VALUE, EnrichWorldOnSceneSpawnActors);
}

} // namespace

static RegisterShipInitFunc enrichWorldPropsInitFunc(RegisterEnrichWorldProps, { CVAR_ENRICH_WORLD_NAME });
