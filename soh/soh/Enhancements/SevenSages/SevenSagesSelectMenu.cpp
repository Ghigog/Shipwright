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
#include "soh/Enhancements/randomizer/savefile.h"
#include "soh/Enhancements/randomizer/randomizer.h"
#include "soh_assets.h"

#include <libultraship/bridge.h>

extern "C" {
#include "functions.h"
#include "macros.h"
#include "variables.h"
#include "textures/title_static/title_static.h"
#include "objects/object_zl1/object_zl1.h"
#include "src/overlays/gamestates/ovl_file_choose/file_choose.h"
extern void FileChoose_UpdateStickDirectionPromptAnim(GameState* thisx);
}

namespace {

constexpr uint8_t SAGE_COUNT = 7;

// Indexed by RO_SAGE_*, so navigation cycles through this table directly.
struct SageRingEntry {
    const char* medallion; // IA8 16x16; nullptr for Zelda, who takes the Triforce
    uint8_t r, g, b;       // vanilla's medallion tint
    // Unit offset in per-mille, multiplied by RING_RADIUS at draw time so the radius stays a
    // single knob. Hexagon points: (0,-1) at the top then clockwise, x = +/-cos(30) = 0.866.
    int16_t unitX;
    int16_t unitY;
    bool isCentre;
};

const SageRingEntry sSageRing[SAGE_COUNT] = {
    { gFileSelLightMedallionTex, 200, 200, 0, 0, -1000, false },     // Rauru - Light
    { gFileSelForestMedallionTex, 0, 255, 0, 866, -500, false },     // Saria - Forest
    { gFileSelFireMedallionTex, 255, 60, 0, 866, 500, false },       // Darunia - Fire
    { gFileSelWaterMedallionTex, 0, 100, 255, 0, 1000, false },      // Ruto - Water
    { gFileSelShadowMedallionTex, 200, 50, 255, -866, 500, false },  // Impa - Shadow
    { gFileSelSpiritMedallionTex, 255, 130, 0, -866, -500, false },  // Nabooru - Spirit
    { nullptr, 255, 255, 255, 0, 0, true },                          // Zelda - crest, centre (RGBA, so white prim)
};

// Left half of the window; the detail pane will take the right.
constexpr int16_t RING_CENTRE_X = 104;
constexpr int16_t RING_CENTRE_Y = 132;
// Radius and icon size move together. On a hexagon the distance between adjacent points
// equals the radius, so an icon wider than the radius makes neighbours overlap - at r=40 with
// 32px icons they were nearly touching, which is part of why the ring read as sprawling.
constexpr int16_t RING_RADIUS = 30;
// Source textures are 16x16. dsdx/dtdy of 1<<11 is 1:1, so 1<<10 is 2x and 1<<11*2/3 would be
// 1.5x; 24px keeps the art legible while leaving a real gap between neighbours at r=30.
constexpr int16_t ICON_DRAW_SIZE = 24;

// dsdx/dtdy is texels-per-pixel with 10 fractional bits, so it is (texels * 1024 / pixels) -
// NOT a power-of-two "zoom" constant. Getting this wrong is what drew the grey bars: too
// large a value walks past the end of the tile, and CLAMP then faithfully repeats the edge
// texel across the remainder of the rectangle. Boss Rush's arrows are the reference - 16
// texels into an 8px rect uses 1<<11, which is exactly 16*1024/8.
constexpr uint16_t TEXELS_PER_PIXEL(int16_t texels, int16_t pixels) {
    return (uint16_t)((texels * 1024) / pixels);
}
constexpr uint16_t ICON_DSDX = TEXELS_PER_PIXEL(16, ICON_DRAW_SIZE);

// Zelda's crest, on the fourth attempt. The three failures were each instructive:
//   gTriforcePieceTex        - a single wedge, the Triforce Hunt counter
//   gTriforceTex (i8 64x64)  - the whole symbol, but 4096 bytes = the entire 4KB TMEM, so it
//                              cannot be loaded as one block and came out garbled
//   gEnHeishiUniformGrey...  - i4, small enough, but a pure I format carries no alpha: the
//                              RDP hands back alpha 1 for every texel, so MODULATEIA_PRIM
//                              painted the whole tile as a solid gold square
// The medallions only work because they are ia8 - a real alpha channel. So the crest needs
// one too. gZelda2TriforceTex is rgba16 16x16: 512 bytes, genuinely transparent around the
// symbol, square so it needs no aspect correction, and it is Zelda's own.
// Doubled: the source is HALF the emblem, with a hard vertical edge on its right where it was
// meant to meet its own mirror on Zelda's headdress. G_TX_MIRROR on S makes the RDP repeat it
// reversed past texel 16, so drawing 32 texels wide reconstructs the whole symmetric crest and
// the seam falls exactly on that edge.
constexpr int16_t TRIFORCE_DRAW_W = 26;
constexpr int16_t TRIFORCE_DRAW_H = 26;
constexpr uint16_t TRIFORCE_DSDX = TEXELS_PER_PIXEL(32, TRIFORCE_DRAW_W);
constexpr uint16_t TRIFORCE_DTDY = TEXELS_PER_PIXEL(32, TRIFORCE_DRAW_H);

// Right-hand detail pane. Y is derived, not fixed: the block is centred on the ring's own
// axis so the two halves read as one composition. Line offsets from the block's top:
//   name 0, location +16, age +27, then each kit line at +43 + n*11.
constexpr int16_t INFO_X = 168;
constexpr int16_t INFO_LOCATION_DY = 16;
constexpr int16_t INFO_AGE_DY = 27;
constexpr int16_t INFO_KIT_DY = 43;
constexpr int16_t INFO_KIT_LINE_H = 11;
constexpr int16_t INFO_LAST_LINE_H = 10;

// Each sage's starting scene and kit are FIXED, not randomized - that is the whole point of
// choosing one. Kept as display strings because sSageDefinitions[] stores the kit as
// RSK_STARTING_* option ids, which have no readable names at this layer.
//
// Transcribed from sSageDefinitions[] in savefile.cpp - the code, NOT docs/characters.md.
// That distinction is load-bearing: the doc's summary table had drifted from the code and
// listed a "random mask" for Saria that is not implemented at all (the doc says so itself,
// further down, under open questions), omitted the Magic every caster starts with, and put
// Impa in Kakariko and Ruto in Zora's Domain when they spawn at the Graveyard and Zora's
// Fountain. If the definitions change, re-derive this from them.
struct SageInfo {
    const char* name;
    const char* location;
    const char* age;    // from sSageDefinitions[], NOT Randomizer_GetSageStartingAge(): that reads
                        // the randomizer context's options, which are only populated at
                        // generation time, so this early it always answers for Rauru.
    const char* kit[5]; // nullptr-terminated
};

const SageInfo sSageInfo[SAGE_COUNT] = {
    { "Rauru", "Lon Lon Ranch", "Adult",
      { "Megaton Hammer", "Bow + Light Arrows", "Magic", "Stone of Agony", nullptr } },
    { "Saria", "Sacred Forest Meadow", "Child",
      { "Deku Sticks & Nuts", "Fairy Ocarina", "Saria's Song", "Magic", nullptr } },
    { "Darunia", "Goron City", "Child",
      { "Bomb Bag + Bombchus", "Goron's Bracelet", "Goron Tunic", nullptr, nullptr } },
    { "Ruto", "Zora's Fountain", "Child",
      { "Golden Scale", "Iron Boots", "Zora Tunic", nullptr, nullptr } },
    { "Impa", "The Graveyard", "Adult",
      { "Bunny Hood", "Hookshot", "Lens of Truth", "Magic", nullptr } },
    { "Nabooru", "Gerudo Fortress", "Adult",
      { "Hover Boots", "Gerudo Card", "Mirror Shield", nullptr, nullptr } },
    { "Zelda", "Hyrule Castle", "Child",
      { "Farore's Wind", "Nayru's Love", "Din's Fire", "Ocarina + Lullaby", "Double Magic" } },
};

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
            // Track the cursor rather than waiting for A, so the detail pane can just ask the
            // savefile.h accessors about "the selected sage" instead of duplicating the table.
            CVarSetInteger(CVAR_RANDOMIZER_SETTING("SelectedSage"), fileChooseContext->sevenSagesIndex);
            Audio_PlaySoundGeneral(NA_SE_SY_FSEL_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                   &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        } else if (fileChooseContext->stickRelX < -30 || (dpad && CHECK_BTN_ANY(input->press.button, BTN_DLEFT))) {
            fileChooseContext->sevenSagesIndex =
                (uint8_t)((fileChooseContext->sevenSagesIndex + SAGE_COUNT - 1) % SAGE_COUNT);
            CVarSetInteger(CVAR_RANDOMIZER_SETTING("SelectedSage"), fileChooseContext->sevenSagesIndex);
            Audio_PlaySoundGeneral(NA_SE_SY_FSEL_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                   &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        }
    }

    if (CHECK_BTN_ALL(input->press.button, BTN_A)) {
        CVarSetInteger(CVAR_RANDOMIZER_SETTING("SelectedSage"), fileChooseContext->sevenSagesIndex);
        CVarSave();
        Audio_PlaySoundGeneral(NA_SE_SY_FSEL_DECIDE_L, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);

        // Generate here, not on the settings screen. The sage is baked into the seed at
        // generation time - Randomizer_ApplySageGenerationSettings() folds its age, kit and
        // home region into the real settings before Fill() runs - so a seed made before this
        // choice carries whichever sage was selected when it was made. Choosing a sage and
        // then playing a stale seed silently gives you the wrong one, which is exactly the
        // trap this screen should close.
        //
        // Must follow CVarSave(): generation reads the setting back through the randomizer
        // context, which is populated from the CVars.
        //
        // Asynchronous - this spawns the generation thread and returns immediately. The
        // randomizer settings menu we hand off to already renders the "Generating..." state
        // and withholds "Start Randomizer" until the seed exists, so no waiting is needed
        // here. A custom seed set in the randomizer options is still honoured, since that is
        // read during generation like any other setting.
        // GenerateRandomizer(), not Randomizer_GenerateRandomizer(): the latter lives inside an
        // #ifndef __cplusplus block in OTRGlobals.h, so it is C-only. This is the C++ entry
        // point, and the same one debugconsole.cpp uses. An empty seed means "use whatever the
        // randomizer options say", so a custom seed set there is still honoured.
        GenerateRandomizer();
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

        const int16_t x = (int16_t)(RING_CENTRE_X + (RING_RADIUS * entry.unitX) / 1000 - ICON_DRAW_SIZE / 2);
        const int16_t y = (int16_t)(RING_CENTRE_Y + (RING_RADIUS * entry.unitY) / 1000 - ICON_DRAW_SIZE / 2);

        // Unselected medallions keep their own colour but dimmed, rather than going grey, so
        // the ring still reads as six distinct sages at a glance.
        const bool selected = (i == fileChooseContext->sevenSagesIndex);
        const uint8_t r = selected ? entry.r : (uint8_t)(entry.r * 2 / 5);
        const uint8_t g = selected ? entry.g : (uint8_t)(entry.g * 2 / 5);
        const uint8_t b = selected ? entry.b : (uint8_t)(entry.b * 2 / 5);

        gDPPipeSync(POLY_OPA_DISP++);
        gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, r, g, b, alpha);
        // CLAMP with a real mask (4, because the tile is 2^4 = 16 texels), not WRAP/NOMASK.
        // With WRAP and no mask, any sampling past the tile repeats the edge instead of
        // stopping - that is what drew a grey bar to the right of and below every medallion.
        gDPLoadTextureBlock(POLY_OPA_DISP++, entry.medallion, G_IM_FMT_IA, G_IM_SIZ_8b, 16, 16, 0,
                            G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, 4, 4, G_TX_NOLOD, G_TX_NOLOD);
        gSPWideTextureRectangle(POLY_OPA_DISP++, x << 2, y << 2, (x + ICON_DRAW_SIZE) << 2,
                                (y + ICON_DRAW_SIZE) << 2, G_TX_RENDERTILE, 0, 0, ICON_DSDX, ICON_DSDX);
    }

    // Zelda's Triforce, at the centre. i8 so intensity carries alpha too - MODULATEIA_PRIM
    // gives a gold symbol on a transparent background. Mask 6 because the tile is 2^6 = 64.
    {
        const bool selected = (fileChooseContext->sevenSagesIndex == SAGE_COUNT - 1);
        const SageRingEntry& zelda = sSageRing[SAGE_COUNT - 1];
        const uint8_t r = selected ? zelda.r : (uint8_t)(zelda.r * 2 / 5);
        const uint8_t g = selected ? zelda.g : (uint8_t)(zelda.g * 2 / 5);
        const uint8_t b = selected ? zelda.b : (uint8_t)(zelda.b * 2 / 5);
        const int16_t x = (int16_t)(RING_CENTRE_X - TRIFORCE_DRAW_W / 2);
        const int16_t y = (int16_t)(RING_CENTRE_Y - TRIFORCE_DRAW_H / 2);

        gDPPipeSync(POLY_OPA_DISP++);
        // RGBA carries its own colour, so this needs MODULATERGBA rather than the IA combine
        // the medallions use; prim still supplies the fade and the unselected dimming.
        gDPSetCombineMode(POLY_OPA_DISP++, G_CC_MODULATERGBA_PRIM, G_CC_MODULATERGBA_PRIM);
        gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, r, g, b, alpha);
        gDPLoadTextureBlock(POLY_OPA_DISP++, gChildZelda1HeaddressTriforceEmblemTex, G_IM_FMT_RGBA, G_IM_SIZ_16b, 16,
                            32, 0, G_TX_MIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_CLAMP, 4, 5, G_TX_NOLOD,
                            G_TX_NOLOD);
        gSPWideTextureRectangle(POLY_OPA_DISP++, x << 2, y << 2, (x + TRIFORCE_DRAW_W) << 2,
                                (y + TRIFORCE_DRAW_H) << 2, G_TX_RENDERTILE, 0, 0, TRIFORCE_DSDX, TRIFORCE_DTDY);
    }

    // Right-hand detail pane. Name and starting age for now; starting location and kit need a
    // region-name lookup that lives behind the randomizer's C++ context, which is not
    // obviously safe to touch this early in the file-select flow.
    {
        const SageInfo& info = sSageInfo[fileChooseContext->sevenSagesIndex];

        uint8_t kitCount = 0;
        while (kitCount < 5 && info.kit[kitCount] != nullptr) {
            kitCount++;
        }
        // Kits run 3 to 5 entries, so the block's height varies; centring on the measured
        // height rather than a fixed top is what keeps each sage balanced instead of leaving
        // the short ones bottom-heavy.
        const int16_t blockH =
            (int16_t)(INFO_KIT_DY + (kitCount > 0 ? (kitCount - 1) * INFO_KIT_LINE_H : 0) + INFO_LAST_LINE_H);
        const int16_t top = (int16_t)(RING_CENTRE_Y - blockH / 2);

        Interface_DrawTextLine(fileChooseContext->state.gfxCtx, (char*)info.name, INFO_X, top, 255, 255, 170, alpha,
                               1.0f, true);
        Interface_DrawTextLine(fileChooseContext->state.gfxCtx, (char*)info.location, INFO_X, top + INFO_LOCATION_DY,
                               190, 220, 255, alpha, 0.7f, true);
        Interface_DrawTextLine(fileChooseContext->state.gfxCtx, (char*)info.age, INFO_X, top + INFO_AGE_DY, 190, 220,
                               255, alpha, 0.7f, true);

        for (uint8_t k = 0; k < kitCount; k++) {
            Interface_DrawTextLine(fileChooseContext->state.gfxCtx, (char*)info.kit[k], INFO_X,
                                   top + INFO_KIT_DY + (k * INFO_KIT_LINE_H), 255, 255, 255, alpha, 0.65f, true);
        }
    }

    CLOSE_DISPS(fileChooseContext->state.gfxCtx);
}
