#ifndef SEVEN_SAGES_CUTSCENES_H
#define SEVEN_SAGES_CUTSCENES_H

// functions.h first, not macros.h: functions.h's own "z64.h" include is what
// defines GraphicsContext (macros.h uses it as `extern GraphicsContext*
// __gfxCtx` without defining it itself) as well as Vec3s/Vec3f/etc. before
// z64cutscene.h needs them - same order global.h itself uses.
extern "C" {
#include <functions.h>
#include <variables.h>
#include <macros.h>
}

// Generated from data/cutscenes.json in the seven-sages repo - edit that file and
// run tools/gen_cutscenes.py, don't hand-edit this.
//
// The extern "C" is required, not stylistic, and it is deliberately outside the
// generated markers so regenerating keeps it. These arrays are DEFINED in
// SevenSagesCutsceneData.c (a C file, because the cutscene macros narrow - see that
// file). Without this wrapper the declarations get C++ linkage while the definitions
// have C linkage. On the Itanium ABI that still links, because a plain global is not
// mangled there and both sides land on the same symbol - so macOS and Linux build
// clean and say nothing. MSVC mangles every C++ variable, so the mismatch surfaces
// only on Windows, as LNK2001 unresolved externals at the very end of a full build.
extern "C" {
// >>> SEVEN_SAGES_GENERATED: CUTSCENE_EXTERNS - edit data/cutscenes.json, not this
extern CutsceneData gSevenSagesSariaForestTempleOpening[];
extern CutsceneData gSevenSagesZeldaCastleCourtyardOpening[];
extern CutsceneData gSevenSagesDaruniaChamberOpening[];
// <<< SEVEN_SAGES_GENERATED: CUTSCENE_EXTERNS
}

#endif // SEVEN_SAGES_CUTSCENES_H
