#ifndef GANONS_CURSE_CUTSCENES_H
#define GANONS_CURSE_CUTSCENES_H

// functions.h first, not macros.h: functions.h's own "z64.h" include is what
// defines GraphicsContext (macros.h uses it as `extern GraphicsContext*
// __gfxCtx` without defining it itself) as well as Vec3s/Vec3f/etc. before
// z64cutscene.h needs them - same order global.h itself uses.
extern "C" {
#include <functions.h>
#include <variables.h>
#include <macros.h>
}

// Generated from data/cutscenes.json in the ganons-curse repo - edit that file and
// run tools/gen_cutscenes.py, don't hand-edit this.
// >>> GANONS_CURSE_GENERATED: CUTSCENE_EXTERNS - edit data/cutscenes.json, not this
extern CutsceneData gGanonsCurseSariaForestTempleOpening[];
// <<< GANONS_CURSE_GENERATED: CUTSCENE_EXTERNS

#endif // GANONS_CURSE_CUTSCENES_H
