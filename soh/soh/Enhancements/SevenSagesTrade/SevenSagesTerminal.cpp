/*
 * Seven Sages trade box - the terminal actor and where the terminals stand.
 *
 * See SevenSagesTerminal.h for why the model comes from gameplay_keep.
 */

#include "SevenSagesTerminal.h"
#include "SevenSagesStash.h"

#include "soh/ActorDB.h"
#include "soh/ShipInit.hpp"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"

#include <spdlog/spdlog.h>

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
#include "objects/gameplay_keep/gameplay_keep.h"

extern PlayState* gPlayState;
}

namespace {

s16 sTerminalActorId = -1;
bool sWindowOpen = false;
bool sPlayerInRange = false;

// How close the player must be to use a terminal, in world units. Roughly the range vanilla NPCs
// use for a talk offer.
constexpr float kUseRadius = 80.0f;

struct TerminalSpawn {
    int16_t sceneNum;
    float x;
    float y;
    float z;
};

// The four v1 terminals. Adding one is a row here and nothing else, because all of them are access
// points into the same stash - which is exactly why decision 5 chose one shared stash.
//
// x/z are taken from real vanilla prop coordinates in the target scene
// (enrich-world/data/scene-props.json), so they sit on ground the game already puts objects on
// rather than on a guess. y is deliberately generous: the actor falls under gravity and snaps to
// the floor in Update, so the height only has to start ABOVE the ground rather than on it. That
// turns the one coordinate that is hardest to get right from source into a non-issue.
//
// ── CHECK EVERY ALTERNATE HEADER, NOT JUST `base` ───────────────────────────────────────────
//
// `base` is the CHILD layout. Scenes carry alternate headers for the other age and time of day,
// and a terminal spawns in all of them, so a spot chosen from `base` alone has only been checked
// against half the game. Lon Lon Ranch has twelve headers; the first pick here sat between a crate
// and a tree that exist only for child, and landed squarely inside the ADULT cucco pen - five
// EN_NIW at x 1106-1299, z -2200..-2338 in header _0002F0.
//
// That is not just cosmetic. A is also "pick up cucco", so the terminal would have been fighting
// the birds for the interact press - the sort of conflict that reads as "the box is broken".
constexpr TerminalSpawn kTerminals[] = {
    // Lon Lon Ranch - the central terminal. The open yard between the horse corral (horses roam
    // z -1343..+429) and the cucco pen (z -2200 and back), clear of both, and clear of Ingo's
    // gates at z -2420. Verified against the child `base` layout and the adult `_0002F0` one.
    { SCENE_LON_LON_RANCH, 1000.0f, 60.0f, -2100.0f },

    // Hyrule Market - off the main square, kept clear of the two crates at (490, 132) and
    // (490, 338). Those are OBJ_KIBAKO2 and liftable, so a terminal beside them would be
    // competing for the A press; ~130 units away is outside the grab but still the same corner.
    { SCENE_MARKET_DAY, 620.0f, 60.0f, 235.0f },

    // Death Mountain Trail - just north of the ring-of-rocks clearing the design doc named, not
    // inside it. The EN_ISHI cluster runs x -1787..-1878, z -465..-614 with a signpost at
    // (-1834, -571); the first pick sat 7 units from one of those rocks. Rocks lift with A, same
    // conflict as the cuccos. This sits ~85 north of the nearest, so the clearing is still the
    // landmark you navigate by. Also clear of the warp at (-1656, -519) in header _0009B0.
    { SCENE_DEATH_MOUNTAIN_TRAIL, -1800.0f, 740.0f, -380.0f },

    // Fishing Pond. UNVERIFIED COORDINATE - turibori carries no props in scene-props.json, so
    // unlike the three above this x/z is not derived from anything. The floor snap will fix the
    // height, but if this lands in the water or inside geometry it needs moving by eye. First
    // thing to check when playtesting terminals.
    { SCENE_FISHING_POND, 0.0f, 100.0f, -200.0f },
};

typedef struct {
    Actor actor;
} SevenSagesTerminalActor;

void TerminalInit(Actor* thisx, PlayState* play) {
    // Calibrated against En_Ishi, which draws rocks from this same object at 0.1 for the small
    // liftable one and 0.4 for the large one (sRockScales, z_en_ishi.c:57). A terminal has to read
    // as a landmark you spot from across the field, so it sits just above the large rock.
    //
    // This started at 0.05 - half the SMALLEST vanilla rock - and the result was a pebble that was
    // effectively invisible in Lon Lon Ranch even though it had spawned correctly. Anchor any new
    // scale to those two numbers rather than guessing.
    Actor_SetScale(thisx, 0.5f);
    thisx->gravity = -2.0f;
}

void TerminalDestroy(Actor* thisx, PlayState* play) {
}

void TerminalUpdate(Actor* thisx, PlayState* play) {
    // Settle onto the floor. This is what lets the spawn table carry an approximate height - see
    // the note above kTerminals.
    Actor_MoveXZGravity(thisx);
    // 0xC5 is the flag set En_Ishi uses for exactly this - floor, wall and ceiling checks with
    // the actor's position updated from them. Same shape of object, same needs.
    Actor_UpdateBgCheckInfo(play, thisx, 7.5f, 35.0f, 0.0f, 0xC5);

    Player* player = GET_PLAYER(play);
    if (player == nullptr) {
        return;
    }

    const bool inRange = Actor_WorldDistXZToActor(thisx, &player->actor) < kUseRadius;
    sPlayerInRange = inRange;

    if (!inRange) {
        // Walking away closes the window, which keeps the terminal from being a thing you open once
        // and then carry around with you.
        if (sWindowOpen) {
            SevenSagesTerminal_Close();
        }
        return;
    }

    if (!sWindowOpen && CHECK_BTN_ALL(play->state.input[0].press.button, BTN_A)) {
        SevenSagesTerminal_Open();
    }
}

void TerminalDraw(Actor* thisx, PlayState* play) {
    Gfx_DrawDListOpa(play, (Gfx*)gLiftableRockDL);
}

void RegisterTerminalActor() {
    if (sTerminalActorId != -1) {
        return;
    }

    ActorDBInit entry = {
        "En_SevenSagesTerminal",
        "Seven Sages Trade Terminal",
        ACTORCAT_PROP,
        // Culling disabled so the terminal does not vanish at the range where the player is still
        // close enough to use it, and so it keeps falling to the floor on the frame it spawns.
        (ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED),
        OBJECT_GAMEPLAY_KEEP,
        sizeof(SevenSagesTerminalActor),
        (ActorFunc)TerminalInit,
        (ActorFunc)TerminalDestroy,
        (ActorFunc)TerminalUpdate,
        (ActorFunc)TerminalDraw,
        nullptr,
    };
    // Explicit narrowing: AddEntry hands back an s32 id and actor ids are s16 everywhere they are
    // used. Left implicit this is a fatal C4244 on the Windows CI, which builds with /WX.
    sTerminalActorId = (s16)ActorDB::Instance->AddEntry(entry).entry.id;
}

void SpawnTerminalsForScene(int16_t sceneNum) {
    if (gPlayState == nullptr) {
        return;
    }

    RegisterTerminalActor();
    if (sTerminalActorId == -1) {
        return;
    }

    for (const TerminalSpawn& spawn : kTerminals) {
        if (spawn.sceneNum != sceneNum) {
            continue;
        }
        Actor_Spawn(&gPlayState->actorCtx, gPlayState, sTerminalActorId, spawn.x, spawn.y, spawn.z, 0, 0, 0, 0);
        SPDLOG_INFO("[SevenSages] trade terminal spawned in scene {} at {},{},{}", sceneNum, spawn.x, spawn.y, spawn.z);
    }
}

void RegisterSevenSagesTerminal() {
    // IS_SEVENSAGES rather than IS_RANDO: the trade box is a Seven Sages feature, and gating it on
    // IS_RANDO would put terminals in a plain Randomizer run too. That exact mistake is documented
    // in savefile.h.
    COND_HOOK(OnSceneInit, IS_SEVENSAGES, SpawnTerminalsForScene);
}

} // namespace

extern "C" bool SevenSagesTerminal_PlayerIsAtTerminal(void) {
    return sPlayerInRange;
}

extern "C" void SevenSagesTerminal_Open(void) {
    sWindowOpen = true;
}

extern "C" bool SevenSagesTerminal_IsOpen(void) {
    return sWindowOpen;
}

extern "C" void SevenSagesTerminal_Close(void) {
    sWindowOpen = false;
}

// Spawn a terminal at the player's feet, for testing placement without walking to one. Exposed
// through the menu; see SevenSagesTradeWindow.cpp.
extern "C" void SevenSagesTerminal_SpawnAtPlayer(void) {
    if (gPlayState == nullptr) {
        return;
    }
    RegisterTerminalActor();
    if (sTerminalActorId == -1) {
        return;
    }

    Player* player = GET_PLAYER(gPlayState);
    if (player == nullptr) {
        return;
    }

    const PosRot& world = player->actor.world;
    Actor_Spawn(&gPlayState->actorCtx, gPlayState, sTerminalActorId, world.pos.x, world.pos.y + 20.0f, world.pos.z, 0,
                0, 0, 0);

    // Log the coordinate in the exact shape of a kTerminals row. Picking these from scene data
    // alone has now been wrong three times - the child/adult header split, and two spots that
    // landed on top of liftable props - so the reliable way to choose one is to stand where it
    // should go and read the number off.
    SPDLOG_INFO("[SevenSages] terminal placed by hand - scene {}: {{ SCENE_?, {:.1f}f, {:.1f}f, {:.1f}f }}",
                gPlayState->sceneNum, world.pos.x, world.pos.y + 20.0f, world.pos.z);
}

static RegisterShipInitFunc sevenSagesTerminalInitFunc(RegisterSevenSagesTerminal, { "IS_RANDO" });
