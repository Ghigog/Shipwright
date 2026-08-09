/**
 * Seven Sages - Phase 6: every changed item documents itself when you pick it up.
 *
 * The mod adds two dozen abilities to items the player already knows, and nothing in the game
 * tells them. Vanilla already hands the player a short piece of text at exactly the right
 * moment - the get-item textbox - so this appends one extra page to it rather than inventing a
 * new surface. Vanilla's own line is kept verbatim; the appended page opens with a blue
 * "Enhanced:" label and says what this run's version of the item does.
 *
 * (Blue and not bold: OoT's text engine has no weight control code at all - see the full set in
 * soh/include/message_data_fmt.h, which has colour, shift and text speed but nothing for bold -
 * and the font is a single bitmap face with no bold variant.)
 *
 * Content lives in seven-sages/data/item-enhanced-text.json; the table below is generated from
 * it by tools/gen_item_text.py. Edit the JSON, not the table.
 *
 * ── Why a filter hook and not COND_ID_HOOK ──────────────────────────────────────────────────
 * Most items open their vanilla text ID (Megaton Hammer is 0x38), which an ID hook handles
 * cleanly - that is what NoSkulltulaFreeze.cpp does. But seven of these items are randomizer
 * custom entries, and every one of those opens the single shared ID TEXT_RANDOMIZER_CUSTOM_ITEM
 * (0xF8), which ItemMessages.cpp's BuildItemMessage already owns. Two ID hooks on one ID run in
 * registration order, and registration order here is static-initialisation order across
 * translation units - unspecified by the standard. Losing that race means rando's LoadIntoFont()
 * lands second and silently discards our page.
 *
 * GameInteractor_Hooks.cpp:395-398 runs unfiltered hooks, then by-ID hooks, then filter hooks.
 * A filter hook is therefore guaranteed to run after BuildItemMessage, whatever the link order.
 * One registration covers both families.
 *
 * ── The two states the message can be in when we run ────────────────────────────────────────
 * loadFromMessageTable still true  - nobody customised this text, and Message_OpenText will read
 *                                    the table after we return. We load the vanilla entry
 *                                    ourselves, append, and take ownership of the load.
 * loadFromMessageTable already false - an earlier hook (rando's, for the custom-item family) has
 *                                    already written a finished message into font->msgBuf. We
 *                                    read that back and append to it, so whatever it built is
 *                                    preserved exactly, custom item icon included.
 *
 * ── Why appending a page is safe where appending text is not ────────────────────────────────
 * Message_Decode stops at the first page break as well as at a terminator (z_message_PAL.c:2302),
 * so its unbounded 200-byte msgBufDecoded budget is spent per *page*. An appended page therefore
 * cannot push the vanilla page any closer to overflowing, no matter how long it is; AutoFormat
 * paginates our own text past four lines for the same reason. Both halves still have to end in a
 * real stop byte - see seven-sages/docs/custom-text-safety.md for what a missing one costs.
 */
// Order matters: these plain-C++ includes must come before the extern "C" block below pulls in
// functions.h/variables.h/macros.h. Several libultraship headers reachable from that C chain are
// template-using C++ headers, and if their first inclusion in this translation unit happens
// inside extern "C" the templates fail to parse. Same reason SevenSagesNpcHints.cpp and
// SevenSagesCutscenes.cpp order their includes this way.
#include <soh/OTRGlobals.h>
#include "soh/ShipInit.hpp"
#include "soh/Enhancements/custom-message/CustomMessageManager.h"
#include "soh/Enhancements/custom-message/CustomMessageTypes.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/randomizer/randomizer.h"
#include "soh/Enhancements/randomizer/randomizerTypes.h"
#include "soh/Enhancements/randomizer/randomizerEnums/RandomizerGet.h"

#include <string>

extern "C" {
extern PlayState* gPlayState;
#include <macros.h>
#include <functions.h>
#include <variables.h>
}

namespace {

/**
 * One item's appended page.
 *
 * textId is the get-item message the game opens. For all but the custom-entry items that ID is
 * unique to the item, so rgId is RG_NONE and the ID alone matches. For the items whose textId is
 * TEXT_RANDOMIZER_CUSTOM_ITEM, that ID is shared by every randomizer custom entry in the game, so
 * rgId names the specific RandomizerGet and both have to match.
 */
struct EnhancedItemText {
    uint16_t textId;
    RandomizerGet rgId;
    const char* text;
};

// Generated from data/item-enhanced-text.json in the seven-sages repo - edit that file, not this.
constexpr EnhancedItemText enhancedItemText[] = {
// >>> SEVEN_SAGES_GENERATED: ENHANCED_TEXT - edit data/item-enhanced-text.json, not this
    // RG_MEGATON_HAMMER
    { 0x38, RG_NONE,
      "A ground strike stuns everything nearby, and the hammer smashes whatever a bomb would." },
    // RG_IRON_BOOTS
    { 0x53, RG_NONE,
      "No slow walk. Nothing knocks you back, and steep ground no longer makes you slide." },
    // RG_HOVER_BOOTS
    { 0x54, RG_NONE,
      "Faster, and less slippery. Hold a direction as a hover ends and you jump instead of dropping." },
    // RG_GORON_TUNIC
    { 0x50, RG_NONE,
      "Fire cannot touch you at all. Walk straight through walls of flame, and across lava." },
    // RG_ZORA_TUNIC
    { 0x51, RG_NONE,
      "Breathe underwater freely, and nothing can freeze you - ice traps included." },
    // RG_SILVER_GAUNTLETS
    { 0x5B, RG_NONE,
      "Force any small-key door for half your magic. Lift and throw ordinary rocks, and stunned small foes." },
    // RG_GOLDEN_GAUNTLETS
    { 0x5C, RG_NONE,
      "Force any door, boss doors too, for half your magic. Lift and throw any rock, or any stunned foe." },
    // RG_MIRROR_SHIELD
    { 0x4E, RG_NONE,
      "Raise the shield and the light it throws stuns whatever stands in front of you." },
    // RG_DEKU_SHIELD
    { 0x4C, RG_NONE,
      "Fire no longer destroys it. The shield catches the flame and carries it, like a lit stick." },
    // RG_STONE_OF_AGONY
    { 0x68, RG_NONE,
      "It sounds whenever something hidden is near, and every hidden grotto is marked for you." },
    // RG_DINS_FIRE
    { 0xAD, RG_NONE,
      "The flames also blast apart anything a bomb could break." },
    // RG_NAYRUS_LOVE
    { 0xAF, RG_NONE,
      "Walk through walls of flame while it holds. Your magic is freed the moment the spell is cast." },
    // RG_FIRE_ARROWS
    { 0x70, RG_NONE,
      "Where the arrow lands, fire lingers. Anything standing in it keeps burning." },
    // RG_ICE_ARROWS
    { 0x71, RG_NONE,
      "Where the arrow lands, a freezing field lingers. Anything caught in it is held fast." },
    // RG_LIGHT_ARROWS
    { 0x72, RG_NONE,
      "Six times the damage, for 24 magic a shot. Spend it where it counts." },
    // RG_ZELDAS_LULLABY
    { 0xD4, RG_NONE,
      "For 24 magic, every enemy in the room falls asleep for 15 seconds." },
    // RG_EPONAS_SONG
    { 0xD2, RG_NONE,
      "For 24 magic, you move half again as fast for a full minute." },
    // RG_SARIAS_SONG
    { 0xD1, RG_NONE,
      "For 24 magic, you can climb any surface at all for 20 seconds." },
    // RG_SUNS_SONG
    { 0xD3, RG_NONE,
      "For 24 magic, every unlit torch in the room catches, and you gain temporary hearts." },
    // RG_SONG_OF_TIME
    { 0xD5, RG_NONE,
      "For 24 magic, change between child and adult anywhere - no pedestal needed." },
    // RG_SONG_OF_STORMS
    { 0xD6, RG_NONE,
      "For 24 magic, the rain puts out the room's torches and slowly refills your magic." },
    // RG_MINUET_OF_FOREST
    { 0x73, RG_NONE,
      "Warping is unchanged. Refuse the warp instead and 24 magic refills your sticks, nuts, seeds and "
      "arrows." },
    // RG_BOLERO_OF_FIRE
    { 0x74, RG_NONE,
      "Warping is unchanged. Refuse the warp instead and 24 magic refills your bombs and bombchus." },
    // RG_SERENADE_OF_WATER
    { 0x75, RG_NONE,
      "Warping is unchanged. Refuse the warp instead and 24 magic heals you slowly for the next few "
      "minutes." },
    // RG_REQUIEM_OF_SPIRIT
    { 0x76, RG_NONE,
      "Warping is unchanged. Refuse the warp instead and 24 magic makes your spells free for 20 seconds." },
    // RG_NOCTURNE_OF_SHADOW
    { 0x77, RG_NONE,
      "Warping is unchanged. Refuse the warp instead and 24 magic hides you from every guard for 20 "
      "seconds." },
    // RG_PRELUDE_OF_LIGHT
    { 0x78, RG_NONE,
      "Warping is unchanged. Refuse the warp instead and 24 magic keeps you from dying for 20 seconds." },
    // RG_KEATON_MASK
    { TEXT_RANDOMIZER_CUSTOM_ITEM, RG_KEATON_MASK,
      "Worn, it halves every price in Hyrule and doubles everything that drops." },
    // RG_SKULL_MASK
    { TEXT_RANDOMIZER_CUSTOM_ITEM, RG_SKULL_MASK,
      "Worn, the dead take you for one of their own and pay you no mind." },
    // RG_GERUDO_MASK
    { TEXT_RANDOMIZER_CUSTOM_ITEM, RG_GERUDO_MASK,
      "Worn, you can force any small-key door for half your magic." },
    // RG_GORON_MASK
    { TEXT_RANDOMIZER_CUSTOM_ITEM, RG_GORON_MASK,
      "Worn, it grants the Goron Tunic's protection - fire cannot touch you, lava included." },
    // RG_ZORA_MASK
    { TEXT_RANDOMIZER_CUSTOM_ITEM, RG_ZORA_MASK,
      "Worn, it grants the Zora Tunic's protection - breathe underwater, and never freeze." },
    // RG_MASK_OF_TRUTH
    { TEXT_RANDOMIZER_CUSTOM_ITEM, RG_MASK_OF_TRUTH,
      "Worn, every chest in the area is marked for you, walls and all." },
    // RG_BOTTLE_WITH_BLUE_FIRE
    { TEXT_RANDOMIZER_CUSTOM_ITEM, RG_BOTTLE_WITH_BLUE_FIRE,
      "Set down, the flame freezes whatever comes near it - and you can bottle it again after." },
// <<< SEVEN_SAGES_GENERATED: ENHANCED_TEXT
};

const EnhancedItemText* FindEntry(uint16_t textId) {
    for (const EnhancedItemText& entry : enhancedItemText) {
        if (entry.textId != textId) {
            continue;
        }
        if (entry.rgId == RG_NONE) {
            return &entry;
        }
        // A shared TEXT_RANDOMIZER_CUSTOM_ITEM row. getItemEntry is the item currently being
        // handed over, and is populated before the textbox opens - the same field
        // ItemMessages.cpp's BuildItemMessage reads to decide what to say.
        Player* player = GET_PLAYER(gPlayState);
        if (player != nullptr && player->getItemEntry.getItemId == entry.rgId) {
            return &entry;
        }
    }
    return nullptr;
}

// Read back whatever a previous hook loaded into the font, preserving its textbox type and
// position - LoadIntoFont() packs those into charTexBuf[0] and reads them back off the
// CustomMessage, so they have to be carried across or the box changes colour under us.
CustomMessage MessageFromFont() {
    MessageContext* msgCtx = &gPlayState->msgCtx;
    Font* font = &msgCtx->font;
    const uint8_t typePos = font->charTexBuf[0];
    return CustomMessage(std::string(font->msgBuf, font->msgLength),
                         static_cast<TextBoxType>(typePos >> 4),
                         static_cast<TextBoxPosition>(typePos & 0xF));
}

void AppendEnhancedPage(uint16_t* textId, bool* loadFromMessageTable) {
    const EnhancedItemText* entry = FindEntry(*textId);
    if (entry == nullptr) {
        return;
    }

    CustomMessage msg = *loadFromMessageTable ? CustomMessage::LoadVanillaMessageTableEntry(*textId)
                                              : MessageFromFont();

    // Read the English slot, not GetForCurrentLanguage, and note this is not an English-only
    // shortcut: both sources put the bytes for the language the player is ACTUALLY playing in
    // there. LoadVanillaMessageTableEntry picks the right language's message table first and
    // then stores the result through the single-string constructor, and that constructor writes
    // messages[LANGUAGE_ENG] and leaves the other two empty. MessageFromFont inherits the same
    // shape. Asking for the current language on a German save would therefore hand back "".
    const std::string base = msg.GetEnglish(MF_RAW);
    // Both sources hand back a finished message, so it ends in a stop byte we have to take off
    // before anything can follow it. Anything else ending the message - MESSAGE_EVENT (0x0B),
    // which hands control to an actor's script, or a choice code - means this textbox drives a
    // flow we would break by extending it, so leave it exactly as it is.
    if (base.empty() || base.back() != CustomMessage::MESSAGE_END()[0]) {
        return;
    }

    // AutoFormat converts %b/%w to colour codes, wraps to the textbox width, paginates past four
    // lines, and appends the terminator this page needs. It is not optional: LoadIntoFont copies
    // the string raw, so every control code has to already be baked in by this point.
    CustomMessage enhanced(std::string("%bEnhanced:%w ") + entry->text);
    enhanced.AutoFormat();

    const std::string combined = base.substr(0, base.size() - 1) + CustomMessage::WAIT_FOR_INPUT() +
                                 enhanced.GetEnglish(MF_RAW);
    // All three language slots get the same string, and that is load-bearing rather than lazy:
    // LoadIntoFont copies whichever slot matches gSaveContext.language, and the single-string
    // constructor would leave the other two empty. An empty message is an UNTERMINATED one -
    // Message_Decode's copy loop has no bounds check and only stops on a stop byte, so it would
    // run off the end of font->msgBuf and overwrite the tail of MessageContext, nulling
    // interfaceCtx.view.gfxCtx and crashing in Interface_Draw several frames later. See
    // seven-sages/docs/custom-text-safety.md. The blurb itself is authored in English only, so a
    // German or French save keeps its own vanilla line and gets an English "Enhanced:" page -
    // the same trade every other Seven Sages custom message makes.
    msg = CustomMessage(combined, combined, combined, msg.GetTextBoxType(), msg.GetTextBoxPosition());
    msg.LoadIntoFont();
    *loadFromMessageTable = false;
}

} // namespace

void RegisterSevenSagesItemText() {
    static HOOK_ID hookId = 0;
    GameInteractor::Instance->UnregisterGameHookForFilter<GameInteractor::OnOpenText>(hookId);
    hookId = 0;
    if (!IS_SEVENSAGES) {
        return;
    }
    hookId = GameInteractor::Instance->RegisterGameHookForFilter<GameInteractor::OnOpenText>(
        [](uint16_t* textId, bool* loadFromMessageTable) { return FindEntry(*textId) != nullptr; },
        AppendEnhancedPage);
}

static RegisterShipInitFunc sevenSagesItemTextInitFunc(RegisterSevenSagesItemText, { "IS_RANDO" });
