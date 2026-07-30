#include "global.h"
#include "vt.h"

#include <string.h>
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/Enhancements/randomizer/savefile.h"
#include "soh/OTRGlobals.h"
#include "soh/SaveManager.h"
#include "soh/ResourceManagerHelpers.h"

#define NUM_DUNGEONS 8
#define NUM_COWS 10

void Save_LoadFile(void);

void BossRush_InitSave(void);

/**
 *  Initialize new save.
 *  This save has an empty inventory with 3 hearts and single magic.
 */
void Sram_InitNewSave(void) {
    Save_InitFile(false);
}

/**
 *  Initialize debug save. This is also used on the Title Screen
 *  This save has a mostly full inventory with 10 hearts and single magic.
 *
 *  Some noteable flags that are set:
 *  Showed Mido sword/shield, met Deku Tree, Deku Tree mouth opened,
 *  used blue warp in Gohmas room, Zelda fled castle, light arrow cutscene watched,
 *  and set water level in Water Temple to lowest level.
 */
void Sram_InitDebugSave(void) {
    Save_InitFile(true);
}

void Sram_InitBossRushSave(void) {
    Save_InitFile(false);
    BossRush_InitSave();
}

static s16 sDungeonEntrances[] = {
    ENTR_DEKU_TREE_ENTRANCE,               // SCENE_DEKU_TREE
    ENTR_DODONGOS_CAVERN_ENTRANCE,         // SCENE_DODONGOS_CAVERN
    ENTR_JABU_JABU_ENTRANCE,               // SCENE_JABU_JABU
    ENTR_FOREST_TEMPLE_ENTRANCE,           // SCENE_FOREST_TEMPLE
    ENTR_FIRE_TEMPLE_ENTRANCE,             // SCENE_FIRE_TEMPLE
    ENTR_WATER_TEMPLE_ENTRANCE,            // SCENE_WATER_TEMPLE
    ENTR_SPIRIT_TEMPLE_ENTRANCE,           // SCENE_SPIRIT_TEMPLE
    ENTR_SHADOW_TEMPLE_ENTRANCE,           // SCENE_SHADOW_TEMPLE
    ENTR_BOTTOM_OF_THE_WELL_ENTRANCE,      // SCENE_BOTTOM_OF_THE_WELL
    ENTR_ICE_CAVERN_ENTRANCE,              // SCENE_ICE_CAVERN
    ENTR_GANONS_TOWER_0,                   // SCENE_GANONS_TOWER
    ENTR_GERUDO_TRAINING_GROUND_ENTRANCE,  // SCENE_GERUDO_TRAINING_GROUND
    ENTR_THIEVES_HIDEOUT_0,                // SCENE_THIEVES_HIDEOUT
    ENTR_INSIDE_GANONS_CASTLE_ENTRANCE,    // SCENE_INSIDE_GANONS_CASTLE
    ENTR_GANONS_TOWER_COLLAPSE_INTERIOR_0, // SCENE_GANONS_TOWER_COLLAPSE_INTERIOR
    ENTR_INSIDE_GANONS_CASTLE_COLLAPSE_0,  // SCENE_INSIDE_GANONS_CASTLE_COLLAPSE
};

/**
 *  Copy save currently on the buffer to Save Context and complete various tasks to open the save.
 *  This includes:
 *  - Set proper entrance depending on where the game was saved
 *  - If health is less than 3 hearts, give 3 hearts
 *  - If either scarecrow song is set, copy them from save context to the proper location
 *  - Handle a case where the player saved and quit after zelda cutscene but didnt get the song
 *  - Give and equip master sword if player is adult and doesnt have kokiri sword (bug?)
 *  - Revert any trade items that spoil
 */
void Sram_OpenSave() {
    u16 i;
    u16 j;
    u8* ptr;

    Save_LoadFile();

    switch (gSaveContext.savedSceneNum) {
        case SCENE_DEKU_TREE:
        case SCENE_DODONGOS_CAVERN:
        case SCENE_JABU_JABU:
        case SCENE_FOREST_TEMPLE:
        case SCENE_FIRE_TEMPLE:
        case SCENE_WATER_TEMPLE:
        case SCENE_SPIRIT_TEMPLE:
        case SCENE_SHADOW_TEMPLE:
        case SCENE_BOTTOM_OF_THE_WELL:
        case SCENE_ICE_CAVERN:
        case SCENE_GANONS_TOWER:
        case SCENE_GERUDO_TRAINING_GROUND:
        case SCENE_THIEVES_HIDEOUT:
        case SCENE_INSIDE_GANONS_CASTLE:
            gSaveContext.entranceIndex = sDungeonEntrances[gSaveContext.savedSceneNum];
            break;
        case SCENE_DEKU_TREE_BOSS:
            gSaveContext.entranceIndex = ENTR_DEKU_TREE_ENTRANCE;
            break;
        case SCENE_DODONGOS_CAVERN_BOSS:
            gSaveContext.entranceIndex = ENTR_DODONGOS_CAVERN_ENTRANCE;
            break;
        case SCENE_JABU_JABU_BOSS:
            gSaveContext.entranceIndex = ENTR_JABU_JABU_ENTRANCE;
            break;
        case SCENE_FOREST_TEMPLE_BOSS:
            gSaveContext.entranceIndex = ENTR_FOREST_TEMPLE_ENTRANCE;
            break;
        case SCENE_FIRE_TEMPLE_BOSS:
            gSaveContext.entranceIndex = ENTR_FIRE_TEMPLE_ENTRANCE;
            break;
        case SCENE_WATER_TEMPLE_BOSS:
            gSaveContext.entranceIndex = ENTR_WATER_TEMPLE_ENTRANCE;
            break;
        case SCENE_SPIRIT_TEMPLE_BOSS:
            gSaveContext.entranceIndex = ENTR_SPIRIT_TEMPLE_ENTRANCE;
            break;
        case SCENE_SHADOW_TEMPLE_BOSS:
            gSaveContext.entranceIndex = ENTR_SHADOW_TEMPLE_ENTRANCE;
            break;
        case SCENE_GANONS_TOWER_COLLAPSE_INTERIOR:
        case SCENE_INSIDE_GANONS_CASTLE_COLLAPSE:
        case SCENE_GANONDORF_BOSS:
        case SCENE_GANONS_TOWER_COLLAPSE_EXTERIOR:
        case SCENE_GANON_BOSS:
            gSaveContext.entranceIndex = ENTR_GANONS_TOWER_0;
            break;

        default:
            // Use the saved entrance value with remember save location, except when in grottos/fairy fountains
            if (CVarGetInteger(CVAR_ENHANCEMENT("RememberSaveLocation"), 0) &&
                gSaveContext.savedSceneNum != SCENE_FAIRYS_FOUNTAIN && gSaveContext.savedSceneNum != SCENE_GROTTOS) {
                break;
            }

            // Ganon's Curse: this branch is what vanilla uses to route every non-dungeon continue back to
            // Link's House (child) / Temple of Time (adult), which was silently overwriting the sage's
            // starting entrance set in Sram_InitSave on every single load (not just the first one, since
            // entranceIndex isn't a persisted field - it's recomputed here each time the file is opened).
            // Reapply the sage's home-base entrance here instead of falling through to the Link default.
            {
                s32 sageEntrance = Randomizer_GetSageHomeEntrance();
                if (sageEntrance != -1) {
                    gSaveContext.entranceIndex = sageEntrance;
                    break;
                }
            }

            if (gSaveContext.savedSceneNum != SCENE_LINKS_HOUSE) {
                gSaveContext.entranceIndex =
                    (LINK_AGE_IN_YEARS == YEARS_CHILD) ? ENTR_LINKS_HOUSE_CHILD_SPAWN : ENTR_TEMPLE_OF_TIME_WARP_PAD;
            } else {
                gSaveContext.entranceIndex = ENTR_LINKS_HOUSE_CHILD_SPAWN;
            }
            break;
    }

    if (!CVarGetInteger(CVAR_ENHANCEMENT("PersistentMasks"), 0)) {
        gSaveContext.ship.maskMemory = PLAYER_MASK_NONE;
    }

    osSyncPrintf("scene_no = %d\n", gSaveContext.entranceIndex);
    osSyncPrintf(VT_RST);

    if (gSaveContext.health < STARTING_HEALTH) {
        gSaveContext.health =
            CVarGetInteger(CVAR_ENHANCEMENT("FullHealthSpawn"), 0) ? gSaveContext.healthCapacity : STARTING_HEALTH;
    }

    if (gSaveContext.scarecrowLongSongSet) {
        osSyncPrintf(VT_FGCOL(BLUE));
        osSyncPrintf("\n====================================================================\n");

        memcpy(gScarecrowLongSongPtr, gSaveContext.scarecrowLongSong, sizeof(gSaveContext.scarecrowLongSong));

        ptr = (u8*)gScarecrowLongSongPtr;
        for (i = 0; i < ARRAY_COUNT(gSaveContext.scarecrowLongSong); i++, ptr++) {
            osSyncPrintf("%d, ", *ptr);
        }

        osSyncPrintf("\n====================================================================\n");
        osSyncPrintf(VT_RST);
    }

    if (gSaveContext.scarecrowSpawnSongSet) {
        osSyncPrintf(VT_FGCOL(GREEN));
        osSyncPrintf("\n====================================================================\n");

        memcpy(gScarecrowSpawnSongPtr, gSaveContext.scarecrowSpawnSong, sizeof(gSaveContext.scarecrowSpawnSong));

        ptr = gScarecrowSpawnSongPtr;
        for (i = 0; i < ARRAY_COUNT(gSaveContext.scarecrowSpawnSong); i++, ptr++) {
            osSyncPrintf("%d, ", *ptr);
        }

        osSyncPrintf("\n====================================================================\n");
        osSyncPrintf(VT_RST);
    }

    // if zelda cutscene has been watched but lullaby was not obtained, restore cutscene and take away letter
    if ((Flags_GetEventChkInf(EVENTCHKINF_OBTAINED_ZELDAS_LETTER)) && !CHECK_QUEST_ITEM(QUEST_SONG_LULLABY) &&
        !IS_RANDO) {
        i = gSaveContext.eventChkInf[4] & ~1;
        gSaveContext.eventChkInf[4] = i;

        INV_CONTENT(ITEM_LETTER_ZELDA) = ITEM_CHICKEN;

        for (j = 1; j < ARRAY_COUNT(gSaveContext.equips.buttonItems); j++) {
            if (gSaveContext.equips.buttonItems[j] == ITEM_LETTER_ZELDA) {
                gSaveContext.equips.buttonItems[j] = ITEM_CHICKEN;
            }
        }
    }

    if (LINK_AGE_IN_YEARS == YEARS_ADULT && !CHECK_OWNED_EQUIP(EQUIP_TYPE_SWORD, EQUIP_INV_SWORD_MASTER)) {
        if (!IS_RANDO || !Randomizer_GetSettingValue(RSK_SHUFFLE_MASTER_SWORD)) {
            gSaveContext.inventory.equipment |= OWNED_EQUIP_FLAG(EQUIP_TYPE_SWORD, EQUIP_INV_SWORD_MASTER);
            gSaveContext.equips.buttonItems[0] = ITEM_SWORD_MASTER;
            gSaveContext.equips.equipment &= ~(0xF << (EQUIP_TYPE_SWORD * 4));
            gSaveContext.equips.equipment |= EQUIP_VALUE_SWORD_MASTER << (EQUIP_TYPE_SWORD * 4);
        }
    }

    if (GameInteractor_Should(VB_REVERT_SPOILING_ITEMS, true)) {
        for (i = 0; i < ARRAY_COUNT(gSpoilingItems); i++) {
            if (INV_CONTENT(ITEM_TRADE_ADULT) == gSpoilingItems[i]) {
                INV_CONTENT(gSpoilingItemReverts[i]) = gSpoilingItemReverts[i];

                for (j = 1; j < ARRAY_COUNT(gSaveContext.equips.buttonItems); j++) {
                    if (gSaveContext.equips.buttonItems[j] == gSpoilingItems[i]) {
                        gSaveContext.equips.buttonItems[j] = gSpoilingItemReverts[i];
                    }
                }
            }
        }
    }

    gSaveContext.magicLevel = 0;
}

// Ganon's Curse: recolor the Kokiri Tunic (what every sage actually starts wearing - none of the
// kits equip Goron/Zora tunic directly, those are just carried for later) so each sage is visually
// distinct at a glance. This is a global cosmetic cvar, not save-specific, so it just reflects
// whichever sage was most recently generated - fine for a single-player curated experience.
static void Sram_SetSageTunicColor(u8 r, u8 g, u8 b) {
    Color_RGB8 color = { r, g, b };
    CVarSetColor24(CVAR_COSMETIC("Link.KokiriTunic.Value"), color);
    CVarSetInteger(CVAR_COSMETIC("Link.KokiriTunic.Changed"), 1);
}

void Sram_InitSave(FileChooseContext* fileChooseCtx) {
    u16 offset;
    u16 j;
    u16* ptr;
    u16 checksum;

    if (fileChooseCtx->buttonIndex != 0 || !CVarGetInteger(CVAR_DEVELOPER_TOOLS("DebugEnabled"), 0)) {
        Sram_InitNewSave();
    } else {
        Sram_InitDebugSave();
    }

    gSaveContext.entranceIndex = ENTR_LINKS_HOUSE_CHILD_SPAWN;
    gSaveContext.linkAge = 1;
    gSaveContext.dayTime = 0x6AAB;
    gSaveContext.cutsceneIndex = 0xFFF1;
    if (ResourceMgr_GetGameRegion(0) == GAME_REGION_PAL && gSaveContext.language != LANGUAGE_JPN) {
        gSaveContext.ship.filenameLanguage = NAME_LANGUAGE_PAL;
    } else { // GAME_REGION_NTSC
        gSaveContext.ship.filenameLanguage =
            (gSaveContext.language == LANGUAGE_JPN) ? NAME_LANGUAGE_NTSC_JPN : NAME_LANGUAGE_NTSC_ENG;
    }

    if ((fileChooseCtx->buttonIndex == 0 && CVarGetInteger(CVAR_DEVELOPER_TOOLS("DebugEnabled"), 0))) {
        gSaveContext.cutsceneIndex = 0;
    }

    for (offset = 0; offset < 8; offset++) {
        gSaveContext.playerName[offset] = Save_GetSaveMetaInfo(fileChooseCtx->buttonIndex)->playerName[offset];
    }

    gSaveContext.n64ddFlag = fileChooseCtx->n64ddFlag;

    u8 currentQuest = fileChooseCtx->questType[fileChooseCtx->buttonIndex];

    if (currentQuest == QUEST_RANDOMIZER && (Randomizer_IsSeedGenerated() || Randomizer_IsSpoilerLoaded())) {
        gSaveContext.ship.quest.id = QUEST_RANDOMIZER;

        Randomizer_InitSaveFile();

        // Ganon's Curse: override starting scene + age based on the selected sage. Must run
        // after Randomizer_InitSaveFile(), not before - that function sets linkAge/entranceIndex
        // itself based on the unrelated RSK_SELECTED_STARTING_AGE setting, which was silently
        // clobbering this override when it ran first (entranceIndex happened to get recomputed
        // correctly later in Sram_OpenSave anyway, which is why only linkAge looked broken).
        // Link is not selectable at all (see RandomizerOptions.h) - Rauru takes his old Lon Lon
        // Ranch / Hyrule Field slot instead of Temple of Time, as the mod's new default sage.
        // Age is set to whichever vanilla equip-slot restrictions the sage's kit actually needs
        // (e.g. Hookshot/Hover Boots/Mirror Shield/Megaton Hammer are adult-only equipment in
        // vanilla), not a narrative choice.
        // Ganon's Curse: age/entrance/tunic all come from the single sage definition table in
        // savefile.cpp, which generation-time code reads too. These used to be hardcoded here in a
        // parallel switch; the duplication is what let the generator and the runtime disagree about a
        // sage's starting age.
        {
            int32_t sageEntrance = Randomizer_GetSageHomeEntrance();
            if (sageEntrance != -1) {
                uint8_t tunicR = 0, tunicG = 0, tunicB = 0;
        
                gSaveContext.entranceIndex = sageEntrance;
                gSaveContext.linkAge =
                    Randomizer_GetSageStartingAge() == RO_AGE_ADULT ? LINK_AGE_ADULT : LINK_AGE_CHILD;
        
                Randomizer_GetSageTunicColor(&tunicR, &tunicG, &tunicB);
                Sram_SetSageTunicColor(tunicR, tunicG, tunicB);

                // Ganon's Curse: mark Saria's Kokiri Forest greeting as already happened.
                //
                // EnSa plays gSpot04Cs_10E20 (z_en_sa.c:511) whenever the player is in Kokiri
                // Forest without the Kokiri Emerald and INFTABLE_GREETED_BY_SARIA is unset. That
                // cutscene is the vanilla *opening* - it shows Link coming out of his house and
                // being greeted - so it is a hard lore break here: Link isn't playable, and Saria
                // herself is a selectable sage who may be the one watching it.
                //
                // It used to be suppressed as a side effect of TimeSavers.SkipCutscene.Entrances,
                // which force-set this flag via VB_NOT_BE_GREETED_BY_SARIA. Phase 4a turned that
                // setting off to restore genuine entrance cutscenes, which un-suppressed this too.
                // Setting the flag directly is the narrow fix: it keeps every real establishing
                // shot (DMT, Kakariko, Zora's Domain, Lake Hylia, Gerudo Valley, ...) while killing
                // only the one cutscene that depicts the vanilla opening.
                //
                // Note this is NOT an entrance cutscene - Kokiri Forest's only entry in
                // sEntranceCutsceneTable is the Deku Sprout CS, which is unrelated. Turning
                // entrance cutscenes back off would not have fixed this.
                //
                // Bonus: with the flag set, Saria falls through to her ordinary talk path, which is
                // what RSK_SARIA_HINT needs in order to deliver her hint (see npc-hints.md).
                Flags_SetInfTable(INFTABLE_GREETED_BY_SARIA);
            }
        }

        // Ganon's Curse: cutsceneIndex >= 0xFFF0 (the "skip cutscene" sentinel this function set
        // earlier, and what Randomizer_InitSaveFile() above also uses for RO_AGE_CHILD) is NOT a
        // generic "no cutscene" flag - z_play.c's scene-layer computation treats it as
        // "sceneLayer = 4 + (cutsceneIndex & 0xF)", and that sceneLayer gets ADDED DIRECTLY to
        // entranceIndex when indexing gEntranceTable. That +5 offset only lands somewhere safe for
        // ENTR_LINKS_HOUSE_CHILD_SPAWN specifically (a legitimate "cutscene variant" of Link's House
        // - literally how the vanilla opening/Navi cutscene triggers). For any other entranceIndex it
        // lands on a completely unrelated, arbitrary entry 5 slots later in the global table -
        // confirmed via live testing that Nabooru's entrance+5 pulled in the game's own credits
        // sequence. Every sage (including Link now, since he has his own fixed Lon Lon Ranch entrance
        // instead of Link's House) needs a normal (< 0xFFF0) cutsceneIndex so scene-layer falls
        // through to the ordinary child/adult day/night branch instead.
        gSaveContext.cutsceneIndex = 0;
    } else {
        gSaveContext.ship.quest.id = currentQuest;
    }

    Save_SaveFile();
    SaveManager_ThreadPoolWait();
}

void Sram_InitSram(GameState* gameState) {
    Save_Init();

    Audio_SetSoundOutputMode(gSaveContext.audioSetting);
}
