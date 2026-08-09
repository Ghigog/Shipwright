/**
 * Seven Sages - Phase 6: Sun's Song lights every unlit torch in the room.
 *
 * Two effects. The torch half is here; the temporary-heart half lives in SevenSagesTempHearts.cpp
 * (its health bookkeeping runs every frame and has to be reachable from SaveManager and
 * z_lifemeter.c, neither of which has any business knowing about a song).
 *
 * docs/item-ability-overhaul.md: "lights all unlit torches in the room, burns anything
 * flammable." This file covers the torch half. The "burns anything flammable" half is not built
 * yet - there's no generic flammable-obstacle actor in this codebase to hook into (checked: no
 * cobweb/spiderweb actor exists, and the only HIT_SPECIAL_EFFECT_FIRE consumer in the whole tree
 * is the Deku Shield burn check in z_player.c). Needs a scoping decision on what "flammable"
 * concretely means here before it can be built - see the open question logged in
 * item-ability-overhaul.md.
 *
 * The generic room torch actor is Obj_Syokudai (z_obj_syokudai.c/h) - "torch" was a red herring:
 * En_Torch is a grotto chest spawner and En_Torch2 is Dark Link, neither has anything to do with
 * actual torches. Obj_Syokudai tracks its own lit state as ObjSyokudai::litTimer (0 = unlit, -1 =
 * permanently lit, >0 = counting down to auto-extinguish) and, when part of a multi-torch puzzle
 * group, a switchFlag (params & 0x3F) that some door/puzzle elsewhere in the scene may depend on.
 * Forcing litTimer alone would light the torch visually but leave any such door still locked, so
 * this also calls Flags_SetSwitch for every torch's switchFlag - setting an already-set flag is a
 * no-op, so this is safe even for torches that don't gate anything.
 *
 * This IS a deliberate bypass of "light the torches" puzzles wherever they appear (Forest/Fire/
 * Spirit Temple rooms, etc.), matching the spec's literal wording ("all unlit torches in the
 * room," no exceptions carved out). Unlike Golden Gauntlets' boss-key bypass, this isn't flagged
 * as a logic-solver concern in the spec's cross-cutting-concerns section - torch puzzles in
 * vanilla OoT are solvable immediately with whatever fire source got you to that room in the first
 * place, they don't gate reachability the way an optional item would.
 */
#include "soh/ShipInit.hpp"
#include "functions.h"
#include "macros.h"
#include "variables.h"
#include "soh/Enhancements/SevenSages/SevenSagesSongMagic.h"
#include "soh/Enhancements/SevenSages/SevenSagesRoomAoe.h"
#include "soh/Enhancements/SevenSages/SevenSagesTempHearts.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "overlays/actors/ovl_Obj_Syokudai/z_obj_syokudai.h"

extern "C" PlayState* gPlayState;

namespace {

constexpr s16 SUNS_SONG_MAGIC_COST = 24;

void LightTorch(Actor* actor) {
    if (actor->id != ACTOR_OBJ_SYOKUDAI) {
        return;
    }

    ObjSyokudai* torch = (ObjSyokudai*)actor;
    if (torch->litTimer == 0) {
        torch->litTimer = -1;
        Flags_SetSwitch(gPlayState, torch->actor.params & 0x3F);
    }
}

void SevenSagesSunsSongPlayed() {
    if (!GameInteractor::IsSaveLoaded(true)) {
        return;
    }
    if (gPlayState->msgCtx.lastPlayedSong != OCARINA_SONG_SUNS) {
        return;
    }

    SevenSagesRequestSongMagic(SUNS_SONG_MAGIC_COST, []() {
        SevenSagesForEachActorInRoom(gPlayState, ACTORCAT_PROP, LightTorch);
        SevenSagesGrantTempHearts();
    });
}

} // namespace

static void RegisterSevenSagesSunsSong() {
    COND_HOOK(OnOcarinaSongAction, IS_SEVENSAGES, SevenSagesSunsSongPlayed);
}

static RegisterShipInitFunc sevenSagesSunsSongInitFunc(RegisterSevenSagesSunsSong, { "IS_RANDO" });
