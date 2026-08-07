/**
 * Seven Sages - the generated CutsceneData arrays, deliberately in C rather than C++.
 *
 * These live apart from SevenSagesCutscenes.cpp for one reason: the cutscene macros
 * emit constants that do not fit s32. CS_END() is 0xFFFFFFFF, and CMD_HH packs any
 * negative camera field (roll, angle) into a value above INT_MAX. In C++ that is a
 * narrowing conversion inside a braced initializer, which is ill-formed - MSVC builds
 * soh/ with /permissive-, making it hard error C2397, and no /W flag can turn that off.
 * Clang only stays quiet because the project passes -Wno-c++11-narrowing, and that
 * flag exists solely in the Clang/GCC branch of soh/CMakeLists.txt; MSVC has no
 * equivalent. C has no narrowing rule, so the same data compiles everywhere - which is
 * why every vanilla cutscene array is a .c file too (see
 * z_bg_toki_swd_cutscene_data_1.c). Keep this file C.
 *
 * The `extern` declarations these match are in SevenSagesCutscenes.h, inside its
 * extern "C" block, so the linkage lines up. That header cannot be included from here
 * (its extern "C" is unguarded and is not valid C), which is why this file includes the
 * vanilla chain directly, exactly as the vanilla cutscene data files do.
 *
 * Generated from data/cutscenes.json in the seven-sages repo - edit that file and run
 * tools/gen_cutscenes.py, don't hand-edit this.
 */
#include "global.h"
#include "z64cutscene_commands.h"

// clang-format off
// >>> SEVEN_SAGES_GENERATED: CUTSCENE_DATA - edit data/cutscenes.json, not this
CutsceneData gSevenSagesSariaForestTempleOpening[] = {
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

CutsceneData gSevenSagesZeldaCastleCourtyardOpening[] = {
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

CutsceneData gSevenSagesDaruniaChamberOpening[] = {
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
// <<< SEVEN_SAGES_GENERATED: CUTSCENE_DATA
