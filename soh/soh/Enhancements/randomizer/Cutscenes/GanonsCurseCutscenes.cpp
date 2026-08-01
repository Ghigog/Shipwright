/**
 * Ganon's Curse - Phase 4b: the JSON-authored cutscene pipeline.
 *
 * A cutscene is a plain CutsceneData[] array (z64cutscene_commands.h macros) -
 * playing one is `play->csCtx.segment = <array>; gSaveContext.cutsceneTrigger = 1;`,
 * the same two lines ~15 vanilla actors already use (e.g. z_item_ocarina.c:175).
 * Cutscene_SetSegment() below is the safer helper over raw assignment: it only
 * wraps in SEGMENTED_TO_VIRTUAL for ROM-segment data, which these compiled-in
 * arrays are not (see z_bg_breakwall.c:258 for the vanilla precedent).
 *
 * v1 scope is deliberately narrow (see ROADMAP.md 4b/4d): SAME-SCENE ONLY, no
 * CS_TERMINATOR, no actor cues. That is not a validator rule, it's a generator
 * one - data/cutscenes.json's schema has no field to request either, so the two
 * real softlock/no-op failure modes documented in ROADMAP.md can't be authored
 * at all right now. Extend deliberately if 4d ever needs a scene transition or
 * actor choreography; don't hand-edit the generated regions below to add one.
 *
 * Custom dialogue reuses the exact mechanism GanonsCurseNpcHints.cpp already
 * established: an OnOpenText hook builds a CustomMessage on the fly and sets
 * loadFromMessageTable = false, rather than going through
 * CustomMessageManager::CreateMessage's table/registration path (that
 * mechanism exists for a different purpose - see MessageViewer.cpp's debug UI -
 * and has no established textID-range convention to build on). Text IDs here
 * are assigned by the generator from 0xF800-0xFFFE, confirmed unused by any
 * vanilla message table (grepped soh/include/message_data_static.h) and
 * disjoint from npc-hints.json's IDs (which are all real vanilla IDs, always
 * < 0x8000).
 */
// Order matters: these plain-C++ includes must come before GanonsCurseCutscenes.h
// pulls in functions.h/variables.h/macros.h inside its extern "C" block. Several
// libultraship headers reachable from that C chain (e.g. macros.h's own
// ship/utils/binarytools/endianness.h) are template-using C++ headers; if their
// *first* inclusion in this translation unit happens while still inside an
// extern "C" block, the templates fail to parse ("templates must have C++
// linkage"). Getting them parsed once here in ordinary C++ context first makes
// every later re-inclusion an include-guard no-op instead - the same reason
// GanonsCurseNpcHints.cpp orders its includes this way.
#include <soh/OTRGlobals.h>
#include "soh/ShipInit.hpp"
#include "soh/Enhancements/custom-message/CustomMessageManager.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"

#include <ship/Context.h>
#include <ship/debug/Console.h>

#include <string>
#include <unordered_map>

#include "GanonsCurseCutscenes.h"

extern "C" {
extern PlayState* gPlayState;
#include <z64cutscene_commands.h>
}

namespace {

struct CutsceneTextEntry {
    uint16_t textId;
    const char* dialogue;
};

// Generated from data/cutscenes.json in the ganons-curse repo - edit that file,
// not this table.
constexpr CutsceneTextEntry cutsceneTextEntries[] = {
// >>> GANONS_CURSE_GENERATED: TEXT_IDS - edit data/cutscenes.json, not this

// <<< GANONS_CURSE_GENERATED: TEXT_IDS
};

void BuildCutsceneMessage(uint16_t* textId, bool* loadFromMessageTable) {
    for (const CutsceneTextEntry& entry : cutsceneTextEntries) {
        if (entry.textId == *textId) {
            CustomMessage msg(entry.dialogue);
            // AutoFormat() is not optional: LoadIntoFont() copies the string in
            // MF_RAW form, so the terminating MESSAGE_END byte has to already be
            // there. Without it Message_Decode's `while (true)` loop never sees a
            // stop byte, runs off the end of font->msgBuf, and keeps writing past
            // msgCtx.msgBufDecoded[200] until it clears the tail of MessageContext
            // and nulls interfaceCtx.view.gfxCtx - crashing in Interface_Draw.
            // It also turns '^' into a real page break and wraps lines to width.
            msg.AutoFormat();
            msg.LoadIntoFont();
            *loadFromMessageTable = false;
            return;
        }
    }
}

} // namespace

// Generated from data/cutscenes.json - edit that file, not this. Defined at
// namespace scope (not inside the anonymous namespace above) because these
// need real external linkage to match the `extern` declarations in
// GanonsCurseCutscenes.h.
// >>> GANONS_CURSE_GENERATED: CUTSCENE_DATA - edit data/cutscenes.json, not this
CutsceneData gGanonsCurseSariaForestTempleOpening[] = {
    CS_BEGIN_CUTSCENE(2, 140),
    CS_CAM_EYE_LIST(0, 1181),
        CS_CAM_EYE(CS_CMD_CONTINUE, 0, 0, 60.0f, 0, 567, -2138, 0),
        CS_CAM_EYE(CS_CMD_CONTINUE, 0, 0, 60.0f, 0, 567, -2138, 0),
        CS_CAM_EYE(CS_CMD_CONTINUE, 0, 0, 60.0f, 0, 565, -2157, -10128),
        CS_CAM_EYE(CS_CMD_CONTINUE, 0, 0, 60.0f, 0, 567, -2194, 16376),
        CS_CAM_EYE(CS_CMD_CONTINUE, 0, 0, 60.0f, 0, 567, -2194, 0),
        CS_CAM_EYE(CS_CMD_CONTINUE, 0, 0, 60.0f, 0, 567, -2194, -11392),
        CS_CAM_EYE(CS_CMD_CONTINUE, 0, 0, 60.0f, 0, 567, -2194, 0),
        CS_CAM_EYE(CS_CMD_STOP, 0, 0, 60.0f, 0, 567, -2194, 7412),
    CS_CAM_AT_LIST(0, 1210),
        CS_CAM_AT(CS_CMD_CONTINUE, 0, 30, 70.5999f, 0, 596, -2225, 0),
        CS_CAM_AT(CS_CMD_CONTINUE, 0, 30, 70.3999f, 0, 596, -2225, 0),
        CS_CAM_AT(CS_CMD_CONTINUE, 0, 30, 70.5999f, 0, 593, -2245, -10128),
        CS_CAM_AT(CS_CMD_CONTINUE, 0, 30, 60.0f, 0, 608, -2281, 16376),
        CS_CAM_AT(CS_CMD_CONTINUE, 0, 30, 60.0f, 0, 608, -2281, 0),
        CS_CAM_AT(CS_CMD_CONTINUE, 0, 1000, 60.0f, 0, 608, -2281, -11392),
        CS_CAM_AT(CS_CMD_CONTINUE, 0, 30, 60.0f, 0, 608, -2281, 0),
        CS_CAM_AT(CS_CMD_STOP, 0, 30, 60.0f, 0, 608, -2281, 7412),
    CS_END(),
};

CutsceneData gGanonsCurseZeldaCastleCourtyardOpening[] = {
    CS_BEGIN_CUTSCENE(2, 150),
    CS_CAM_EYE_LIST(0, 150),
        CS_CAM_EYE(CS_CMD_CONTINUE, 0, 0, 45.1999f, -461, 133, 0, 0),
        CS_CAM_EYE(CS_CMD_CONTINUE, 0, 100, 45.1999f, -461, 133, 0, 0),
        CS_CAM_EYE(CS_CMD_CONTINUE, 0, 100, 45.1999f, -311, 133, 0, 0),
        CS_CAM_EYE(CS_CMD_CONTINUE, 0, 100, 45.1999f, -161, 133, 0, 0),
        CS_CAM_EYE(CS_CMD_CONTINUE, 0, 0, 45.1999f, -161, 133, 0, 0),
        CS_CAM_EYE(CS_CMD_STOP, 0, 0, 45.1999f, -161, 133, 0, 0),
    CS_CAM_AT_LIST(0, 150),
        CS_CAM_AT(CS_CMD_CONTINUE, 0, 0, 45.1999f, -535, 133, 0, 198),
        CS_CAM_AT(CS_CMD_CONTINUE, 0, 0, 45.1999f, -535, 133, 0, 215),
        CS_CAM_AT(CS_CMD_CONTINUE, 0, 0, 45.1999f, -535, 133, 0, 232),
        CS_CAM_AT(CS_CMD_CONTINUE, 0, 0, 45.1999f, -535, 133, 0, 316),
        CS_CAM_AT(CS_CMD_CONTINUE, 0, 0, 45.1999f, -535, 133, 0, 318),
        CS_CAM_AT(CS_CMD_STOP, 0, 0, 45.1999f, -535, 133, 0, 335),
    CS_END(),
};

CutsceneData gGanonsCurseDaruniaChamberOpening[] = {
    CS_BEGIN_CUTSCENE(2, 150),
    CS_CAM_EYE_LIST(0, 241),
        CS_CAM_EYE(CS_CMD_CONTINUE, 0, 0, 60.0f, 168, 203, -1311, 0),
        CS_CAM_EYE(CS_CMD_CONTINUE, 0, 0, 60.0f, 168, 203, -1311, 21708),
        CS_CAM_EYE(CS_CMD_CONTINUE, 0, 0, 60.0f, 174, 208, -1266, -13632),
        CS_CAM_EYE(CS_CMD_CONTINUE, 0, 0, 60.0f, 188, 209, -1163, 0),
        CS_CAM_EYE(CS_CMD_CONTINUE, 0, 0, 60.0f, 197, 127, -971, 21672),
        CS_CAM_EYE(CS_CMD_CONTINUE, 0, 0, 60.0f, 197, 127, -971, 0),
        CS_CAM_EYE(CS_CMD_CONTINUE, 0, 0, 60.0f, 197, 127, -971, 0),
        CS_CAM_EYE(CS_CMD_CONTINUE, 0, 0, 60.0f, 197, 127, -971, 356),
        CS_CAM_EYE(CS_CMD_STOP, 0, 0, 60.0f, 197, 127, -971, 22032),
    CS_CAM_AT_LIST(0, 270),
        CS_CAM_AT(CS_CMD_CONTINUE, 0, 30, 60.0f, 160, 159, -1371, 0),
        CS_CAM_AT(CS_CMD_CONTINUE, 0, 30, 60.0f, 160, 159, -1371, 21708),
        CS_CAM_AT(CS_CMD_CONTINUE, 0, 30, 60.0f, 166, 165, -1326, -13632),
        CS_CAM_AT(CS_CMD_CONTINUE, 0, 30, 60.0f, 177, 170, -1225, 0),
        CS_CAM_AT(CS_CMD_CONTINUE, 0, 30, 60.0f, 157, 134, -1031, 21672),
        CS_CAM_AT(CS_CMD_CONTINUE, 0, 30, 60.0f, 157, 134, -1031, 0),
        CS_CAM_AT(CS_CMD_CONTINUE, 0, 30, 60.0f, 157, 134, -1031, 0),
        CS_CAM_AT(CS_CMD_CONTINUE, 0, 30, 60.0f, 157, 134, -1031, 356),
        CS_CAM_AT(CS_CMD_STOP, 0, 30, 60.0f, 157, 134, -1031, 22032),
    CS_END(),
};
// <<< GANONS_CURSE_GENERATED: CUTSCENE_DATA

namespace {

// Generated from data/cutscenes.json - edit that file, not this. Backs the
// gc_play_cutscene debug console command below.
const std::unordered_map<std::string, CutsceneData*> cutsceneById = {
// >>> GANONS_CURSE_GENERATED: CUTSCENE_REGISTRY - edit data/cutscenes.json, not this
    { "SARIA_FOREST_TEMPLE_OPENING", gGanonsCurseSariaForestTempleOpening },
    { "ZELDA_CASTLE_COURTYARD_OPENING", gGanonsCurseZeldaCastleCourtyardOpening },
    { "DARUNIA_CHAMBER_OPENING", gGanonsCurseDaruniaChamberOpening },
// <<< GANONS_CURSE_GENERATED: CUTSCENE_REGISTRY
};

// Dev-only trigger for the 4b pipeline: `gc_play_cutscene <id>` plays a
// cutscenes.json entry on demand, e.g. to verify a throwaway test cutscene
// in-game without wiring it to real game progression (that's 4c/4d/4e's job).
//
// No "already playing a cutscene" guard needed here: the framework itself
// ignores cutsceneTrigger while csCtx.state != CS_STATE_IDLE (func_800645A0,
// z_demo.c) and only consumes it once state returns to idle, so calling this
// mid-cutscene is a safe no-op rather than a stomp.
int32_t GanonsCursePlayCutsceneCommand(std::shared_ptr<Ship::Console> console, std::vector<std::string> args,
                                       std::string* output) {
    if (gPlayState == nullptr) {
        if (output != nullptr) {
            *output = "no active play session";
        }
        return 1;
    }
    if (args.size() < 2) {
        if (output != nullptr) {
            *output = "usage: gc_play_cutscene <id>";
        }
        return 1;
    }
    auto it = cutsceneById.find(args[1]);
    if (it == cutsceneById.end()) {
        if (output != nullptr) {
            *output = "unknown cutscene id: " + args[1];
        }
        return 1;
    }
    Cutscene_SetSegment(gPlayState, it->second);
    gSaveContext.cutsceneTrigger = 1;
    return 0;
}

void RegisterGanonsCurseCutsceneDebugCommand() {
    Ship::Context::GetRawInstance()->GetConsole()->AddCommand(
        "gc_play_cutscene",
        { GanonsCursePlayCutsceneCommand,
          "Ganon's Curse: play a JSON-authored cutscene from data/cutscenes.json by id (4b pipeline)." });
}

void RegisterGanonsCurseCutsceneText() {
    COND_HOOK(OnOpenText, IS_RANDO, BuildCutsceneMessage);
}

} // namespace

static RegisterShipInitFunc ganonsCurseCutsceneDebugCommandInitFunc(RegisterGanonsCurseCutsceneDebugCommand);
static RegisterShipInitFunc ganonsCurseCutsceneTextInitFunc(RegisterGanonsCurseCutsceneText, { "IS_RANDO" });
