/*
 * Seven Sages - a second way in and out of Lon Lon Ranch.
 *
 * A grotto hole at each end: one inside the ranch, one out in Hyrule Field. Walk into either and
 * you come out of the other. Two-way, on foot, both ages.
 *
 * ── Why the ranch needed one ────────────────────────────────────────────────────────────────
 *
 * Lon Lon Ranch has exactly one external edge - `RR_HYRULE_FIELD` - so it is a cul-de-sac. You
 * enter and leave by the same door, which makes visiting the beggar a round trip rather than
 * something on your way somewhere. The ranch sits in the middle of a hub-and-spoke field and is
 * still a pocket.
 *
 * Reachability does not change: both ends land in the same two logical regions the existing door
 * already connects, and Hyrule Field is ONE region in the randomizer's graph. So this alters travel
 * time and nothing else - no seed becomes unbeatable, no check becomes reachable that was not.
 *
 * ── Two rejected approaches, and why this one ───────────────────────────────────────────────
 *
 * A fence Epona can jump. The eight jumpable perimeter fences (BG_UMAJUMP params 1) exist only in
 * scene layer 4, the Ingo race, so ordinary adult play has none - and a jump is inherently one-way.
 * The point was reaching the beggar coming UP from Lake Hylia and Gerudo, which a jump-out cannot
 * do, and it would have been adult-and-horse-only besides.
 *
 * The real grotto system. Door_Ana's params pick a grotto from `grottoLoadTable` /
 * `grottoReturnTable`, arrays sized NUM_GROTTOS that rando's grotto shuffle reads. Adding entries
 * means editing randomizer internals - the same class of mistake as calling Randomizer_Item_Give
 * from a quest that is not the randomizer.
 *
 * So: the grotto MECHANISM, not the grotto SYSTEM. The hole is drawn here, the trigger is ours, and
 * the transition uses the respawn machinery vanilla already exposes.
 *
 * ── How arriving at an arbitrary spot works ─────────────────────────────────────────────────
 *
 * This is the part that makes a custom door cheap, and it is entirely vanilla. `gSaveContext.respawn`
 * is an array of three RespawnData - void-outs, grotto return points, Farore's Wind - each carrying
 * an entranceIndex, roomIndex, pos, yaw and playerParams (z64save.h:149).
 *
 * Player's exit handling resolves ENTR_RETURN_GROTTO by setting `respawnFlag = 2` and swapping in
 * `respawn[RESPAWN_MODE_RETURN].entranceIndex` (z_player.c:5290); Player_Init then sees
 * respawnFlag != 0 and copies the position straight out of `respawn[respawnFlag - 1]`
 * (z_player.c:11134). That is how leaving a grotto puts you beside the right hole, and how Farore's
 * Wind returns you to the exact spot you planted it.
 *
 * We do the same thing by hand rather than routing through ENTR_RETURN_GROTTO, because that
 * resolution lives inside Player_HandleExitsAndVoids - reached by walking onto an exit polygon,
 * which a proximity trigger never does.
 */
#include "soh/ActorDB.h"
#include "soh/ShipInit.hpp"
#include "soh/SohGui/SohGui.hpp"
#include "soh/SohGui/SohMenu.h"
#include "soh/SohGui/UIWidgets.hpp"
#include "soh/Notification/Notification.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"

#include <libultraship/bridge.h>
#include <spdlog/spdlog.h>
#include <spdlog/fmt/fmt.h>

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
#include "objects/gameplay_field_keep/gameplay_field_keep.h" // gGrottoDL

extern PlayState* gPlayState;
extern SaveContext gSaveContext;

// OPEN_DISPS declares these at its point of use (macros.h:208); a block-scope declaration in a C++
// file resolves to the mangled name and fails to link against the C symbol unless an extern "C"
// declaration is already visible.
void FrameInterpolation_RecordOpenChild(const void* a, int b);
void FrameInterpolation_RecordCloseChild(void);

s32 Object_Spawn(ObjectContext* objectCtx, s16 objectId);
}

namespace SohGui {
extern std::shared_ptr<SohMenu> mSohMenu;
} // namespace SohGui

// At file scope, NOT in the anonymous namespace below. OPEN_DISPS declares
// FrameInterpolation_RecordOpenChild at its point of use (macros.h:208), and inside an anonymous
// namespace that declaration picks up internal linkage and fails to link against the C symbol.
// Every other C++ file in the tree that draws (nametag.cpp, kaleido.cpp) sits at this scope for the
// same reason.
void TunnelDraw(Actor* thisx, PlayState* play) {
    OPEN_DISPS(play->state.gfxCtx);

    // XLU, not OPA. The grotto hole is a translucent decal laid over the ground; drawing it opaque
    // gives a black disc sitting on the grass.
    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    // Cast because the asset headers declare display lists as char[] OTR paths, and C++ will
    // not convert that to Gfx* implicitly the way the C actor files get away with.
    gSPDisplayList(POLY_XLU_DISP++, (Gfx*)gGrottoDL);

    CLOSE_DISPS(play->state.gfxCtx);
}

namespace {

// The two ends. Index is the actor's params, and `destination` is the index of the other one.
struct TunnelMouth {
    int16_t sceneNum;
    // The entrance that leads AWAY from this mouth, i.e. into the OTHER mouth's scene. Named for
    // the direction of travel because the previous name (destinationEntrance) read as "the entrance
    // belonging to the destination" and got used that way: Fall() took it off the mouth being
    // travelled TO, so walking into the ranch hole loaded ENTR_LON_LON_RANCH_ENTRANCE and dropped
    // the player back into Lon Lon at the far mouth's coordinate - empty ground, right scene, no
    // apparent transition.
    int16_t exitEntrance;
    uint8_t destination;
    float x;
    float y;
    float z;
    int16_t yaw; // which way you face on arrival at THIS mouth
};

// Both coordinates are placeholders. Place each end with its menu button and the real values
// persist; this table is only the default for someone who never does.
TunnelMouth sMouths[] = {
    // 0 - inside the ranch, up by the fence.
    { SCENE_LON_LON_RANCH, ENTR_HYRULE_FIELD_CENTER_EXIT, 1, 0.0f, 0.0f, -2400.0f, 0 },
    // 1 - out in Hyrule Field, south of the ranch.
    { SCENE_HYRULE_FIELD, ENTR_LON_LON_RANCH_ENTRANCE, 0, 0.0f, 0.0f, 0.0f, 0 },
};
constexpr uint8_t kMouthCount = (uint8_t)ARRAY_COUNT(sMouths);

// Door_Ana's own swallow thresholds (z_door_ana.c:155). Matching them means the hole behaves the
// way every other grotto in the game does rather than having its own feel.
constexpr float kSwallowRadius = 15.0f;
constexpr float kSwallowAbove = 15.0f;
constexpr float kSwallowBelow = -50.0f;

// Door_Ana draws the hole at this scale, and it is the size players read as "a grotto".
constexpr float kHoleScale = 0.01f;

// How far in front of the destination hole you land.
//
// You cannot arrive ON the hole. The mouth's coordinate is both where its hole is drawn and where
// arrivals appear, so landing exactly there puts the player inside the swallow radius on the frame
// they spawn - which fires the trigger again, sends them back, and bounces them between the two
// scenes until the game is killed. Vanilla has the same constraint: a grotto return point is
// beside the hole, never in it.
//
// Comfortably clear of kSwallowRadius (15) so a slope or a bit of drift cannot put the player back
// inside it on landing.
constexpr float kArrivalOffset = 60.0f;

// Set once the player has been clear of a mouth since it spawned.
//
// Belt to kArrivalOffset's braces, and the reason is that the offset depends on coordinates a human
// picks with a menu button. Someone will eventually place the two mouths close together, or on a
// slope that slides the player back in, and the failure mode there is an unrecoverable loop rather
// than a wrong-looking hole. This makes the trigger impossible to fire until the player has
// genuinely stood clear of it once.
bool sArmed[2] = {};

// Frames of falling before the scene changes. ONE, matching Door_Ana, and this is not a tuning
// knob - it is a correctness bound.
//
// Disabling the floor makes Link fall under gravity with nothing beneath him. Hold the transition
// and he clears the void plane, at which point the engine fires its own void-out and reloads the
// CURRENT scene at its entrance - so the tunnel looks like it teleports you back to where you came
// into the scene. An 8-frame hold did exactly that. Door_Ana sets the flag and transitions on the
// next frame for this reason; the drop still reads, because Link is visibly falling underneath the
// fade rather than before it.
constexpr uint8_t kFallFrames = 1;

// Set once a mouth has committed to swallowing the player.
//
// This latch is the whole trigger, and leaving it out is what made the tunnel look like it voided
// you out. The proximity test cannot be re-evaluated after the floor is disabled: Link immediately
// starts falling, yDistToPlayer runs past the -50 bound within a frame or two, the "player is not
// in the mouth" branch wins, and the transition is never fired - while the floor stays disabled and
// he drops until the engine voids him out and reloads the scene at its entrance.
//
// Door_Ana has the same shape for the same reason: once its targetMode is set it fires on state,
// never on distance.
bool sFalling[2] = {};
uint8_t sFallFrames[2] = {};

s16 sTunnelActorId = -1;

#define CVAR_TUNNEL(mouth, field) CVAR_ENHANCEMENT("SevenSagesTunnel." mouth "." field)

/** Register `objectId` in the current scene's bank so the actor's own gates pass. */
bool EnsureObjectLoaded(int16_t objectId) {
    if (gPlayState == nullptr || objectId <= 0 || objectId >= OBJECT_ID_MAX) {
        return false;
    }
    if (Object_GetIndex(&gPlayState->objectCtx, objectId) >= 0) {
        return true;
    }
    if (gPlayState->objectCtx.num >= OBJECT_EXCHANGE_BANK_MAX - 1) {
        return false;
    }
    Object_Spawn(&gPlayState->objectCtx, objectId);
    return Object_GetIndex(&gPlayState->objectCtx, objectId) >= 0;
}

/** Send the player down this hole and out of the other one. */
void Fall(PlayState* play, const TunnelMouth& from) {
    const TunnelMouth& to = sMouths[from.destination];

    // Seed the struct from the player's current state, then overwrite the parts that matter. Doing
    // it this way rather than filling every field means anything the engine tracks here that this
    // file does not know about still gets a sane value - which is exactly why Door_Ana calls it.
    Play_SetupRespawnPoint(play, RESPAWN_MODE_RETURN, 0x4FF);

    // In front of the destination hole rather than on it - see kArrivalOffset. Yaw 0 faces +Z, so
    // forward is (sin, cos), the same convention Actor_WorldYawTowardActor and friends use.
    const float forwardX = Math_SinS(to.yaw);
    const float forwardZ = Math_CosS(to.yaw);

    RespawnData& respawn = gSaveContext.respawn[RESPAWN_MODE_RETURN];
    respawn.entranceIndex = from.exitEntrance;
    respawn.roomIndex = (u8)0;
    respawn.pos.x = to.x + forwardX * kArrivalOffset;
    respawn.pos.y = to.y;
    respawn.pos.z = to.z + forwardZ * kArrivalOffset;
    respawn.yaw = to.yaw;
    // Cast explicit: playerParams is s16 and /WX makes MSVC C4244 fatal on the Windows CI.
    respawn.playerParams = (s16)0x04FF; // no initial camera focus, same as a grotto exit

    // 2 rather than 1: Player_Init reads respawn[respawnFlag - 1], and RESPAWN_MODE_RETURN is 1.
    // Getting this wrong lands the player at their last void-out point instead, which looks like
    // the tunnel working and going somewhere unrelated.
    gSaveContext.respawnFlag = 2;

    // The destination entrance rather than ENTR_RETURN_GROTTO: the engine only translates that
    // value inside Player_HandleExitsAndVoids, which a proximity trigger never reaches. Setting
    // the resolved entrance and respawnFlag together is what that code would have done for us.
    play->nextEntranceIndex = from.exitEntrance;
    play->transitionTrigger = TRANS_TRIGGER_START;
    play->transitionType = TRANS_TYPE_FADE_WHITE;
    gSaveContext.nextTransitionType = TRANS_TYPE_FADE_WHITE;
}

void TunnelInit(Actor* thisx, PlayState* play) {
    Actor_SetScale(thisx, kHoleScale);
}

void TunnelUpdate(Actor* thisx, PlayState* play) {
    Player* player = GET_PLAYER(play);
    if (player == nullptr || play->transitionTrigger != TRANS_TRIGGER_OFF) {
        return;
    }

    const uint8_t index = (uint8_t)thisx->params;
    if (index >= kMouthCount) {
        return;
    }

    // ── The latch sits ABOVE every entry guard, and that ordering is the fix ────────────────
    //
    // Once committed there is nothing that should stop the transition. The guards below are ENTRY
    // conditions - they decide whether you may fall in - and re-applying them after the floor is
    // disabled is what broke this: falling out of the world puts the player into a state where
    // Player_InCsMode is true, so the update bailed every frame, Fall() was never reached, and he
    // dropped until the engine voided him out and reloaded the same scene. The log showed ENTER
    // with no matching FALL, and a scene reload two seconds later.
    if (sFalling[index]) {
        if (++sFallFrames[index] >= kFallFrames) {
            Fall(play, sMouths[index]);
        }
        return;
    }

    // On a horse you would arrive on foot with Epona left behind in the other scene, and in water
    // the fall reads as drowning into the ground. Door_Ana refuses both for the same reasons.
    if (Player_InCsMode(play) || (player->stateFlags1 & (PLAYER_STATE1_ON_HORSE | PLAYER_STATE1_IN_WATER))) {
        return;
    }

    const bool inMouth = thisx->xzDistToPlayer <= kSwallowRadius && thisx->yDistToPlayer >= kSwallowBelow &&
                         thisx->yDistToPlayer <= kSwallowAbove;

    // Arm on the first frame the player is clear, and only then allow a fall. See sArmed.
    if (!inMouth) {
        sArmed[index] = true;
        return;
    }

    if (!sArmed[index]) {
        return;
    }

    // Disabling the floor is what produces the fall - the player drops through the ground under his
    // own gravity. Nothing clears this flag; the scene change does, by rebuilding the player.
    player->stateFlags1 |= PLAYER_STATE1_FLOOR_DISABLED;
    sFalling[index] = true;
    sFallFrames[index] = 0;
}

void RegisterTunnelActor() {
    if (sTunnelActorId != -1) {
        return;
    }
    ActorDBInit entry = {
        "En_SevenSagesTunnel",
        "Seven Sages Ranch Tunnel",
        ACTORCAT_PROP,
        (ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED),
        OBJECT_GAMEPLAY_FIELD_KEEP,
        sizeof(Actor),
        (ActorFunc)TunnelInit,
        nullptr,
        (ActorFunc)TunnelUpdate,
        (ActorFunc)TunnelDraw,
        nullptr,
    };
    // Explicit narrowing: AddEntry returns s32 and actor ids are s16 everywhere they are used.
    // Implicit, this is a fatal C4244 on the Windows CI, which builds with /WX.
    sTunnelActorId = (s16)ActorDB::Instance->AddEntry(entry).entry.id;
}

/** Read a mouth's persisted placement over the table default, if one was set. */
void ApplyPlacementOverrides() {
    static const char* kNames[] = { "Ranch", "Field" };

    for (uint8_t i = 0; i < kMouthCount; i++) {
        const std::string prefix = fmt::format(CVAR_ENHANCEMENT("SevenSagesTunnel.{}."), kNames[i]);
        if (CVarGetInteger((prefix + "Set").c_str(), 0) == 0) {
            continue;
        }
        sMouths[i].x = CVarGetFloat((prefix + "X").c_str(), sMouths[i].x);
        sMouths[i].y = CVarGetFloat((prefix + "Y").c_str(), sMouths[i].y);
        sMouths[i].z = CVarGetFloat((prefix + "Z").c_str(), sMouths[i].z);
        sMouths[i].yaw = (int16_t)CVarGetInteger((prefix + "Yaw").c_str(), sMouths[i].yaw);
        // The scene too: placing a mouth somewhere its table row did not expect is a legitimate
        // thing to do while trying spots, and without this the hole would spawn in the old scene at
        // the new scene's coordinate.
        sMouths[i].sceneNum = (int16_t)CVarGetInteger((prefix + "Scene").c_str(), sMouths[i].sceneNum);
    }
}

void SpawnForScene() {
    if (gPlayState == nullptr) {
        return;
    }

    ApplyPlacementOverrides();
    RegisterTunnelActor();
    if (sTunnelActorId == -1 || !EnsureObjectLoaded(OBJECT_GAMEPLAY_FIELD_KEEP)) {
        return;
    }

    const int16_t sceneNum = (int16_t)gPlayState->sceneNum;
    for (uint8_t i = 0; i < kMouthCount; i++) {
        if (sMouths[i].sceneNum != sceneNum) {
            continue;
        }
        // Disarmed until the player is seen clear of it, so an arrival that lands short cannot
        // immediately fall back through.
        sArmed[i] = false;
        sFalling[i] = false;
        sFallFrames[i] = 0;
        Actor_Spawn(&gPlayState->actorCtx, gPlayState, sTunnelActorId, sMouths[i].x, sMouths[i].y, sMouths[i].z, 0,
                    sMouths[i].yaw, 0, i);
    }
}

void RegisterRanchTunnel() {
    // OnSceneSpawnActors, not OnSceneInit: OnSceneInit fires before Actor_InitContext, whose first
    // act is memset(actorCtx, 0, ...) (z_actor.c:2541), so anything spawned there is erased a
    // hundred lines later while still logging that it spawned.
    COND_HOOK(OnSceneSpawnActors, IS_SEVENSAGES, SpawnForScene);
}

} // namespace

/**
 * Put a mouth where the player stands. `mouth` is 0 for the ranch end, 1 for the field end.
 *
 * Deliberately does not check that you are in the right scene: the two ends are independent
 * coordinates in independent scenes, and a tunnel is a teleport rather than a hole through a wall,
 * so there is nothing to keep geometrically consistent.
 */
extern "C" void SevenSagesRanchTunnel_PlaceMouth(uint8_t mouth) {
    if (gPlayState == nullptr || mouth >= kMouthCount) {
        return;
    }
    Player* player = GET_PLAYER(gPlayState);
    if (player == nullptr) {
        return;
    }

    static const char* kNames[] = { "Ranch", "Field" };
    const std::string prefix = fmt::format(CVAR_ENHANCEMENT("SevenSagesTunnel.{}."), kNames[mouth]);

    const PosRot& world = player->actor.world;
    const int16_t yaw = player->actor.shape.rot.y;

    CVarSetInteger((prefix + "Set").c_str(), 1);
    CVarSetInteger((prefix + "Scene").c_str(), gPlayState->sceneNum);
    CVarSetFloat((prefix + "X").c_str(), world.pos.x);
    CVarSetFloat((prefix + "Y").c_str(), world.pos.y);
    CVarSetFloat((prefix + "Z").c_str(), world.pos.z);
    CVarSetInteger((prefix + "Yaw").c_str(), yaw);
    CVarSave();

    sMouths[mouth].sceneNum = (int16_t)gPlayState->sceneNum;
    sMouths[mouth].x = world.pos.x;
    sMouths[mouth].y = world.pos.y;
    sMouths[mouth].z = world.pos.z;
    sMouths[mouth].yaw = yaw;

    const std::string row =
        fmt::format("{} mouth: {{ SCENE_?, ENTR_?, {}, {:.1f}f, {:.1f}f, {:.1f}f, {} }}", kNames[mouth],
                    sMouths[mouth].destination, world.pos.x, world.pos.y, world.pos.z, yaw);
    Notification::Emit({ .message = row, .remainingTime = 15 });
    SPDLOG_INFO("[SevenSages] tunnel {} - scene {}: {}", kNames[mouth], gPlayState->sceneNum, row);
}

/**
 * Add the tunnel's buttons to the co-op page.
 *
 * Called from RegisterSevenSagesCoopMenu, NOT registered as its own RegisterMenuInitFunc, and that
 * is load-bearing rather than tidiness.
 *
 * SohMenu::AddWidget asserts that the sidebar already exists (SohMenu.cpp:27), and
 * RegisterMenuInitFunc just appends to a vector in static-initialisation order (MenuTypes.h:307) -
 * which is unspecified across translation units. So "my menu function runs after the one that
 * creates the sidebar" is a coin flip decided by link order, and adding this file lost it: the
 * first launch aborted in AddWidget before the title screen.
 *
 * Being called from the function that creates the sidebar makes the ordering a fact instead.
 */
extern "C" void SevenSagesRanchTunnel_AddMenuWidgets(void) {
    WidgetPath path = { "Network", "Seven Sages Co-op", SECTION_COLUMN_2 };

    SohGui::mSohMenu->AddWidget(path, "Ranch Tunnel", WIDGET_SEPARATOR_TEXT);

    SohGui::mSohMenu->AddWidget(path, "Place Ranch Mouth Here", WIDGET_BUTTON)
        .Callback([](WidgetInfo& info) { SevenSagesRanchTunnel_PlaceMouth(0); })
        .Options(UIWidgets::ButtonOptions().Tooltip(
            "Put the ranch end of the tunnel where you stand, facing the way you face.\n"
            "\n"
            "The facing is how you come OUT of this hole when arriving from the other end."));

    SohGui::mSohMenu->AddWidget(path, "Place Field Mouth Here", WIDGET_BUTTON)
        .Callback([](WidgetInfo& info) { SevenSagesRanchTunnel_PlaceMouth(1); })
        .Options(UIWidgets::ButtonOptions().Tooltip(
            "Put the Hyrule Field end of the tunnel where you stand, facing the way you face.\n"
            "\n"
            "Takes effect on the next scene load."));
}

static RegisterShipInitFunc sevenSagesRanchTunnelInitFunc(RegisterRanchTunnel, { "IS_RANDO" });
