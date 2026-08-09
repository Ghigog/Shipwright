/**
 * Seven Sages - Phase 4b: the JSON-authored cutscene pipeline.
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
 * Custom dialogue reuses the exact mechanism SevenSagesNpcHints.cpp already
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
// Order matters: these plain-C++ includes must come before SevenSagesCutscenes.h
// pulls in functions.h/variables.h/macros.h inside its extern "C" block. Several
// libultraship headers reachable from that C chain (e.g. macros.h's own
// ship/utils/binarytools/endianness.h) are template-using C++ headers; if their
// *first* inclusion in this translation unit happens while still inside an
// extern "C" block, the templates fail to parse ("templates must have C++
// linkage"). Getting them parsed once here in ordinary C++ context first makes
// every later re-inclusion an include-guard no-op instead - the same reason
// SevenSagesNpcHints.cpp orders its includes this way.
#include <soh/OTRGlobals.h>
#include "soh/ShipInit.hpp"
#include "soh/Enhancements/custom-message/CustomMessageManager.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"

#include <ship/Context.h>
#include <ship/debug/Console.h>

#include <string>
#include <unordered_map>

#include "SevenSagesCutscenes.h"

extern "C" {
extern PlayState* gPlayState;
#include <z64cutscene_commands.h>
}

namespace {

struct CutsceneTextEntry {
    uint16_t textId;
    const char* dialogue;
};

// Generated from data/cutscenes.json in the seven-sages repo - edit that file,
// not this table.
constexpr CutsceneTextEntry cutsceneTextEntries[] = {
// >>> SEVEN_SAGES_GENERATED: TEXT_IDS - edit data/cutscenes.json, not this

// <<< SEVEN_SAGES_GENERATED: TEXT_IDS
    // Sentinel, deliberately outside the generated markers so regenerating keeps it. No
    // cutscene declares dialogue right now, so without this the generated region is empty
    // and the array is zero-length - a clang extension, but hard error C2466 on MSVC
    // ("cannot allocate an array of constant size 0"). The loop below skips it by its null
    // dialogue rather than by its id, so it stays correct if a real entry ever uses id 0.
    { 0, nullptr },
};

void BuildCutsceneMessage(uint16_t* textId, bool* loadFromMessageTable) {
    for (const CutsceneTextEntry& entry : cutsceneTextEntries) {
        if (entry.dialogue == nullptr) {
            continue; // the sentinel above, not a real entry
        }
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

// The generated CutsceneData arrays used to live here. They are in
// SevenSagesCutsceneData.c now - the cutscene macros emit constants that do not fit
// s32, which is a hard error (MSVC C2397) in a C++ braced initializer but perfectly
// legal in C. That file explains it in full. They are declared in
// SevenSagesCutscenes.h, so everything below still sees them.

namespace {

// Generated from data/cutscenes.json - edit that file, not this. Backs the
// gc_play_cutscene debug console command below.
const std::unordered_map<std::string, CutsceneData*> cutsceneById = {
// >>> SEVEN_SAGES_GENERATED: CUTSCENE_REGISTRY - edit data/cutscenes.json, not this
    { "SARIA_FOREST_TEMPLE_OPENING", gSevenSagesSariaForestTempleOpening },
    { "ZELDA_CASTLE_COURTYARD_OPENING", gSevenSagesZeldaCastleCourtyardOpening },
    { "DARUNIA_CHAMBER_OPENING", gSevenSagesDaruniaChamberOpening },
// <<< SEVEN_SAGES_GENERATED: CUTSCENE_REGISTRY
};

// Dev-only trigger for the 4b pipeline: `gc_play_cutscene <id>` plays a
// cutscenes.json entry on demand, e.g. to verify a throwaway test cutscene
// in-game without wiring it to real game progression (that's 4c/4d/4e's job).
//
// No "already playing a cutscene" guard needed here: the framework itself
// ignores cutsceneTrigger while csCtx.state != CS_STATE_IDLE (func_800645A0,
// z_demo.c) and only consumes it once state returns to idle, so calling this
// mid-cutscene is a safe no-op rather than a stomp.
int32_t SevenSagesPlayCutsceneCommand(std::shared_ptr<Ship::Console> console, std::vector<std::string> args,
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

void RegisterSevenSagesCutsceneDebugCommand() {
    Ship::Context::GetRawInstance()->GetConsole()->AddCommand(
        "gc_play_cutscene",
        { SevenSagesPlayCutsceneCommand,
          "Seven Sages: play a JSON-authored cutscene from data/cutscenes.json by id (4b pipeline)." });
}

void RegisterSevenSagesCutsceneText() {
    COND_HOOK(OnOpenText, IS_SEVENSAGES, BuildCutsceneMessage);
}

} // namespace

static RegisterShipInitFunc sevenSagesCutsceneDebugCommandInitFunc(RegisterSevenSagesCutsceneDebugCommand);
static RegisterShipInitFunc sevenSagesCutsceneTextInitFunc(RegisterSevenSagesCutsceneText, { "IS_RANDO" });
