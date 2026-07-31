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
 * regardless of where the player happens to be standing. Reusing one across scenes would not work,
 * which is exactly why Zelda has no entry here (her new spawn crosses into
 * SCENE_CASTLE_COURTYARD_GUARDS_DAY).
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
 * There is no race to handle. GanonInteractor's OnSceneInit fires from OTRPlay_SpawnScene, which
 * Play_Init calls immediately before Cutscene_HandleEntranceTriggers (z_play.c:476/493) with
 * nothing in between that touches csCtx - and for every sage listed here the entrance table has no
 * matching row, so it will not overwrite what we set. Ruto is deliberately absent for the opposite
 * reason: her new spawn IS in the entrance table (gZorasFountainIntroCs, age-2 so a child
 * qualifies), so vanilla already plays her opening and an entry here would fight it.
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
#include <scenes/overworld/spot18/spot18_scene.h>
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
    // Darunia - inside Darunia's chamber. Was ENTR_GORON_CITY_UPPER_EXIT.
    { ENTR_GORON_CITY_DARUNIA_ROOM_EXIT, EVENTCHKINF_ENTERED_GORON_CITY, gGoronCityIntroCs },
    // Impa - Kakariko, arriving from the graveyard. Was ENTR_KAKARIKO_VILLAGE_FRONT_GATE.
    { ENTR_KAKARIKO_VILLAGE_SOUTHEAST_EXIT, EVENTCHKINF_ENTERED_KAKARIKO_VILLAGE, gKakarikoVillageIntroCs },
    // Nabooru - the fortress west gate. Was ENTR_GERUDOS_FORTRESS_EAST_EXIT.
    { ENTR_GERUDOS_FORTRESS_GATE_EXIT, EVENTCHKINF_ENTERED_GERUDOS_FORTRESS, gGerudoFortressIntroCs },
    // Zelda (castle courtyard) has no entry on purpose - her spawn changed scene, so no vanilla
    // shot's absolute coordinates fit. She falls through to 4c's premise text with no
    // establishing shot.
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

void PlaySageOpening(int16_t sceneNum) {
    // Deliberately gated on the SELECTED sage's own home entrance, not merely on any entrance in
    // the table below. Matching the table alone would also fire Impa's Kakariko shot for, say, a
    // Rauru run that happens to walk in from the graveyard - defensible, but it would quietly
    // change establishing-shot behaviour across the whole game rather than doing the one job this
    // file exists for. gSaveContext.entranceIndex is still the spawn entrance at this point in
    // Play_Init.
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

void RegisterGanonsCurseSageOpenings() {
    COND_HOOK(OnSceneInit, IS_RANDO, PlaySageOpening);
}

} // namespace

static RegisterShipInitFunc ganonsCurseSageOpeningsInitFunc(RegisterGanonsCurseSageOpenings, { "IS_RANDO" });
