/**
 * Ganon's Curse - the sage's home-region establishing shot.
 *
 * Phase 4a's whole result was that six of the seven sages spawned on an entrance that already had
 * a thematically correct vanilla cutscene waiting, so restoring SkipCutscene.Entrances gave us
 * those openings for free. Phase 4's spawn relocation (2026-07-31, see the sage table in
 * savefile.cpp) breaks that for free-ness: sEntranceCutsceneTable (z_demo.c:61) matches on the
 * EXACT entranceIndex, so moving Rauru from the ranch gate to the ranch tower stops
 * gLonLonRanchIntroCs firing even though he is standing in the same field.
 *
 * This file gives the cutscene back. It is not a new cutscene or a new mechanism - it replays the
 * same vanilla CutsceneData the entrance table would have played, using 4b's two-liner
 * (Cutscene_SetSegment + cutsceneTrigger). That works precisely because every relocated spawn is
 * in the SAME SCENE as the entrance it replaced: CS_CAM_EYE/CS_CAM_AT carry absolute world
 * coordinates, so a vanilla shot reused inside its own scene is pixel-identical to the original,
 * regardless of where the player happens to be standing.
 *
 * WHY THE VANILLA EVENTCHKINF FLAG IS REUSED RATHER THAN A NEW ONE. Each entry carries the flag
 * sEntranceCutsceneTable uses for that same cutscene, and the gate here is the same
 * check-then-set. Three things fall out of that for free:
 *   - it only ever plays once per file, like vanilla;
 *   - it survives quitting, because EventChkInf lives in the save. The reverted 4c opening used a
 *     process-local arm flag and lost the opening if you quit mid-cutscene (ROADMAP.md 4c calls
 *     this out as needing a save bit) - reusing the vanilla flag IS that save bit;
 *   - walking into the region's real entrance later doesn't replay the establishing shot, which is
 *     correct: the player has already been shown that region.
 *
 * No race to handle with vanilla's own entrance-cutscene trigger: for every sage listed here (and
 * Zelda, Saria, Darunia below) the entrance table has no matching sEntranceCutsceneTable row, so
 * nothing else ever touches csCtx.segment for these entrances regardless of hook timing. Ruto and
 * Impa are deliberately absent for the opposite reason: each one's new spawn IS in the entrance
 * table in its own right (gZorasFountainIntroCs for Ruto, age-2 so a child qualifies;
 * gGraveyardIntroCs for Impa, moved to ENTR_GRAVEYARD_ENTRANCE 2026-07-31), so vanilla already
 * plays that opening and an entry here would fight it.
 *
 * Hooked on OnSceneSpawnActors, not OnSceneInit - see the comment above
 * RegisterGanonsCurseSageOpenings for why (Player doesn't exist yet at OnSceneInit, which broke
 * relativeToPlayer cutscenes specifically).
 */
// Order matters - same include-ordering constraint as GanonsCurseCutscenes.cpp: these plain-C++
// headers have to be parsed outside the extern "C" block that GanonsCurseCutscenes.h opens.
#include <soh/OTRGlobals.h>
#include "soh/ShipInit.hpp"
#include "soh/Enhancements/game-interactor/GameInteractor.h"

#include "soh/Enhancements/randomizer/savefile.h"

#include "GanonsCurseCutscenes.h"

extern "C" {
extern PlayState* gPlayState;
}

#include <scenes/overworld/spot01/spot01_scene.h>
#include <scenes/overworld/spot12/spot12_scene.h>
#include <scenes/overworld/spot20/spot20_scene.h>

namespace {

struct SageOpening {
    int32_t homeEntrance;   // must match the sage's homeEntrance in savefile.cpp exactly
    uint16_t playedFlag;    // the vanilla EVENTCHKINF for this cutscene - see the header comment
    // The scene headers define these g*Cs symbols as OTR resource PATHS, not as CutsceneData
    // arrays - Cutscene_SetSegment resolves the path. Same handling as
    // GanonsCurseVanillaCutscenes.cpp's table, which is where this pattern is already proven.
    const char* cutscene;
};

// Keyed on entrance rather than on sage so this stays honest: if someone moves a sage's spawn in
// savefile.cpp without revisiting this table, the opening simply stops firing rather than playing
// a shot framed for somewhere else.
const SageOpening sSageOpenings[] = {
    // Rauru - Lon Lon Ranch tower. Was ENTR_LON_LON_RANCH_ENTRANCE.
    { ENTR_LON_LON_RANCH_OUTSIDE_TOWER, EVENTCHKINF_ENTERED_LON_LON_RANCH, gLonLonRanchIntroCs },
    // Darunia is deliberately absent here (dropped 2026-07-31, was gGoronCityIntroCs). His chamber
    // is a different ROOM (room 1) than the plaza gGoronCityIntroCs's camera was captured in
    // (room 0) - this scene's rooms don't stay co-loaded, so the shot pointed at unrendered black
    // space. No same-room vanilla shot to fall back on, and after Saria's custom-camera miss
    // nothing here should be hand-authored blind either - see the relativeToPlayer note in
    // data/cutscenes.json for the real fix to chase before reattempting this one. Chamber spawn
    // kept (the doors are now open, see z_bg_spot18_shutter.c) - just no establishing shot.
    // Impa is deliberately absent here now (moved to ENTR_GRAVEYARD_ENTRANCE, 2026-07-31) - same
    // reasoning as Ruto: her spawn is itself a vanilla entrance-cutscene trigger (gGraveyardIntroCs),
    // not a re-fire target, and needs no entry or code of any kind in this file.
    // Nabooru - the fortress west gate. Was ENTR_GERUDOS_FORTRESS_EAST_EXIT.
    { ENTR_GERUDOS_FORTRESS_GATE_EXIT, EVENTCHKINF_ENTERED_GERUDOS_FORTRESS, gGerudoFortressIntroCs },
    // Zelda is deliberately absent here - see kZeldaHomeEntrance below, handled the same way as
    // Saria and Darunia because her shot is ours, not a whole vanilla cutscene.
};

// Saria is handled separately from the table above because her shot is ours, not a whole vanilla
// cutscene. The Sacred Forest Meadow has no vanilla ENTRANCE cutscene - it only has two NPC
// scenes, Sheik's gMinuetCs and Saria's own Saria's Song - and neither is usable: playing the one
// she stars in would show the player watching herself, the same lore break the Kokiri Forest
// greeting already needed suppressing for, and gMinuetCs is 3270 frames of conversation coverage
// built around a Sheik who isn't there.
//
// What IS reusable is gMinuetCs's opening shot in isolation: a wide (viewAngle 60) push-in on the
// Forest Temple with no actor cues and no text. It was lifted verbatim into data/cutscenes.json as
// SARIA_FOREST_TEMPLE_OPENING via tools/dump_vanilla_cutscene.py, and comes back through the 4b
// pipeline as a normal generated CutsceneData[]. Frames its subject correctly regardless of
// entrance shuffle, since it establishes the meadow's landmark building rather than asserting
// which dungeon is behind the door.
constexpr int32_t kSariaHomeEntrance = ENTR_SACRED_FOREST_MEADOW_WARP_PAD;

// Zelda's home scene was corrected 2026-07-31: nakaniwa (SCENE_CASTLE_COURTYARD_ZELDA), not
// hairal_niwa (SCENE_CASTLE_COURTYARD_GUARDS_DAY, the guard-patrolled crawlspace) - see
// savefile.cpp's SageDefinition for the full story. ZELDA_CASTLE_COURTYARD_OPENING
// (data/cutscenes.json) was rebuilt for the new scene using camera.relativeToPlayer, since her
// exact spawn coordinate within ENTR_CASTLE_COURTYARD_ZELDA_0 is binary scene data this file has
// no way to read.
constexpr int32_t kZeldaHomeEntrance = ENTR_CASTLE_COURTYARD_ZELDA_0;

// Darunia is handled separately (added 2026-07-31, replacing his table entry above) because
// gGoronCityIntroCs's own re-fire (via the table below) rendered almost entirely black at his
// chamber spawn - his chamber is a different ROOM (room 1, ENTR_GORON_CITY_DARUNIA_ROOM_EXIT)
// than the plaza that shot's camera was captured in (room 0). Four follow-up attempts in
// DARUNIA_CHAMBER_OPENING (data/cutscenes.json) also came up black; attempt 5 reverted to
// gGoronCityIntroCs's own coordinates unmodified, since that original re-fire was the only one
// that ever showed ANYTHING (a small piece of the city, distant, in one corner) - a known-partial
// baseline to iterate from rather than another fresh guess. See the cutscene's own description
// for the full attempt history.
constexpr int32_t kDaruniaHomeEntrance = ENTR_GORON_CITY_DARUNIA_ROOM_EXIT;

void PlaySageOpening() {
    // Deliberately gated on the SELECTED sage's own home entrance, not merely on any entrance in
    // the table below. Matching the table alone would also fire Impa's Kakariko shot for, say, a
    // Rauru run that happens to walk in from the graveyard - defensible, but it would quietly
    // change establishing-shot behaviour across the whole game rather than doing the one job this
    // file exists for. gSaveContext.entranceIndex doesn't change again until the next transition,
    // so it's still the spawn entrance here even though this now fires a frame later than
    // Play_Init (see the OnSceneSpawnActors note above RegisterGanonsCurseSageOpenings).
    const int32_t sageEntrance = Randomizer_GetSageHomeEntrance();
    if (sageEntrance == -1 || gSaveContext.entranceIndex != sageEntrance) {
        return;
    }

    // Saria's own shot. The table entries reuse the vanilla EVENTCHKINF their cutscene already
    // owns; the meadow has no vanilla entrance cutscene and so no flag to borrow, hence a
    // mod-owned RandomizerInf. Same play-once-per-file, survives-a-quit behaviour either way,
    // which is the property that matters.
    if (sageEntrance == kSariaHomeEntrance) {
        if (!Flags_GetRandomizerInf(RAND_INF_GANONS_CURSE_SARIA_OPENING_PLAYED)) {
            Flags_SetRandomizerInf(RAND_INF_GANONS_CURSE_SARIA_OPENING_PLAYED);
            Cutscene_SetSegment(gPlayState, gGanonsCurseSariaForestTempleOpening);
            gSaveContext.cutsceneTrigger = 1;
        }
        return;
    }

    if (sageEntrance == kZeldaHomeEntrance) {
        if (!Flags_GetRandomizerInf(RAND_INF_GANONS_CURSE_ZELDA_OPENING_PLAYED)) {
            Flags_SetRandomizerInf(RAND_INF_GANONS_CURSE_ZELDA_OPENING_PLAYED);
            Cutscene_SetSegment(gPlayState, gGanonsCurseZeldaCastleCourtyardOpening);
            gSaveContext.cutsceneTrigger = 1;
        }
        return;
    }

    if (sageEntrance == kDaruniaHomeEntrance) {
        if (!Flags_GetRandomizerInf(RAND_INF_GANONS_CURSE_DARUNIA_OPENING_PLAYED)) {
            Flags_SetRandomizerInf(RAND_INF_GANONS_CURSE_DARUNIA_OPENING_PLAYED);
            Cutscene_SetSegment(gPlayState, gGanonsCurseDaruniaChamberOpening);
            gSaveContext.cutsceneTrigger = 1;
        }
        return;
    }

    for (const SageOpening& opening : sSageOpenings) {
        if (opening.homeEntrance != sageEntrance) {
            continue;
        }
        if (Flags_GetEventChkInf(opening.playedFlag)) {
            return;
        }
        Flags_SetEventChkInf(opening.playedFlag);
        Cutscene_SetSegment(gPlayState, const_cast<char*>(opening.cutscene));
        gSaveContext.cutsceneTrigger = 1;
        return;
    }
}

// OnSceneSpawnActors, not OnSceneInit (switched 2026-07-31). OnSceneInit fires from
// OTRPlay_SpawnScene during Play_Init, before the scene's actor-spawn loop runs - Player doesn't
// exist yet. relativeToPlayer cutscenes (ZELDA_CASTLE_COURTYARD_OPENING) need
// Camera_Demo1/Camera_RotateAroundPoint (z_camera.c) to read a real, positioned player actor each
// frame; triggering before Player exists gave three straight all-black results across two scenes
// and three different offset scales - a positioning bug would vary with scale, this didn't, so
// the actual cause was never the coordinates. OnSceneSpawnActors fires from Actor_UpdateAll
// (z_actor.c) right after the room's actor-spawn loop, confirmed GET_PLAYER is valid there
// (see the reposition code this file used to have, now removed). This is later than Play_Init
// - specifically the first real game-loop frame after it - which matters only if a sage's home
// entrance also has a vanilla auto-trigger row in sEntranceCutsceneTable; none of them do, so the
// original race this file's header describes doesn't apply here. Absolute-coordinate openings
// (Saria, Darunia) don't need this fix but aren't hurt by it either - moved along with the rest
// rather than splitting the hook.
void RegisterGanonsCurseSageOpenings() {
    COND_HOOK(OnSceneSpawnActors, IS_RANDO, PlaySageOpening);
}

} // namespace

static RegisterShipInitFunc ganonsCurseSageOpeningsInitFunc(RegisterGanonsCurseSageOpenings, { "IS_RANDO" });
