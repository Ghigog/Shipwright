/**
 * Seven Sages - the sage select screen, reached from the Seven Sages quest entry.
 *
 * First pass: the medallion ring and its navigation. The right-hand detail pane (name,
 * starting location, starting kit) is deliberately not here yet.
 *
 * The medallion IS the sage's identity in this game, so the six who have one are drawn with
 * theirs. These are the *file-select* medallions from title_static - gFileSelForestMedallionTex
 * and friends - not the pause menu's gItemIcons set. That matters twice over: they are the
 * ones already drawn correctly on the save summary two inches away on this same screen, and
 * they are IA8 16x16, a single alpha channel tinted by prim colour. Drawing them as RGBA32
 * 32x32 (the first attempt) reads every row at the wrong stride and produces coloured bands.
 * The colours below are vanilla's own medallion colours, lifted from sQuestItemRed/Green/Blue
 * in z_file_choose.c so the ring matches the save summary exactly.
 *
 * Zelda is the seventh sage and the game never gives her a medallion - she is the Sage of
 * Time - so she takes the Triforce at the centre. gTriforcePieceTex is RGBA32 32x32 and is
 * drawn as-is, which is why it goes through a different call than the six.
 *
 * Selecting writes CVAR_RANDOMIZER_SETTING("SelectedSage"), which is all RSK_SELECTED_SAGE
 * is. Generation reads it from there, so this screen runs before the seed is made.
 */
#include "SevenSagesSelectMenu.h"

#include <soh/OTRGlobals.h>
#include "soh/Enhancements/randomizer/randomizerTypes.h"
#include "soh_assets.h"

#include <libultraship/bridge.h>

extern "C" {
#include "functions.h"
#include "macros.h"
#include "variables.h"
#include "textures/title_static/title_static.h"
#include "src/overlays/gamestates/ovl_file_choose/file_choose.h"
extern void FileChoose_UpdateStickDirectionPromptAnim(GameState* thisx);
}

namespace {

constexpr uint8_t SAGE_COUNT = 7;

// Indexed by RO_SAGE_*, so navigation cycles through this table directly.
struct SageRingEntry {
    const char* medallion; // IA8 16x16; nullptr for Zelda, who takes the Triforce
    uint8_t r, g, b;       // vanilla's medallion tint
    int16_t ringDX;        // offset from the ring centre; ignored for the centre slot
    int16_t ringDY;
    bool isCentre;
};

// Six positions on a circle of radius 40, starting at the top and going clockwise. Written
// out rather than computed: the values never change, and a static table keeps the draw path
// free of trig.
const SageRingEntry sSageRing[SAGE_COUNT] = {
    { gFileSelLightMedallionTex, 200, 200, 0, 0, -40, false },   // Rauru - Light
    { gFileSelForestMedallionTex, 0, 255, 0, 35, -20, false },   // Saria - Forest
    { gFileSelFireMedallionTex, 255, 60, 0, 35, 20, false },     // Darunia - Fire
    { gFileSelWaterMedallionTex, 0, 100, 255, 0, 40, false },    // Ruto - Water
    { gFileSelShadowMedallionTex, 200, 50, 255, -35, 20, false },// Impa - Shadow
    { gFileSelSpiritMedallionTex, 255, 130, 0, -35, -20, false },// Nabooru - Spirit
    { nullptr, 255, 220, 0, 0, 0, true },                        // Zelda - Triforce, centre
};

// Left half of the window; the detail pane will take the right.
constexpr int16_t RING_CENTRE_X = 104;
constexpr int16_t RING_CENTRE_Y = 132;
// Source textures are 16x16, drawn over a 32x32 rect. dsdx/dtdy of 1<<10 steps half a texel
// per pixel, which is the 2x magnification - 16x16 is too small to read on this screen.
constexpr int16_t ICON_DRAW_SIZE = 32;

} // namespace

extern "C" void FileChoose_UpdateSevenSagesMenu(GameState* gameState) {
    FileChoose_UpdateStickDirectionPromptAnim(gameState);
    FileChooseContext* fileChooseContext = (FileChooseContext*)gameState;
    Input* input = &fileChooseContext->state.input[0];
    bool dpad = CVarGetInteger(CVAR_SETTING("DpadInText"), 0);

    fileChooseContext->sevenSagesUIAlpha += 25;
    if (fileChooseContext->sevenSagesUIAlpha > 255) {
        fileChooseContext->sevenSagesUIAlpha = 255;
    }

    // Left/right, not up/down: the sages are arranged in a ring, so right should carry you
    // clockwise around it. Index order is the ring's clockwise order, so this is a plain
    // increment - Zelda in the centre is simply the last step before wrapping back to Rauru.
    if (ABS(fileChooseContext->stickRelX) > 30 || (dpad && CHECK_BTN_ANY(input->press.button, BTN_DRIGHT | BTN_DLEFT))) {
        if (fileChooseContext->stickRelX > 30 || (dpad && CHECK_BTN_ANY(input->press.button, BTN_DRIGHT))) {
            fileChooseContext->sevenSagesIndex = (uint8_t)((fileChooseContext->sevenSagesIndex + 1) % SAGE_COUNT);
            Audio_PlaySoundGeneral(NA_SE_SY_FSEL_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                   &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        } else if (fileChooseContext->stickRelX < -30 || (dpad && CHECK_BTN_ANY(input->press.button, BTN_DLEFT))) {
            fileChooseContext->sevenSagesIndex =
                (uint8_t)((fileChooseContext->sevenSagesIndex + SAGE_COUNT - 1) % SAGE_COUNT);
            Audio_PlaySoundGeneral(NA_SE_SY_FSEL_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                   &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        }
    }

    if (CHECK_BTN_ALL(input->press.button, BTN_A)) {
        CVarSetInteger(CVAR_RANDOMIZER_SETTING("SelectedSage"), fileChooseContext->sevenSagesIndex);
        CVarSave();
        Audio_PlaySoundGeneral(NA_SE_SY_FSEL_DECIDE_L, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        fileChooseContext->prevConfigMode = fileChooseContext->configMode;
        fileChooseContext->configMode = CM_ROTATE_TO_RANDOMIZER_SETTINGS_MENU;
        return;
    }

    if (CHECK_BTN_ALL(input->press.button, BTN_B)) {
        Audio_PlaySoundGeneral(NA_SE_SY_FSEL_CLOSE, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        fileChooseContext->prevConfigMode = fileChooseContext->configMode;
        fileChooseContext->configMode = CM_SEVEN_SAGES_TO_QUEST;
        return;
    }
}

extern "C" void FileChoose_DrawSevenSagesMenuWindowContents(FileChooseContext* fileChooseContext) {
    OPEN_DISPS(fileChooseContext->state.gfxCtx);

    const uint8_t alpha = (uint8_t)fileChooseContext->sevenSagesUIAlpha;

    Gfx_SetupDL_39Opa(fileChooseContext->state.gfxCtx);
    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);

    for (uint8_t i = 0; i < SAGE_COUNT; i++) {
        const SageRingEntry& entry = sSageRing[i];
        if (entry.isCentre) {
            continue; // drawn after the ring, in RGBA
        }

        const int16_t x = (int16_t)(RING_CENTRE_X + entry.ringDX - ICON_DRAW_SIZE / 2);
        const int16_t y = (int16_t)(RING_CENTRE_Y + entry.ringDY - ICON_DRAW_SIZE / 2);

        // Unselected medallions keep their own colour but dimmed, rather than going grey, so
        // the ring still reads as six distinct sages at a glance.
        const bool selected = (i == fileChooseContext->sevenSagesIndex);
        const uint8_t r = selected ? entry.r : (uint8_t)(entry.r * 2 / 5);
        const uint8_t g = selected ? entry.g : (uint8_t)(entry.g * 2 / 5);
        const uint8_t b = selected ? entry.b : (uint8_t)(entry.b * 2 / 5);

        gDPPipeSync(POLY_OPA_DISP++);
        gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, r, g, b, alpha);
        gDPLoadTextureBlock(POLY_OPA_DISP++, entry.medallion, G_IM_FMT_IA, G_IM_SIZ_8b, 16, 16, 0,
                            G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD,
                            G_TX_NOLOD);
        gSPWideTextureRectangle(POLY_OPA_DISP++, x << 2, y << 2, (x + ICON_DRAW_SIZE) << 2,
                                (y + ICON_DRAW_SIZE) << 2, G_TX_RENDERTILE, 0, 0, 1 << 10, 1 << 10);
    }

    // Zelda's Triforce. RGBA32 32x32, so it goes through the same helper the quest subtitles
    // use rather than the IA8 path above.
    {
        const bool selected = (fileChooseContext->sevenSagesIndex == SAGE_COUNT - 1);
        const uint8_t shade = selected ? 255 : 102;
        gDPPipeSync(POLY_OPA_DISP++);
        gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, shade, shade, shade, alpha);
        FileChoose_DrawImageRGBA32(fileChooseContext->state.gfxCtx, RING_CENTRE_X, RING_CENTRE_Y, gTriforcePieceTex, 32,
                                   32);
    }

    CLOSE_DISPS(fileChooseContext->state.gfxCtx);
}
