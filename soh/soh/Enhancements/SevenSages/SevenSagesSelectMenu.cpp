/**
 * Seven Sages - the sage select screen, reached from the Seven Sages quest entry.
 *
 * First pass: the medallion ring and its navigation. The right-hand detail pane (name,
 * starting location, starting kit) is deliberately not here yet - layout is the part most
 * worth looking at before text is hung off it.
 *
 * The medallion IS the sage's identity in this game, so the six sages who have one are drawn
 * with it: the item icons the pause menu already uses, straight out of gItemIcons. Zelda is
 * the seventh sage and has no medallion - she is the Sage of Time, and the game never gives
 * her one - so she takes the Triforce at the centre of the ring. gTriforcePieceTex is the
 * right art for that rather than any of the Zelda-model triforces: it lives in
 * parameter_static, the UI texture bank, is already RGBA32 32x32 like the item icons, and is
 * already drawn flat by the Triforce Hunt HUD counter (kaleido.cpp).
 *
 * Selecting writes CVAR_RANDOMIZER_SETTING("SelectedSage"), which is all RSK_SELECTED_SAGE
 * is. Generation reads it from there, so this screen has to run before the seed is made -
 * which is exactly where the quest menu routes it.
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
#include "src/overlays/gamestates/ovl_file_choose/file_choose.h"
extern void FileChoose_UpdateStickDirectionPromptAnim(GameState* thisx);
}

namespace {

constexpr uint8_t SAGE_COUNT = 7;

// Index is the RO_SAGE_* value, so this table is ordered by the sage enum rather than by
// medallion order - navigation cycles through it directly.
struct SageRingEntry {
    uint8_t itemIcon;  // ITEM_MEDALLION_*, or ITEM_NONE for Zelda who takes the Triforce
    int16_t ringDX;    // offset from the ring centre; ignored for the centre slot
    int16_t ringDY;
    bool isCentre;
};

// Six positions on a circle of radius 40, starting at the top and going clockwise. Written
// out rather than computed: the values never change, and a static table keeps the draw path
// free of trig and of any float determinism question.
const SageRingEntry sSageRing[SAGE_COUNT] = {
    { ITEM_MEDALLION_LIGHT, 0, -40, false },    // Rauru - Light
    { ITEM_MEDALLION_FOREST, 35, -20, false },  // Saria - Forest
    { ITEM_MEDALLION_FIRE, 35, 20, false },     // Darunia - Fire
    { ITEM_MEDALLION_WATER, 0, 40, false },     // Ruto - Water
    { ITEM_MEDALLION_SHADOW, -35, 20, false },  // Impa - Shadow
    { ITEM_MEDALLION_SPIRIT, -35, -20, false }, // Nabooru - Spirit
    { ITEM_NONE, 0, 0, true },                  // Zelda - Triforce, centre
};

// Left half of the window; the detail pane will take the right.
constexpr int16_t RING_CENTRE_X = 104;
constexpr int16_t RING_CENTRE_Y = 132;
constexpr uint32_t ICON_SIZE = 32;

} // namespace

extern "C" void FileChoose_UpdateSevenSagesMenu(GameState* gameState) {
    FileChoose_UpdateStickDirectionPromptAnim(gameState);
    FileChooseContext* fileChooseContext = (FileChooseContext*)gameState;
    Input* input = &fileChooseContext->state.input[0];
    bool dpad = CVarGetInteger(CVAR_SETTING("DpadInText"), 0);

    // Fade in after the window finishes rotating, same as the Boss Rush menu.
    fileChooseContext->sevenSagesUIAlpha += 25;
    if (fileChooseContext->sevenSagesUIAlpha > 255) {
        fileChooseContext->sevenSagesUIAlpha = 255;
    }

    // Flat cycle through all seven, including Zelda in the centre. Deliberately not modelled
    // as ring geometry: stepping "around" six and then "into" the middle is more to explain
    // than it is to use, and a wrapping list is what the rest of this screen already does.
    if (ABS(fileChooseContext->stickRelY) > 30 || (dpad && CHECK_BTN_ANY(input->press.button, BTN_DDOWN | BTN_DUP))) {
        if (fileChooseContext->stickRelY < -30 || (dpad && CHECK_BTN_ANY(input->press.button, BTN_DDOWN))) {
            fileChooseContext->sevenSagesIndex = (fileChooseContext->sevenSagesIndex + 1) % SAGE_COUNT;
            Audio_PlaySoundGeneral(NA_SE_SY_FSEL_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                   &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        } else if (fileChooseContext->stickRelY > 30 || (dpad && CHECK_BTN_ANY(input->press.button, BTN_DUP))) {
            fileChooseContext->sevenSagesIndex =
                (uint8_t)((fileChooseContext->sevenSagesIndex + SAGE_COUNT - 1) % SAGE_COUNT);
            Audio_PlaySoundGeneral(NA_SE_SY_FSEL_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                   &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        }
    }

    // Confirm: write the setting and continue to the randomizer settings menu, where the seed
    // is actually generated.
    if (CHECK_BTN_ALL(input->press.button, BTN_A)) {
        CVarSetInteger(CVAR_RANDOMIZER_SETTING("SelectedSage"), fileChooseContext->sevenSagesIndex);
        CVarSave();
        Audio_PlaySoundGeneral(NA_SE_SY_FSEL_DECIDE_L, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        fileChooseContext->prevConfigMode = fileChooseContext->configMode;
        fileChooseContext->configMode = CM_ROTATE_TO_RANDOMIZER_SETTINGS_MENU;
        return;
    }

    // Back to the quest menu.
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

    const int16_t alpha = fileChooseContext->sevenSagesUIAlpha;

    for (uint8_t i = 0; i < SAGE_COUNT; i++) {
        const SageRingEntry& entry = sSageRing[i];
        const int16_t x = RING_CENTRE_X + (entry.isCentre ? 0 : entry.ringDX);
        const int16_t y = RING_CENTRE_Y + (entry.isCentre ? 0 : entry.ringDY);

        // The selected sage draws at full brightness, the rest dimmed, so the ring reads at a
        // glance without needing a cursor sprite on top of it.
        const uint8_t shade = (i == fileChooseContext->sevenSagesIndex) ? 255 : 100;
        gDPPipeSync(POLY_OPA_DISP++);
        gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, shade, shade, shade, (uint8_t)alpha);

        const char* icon =
            entry.isCentre ? gTriforcePieceTex : static_cast<const char*>(gItemIcons[entry.itemIcon]);
        FileChoose_DrawImageRGBA32(fileChooseContext->state.gfxCtx, x, y, icon, ICON_SIZE, ICON_SIZE);
    }

    CLOSE_DISPS(fileChooseContext->state.gfxCtx);
}
