/**
 * Ganon's Curse - Phase 6: Song of Storms douses the room's torches and slowly restores magic.
 *
 * docs/item-ability-overhaul.md. Two of the three effects the spec lists for this song; the third,
 * water-raising, is deferred - it is Water Temple-specific (Bg_Mizu_Water, scene-flag driven)
 * rather than a room-AOE effect, so it is a per-scene special case that does not belong in the same
 * build as these two. See that doc's Song of Storms section for the scoping decisions.
 *
 * Dousing is the mirror of Sun's Song's torch-lighting and shares its room-AOE helper, with one
 * deliberate asymmetry: Sun's Song sets each torch's switch flag so puzzle doors actually open,
 * but this does NOT unset them. Unsetting a switch flag could strand a player behind a door they
 * had already opened, and no vanilla puzzle is known to require putting a torch out. So the flame
 * goes out; whatever it unlocked stays unlocked.
 *
 * The magic refill is deliberately slow: a full meter takes about three minutes, at either
 * capacity - the rate scales with the meter, so double magic refills twice as fast in absolute
 * units and the wall-clock feel is identical. That is the point of the effect (rain as rest), not
 * an oversight. Note this makes the song net-positive on magic - it costs 24 to cast and can return
 * a whole meter - which turns magic from a scarce resource into a time-gated one. That was a
 * deliberate call; the tuning knob if it ever feels too free is breaking the rain on any other
 * cast, deliberately not done here (the user's call: only transitions break it).
 *
 * Magic is nudged one unit at a time rather than through Magic_Fill, whose MAGIC_STATE_FILL adds
 * 4 per frame (z_parameter.c:3237) and would empty the whole three minutes into about a second. The
 * accumulator below spreads exactly one meter's worth over STORMS_RAIN_FRAMES without needing
 * fractional arithmetic. Adds only while magicState is idle, so this never fights the meter's own
 * animations - the same class of race the shared song-magic helper exists to avoid.
 */
#include "soh/ShipInit.hpp"
#include "functions.h"
#include "macros.h"
#include "variables.h"
#include "soh/Enhancements/GanonsCurse/GanonsCurseSongMagic.h"
#include "soh/Enhancements/GanonsCurse/GanonsCurseRoomAoe.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "overlays/actors/ovl_Obj_Syokudai/z_obj_syokudai.h"

extern "C" PlayState* gPlayState;

namespace {

constexpr s16 STORMS_SONG_MAGIC_COST = 24;
// Three minutes at the game's ~20Hz logic tick, the same cadence assumption Epona's Song's duration
// makes - confirm empirically and adjust if a full meter does not take roughly three real minutes.
constexpr s32 STORMS_RAIN_FRAMES = 20 * 60 * 3;

s32 sRainFramesRemaining = 0;
s32 sRefillAccumulator = 0;

void DouseTorch(Actor* actor) {
    if (actor->id != ACTOR_OBJ_SYOKUDAI) {
        return;
    }

    ObjSyokudai* torch = (ObjSyokudai*)actor;
    // litTimer: 0 unlit, -1 permanently lit, >0 counting down to auto-extinguish. Anything non-zero
    // is currently burning. The switch flag is deliberately left set - see the header comment.
    if (torch->litTimer != 0) {
        torch->litTimer = 0;
    }
}

s16 MagicMeterCapacity() {
    return (s16)((gSaveContext.isDoubleMagicAcquired + 1) * MAGIC_NORMAL_METER);
}

void GanonsCurseSongOfStormsPlayed() {
    if (!GameInteractor::IsSaveLoaded(true)) {
        return;
    }
    if (gPlayState->msgCtx.lastPlayedSong != OCARINA_SONG_STORMS) {
        return;
    }

    GanonsCurseRequestSongMagic(STORMS_SONG_MAGIC_COST, []() {
        GanonsCurseForEachActorInRoom(gPlayState, ACTORCAT_PROP, DouseTorch);
        // Replaying while it is already raining restarts the three minutes rather than stacking a
        // second refill on top, matching how Epona's Song refreshes rather than compounds.
        sRainFramesRemaining = STORMS_RAIN_FRAMES;
        sRefillAccumulator = 0;
    });
}

void GanonsCurseSongOfStormsFrameUpdate() {
    if (sRainFramesRemaining <= 0) {
        return;
    }
    if (!GameInteractor::IsSaveLoaded(true)) {
        sRainFramesRemaining = 0;
        return;
    }

    sRainFramesRemaining--;

    // Don't fight the magic meter's own state machine - only top up from a settled idle state.
    if (gSaveContext.magicState != MAGIC_STATE_IDLE) {
        return;
    }

    s16 capacity = MagicMeterCapacity();
    if (gSaveContext.magic >= capacity) {
        return;
    }

    // One full meter spread evenly across STORMS_RAIN_FRAMES, without fractional arithmetic.
    sRefillAccumulator += capacity;
    while (sRefillAccumulator >= STORMS_RAIN_FRAMES && gSaveContext.magic < capacity) {
        sRefillAccumulator -= STORMS_RAIN_FRAMES;
        gSaveContext.magic++;
    }
}

// The rain is a room-scoped effect, so it ends at the room's edge - walking through any loading
// door or zone stops it. This is the deliberate opposite of Epona's Song, whose buff was built to
// survive transitions.
void GanonsCurseSongOfStormsOnSceneInit(int16_t sceneNum) {
    sRainFramesRemaining = 0;
    sRefillAccumulator = 0;
}

} // namespace

static void RegisterGanonsCurseSongOfStorms() {
    COND_HOOK(OnOcarinaSongAction, IS_RANDO, GanonsCurseSongOfStormsPlayed);
    COND_HOOK(OnGameFrameUpdate, IS_RANDO, GanonsCurseSongOfStormsFrameUpdate);
    COND_HOOK(OnSceneInit, IS_RANDO, GanonsCurseSongOfStormsOnSceneInit);
}

static RegisterShipInitFunc ganonsCurseSongOfStormsInitFunc(RegisterGanonsCurseSongOfStorms, { "IS_RANDO" });
