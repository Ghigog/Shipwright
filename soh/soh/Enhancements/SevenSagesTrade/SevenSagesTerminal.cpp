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
constexpr TerminalSpawn kTerminals[] = {
    // Lon Lon Ranch - the central terminal, near the crates and tree by the ranch buildings
    // (ACTOR_OBJ_KIBAKO2 at 1160,0,-2376; ACTOR_EN_WOOD02 at 1309,0,-2241).
    { SCENE_LON_LON_RANCH, 1230.0f, 60.0f, -2300.0f },

    // Hyrule Market - by the crates off the main square (ACTOR_OBJ_KIBAKO2 at 490,0,132).
    { SCENE_MARKET_DAY, 540.0f, 60.0f, 150.0f },

    // Death Mountain Trail - the ring-of-rocks clearing outside Goron City's entrance. The rock
    // cluster is real and tightly grouped (ACTOR_EN_ISHI at -1816/-1831/-1857, y 681, z -513..-614),
    // which is the landmark the design doc named.
    { SCENE_DEATH_MOUNTAIN_TRAIL, -1780.0f, 740.0f, -560.0f },

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
}

static RegisterShipInitFunc sevenSagesTerminalInitFunc(RegisterSevenSagesTerminal, { "IS_RANDO" });
