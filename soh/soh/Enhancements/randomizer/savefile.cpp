#include "savefile.h"
#include "soh/OTRGlobals.h"
#include "soh/ResourceManagerHelpers.h"
#include "soh/cvar_prefixes.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/randomizer/logic.h"
#include "soh/Enhancements/randomizer/randomizer.h"
#include "soh/Enhancements/SevenSagesCoop/SevenSagesCoop.h"

#include <libultraship/bridge.h>
#include <spdlog/spdlog.h>

extern "C" {
#include <z64.h>
#include "variables.h"
#include "functions.h"
#include "macros.h"

uint8_t Randomizer_GetSettingValue(RandomizerSettingKey randoSettingKey);
GetItemEntry Randomizer_GetItemFromKnownCheck(RandomizerCheck randomizerCheck, GetItemID ogId);
}

// RANDOTODO: Replace most of these GiveLink functions with calls to
// Item_Give in z_parameter, we'll need to update Item_Give to ensure
// nothing breaks when calling it without a valid play first.
void GiveLinkRupees(int numOfRupees) {
    int maxRupeeCount = 0;
    if (CUR_UPG_VALUE(UPG_WALLET) == 0) {
        maxRupeeCount = 99;
    } else if (CUR_UPG_VALUE(UPG_WALLET) == 1) {
        maxRupeeCount = 200;
    } else if (CUR_UPG_VALUE(UPG_WALLET) == 2) {
        maxRupeeCount = 500;
    } else if (CUR_UPG_VALUE(UPG_WALLET) == 3) {
        maxRupeeCount = 999;
    }

    int newRupeeCount = gSaveContext.rupees;
    newRupeeCount += numOfRupees;

    if (newRupeeCount > maxRupeeCount) {
        gSaveContext.rupees = maxRupeeCount;
    } else {
        gSaveContext.rupees = newRupeeCount;
    }
}

static uint16_t rupeeCounts[] = {
    1,   // ITEM_RUPEE_GREEN
    5,   // ITEM_RUPEE_BLUE
    20,  // ITEM_RUPEE_RED
    50,  // ITEM_RUPEE_PURPLE
    200, // ITEM_RUPEE_GOLD
};

void StartingItemGive(GetItemEntry getItemEntry, RandomizerCheck randomizerCheck) {
    if (randomizerCheck != RC_MAX) {
        OTRGlobals::Instance->gRandoContext->GetItemLocation(randomizerCheck)->SetCheckStatus(RCSHOW_SAVED);
    }
    if (getItemEntry.modIndex == MOD_NONE) {
        if (getItemEntry.itemId >= ITEM_RUPEE_GREEN && getItemEntry.itemId <= ITEM_RUPEE_GOLD) {
            GiveLinkRupees(rupeeCounts[getItemEntry.itemId - ITEM_RUPEE_GREEN]);
        } else {
            if (getItemEntry.getItemId == GI_SWORD_BGS) {
                gSaveContext.bgsFlag = true;
            }
            Item_Give(NULL, static_cast<uint8_t>(getItemEntry.itemId));
        }
    } else if (getItemEntry.modIndex == MOD_RANDOMIZER) {
        if (getItemEntry.getItemId == RG_ICE_TRAP) {
            gSaveContext.ship.pendingIceTrapCount++;
        } else {
            Randomizer_Item_Give(NULL, getItemEntry);
        }
    }
}

void GiveLinkDekuSticks(int howManySticks) {
    int maxStickCount = 0;
    if (CUR_UPG_VALUE(UPG_STICKS) == 0) {
        INV_CONTENT(ITEM_STICK) = ITEM_STICK;
        Inventory_ChangeUpgrade(UPG_STICKS, 1);
        maxStickCount = 10;
    } else if (CUR_UPG_VALUE(UPG_STICKS) == 1) {
        maxStickCount = 10;
    } else if (CUR_UPG_VALUE(UPG_STICKS) == 2) {
        maxStickCount = 20;
    } else if (CUR_UPG_VALUE(UPG_STICKS) == 3) {
        maxStickCount = 30;
    }

    if ((AMMO(ITEM_STICK) + howManySticks) > maxStickCount) {
        AMMO(ITEM_STICK) = maxStickCount;
    } else {
        AMMO(ITEM_STICK) += howManySticks;
    }
}

void GiveLinkDekuNuts(int howManyNuts) {
    int maxNutCount = 0;
    if (CUR_UPG_VALUE(UPG_NUTS) == 0) {
        INV_CONTENT(ITEM_NUT) = ITEM_NUT;
        Inventory_ChangeUpgrade(UPG_NUTS, 1);
        maxNutCount = 20;
    } else if (CUR_UPG_VALUE(UPG_NUTS) == 1) {
        maxNutCount = 20;
    } else if (CUR_UPG_VALUE(UPG_NUTS) == 2) {
        maxNutCount = 30;
    } else if (CUR_UPG_VALUE(UPG_NUTS) == 3) {
        maxNutCount = 40;
    }

    if ((AMMO(ITEM_NUT) + howManyNuts) > maxNutCount) {
        AMMO(ITEM_NUT) = maxNutCount;
    } else {
        AMMO(ITEM_NUT) += howManyNuts;
    }
}

void GiveLinksPocketItem() {
    if (Randomizer_GetSettingValue(RSK_LINKS_POCKET) != RO_LINKS_POCKET_NOTHING) {
        GetItemEntry getItemEntry = Randomizer_GetItemFromKnownCheck(RC_LINKS_POCKET, (GetItemID)RG_NONE);
        StartingItemGive(getItemEntry, RC_LINKS_POCKET);
        // If we re-add the above, we'll get the item on save creation, now it's given on first load.
        Flags_SetRandomizerInf(RAND_INF_LINKS_POCKET);
    }
}

void SetStartingItems() {
    int startingAge = OTRGlobals::Instance->gRandoContext->GetOption(RSK_SELECTED_STARTING_AGE).Get();
    if (Randomizer_GetSettingValue(RSK_STARTING_KOKIRI_SWORD))
        Item_Give(NULL, ITEM_SWORD_KOKIRI);
    if (Randomizer_GetSettingValue(RSK_STARTING_DEKU_SHIELD))
        Item_Give(NULL, ITEM_SHIELD_DEKU);
    if (Randomizer_GetSettingValue(RSK_STARTING_HYLIAN_SHIELD))
        Item_Give(NULL, ITEM_SHIELD_HYLIAN);
    if (Randomizer_GetSettingValue(RSK_STARTING_MIRROR_SHIELD))
        Item_Give(NULL, ITEM_SHIELD_MIRROR);
    if (Randomizer_GetSettingValue(RSK_STARTING_GORON_TUNIC))
        Item_Give(NULL, ITEM_TUNIC_GORON);
    if (Randomizer_GetSettingValue(RSK_STARTING_ZORA_TUNIC))
        Item_Give(NULL, ITEM_TUNIC_ZORA);
    if (Randomizer_GetSettingValue(RSK_STARTING_IRON_BOOTS))
        Item_Give(NULL, ITEM_BOOTS_IRON);
    if (Randomizer_GetSettingValue(RSK_STARTING_HOVER_BOOTS))
        Item_Give(NULL, ITEM_BOOTS_HOVER);
    if (Randomizer_GetSettingValue(RSK_STARTING_MEGATON_HAMMER))
        Item_Give(NULL, ITEM_HAMMER);
    if (Randomizer_GetSettingValue(RSK_STARTING_BOOMERANG))
        Item_Give(NULL, ITEM_BOOMERANG);
    if (Randomizer_GetSettingValue(RSK_STARTING_LENS_OF_TRUTH))
        Item_Give(NULL, ITEM_LENS);
    if (Randomizer_GetSettingValue(RSK_STARTING_DINS_FIRE))
        Item_Give(NULL, ITEM_DINS_FIRE);
    if (Randomizer_GetSettingValue(RSK_STARTING_FARORES_WIND))
        Item_Give(NULL, ITEM_FARORES_WIND);
    if (Randomizer_GetSettingValue(RSK_STARTING_NAYRUS_LOVE))
        Item_Give(NULL, ITEM_NAYRUS_LOVE);
    if (Randomizer_GetSettingValue(RSK_STARTING_FIRE_ARROWS))
        Item_Give(NULL, ITEM_ARROW_FIRE);
    if (Randomizer_GetSettingValue(RSK_STARTING_ICE_ARROWS))
        Item_Give(NULL, ITEM_ARROW_ICE);
    if (Randomizer_GetSettingValue(RSK_STARTING_LIGHT_ARROWS))
        Item_Give(NULL, ITEM_ARROW_LIGHT);
    if (Randomizer_GetSettingValue(RSK_STARTING_STONE_OF_AGONY))
        Item_Give(NULL, ITEM_STONE_OF_AGONY);

    // Songs
    if (Randomizer_GetSettingValue(RSK_STARTING_ZELDAS_LULLABY))
        Item_Give(NULL, ITEM_SONG_LULLABY);
    if (Randomizer_GetSettingValue(RSK_STARTING_EPONAS_SONG))
        Item_Give(NULL, ITEM_SONG_EPONA);
    if (Randomizer_GetSettingValue(RSK_STARTING_SARIAS_SONG))
        Item_Give(NULL, ITEM_SONG_SARIA);
    if (Randomizer_GetSettingValue(RSK_STARTING_SUNS_SONG))
        Item_Give(NULL, ITEM_SONG_SUN);
    if (Randomizer_GetSettingValue(RSK_STARTING_SONG_OF_TIME))
        Item_Give(NULL, ITEM_SONG_TIME);
    if (Randomizer_GetSettingValue(RSK_STARTING_SONG_OF_STORMS))
        Item_Give(NULL, ITEM_SONG_STORMS);
    if (Randomizer_GetSettingValue(RSK_STARTING_MINUET_OF_FOREST))
        Item_Give(NULL, ITEM_SONG_MINUET);
    if (Randomizer_GetSettingValue(RSK_STARTING_BOLERO_OF_FIRE))
        Item_Give(NULL, ITEM_SONG_BOLERO);
    if (Randomizer_GetSettingValue(RSK_STARTING_SERENADE_OF_WATER))
        Item_Give(NULL, ITEM_SONG_SERENADE);
    if (Randomizer_GetSettingValue(RSK_STARTING_REQUIEM_OF_SPIRIT))
        Item_Give(NULL, ITEM_SONG_REQUIEM);
    if (Randomizer_GetSettingValue(RSK_STARTING_NOCTURNE_OF_SHADOW))
        Item_Give(NULL, ITEM_SONG_NOCTURNE);
    if (Randomizer_GetSettingValue(RSK_STARTING_PRELUDE_OF_LIGHT))
        Item_Give(NULL, ITEM_SONG_PRELUDE);

    if (Randomizer_GetSettingValue(RSK_STARTING_SKULLTULA_TOKEN)) {
        gSaveContext.inventory.questItems |= gBitFlags[QUEST_SKULL_TOKEN];
        gSaveContext.inventory.gsTokens = Randomizer_GetSettingValue(RSK_STARTING_SKULLTULA_TOKEN);
    }

    if ((Randomizer_GetSettingValue(RSK_STARTING_HEARTS) + 1) != 3) {
        gSaveContext.healthCapacity = (Randomizer_GetSettingValue(RSK_STARTING_HEARTS) + 1) * 16;
        gSaveContext.health = gSaveContext.healthCapacity;
    }

    if (Randomizer_GetSettingValue(RSK_STARTING_OCARINA)) {
        INV_CONTENT(ITEM_OCARINA_FAIRY) = Randomizer_GetSettingValue(RSK_STARTING_OCARINA) == RO_STARTING_OCARINA_FAIRY
                                              ? ITEM_OCARINA_FAIRY
                                              : ITEM_OCARINA_TIME;
    }

    if (Randomizer_GetSettingValue(RSK_STARTING_STICKS) && !Randomizer_GetSettingValue(RSK_SHUFFLE_DEKU_STICK_BAG)) {
        GiveLinkDekuSticks(10);
    }
    if (Randomizer_GetSettingValue(RSK_STARTING_NUTS) && !Randomizer_GetSettingValue(RSK_SHUFFLE_DEKU_NUT_BAG)) {
        GiveLinkDekuNuts(20);
    }
    if (Randomizer_GetSettingValue(RSK_STARTING_MASTER_SWORD)) {
        if (startingAge == RO_AGE_ADULT) {
            Item_Give(NULL, ITEM_SWORD_MASTER);
        } else {
            gSaveContext.inventory.equipment |= 1 << 1;
        }
    }

    // Tiered/progressive starting items. The upgrade items are given cumulatively where Item_Give
    // only sets the base inventory content on the first tier.
    switch (Randomizer_GetSettingValue(RSK_STARTING_HOOKSHOT)) {
        case 2:
            Item_Give(NULL, ITEM_LONGSHOT);
            break;
        case 1:
            Item_Give(NULL, ITEM_HOOKSHOT);
            break;
    }

    uint8_t startBow = Randomizer_GetSettingValue(RSK_STARTING_BOW);
    if (startBow >= 1)
        Item_Give(NULL, ITEM_BOW);
    if (startBow >= 2)
        Item_Give(NULL, ITEM_QUIVER_40);
    if (startBow >= 3)
        Item_Give(NULL, ITEM_QUIVER_50);

    uint8_t startSlingshot = Randomizer_GetSettingValue(RSK_STARTING_SLINGSHOT);
    if (startSlingshot >= 1)
        Item_Give(NULL, ITEM_SLINGSHOT);
    if (startSlingshot >= 2)
        Item_Give(NULL, ITEM_BULLET_BAG_40);
    if (startSlingshot >= 3)
        Item_Give(NULL, ITEM_BULLET_BAG_50);

    uint8_t startBombBag = Randomizer_GetSettingValue(RSK_STARTING_BOMB_BAG);
    if (startBombBag >= 1)
        Item_Give(NULL, ITEM_BOMB_BAG_20);
    if (startBombBag >= 2)
        Item_Give(NULL, ITEM_BOMB_BAG_30);
    if (startBombBag >= 3)
        Item_Give(NULL, ITEM_BOMB_BAG_40);

    switch (Randomizer_GetSettingValue(RSK_STARTING_STRENGTH)) {
        case 3:
            Item_Give(NULL, ITEM_GAUNTLETS_GOLD);
            break;
        case 2:
            Item_Give(NULL, ITEM_GAUNTLETS_SILVER);
            break;
        case 1:
            Item_Give(NULL, ITEM_BRACELET);
            break;
    }

    switch (Randomizer_GetSettingValue(RSK_STARTING_SCALE)) {
        case 2:
            Item_Give(NULL, ITEM_SCALE_GOLDEN);
            break;
        case 1:
            Item_Give(NULL, ITEM_SCALE_SILVER);
            break;
    }

    switch (Randomizer_GetSettingValue(RSK_STARTING_WALLET)) {
        case 2:
            Item_Give(NULL, ITEM_WALLET_GIANT);
            break;
        case 1:
            Item_Give(NULL, ITEM_WALLET_ADULT);
            break;
    }

    uint8_t startMagic = Randomizer_GetSettingValue(RSK_STARTING_MAGIC_METER);
    if (startMagic > 0) {
        gSaveContext.isMagicAcquired = true;
        gSaveContext.isDoubleMagicAcquired = startMagic >= 2;
        gSaveContext.magicLevel = startMagic;
        gSaveContext.magicCapacity = startMagic * MAGIC_NORMAL_METER;
        gSaveContext.magic = static_cast<s8>(gSaveContext.magicCapacity);
    }

    uint8_t startBombchu = Randomizer_GetSettingValue(RSK_STARTING_BOMBCHU_BAG);
    if (startBombchu > 0) {
        uint8_t bombchuMode = Randomizer_GetSettingValue(RSK_BOMBCHU_BAG);
        if (bombchuMode == RO_BOMBCHU_BAG_SINGLE) {
            INV_CONTENT(ITEM_BOMBCHU) = ITEM_BOMBCHU;
            AMMO(ITEM_BOMBCHU) = 20;
        } else if (bombchuMode == RO_BOMBCHU_BAG_PROGRESSIVE) {
            static const uint8_t bombchuCapacities[] = { 0, 20, 30, 50 };
            gSaveContext.ship.quest.data.randomizer.bombchuUpgradeLevel = startBombchu;
            INV_CONTENT(ITEM_BOMBCHU) = ITEM_BOMBCHU;
            AMMO(ITEM_BOMBCHU) = bombchuCapacities[startBombchu];
        }
    }

    // Big poe bottles first: Item_Give for a bottled content fills the first empty-bottle
    // slot, so each poe is paired with the bottle given right before it. Ruto's Letter fills
    // an empty inventory slot on its own. The remaining plain empty bottles follow.
    uint8_t emptyBottles = 0;
    for (RandomizerSettingKey bottleKey :
         { RSK_STARTING_BOTTLE_1, RSK_STARTING_BOTTLE_2, RSK_STARTING_BOTTLE_3, RSK_STARTING_BOTTLE_4 }) {
        uint8_t bottle = Randomizer_GetSettingValue(bottleKey);
        switch (bottle) {
            case RO_STARTING_BOTTLE_OFF:
                break;
            case RO_STARTING_BOTTLE_EMPTY:
                emptyBottles++;
                break;
            case RO_STARTING_BOTTLE_BIG_POE:
                Item_Give(NULL, ITEM_BOTTLE);
                Item_Give(NULL, ITEM_BIG_POE);
                break;
            case RO_STARTING_BOTTLE_RUTOS_LETTER:
                Item_Give(NULL, ITEM_LETTER_RUTO);
                break;
            default:
                SPDLOG_ERROR("[SetStartingItems] Unhandled value for bottleKey {}: {}", (int)bottleKey, bottle);
                assert(false);
                break;
        }
    }
    for (uint8_t i = 0; i < emptyBottles; i++) {
        Item_Give(NULL, ITEM_BOTTLE);
    }

    if (Randomizer_GetSettingValue(RSK_STARTING_WEIRD_EGG) &&
        Randomizer_GetSettingValue(RSK_SHUFFLE_WEIRD_EGG) == RO_WEIRD_EGG_SHUFFLED) {
        Item_Give(NULL, ITEM_WEIRD_EGG);
    }
    if (Randomizer_GetSettingValue(RSK_STARTING_ZELDAS_LETTER) &&
        Randomizer_GetSettingValue(RSK_SHUFFLE_ZELDAS_LETTER)) {
        Item_Give(NULL, ITEM_LETTER_ZELDA);
    }
    if (Randomizer_GetSettingValue(RSK_STARTING_CLAIM_CHECK)) {
        Item_Give(NULL, ITEM_CLAIM_CHECK);
    }
    if (Randomizer_GetSettingValue(RSK_STARTING_GERUDO_CARD)) {
        Item_Give(NULL, ITEM_GERUDO_CARD);
    }

    if (Randomizer_GetSettingValue(RSK_STARTING_BUNNY_HOOD)) {
        Flags_SetRandomizerInf(RAND_INF_CHILD_TRADES_HAS_MASK_BUNNY);
        if (INV_CONTENT(ITEM_TRADE_CHILD) == ITEM_NONE) {
            INV_CONTENT(ITEM_TRADE_CHILD) = ITEM_MASK_BUNNY;
        }
    }

    // Giant's Knife and Biggoron's Sword share an item slot, bgsFlag marks unbreakable
    switch (Randomizer_GetSettingValue(RSK_STARTING_BIGGORON_SWORD)) {
        case RO_STARTING_BGS_BIGGORON_SWORD:
            gSaveContext.bgsFlag = true;
            [[fallthrough]];
        case RO_STARTING_BGS_GIANTS_KNIFE:
            Item_Give(NULL, ITEM_SWORD_BGS);
            break;
    }

    if (Randomizer_GetSettingValue(RSK_SHUFFLE_MAPANDCOMPASS) == RO_DUNGEON_ITEM_LOC_STARTWITH) {
        uint32_t mapBitMask = 1 << 1;
        uint32_t compassBitMask = 1 << 2;
        uint32_t startingDungeonItemsBitMask = mapBitMask | compassBitMask;
        for (int scene = SCENE_DEKU_TREE; scene <= SCENE_ICE_CAVERN; scene++) {
            gSaveContext.inventory.dungeonItems[scene] |= startingDungeonItemsBitMask;
        }
    }

    if (Randomizer_GetSettingValue(RSK_KEYSANITY) == RO_DUNGEON_ITEM_LOC_STARTWITH) {
        gSaveContext.inventory.dungeonKeys[SCENE_FOREST_TEMPLE] = FOREST_TEMPLE_SMALL_KEY_MAX;            // Forest
        gSaveContext.ship.stats.dungeonKeys[SCENE_FOREST_TEMPLE] = FOREST_TEMPLE_SMALL_KEY_MAX;           // Forest
        gSaveContext.inventory.dungeonKeys[SCENE_FIRE_TEMPLE] = FIRE_TEMPLE_SMALL_KEY_MAX;                // Fire
        gSaveContext.ship.stats.dungeonKeys[SCENE_FIRE_TEMPLE] = FIRE_TEMPLE_SMALL_KEY_MAX;               // Fire
        gSaveContext.inventory.dungeonKeys[SCENE_WATER_TEMPLE] = WATER_TEMPLE_SMALL_KEY_MAX;              // Water
        gSaveContext.ship.stats.dungeonKeys[SCENE_WATER_TEMPLE] = WATER_TEMPLE_SMALL_KEY_MAX;             // Water
        gSaveContext.inventory.dungeonKeys[SCENE_SPIRIT_TEMPLE] = SPIRIT_TEMPLE_SMALL_KEY_MAX;            // Spirit
        gSaveContext.ship.stats.dungeonKeys[SCENE_SPIRIT_TEMPLE] = SPIRIT_TEMPLE_SMALL_KEY_MAX;           // Spirit
        gSaveContext.inventory.dungeonKeys[SCENE_SHADOW_TEMPLE] = SHADOW_TEMPLE_SMALL_KEY_MAX;            // Shadow
        gSaveContext.ship.stats.dungeonKeys[SCENE_SHADOW_TEMPLE] = SHADOW_TEMPLE_SMALL_KEY_MAX;           // Shadow
        gSaveContext.inventory.dungeonKeys[SCENE_BOTTOM_OF_THE_WELL] = BOTTOM_OF_THE_WELL_SMALL_KEY_MAX;  // BotW
        gSaveContext.ship.stats.dungeonKeys[SCENE_BOTTOM_OF_THE_WELL] = BOTTOM_OF_THE_WELL_SMALL_KEY_MAX; // BotW
        gSaveContext.inventory.dungeonKeys[SCENE_GERUDO_TRAINING_GROUND] = GERUDO_TRAINING_GROUND_SMALL_KEY_MAX;  // GTG
        gSaveContext.ship.stats.dungeonKeys[SCENE_GERUDO_TRAINING_GROUND] = GERUDO_TRAINING_GROUND_SMALL_KEY_MAX; // GTG
        gSaveContext.inventory.dungeonKeys[SCENE_INSIDE_GANONS_CASTLE] = GANONS_CASTLE_SMALL_KEY_MAX;  // Ganon
        gSaveContext.ship.stats.dungeonKeys[SCENE_INSIDE_GANONS_CASTLE] = GANONS_CASTLE_SMALL_KEY_MAX; // Ganon
    } else if (Randomizer_GetSettingValue(RSK_KEYSANITY) == RO_DUNGEON_ITEM_LOC_VANILLA) {
        // Logic cannot handle vanilla key layout in some dungeons
        // this is because vanilla expects the dungeon major item to be
        // locked behind the keys, which is not always true in rando.
        // We can resolve this by starting with some extra keys.
        if (ResourceMgr_IsSceneMasterQuest(SCENE_SPIRIT_TEMPLE)) {
            // MQ Spirit needs 3 keys.
            if (gSaveContext.inventory.dungeonKeys[SCENE_SPIRIT_TEMPLE] < 3) {
                gSaveContext.inventory.dungeonKeys[SCENE_SPIRIT_TEMPLE] = 3;
                gSaveContext.ship.stats.dungeonKeys[SCENE_SPIRIT_TEMPLE] = 3;
            }
        }
    }

    if (Randomizer_GetSettingValue(RSK_BOSS_KEYSANITY) == RO_DUNGEON_ITEM_LOC_STARTWITH) {
        gSaveContext.inventory.dungeonItems[SCENE_FOREST_TEMPLE] |= 1; // Forest
        gSaveContext.inventory.dungeonItems[SCENE_FIRE_TEMPLE] |= 1;   // Fire
        gSaveContext.inventory.dungeonItems[SCENE_WATER_TEMPLE] |= 1;  // Water
        gSaveContext.inventory.dungeonItems[SCENE_SPIRIT_TEMPLE] |= 1; // Spirit
        gSaveContext.inventory.dungeonItems[SCENE_SHADOW_TEMPLE] |= 1; // Shadow
    }

    if (Randomizer_GetSettingValue(RSK_GANONS_BOSS_KEY) == RO_GANON_BOSS_KEY_STARTWITH) {
        gSaveContext.inventory.dungeonItems[SCENE_GANONS_TOWER] |= 1;
    }
}

// Seven Sages: single source of truth for every sage's fixed starting state.
//
// The kit is expressed as real SoH RSK_STARTING_* option values rather than raw Item_Give calls,
// and that distinction is the entire point. SoH already threads those options through three
// separate places that all have to agree:
//   - starting_inventory.cpp  -> so the logic solver knows what the run actually begins with
//   - item_pool.cpp           -> so a kit item is removed from the shuffled pool instead of also
//                                being placed in the world as a redundant duplicate
//   - SetStartingItems()      -> the real runtime grant at file creation
// The original implementation granted kits with bare Item_Give calls at file creation, which
// bypassed all three: the solver reasoned as if the sage started empty-handed, every kit item was
// also placed somewhere in the world (wasting that location), and the spoiler log disagreed with
// what the player was actually holding.
//
// Read by generation-time code (Randomizer_ApplySageGenerationSettings, called from
// Context::FinalizeSettings) and by runtime file creation (z_sram.c's Sram_InitSave, for
// age/entrance). Keeping all of it in one table is deliberate - age in particular used to be
// duplicated between this file and z_sram.c, and drift between those two copies is exactly what
// left the generator computing reachability from the wrong starting age.
//
// Starting state only. Everything about how a sage *looks and sounds* - tunic colors included,
// which this table used to carry - lives in SevenSagesSageCosmetics.cpp instead. The split is
// deliberate: this struct is what the generator and file creation read, and the presentation set
// has since grown to HUD layout, proportions, voice mappings and an instrument, none of which the
// generator has any business knowing about.
//
// Known follow-up, still unimplemented: Saria's "random mask" has no RSK_STARTING_* equivalent
// (see characters.md).
#define SAGE_MAX_KIT_OPTIONS 8
#define SAGE_MAX_WORLD_OPTIONS 4
#define SAGE_MAX_SAVE_FLAGS 4

struct SageStartingOption {
    RandomizerSettingKey key;
    uint8_t value;
};

struct SageDefinition {
    uint8_t sage;
    uint8_t age;          // RO_AGE_CHILD / RO_AGE_ADULT
    int32_t homeEntrance; // ENTR_* - the runtime spawn point
    // ENTR_* used in place of homeEntrance when the file is RELOADED while the sage is in the
    // other age, or -1 to keep using homeEntrance in both ages. Only reachable after an age
    // switch, so it never affects generation or the seed's starting state - see the note above
    // sSageDefinitions.
    int32_t offAgeEntrance;
    uint16_t homeRegion; // RandomizerRegion - the solver's starting position
    SageStartingOption kit[SAGE_MAX_KIT_OPTIONS];
    uint8_t kitCount;
    // World-state options, kept separate from `kit` on purpose even though both are applied by the
    // same generation-time loop. A kit entry changes what the sage is *holding*; a world entry
    // changes what the *world* looks like for the whole seed (e.g. King Zora already stepped
    // aside). The solver has to reason about them differently, and mixing them into one list made
    // it easy to misread a world change as a free item. Set at generation time so the logic solver,
    // the item pool and the spoiler log all agree, exactly like the kit.
    SageStartingOption world[SAGE_MAX_WORLD_OPTIONS];
    uint8_t worldCount;
    // EventChkInf flags forced on at file creation, for world state SoH exposes no setting for.
    // These are invisible to the generator, so they may only ever make MORE of the world reachable
    // than the solver assumed - never less. That direction is safe (a seed stays beatable, it just
    // has a shortcut the spoiler log didn't predict); the reverse would produce unbeatable seeds.
    uint16_t saveFlags[SAGE_MAX_SAVE_FLAGS];
    uint8_t saveFlagCount;
};

// Seven Sages: every sage spawns INSIDE their home region, at the landmark they are actually
// tied to, rather than at the doormat of the region they own (changed 2026-07-31). A sage arriving
// at their own front gate like a tourist was backwards.
//
// Two hard constraints govern every entrance below, and both have already cost a retest:
//
//  1. NEVER use an EntranceType::Dungeon entrance. Our preset sets ShuffleDungeonsEntrances, so
//     those indices are reassigned per-seed and a fixed spawn on one lands somewhere arbitrary.
//     Check entrance.cpp's registration, not the name - several innocuous-sounding overworld-ish
//     entrances are in the dungeon pool. This rules out the three most obvious picks here:
//     SACRED_FOREST_MEADOW_OUTSIDE_TEMPLE (Saria), KAKARIKO_VILLAGE_OUTSIDE_BOTTOM_OF_THE_WELL
//     (Impa) and GERUDOS_FORTRESS_OUTSIDE_GERUDO_TRAINING_GROUND (Nabooru - the original offender,
//     which dumped a retest into the ending credits). Overworld/Interior/WarpSong are all safe
//     under this preset because none of their pools are shuffled.
//
//  2. Moving off an entrance LOSES its vanilla entrance cutscene. sEntranceCutsceneTable
//     (z_demo.c:61) is keyed on the exact entranceIndex, so e.g. Rauru at the ranch tower no
//     longer triggers gLonLonRanchIntroCs the way LON_LON_RANCH_ENTRANCE did. That is deliberate
//     and handled elsewhere: SevenSagesOpenings.cpp re-fires the same vanilla cutscene from the
//     new spawn, which works because each new spawn is in the SAME SCENE as the old one (camera
//     shots are absolute world coordinates, so a shot reused in its own scene is pixel-identical).
//     Ruto and Impa are the exception - each spawn has an entrance cutscene of its own, so for a
//     while vanilla handled them and Openings deliberately had no entry. That ended 2026-08-09,
//     when the preset took up SkipCutscene.Entrances: vanilla now SPENDS the EVENTCHKINF and then
//     suppresses the shot, so Openings restores those two through its own earlier-armed path
//     (sVanillaSpawnOpenings) rather than the ordinary re-fire table.
//
// ── offAgeEntrance: the home base is also the RELOAD point, in both ages ────────────────────────
//
// homeEntrance is not just where a file starts. Sram_OpenSave and Entrance_SetSavewarpEntrance
// both re-apply it every time the file is loaded from the file-select screen, and neither looked
// at linkAge - so a sage who switched age at the pedestal and then quit came back to a spawn
// picked for the age they are no longer in. (Death is NOT affected: that path is
// Play_TriggerRespawn -> last entrance, and never reaches either function.)
//
// The age-swapped spawn is usually harmless, because most home bases are the same place in both
// ages. Two were not, so they get an explicit off-age entrance and the other five stay -1:
//
//   Saria  - the meadow swaps to its Moblin layer for adult (OBJECT_MB appears only in spot05
//            room 0's alternate headers), and her home is the warp pad at the TOP, on the far
//            side of the maze from the exit. Reloading there as adult with no weapon means
//            fighting back down through it, and dying just returns to the same pad.
//   Zelda  - her home is a child-only scene reached in vanilla only via the guard sneak, and
//            SCENE_CASTLE_COURTYARD_ZELDA has no meaningful adult state to arrive in.
//
// An off-age entrance is under the SAME two constraints above, plus a third: because it is picked
// per age, prefer an index whose entrance-table group already splits by age. Both picks below do,
// which is what makes them correct without any extra code - ENTR_CASTLE_GROUNDS_SOUTH_EXIT's
// child rows are SCENE_HYRULE_CASTLE and its adult rows are SCENE_OUTSIDE_GANONS_CASTLE, so
// naming the one index lands each age in its own version of that place.
static const SageDefinition sSageDefinitions[] = {
    // Light Arrows are an arrow *type* - useless without a Bow to fire them, and firing one costs
    // magic. All three have to be present together or none of them function.
    // Spawns at the ranch tower (the tall round silo at the back), not the front gate. Interior
    // pool, unshuffled here. Same scene as before, so gLonLonRanchIntroCs still frames correctly.
    { RO_SAGE_RAURU,
      RO_AGE_ADULT,
      ENTR_LON_LON_RANCH_OUTSIDE_TOWER,
      -1, // ranch tower interior is the same room in both ages
      RR_LON_LON_RANCH,
      { { RSK_STARTING_MEGATON_HAMMER, 1 },
        { RSK_STARTING_BOW, 1 },
        { RSK_STARTING_LIGHT_ARROWS, 1 },
        { RSK_STARTING_MAGIC_METER, 1 },
        // The Stone of Agony, added 2026-08-06. Rauru is the sage who built the Temple of Time and
        // hid the Sacred Realm behind it, so the item that senses what is hidden belongs to him.
        // Note this is a real RSK_STARTING_* setting like the rest of the kit, not a direct item
        // grant - so the generator sees it, excludes it from the pool, and the spoiler log agrees.
        { RSK_STARTING_STONE_OF_AGONY, 1 } },
      5,
      {},
      0,
      {},
      0 },
    // Spawns on the Minuet warp pad at the top of the meadow, at the Forest Temple steps - i.e.
    // where the Sage of Forest belongs, rather than at the maze entrance. WarpSong pool, and warp
    // songs aren't shuffled under this preset. The meadow itself needs no unlocking: ClosedForest
    // is already RO_CLOSED_FOREST_OFF in the seed preset, so Mido never blocks the way in.
    // Fairy Ocarina, not Ocarina of Time (RO_STARTING_OCARINA_FAIRY vs _TIME - no mechanical
    // difference, either lets you play any known song, see savefile.cpp's INV_CONTENT(ITEM_OCARINA_FAIRY)
    // assignment above) - vanilla has Saria giving Link the Fairy Ocarina specifically, and Zelda's
    // kit already uses Ocarina of Time (her family's), so this keeps the two distinct rather than
    // duplicating Zelda's choice. She now also starts knowing her own song, matching Zelda's kit
    // pattern of "signature-song sage carries the ocarina and the song together" - added 2026-08-01
    // once Saria's Song got a real gameplay effect worth having from the start. Magic meter 1
    // added alongside it - without one, casting the song (24 magic under the Phase 6 system-wide
    // song cost) would be dead in the water from minute one, same class of gap as Rauru's Light
    // Arrows and Impa's Lens of Truth before their kits got the same fix (see "Magic is part of a
    // kit, not an extra" in characters.md).
    // Off-age (adult): the meadow's south entryway, in from the Lost Woods - the one part of the
    // scene that is on the safe side of the Moblin maze, with the exit to the woods at her back
    // rather than the whole maze between her and it. Deliberately still INSIDE the meadow rather
    // than out in the woods (ENTR_LOST_WOODS_NORTH_EXIT, the other half of this connector): the
    // Sage of Forest reloading in her own region is the point, she just no longer does it stranded
    // at the top of it. EntranceType::Overworld both directions (entrance.cpp:496), unshuffled
    // here, and absent from sEntranceCutsceneTable so nothing fires on arrival.
    { RO_SAGE_SARIA,
      RO_AGE_CHILD,
      ENTR_SACRED_FOREST_MEADOW_WARP_PAD,
      ENTR_SACRED_FOREST_MEADOW_SOUTH_EXIT,
      RR_SACRED_FOREST_MEADOW,
      { { RSK_STARTING_STICKS, 1 },
        { RSK_STARTING_NUTS, 1 },
        { RSK_STARTING_OCARINA, RO_STARTING_OCARINA_FAIRY },
        { RSK_STARTING_SARIAS_SONG, 1 },
        { RSK_STARTING_MAGIC_METER, 1 } },
      5,
      {},
      0,
      {},
      0 },
    // Spawns inside Darunia's own chamber, at the door to the crater. Deliberately the Goron City
    // side of that door, NOT DEATH_MOUNTAIN_CRATER_GC_EXIT: Darunia is a child, RG_GORON_TUNIC is
    // adult-only equipment (logic.cpp's ItemUseAllowed), so the crater side is uninterruptible heat
    // damage on three hearts - and dying there respawns him right back into it. This is also a real
    // benefit rather than just flavour: child access to this chamber otherwise needs Zelda's
    // Lullaby, which is not in his kit, so he would never have gotten in on his own.
    { RO_SAGE_DARUNIA,
      RO_AGE_CHILD,
      ENTR_GORON_CITY_DARUNIA_ROOM_EXIT,
      -1, // his chamber is open and exitable in both ages
      RR_GC_DARUNIAS_CHAMBER,
      { { RSK_STARTING_BOMB_BAG, 1 },
        { RSK_STARTING_BOMBCHU_BAG, 1 },
        { RSK_STARTING_STRENGTH, 1 }, // Goron's Bracelet
        { RSK_STARTING_GORON_TUNIC, 1 } },
      4,
      {},
      0,
      {},
      0 },
    // Spawns in Zora's Fountain at the mouth of the King Zora tunnel, facing Jabu-Jabu. Overworld
    // pool, unshuffled. RSK_ZORAS_FOUNTAIN must be RO_ZF_OPEN for this to be playable at all: as a
    // child she is on the far side of King Zora, and VB_KING_ZORA_BE_MOVED (hook_handlers.cpp) only
    // returns true unconditionally on Open - the preset's global RO_ZF_CLOSED_CHILD would leave her
    // walled into the fountain. Setting it per-sage at generation time rather than globally keeps
    // the other six seeds unaffected.
    // Note her spawn is itself in sEntranceCutsceneTable (gZorasFountainIntroCs, age-2 so child
    // qualifies). That used to mean vanilla played her opening unaided; since the preset started
    // skipping entrance cutscenes (2026-08-09) she is in SevenSagesOpenings.cpp's
    // sVanillaSpawnOpenings instead - same shot, same flag, triggered from there.
    { RO_SAGE_RUTO,
      RO_AGE_CHILD,
      ENTR_ZORAS_FOUNTAIN_TUNNEL_EXIT,
      -1, // adult's fountain is frozen over, but the tunnel back to the domain is still open
      RR_ZORAS_FOUNTAIN,
      { { RSK_STARTING_SCALE, 2 }, // golden scale - the max tier, i.e. "all diving scales"
        { RSK_STARTING_IRON_BOOTS, 1 },
        { RSK_STARTING_ZORA_TUNIC, 1 } },
      3,
      { { RSK_ZORAS_FOUNTAIN, RO_ZF_OPEN } },
      1,
      {},
      0 },
    // The Lens of Truth drains magic continuously while active, so it's as inert without a meter as
    // Light Arrows are without a Bow - basic magic (level 1), not Zelda's double.
    // Spawns in the Graveyard proper (2026-07-31, moved from mid-Kakariko) - Impa's actual home
    // turf, behind Kakariko and right by Shadow Temple's door. `ENTR_GRAVEYARD_ENTRANCE` is
    // EntranceType::Overworld, unshuffled, and carries its own vanilla entrance cutscene
    // (gGraveyardIntroCs, EVENTCHKINF_ENTERED_GRAVEYARD) - a real establishing shot, not a re-fire
    // of Kakariko's. NOT `ENTR_GRAVEYARD_OUTSIDE_TEMPLE`, which reads like the obvious "right at
    // Shadow Temple's door" pick but is an EntranceType::Dungeon connector and would be reassigned
    // under ShuffleDungeonsEntrances - the exact trap that already caught the Well and the Training
    // Ground. The Graveyard<->Kakariko link itself is an unconditional Overworld connector (no
    // logic gate in entrance.cpp), so she can walk back into the village freely either direction.
    // NOT the Bottom of the Well exit either, for the same EntranceType::Dungeon reason. The well is
    // instead handed to her via EVENTCHKINF_DRAINED_WELL_IN_KAKARIKO below: her kit has no ocarina,
    // so she can never play Song of Storms to drain it herself, and the well would otherwise be
    // permanently shut to the one sage it belongs to. Draining it only widens what she can reach,
    // which is the safe direction for a flag the generator can't see.
    { RO_SAGE_IMPA,
      RO_AGE_ADULT,
      ENTR_GRAVEYARD_ENTRANCE,
      -1, // the graveyard is the same walk-in, walk-out yard for both ages
      RR_THE_GRAVEYARD,
      { { RSK_STARTING_BUNNY_HOOD, 1 },
        { RSK_STARTING_HOOKSHOT, 1 },
        { RSK_STARTING_LENS_OF_TRUTH, 1 },
        { RSK_STARTING_MAGIC_METER, 1 } },
      4,
      {},
      0,
      { EVENTCHKINF_DRAINED_WELL_IN_KAKARIKO },
      1 },
    // Nabooru's entrance is NOT ENTR_GERUDO_TRAINING_GROUND_ENTRANCE (the dungeon interior -
    // confirmed via live testing to cause an infinite void-out softlock when cold-spawned into) and
    // NOT ENTR_GERUDOS_FORTRESS_OUTSIDE_GERUDO_TRAINING_GROUND either (looks like a safe overworld
    // spot by name, but entrance.cpp registers it as a SHUFFLED DUNGEON connector - same pool as the
    // interior door - so under our preset's ShuffleDungeonsEntrances its real destination gets
    // reassigned per-seed; that's what dumped a retest into the ending credits). General rule
    // learned here: always check entrance.cpp's EntranceType registration before using any entrance
    // near a dungeon door as a fixed spawn.
    //
    // She used to spawn at EAST_EXIT, which is safe but is the ordinary arrival from Gerudo Valley -
    // the tourist doormat this whole pass is moving away from. GATE_EXIT is the west gate, arriving
    // out of the Haunted Wasteland: also EntranceType::Overworld and unshuffled, and it reads as the
    // Gerudo leader coming home across her own desert. It does NOT strand her outside the fortress -
    // RR_GF_OUTSIDE_GATE -> RR_GF_OUTSKIRTS needs LOGIC_GF_GATE_OPEN (adult + Gerudo Card + Gerudo
    // speech, gerudo_fortress.cpp:257) and she satisfies all three, the Card from her kit and the
    // speech because RSK_SHUFFLE_SPEAK is off in our preset.
    // The Gerudo Training Ground needs no unlocking for her: its door only asks for the Gerudo Card,
    // which she starts with.
    { RO_SAGE_NABOORU,
      RO_AGE_ADULT,
      ENTR_GERUDOS_FORTRESS_GATE_EXIT,
      -1, // child at the fortress is safe on her kit's Gerudo Card, and capture only ejects anyway
      RR_GF_OUTSIDE_GATE,
      { { RSK_STARTING_HOVER_BOOTS, 1 }, { RSK_STARTING_GERUDO_CARD, 1 }, { RSK_STARTING_MIRROR_SHIELD, 1 } },
      3,
      {},
      0,
      {},
      0 },
    // Magic meter 2 is double magic (SetStartingItems derives isDoubleMagicAcquired from >= 2),
    // which her three-spell kit needs to be usable at all.
    //
    // WRONG SCENE, CORRECTED 2026-07-31: this used to point at
    // ENTR_CASTLE_COURTYARD_GUARDS_DAY_0/_1 in hairal_niwa (SCENE_CASTLE_COURTYARD_GUARDS_DAY) -
    // the guard-patrolled CRAWLSPACE, not her own courtyard. Neither of that scene's two
    // entrances actually lands in open space; both are in the corridor, confirmed by live
    // testing across two attempts. The real garden - RR_HC_GARDEN, her homeRegion, and correct
    // all along - lives in a DIFFERENT scene: SCENE_CASTLE_COURTYARD_ZELDA (nakaniwa), per
    // castle_grounds.cpp's own region graph (RR_HC_DRAIN_LEDGE -crawl-> RR_HC_GARDEN, and
    // RR_HC_GARDEN's areaTable entry is explicitly SCENE_CASTLE_COURTYARD_ZELDA, not
    // SCENE_CASTLE_COURTYARD_GUARDS_DAY). ENTR_CASTLE_COURTYARD_ZELDA_0 is a normal registered
    // entrance for that scene - no re-fire, no reposition hack, just spawns there directly like
    // every other sage.
    //
    // She has no opening cutscene right now. The old ZELDA_CASTLE_COURTYARD_OPENING
    // (data/cutscenes.json) was built against hairal_niwa's coordinates and is stale in the new
    // scene - dropped from SevenSagesOpenings.cpp rather than left wired to the wrong place.
    // nakaniwa has four of its own embedded cutscenes (gZeldasCourtyardGanonCs/WindowCs/MeetCs/
    // LullabyCs, see nakaniwa_scene.h) worth investigating for a replacement, not yet done.
    //
    // TimeSavers.SkipChildStealth is already 0 in the enhancements preset, so the crawlspace
    // guards on the way here are live - she genuinely has to sneak out of her own castle.
    //
    // NOT addressed here: opening the Great Fairy fountains. SoH has no setting for it - the
    // boulders are ordinary actors gated on BlastOrSmash() (explosives or Megaton Hammer), neither
    // of which is in her kit - so it would take real engine work. It is also worth less than it
    // looks: five of the six fountains are adult-only, leaving just the Hyrule Castle one in reach
    // of a child sage.
    // Off-age (adult): the castle grounds' south entrance, in from the market. One index covers
    // both halves of the age split on its own - ENTR_CASTLE_GROUNDS_SOUTH_EXIT's child rows are
    // SCENE_HYRULE_CASTLE and its adult rows are SCENE_OUTSIDE_GANONS_CASTLE (entrance_table.h:392,
    // 0x138-0x13B), so an adult reload arrives at the foot of Ganon's castle, on her own castle's
    // grounds, with the ruined market open behind her. EntranceType::Overworld (entrance.cpp:522).
    //
    // It also has a matching entry in sEntranceCutsceneTable that the child spawn cannot reach:
    // { ENTR_CASTLE_GROUNDS_SOUTH_EXIT, 0, 0xBA, gGanonsCastleIntroCs }, ageRestriction 0 = adult
    // only. Its respawnFlag <= 0 guard is satisfied on a file-select load (z_file_choose.c:2675
    // zeroes it), so adult Zelda used to get vanilla's own establishing shot of the castle free,
    // once per file. SkipCutscene.Entrances (preset, 2026-08-09) suppresses it and it is NOT
    // restored: SevenSagesOpenings.cpp is keyed on Randomizer_GetSageHomeEntrance() by design -
    // "an opening belongs to the sage's real home and must not fire at the off-age substitute"
    // (see Randomizer_GetSageSpawnEntrance below) - and this is an off-age reload spawn. Known
    // and accepted; reversing it means deciding that rule was wrong, not adding a table row.
    { RO_SAGE_ZELDA,
      RO_AGE_CHILD,
      ENTR_CASTLE_COURTYARD_ZELDA_0,
      ENTR_CASTLE_GROUNDS_SOUTH_EXIT,
      RR_HC_GARDEN,
      { { RSK_STARTING_FARORES_WIND, 1 },
        { RSK_STARTING_NAYRUS_LOVE, 1 },
        { RSK_STARTING_DINS_FIRE, 1 },
        { RSK_STARTING_OCARINA, RO_STARTING_OCARINA_TIME },
        { RSK_STARTING_ZELDAS_LULLABY, 1 },
        { RSK_STARTING_MAGIC_METER, 2 } },
      6,
      {},
      0,
      {},
      0 },
};

static const SageDefinition* FindSageDefinition(uint8_t sage) {
    for (const SageDefinition& def : sSageDefinitions) {
        if (def.sage == sage) {
            return &def;
        }
    }
    return nullptr;
}

// The selected sage, or nullptr when this isn't a Seven Sages save.
//
// IS_SEVENSAGES, not IS_RANDO. RSK_SELECTED_SAGE defaults to RO_SAGE_RAURU (0) and every value in
// that enum maps to a real sage, so FindSageDefinition never returns nullptr on its own - the quest
// check is the ONLY thing standing between a plain Randomizer file and Rauru's kit, starting age
// and Lon Lon Ranch respawn being forced onto it. It was IS_RANDO until 2026-08-09, and that is
// exactly what happened. See the IS_RANDO/IS_SEVENSAGES note in z64save.h.
static const SageDefinition* GetSelectedSageDefinition() {
    if (!IS_SEVENSAGES) {
        return nullptr;
    }
    return FindSageDefinition(Randomizer_GetSettingValue(RSK_SELECTED_SAGE));
}

// Seven Sages: the generation-time counterpart of IS_SEVENSAGES, and it has to be a CVar rather
// than a quest check because at generation time there is no save file yet - the seed is built from
// the file-select screen, and gSaveContext.ship.quest.id still describes whatever was last loaded
// (or nothing at all on a fresh launch). Asking IS_SEVENSAGES here would answer for the previous
// session.
//
// Written by FileChoose_UpdateQuestMenu the moment a quest is chosen, in both directions, so
// picking Randomizer after Seven Sages clears it. Deliberately under CVAR_GENERAL rather than
// CVAR_RANDOMIZER_SETTING: applyPreset does a whole-block SetBlock on `gRandoSettings`, so a flag
// living there would be wiped the moment either Seven Sages preset was applied - which is the one
// thing guaranteed to happen right next to where this gets set.
extern "C" bool Randomizer_IsSevenSagesGeneration() {
    return CVarGetInteger(CVAR_GENERAL("SevenSages.QuestSelected"), 0) != 0;
}

// Generation-time hook: fold the selected sage's fixed starting state into the real settings the
// generator reads, before Fill() runs. This is the single step that keeps the logic solver, the
// item pool, and the spoiler log all in agreement with what the player will actually start with.
//
// Writes land in the Context's own option array (OptionValue::Set is a plain in-memory assignment,
// no CVar write), so this is scoped to the seed being generated and never leaks back into the
// user's saved settings UI.
// Fold one sage's kit and world overrides into the Context's option array.
//
// World-state overrides go through the same Set() as the kit, and for the same reason: applied
// before Fill() they are simply what this seed's world IS, so the solver, the item pool and the
// spoiler log all reason about the real world rather than the preset's. Ruto's RO_ZF_OPEN is
// the current example - it is what removes King Zora from her path home, and doing it here
// rather than in the shared preset keeps the other six sages on the preset's own value.
static void ApplySageOptionsToContext(const SageDefinition* def) {
    auto ctx = Rando::Context::GetInstance();

    for (uint8_t i = 0; i < def->kitCount; i++) {
        ctx->GetOption(def->kit[i].key).Set(def->kit[i].value);
    }
    for (uint8_t i = 0; i < def->worldCount; i++) {
        ctx->GetOption(def->world[i].key).Set(def->world[i].value);
    }
}

extern "C" void Randomizer_ApplySageGenerationSettings() {
    // No-op for a plain Randomizer seed. Called unconditionally from Context::FinalizeSettings,
    // which runs for every generation regardless of which quest asked for it.
    if (!Randomizer_IsSevenSagesGeneration()) {
        return;
    }

    auto ctx = Rando::Context::GetInstance();
    const SageDefinition* def = FindSageDefinition(ctx->GetOption(RSK_SELECTED_SAGE).Get());
    if (def == nullptr) {
        return;
    }

    // The sage's age IS the run's starting age. RSK_SELECTED_STARTING_AGE drives the solver's
    // starting position, whether the Temple of Time pedestal check auto-resolves, and the
    // "can you still reach ToT as the other age" seed validation. Leaving it derived from the
    // static preset value meant every child-starting sage was generated as if it started adult.
    //
    // Stays the LOCAL sage's age even for a co-op seed, and deliberately: one seed has exactly one
    // starting age, and the host's is as good as any. A teammate who starts at the other age simply
    // has access the solver did not assume, which only ever makes more of the world reachable, not
    // less - the safe direction. See docs/multiplayer-anchor.md, "Mixed starting ages".
    ctx->GetOption(RSK_SELECTED_STARTING_AGE).Set(def->age);

    ApplySageOptionsToContext(def);

    // ── Co-op: exclude every claimed sage's kit, not just the host's ────────────────────────
    //
    // A kit item set as RSK_STARTING_* is removed from the shuffled pool (item_pool.cpp) because
    // the player is deemed to already hold it. In a co-op run the *team* holds the union of every
    // claimed kit, so the union is what has to leave the pool - otherwise a teammate's kit item is
    // also placed in the world, wasting that location on something its owner already has.
    //
    // This makes the solver reason about the team as a single agent holding the union. That is
    // sound while the team plays together, because world state is shared: Darunia smashing a
    // hammer wall clears it for everyone, so a requirement satisfied by ANY player is satisfied
    // for all. It is not sound for a player exploring alone, who holds only their own kit - which
    // is the deliberate "sometimes you need to ask your friend to come with you" property.
    //
    // Only the roster's kits are excluded, never all seven. Excluding a sage nobody is playing
    // would delete their kit items from the world without giving them to anyone, which can strand
    // progression and make a seed unbeatable.
    if (SevenSagesCoop_ShouldUseRosterForGeneration()) {
        const uint8_t roster = SevenSagesCoop_GetRoster();
        for (const SageDefinition& other : sSageDefinitions) {
            if (other.sage == def->sage || !(roster & (1 << other.sage))) {
                continue;
            }
            ApplySageOptionsToContext(&other);
        }
    }

    // Seven Sages: an adult-starting sage gets the Temple of Time pedestal check's contents for
    // free at file creation - SetStartingItems() does that for any adult start with Master Sword
    // shuffled, on the reasoning that an adult start has already pulled the sword. That grant can't
    // simply be removed: an adult sage has no Master Sword, so it can never turn child, so it can
    // never reach that pedestal, and the item would be stranded (potentially unbeatable seed).
    // Instead exclude the location so the freebie is junk rather than a real item - live testing
    // found Nabooru being handed a Lens of Truth this way, which is exactly the kind of
    // outside-the-kit bonus we disabled Link's Pocket to avoid. The Master Sword itself is
    // unaffected and still placed somewhere in the world.
    if (def->age == RO_AGE_ADULT) {
        ctx->GetItemLocation(RC_TOT_MASTER_SWORD)->SetExcludedOption(RO_LOCATION_EXCLUDE);
    }
}

// Seven Sages: re-assert the LOCAL player's sage into the Context immediately before file
// creation reads it. See savefile.h.
extern "C" void Randomizer_ApplySageRuntimeKit() {
    if (!IS_SEVENSAGES) {
        return;
    }

    auto ctx = Rando::Context::GetInstance();

    // The CVar, not the Context, is authoritative for "which sage is this player". The sage ring
    // writes the CVar as the cursor moves (SevenSagesSelectMenu.cpp), whereas the Context's copy is
    // whatever generation or a spoiler load last put there - and for a co-op joiner that is the
    // HOST's sage, because Settings::ParseJson (settings.cpp:3113) writes every setting the spoiler
    // carries straight into the Context, and WriteSettings (spoiler_log.cpp:151) wrote them out
    // post-ApplySageGenerationSettings. Without this line, loading the host's spoiler silently
    // turns every joiner into the host, kit and starting age included.
    const uint8_t localSage = (uint8_t)CVarGetInteger(CVAR_RANDOMIZER_SETTING("SelectedSage"), RO_SAGE_RAURU);
    const SageDefinition* def = FindSageDefinition(localSage);
    if (def == nullptr) {
        return;
    }
    ctx->GetOption(RSK_SELECTED_SAGE).Set(localSage);
    ctx->GetOption(RSK_SELECTED_STARTING_AGE).Set(def->age);

    // Now narrow the kit. A co-op seed was generated with the UNION of every claimed sage's kit set
    // as RSK_STARTING_*, so that the whole union left the item pool - and SetStartingItems() grants
    // whatever it finds set. Left alone, every player would be handed all seven kits at file
    // creation, which is precisely the opposite of the mod.
    //
    // Clear every option that appears in ANY sage's kit, then re-apply only this sage's. Scoped to
    // kit options rather than all RSK_STARTING_* so a starting item the preset legitimately grants
    // to everyone survives untouched.
    //
    // Standing constraint this creates, checked 2026-08-10 and currently satisfied: the Seven Sages
    // seed preset must not set a NON-ZERO value for any option that also appears in a sage kit,
    // because the clear below would strip it from every sage who doesn't carry it themselves. Two
    // options currently overlap - RSK_STARTING_OCARINA and RSK_STARTING_ZELDAS_LULLABY, both in
    // Zelda's kit - and the preset sets both to 0, so clearing them is a no-op. StartingZeldasLetter
    // and StartingMapsCompasses are non-zero in the preset but are in no kit, so they are untouched.
    // If a future preset change breaks that, the symptom is a silently missing starting item.
    //
    // World overrides are deliberately NOT narrowed: they describe the world this seed was built
    // with, which is the same world for everyone in the room, and undoing one here would put this
    // player's world out of step with the placement they just loaded.
    for (const SageDefinition& other : sSageDefinitions) {
        for (uint8_t i = 0; i < other.kitCount; i++) {
            ctx->GetOption(other.kit[i].key).Set(0);
        }
    }
    for (uint8_t i = 0; i < def->kitCount; i++) {
        ctx->GetOption(def->kit[i].key).Set(def->kit[i].value);
    }

    // One line per file creation. Keeps "did I spawn with it or find it?" answerable from the log
    // rather than from memory - the question raised by the open 2026-08-11 report of co-op players
    // holding items belonging to neither kit. Cheap enough to leave in permanently.
    SPDLOG_INFO("[SevenSages] runtime kit applied: sage {}, age {}, {} kit options", localSage, def->age,
                def->kitCount);
}

extern "C" uint8_t Randomizer_GetSageStartingAge() {
    const SageDefinition* def = GetSelectedSageDefinition();
    return def == nullptr ? RO_AGE_CHILD : def->age;
}

// RR_NONE-equivalent isn't meaningful here; 0 means "no sage", which only happens off a Seven
// Sages save. Currently unused - the solver's starting region is still the vanilla Child/Adult
// Spawn, see the note in location_access/root.cpp.
extern "C" uint16_t Randomizer_GetSageHomeRegion() {
    const SageDefinition* def = GetSelectedSageDefinition();
    return def == nullptr ? 0 : def->homeRegion;
}

// Seven Sages: see savefile.h - the single source of truth for each sage's fixed home-base
// entrance, shared by every piece of code that independently recomputes the fallback "where does
// this file spawn" entrance. -1 means no override (randomizer not active).
int32_t Randomizer_GetSageHomeEntrance() {
    const SageDefinition* def = GetSelectedSageDefinition();
    return def == nullptr ? -1 : def->homeEntrance;
}

// Seven Sages: see savefile.h. Same answer as Randomizer_GetSageHomeEntrance() while the sage is
// in their own age, which is every case at file creation and most cases after - it only diverges
// once the player has switched age at the pedestal AND that sage defines an offAgeEntrance.
//
// Callers are only the two "where does this file spawn" recomputes. Deliberately NOT used by
// SevenSagesOpenings.cpp or SevenSagesSageCosmetics.cpp: an opening belongs to the sage's real
// home and must not fire at the off-age substitute, and cosmetics only use the home accessor as
// an "is a sage selected" test.
int32_t Randomizer_GetSageSpawnEntrance() {
    const SageDefinition* def = GetSelectedSageDefinition();
    if (def == nullptr) {
        return -1;
    }

    const uint8_t ownAge = def->age == RO_AGE_ADULT ? LINK_AGE_ADULT : LINK_AGE_CHILD;
    if (gSaveContext.linkAge != ownAge && def->offAgeEntrance != -1) {
        return def->offAgeEntrance;
    }

    return def->homeEntrance;
}

extern "C" void Randomizer_InitSaveFile() {
    auto ctx = Rando::Context::GetInstance();
    ctx->GetLogic()->SetSaveContext(&gSaveContext);

    // Starts pending ice traps out at 0 before potentially incrementing them down the line.
    gSaveContext.ship.pendingIceTrapCount = 0;

    // Reset triforce pieces collected.
    gSaveContext.ship.quest.data.randomizer.triforcePiecesCollected = 0;

    // Reset Bombchu Bag Upgrade
    gSaveContext.ship.quest.data.randomizer.bombchuUpgradeLevel = 0;

    // Seven Sages: point the Context at THIS player's sage before anything reads it. Required
    // whenever the options in the Context were not written by this player's own generation - a
    // co-op joiner who loaded the host's spoiler, or a host whose co-op generation set the union of
    // every claimed kit. Harmless and idempotent on a solo run, where it re-asserts what generation
    // already put there. Must run before SetStartingItems().
    Randomizer_ApplySageRuntimeKit();

    // Seven Sages: the selected sage's kit is granted right here by SetStartingItems(), from the
    // real RSK_STARTING_* options that Randomizer_ApplySageGenerationSettings() set at generation
    // time. There is deliberately no separate sage-kit grant step anymore - the old one used bare
    // Item_Give calls that the generator knew nothing about, which is what caused kit items to also
    // be placed in the world as duplicates. See the sage definition table above.
    SetStartingItems();

    // Seven Sages: per-sage world flags SoH exposes no setting for (see SageDefinition::saveFlags
    // for why this direction is safe). Applied after SetStartingItems() so a sage flag always wins
    // over anything the generic starting-item path set.
    {
        const SageDefinition* sageDef = GetSelectedSageDefinition();
        if (sageDef != nullptr) {
            for (uint8_t i = 0; i < sageDef->saveFlagCount; i++) {
                Flags_SetEventChkInf(sageDef->saveFlags[i]);
            }
        }
    }

    // Set Cutscene flags and texts to skip them.
    Flags_SetEventChkInf(EVENTCHKINF_FIRST_SPOKE_TO_MIDO);
    Flags_SetInfTable(INFTABLE_SPOKE_TO_KAEPORA_IN_LAKE_HYLIA);
    Flags_SetEventChkInf(EVENTCHKINF_SHEIK_SPAWNED_AT_MASTER_SWORD_PEDESTAL);
    Flags_SetEventChkInf(EVENTCHKINF_RENTED_HORSE_FROM_INGO);
    Flags_SetInfTable(INFTABLE_SPOKE_TO_POE_COLLECTOR_IN_RUINED_MARKET);
    Flags_SetEventChkInf(EVENTCHKINF_WATCHED_GANONS_CASTLE_COLLAPSE_CAUGHT_BY_GERUDO);

    if (Randomizer_GetSettingValue(RSK_FOREST) == RO_CLOSED_FOREST_OFF) {
        Flags_SetEventChkInf(EVENTCHKINF_SHOWED_MIDO_SWORD_SHIELD);
        Flags_SetEventChkInf(EVENTCHKINF_SPOKE_TO_MIDO_AFTER_DEKU_TREES_DEATH);
    }

    // Go away Ruto (Water Temple first cutscene).
    gSaveContext.sceneFlags[SCENE_WATER_TEMPLE].swch |= (1 << 0x10);

    if (Randomizer_GetSettingValue(RSK_STARTING_BEANS)) {
        INV_CONTENT(ITEM_BEAN) = ITEM_BEAN;
        if (Randomizer_GetSettingValue(RSK_SHUFFLE_MERCHANTS) != RO_SHUFFLE_MERCHANTS_BEANS_ONLY &&
            Randomizer_GetSettingValue(RSK_SHUFFLE_MERCHANTS) != RO_SHUFFLE_MERCHANTS_ALL) {
            BEANS_BOUGHT = 10;
        }
        if (Randomizer_GetSettingValue(RSK_SKIP_PLANTING_BEANS)) {
            AMMO(ITEM_BEAN) = 0;
            gSaveContext.sceneFlags[SCENE_DEATH_MOUNTAIN_CRATER].swch |= (1 << 3);
            gSaveContext.sceneFlags[SCENE_DEATH_MOUNTAIN_TRAIL].swch |= (1 << 6);
            gSaveContext.sceneFlags[SCENE_DESERT_COLOSSUS].swch |= (1 << 24);
            gSaveContext.sceneFlags[SCENE_GERUDO_VALLEY].swch |= (1 << 3);
            gSaveContext.sceneFlags[SCENE_GRAVEYARD].swch |= (1 << 3);
            gSaveContext.sceneFlags[SCENE_KOKIRI_FOREST].swch |= (1 << 9);
            gSaveContext.sceneFlags[SCENE_LAKE_HYLIA].swch |= (1 << 1);
            gSaveContext.sceneFlags[SCENE_LOST_WOODS].swch |= (1 << 4) | (1 << 18);
            gSaveContext.sceneFlags[SCENE_ZORAS_RIVER].swch |= (1 << 3);
        } else {
            AMMO(ITEM_BEAN) = 10;
        }
    }

    if (Randomizer_GetSettingValue(RSK_SHUFFLE_BEAN_SOULS) == RO_GENERIC_OFF) {
        Flags_SetRandomizerInf(RAND_INF_DEATH_MOUNTAIN_CRATER_BEAN_SOUL);
        Flags_SetRandomizerInf(RAND_INF_DEATH_MOUNTAIN_TRAIL_BEAN_SOUL);
        Flags_SetRandomizerInf(RAND_INF_DESERT_COLOSSUS_BEAN_SOUL);
        Flags_SetRandomizerInf(RAND_INF_GERUDO_VALLEY_BEAN_SOUL);
        Flags_SetRandomizerInf(RAND_INF_GRAVEYARD_BEAN_SOUL);
        Flags_SetRandomizerInf(RAND_INF_KOKIRI_FOREST_BEAN_SOUL);
        Flags_SetRandomizerInf(RAND_INF_LAKE_HYLIA_BEAN_SOUL);
        Flags_SetRandomizerInf(RAND_INF_LOST_WOODS_BRIDGE_BEAN_SOUL);
        Flags_SetRandomizerInf(RAND_INF_LOST_WOODS_BEAN_SOUL);
        Flags_SetRandomizerInf(RAND_INF_ZORAS_RIVER_BEAN_SOUL);
    }

    if (Randomizer_GetSettingValue(RSK_SHUFFLE_OCARINA_BUTTONS) == RO_GENERIC_OFF) {
        Flags_SetRandomizerInf(RAND_INF_HAS_OCARINA_A);
        Flags_SetRandomizerInf(RAND_INF_HAS_OCARINA_C_LEFT);
        Flags_SetRandomizerInf(RAND_INF_HAS_OCARINA_C_RIGHT);
        Flags_SetRandomizerInf(RAND_INF_HAS_OCARINA_C_UP);
        Flags_SetRandomizerInf(RAND_INF_HAS_OCARINA_C_DOWN);
    }

    if (Randomizer_GetSettingValue(RSK_SHUFFLE_SWIM) == RO_GENERIC_OFF) {
        Flags_SetRandomizerInf(RAND_INF_CAN_SWIM);
    }

    if (Randomizer_GetSettingValue(RSK_SHUFFLE_GRAB) == RO_GENERIC_OFF) {
        Flags_SetRandomizerInf(RAND_INF_CAN_GRAB);
    }

    if (Randomizer_GetSettingValue(RSK_SHUFFLE_CLIMB) == RO_GENERIC_OFF) {
        Flags_SetRandomizerInf(RAND_INF_CAN_CLIMB);
    }

    if (Randomizer_GetSettingValue(RSK_SHUFFLE_CRAWL) == RO_GENERIC_OFF) {
        Flags_SetRandomizerInf(RAND_INF_CAN_CRAWL);
    }

    if (Randomizer_GetSettingValue(RSK_SHUFFLE_SPEAK) == RO_GENERIC_OFF) {
        Flags_SetEventChkInf(EVENTCHKINF_SPOKE_TO_NABOORU_IN_SPIRIT_TEMPLE);
        Flags_SetRandomizerInf(RAND_INF_CAN_SPEAK_DEKU);
        Flags_SetRandomizerInf(RAND_INF_CAN_SPEAK_GERUDO);
        Flags_SetRandomizerInf(RAND_INF_CAN_SPEAK_GORON);
        Flags_SetRandomizerInf(RAND_INF_CAN_SPEAK_HYLIAN);
        Flags_SetRandomizerInf(RAND_INF_CAN_SPEAK_KOKIRI);
        Flags_SetRandomizerInf(RAND_INF_CAN_SPEAK_ZORA);
    }

    if (Randomizer_GetSettingValue(RSK_SHUFFLE_OPEN_CHEST) == RO_OPEN_CHEST_OFF) {
        Flags_SetRandomizerInf(RAND_INF_CAN_OPEN_CHEST);
        Flags_SetRandomizerInf(RAND_INF_CAN_OPEN_LARGE_CHEST);
    }

    if (Randomizer_GetSettingValue(RSK_SHUFFLE_CHILD_WALLET) == RO_GENERIC_OFF) {
        Flags_SetRandomizerInf(RAND_INF_HAS_WALLET);
    }

    if (Randomizer_GetSettingValue(RSK_SHUFFLE_FISHING_POLE) == RO_GENERIC_OFF) {
        Flags_SetRandomizerInf(RAND_INF_FISHING_POLE_FOUND);
    }

    // Give Link's pocket item
    GiveLinksPocketItem();

    if (Randomizer_GetSettingValue(RSK_FULL_WALLETS)) {
        GiveLinkRupees(9001);
    }

    // Remove One Time Scrubs with Scrubsanity off
    if (Randomizer_GetSettingValue(RSK_SHUFFLE_SCRUBS) == RO_SCRUBS_OFF) {
        Flags_SetItemGetInf(ITEMGETINF_DEKU_SCRUB_HEART_PIECE);
        Flags_SetInfTable(INFTABLE_BOUGHT_STICK_UPGRADE);
        Flags_SetInfTable(INFTABLE_BOUGHT_NUT_UPGRADE);
    }

    int startingAge = OTRGlobals::Instance->gRandoContext->GetOption(RSK_SELECTED_STARTING_AGE).Get();
    gSaveContext.savedSceneNum = -1;
    switch (startingAge) {
        case RO_AGE_ADULT: // Adult
            gSaveContext.linkAge = LINK_AGE_ADULT;
            gSaveContext.entranceIndex = ENTR_TEMPLE_OF_TIME_WARP_PAD;
            gSaveContext.cutsceneIndex = 0;
            break;
        case RO_AGE_CHILD: // Child
            gSaveContext.linkAge = LINK_AGE_CHILD;
            break;
        default:
            break;
    }

    if (Randomizer_GetSettingValue(RSK_SHUFFLE_OVERWORLD_SPAWNS)) {
        // Override the spawn entrance so entrance rando can take control,
        // and to prevent remember save location from breaking initial spawn.
        gSaveContext.entranceIndex = -1;
    }

    for (auto trialFlag : { EVENTCHKINF_COMPLETED_LIGHT_TRIAL, EVENTCHKINF_COMPLETED_FOREST_TRIAL,
                            EVENTCHKINF_COMPLETED_FIRE_TRIAL, EVENTCHKINF_COMPLETED_WATER_TRIAL,
                            EVENTCHKINF_COMPLETED_SPIRIT_TRIAL, EVENTCHKINF_COMPLETED_SHADOW_TRIAL }) {
        if (!OTRGlobals::Instance->gRandomizer->IsTrialRequired(trialFlag)) {
            Flags_SetEventChkInf(trialFlag);
        }
    }

    // Skip Waking Talon: the egg already hatched and woke him, Malon/Talon start back at the ranch.
    if (Randomizer_GetSettingValue(RSK_SHUFFLE_WEIRD_EGG) == RO_WEIRD_EGG_SKIP_TALON) {
        OTRGlobals::Instance->gRandoContext->GetItemLocation(RC_HC_MALON_EGG)->SetCheckStatus(RCSHOW_SAVED);

        Flags_SetEventChkInf(EVENTCHKINF_OBTAINED_POCKET_EGG);
        Flags_SetRandomizerInf(RAND_INF_WEIRD_EGG);
        Flags_SetEventChkInf(EVENTCHKINF_TALON_WOKEN_IN_CASTLE);
        Flags_SetEventChkInf(EVENTCHKINF_TALON_RETURNED_FROM_CASTLE);
    }

    // Starting with an unshuffled letter skips child Zelda.
    if (Randomizer_GetSettingValue(RSK_STARTING_ZELDAS_LETTER) &&
        !Randomizer_GetSettingValue(RSK_SHUFFLE_ZELDAS_LETTER)) {
        GetItemEntry getItemEntry = Randomizer_GetItemFromKnownCheck(RC_SONG_FROM_IMPA, (GetItemID)RG_ZELDAS_LULLABY);
        StartingItemGive(getItemEntry, RC_SONG_FROM_IMPA);
        getItemEntry = Randomizer_GetItemFromKnownCheck(RC_HC_ZELDAS_LETTER, (GetItemID)RG_ZELDAS_LETTER);
        StartingItemGive(getItemEntry, RC_HC_ZELDAS_LETTER);

        // Set "Met Zelda" flag. Also ensures Saria is back at SFM.
        Flags_SetEventChkInf(EVENTCHKINF_OBTAINED_ZELDAS_LETTER);
        Flags_SetRandomizerInf(RAND_INF_ZELDAS_LETTER);

        // Got item from Impa.
        Flags_SetEventChkInf(EVENTCHKINF_LEARNED_ZELDAS_LULLABY);
    }

    // Starting with the letter opens the Kakariko gate, shuffled or not.
    // The letter then has no use, so drop it from the trade cycle.
    if (Randomizer_GetSettingValue(RSK_STARTING_ZELDAS_LETTER)) {
        Flags_SetInfTable(INFTABLE_SHOWED_ZELDAS_LETTER_TO_GATE_GUARD);
        Flags_UnsetRandomizerInf(RAND_INF_CHILD_TRADES_HAS_LETTER_ZELDA);
    }

    if (Randomizer_GetSettingValue(RSK_SHUFFLE_MASTER_SWORD) && startingAge == RO_AGE_ADULT) {
        GetItemEntry getItemEntry = Randomizer_GetItemFromKnownCheck(RC_TOT_MASTER_SWORD, GI_NONE);
        StartingItemGive(getItemEntry, RC_TOT_MASTER_SWORD);
        Flags_SetRandomizerInf(RAND_INF_TOT_MASTER_SWORD);
    }

    HIGH_SCORE(HS_POE_POINTS) = 1000 - (100 * Randomizer_GetSettingValue(RSK_BIG_POE_COUNT));

    // Open lowest Vanilla Fire Temple locked door (to prevent key logic lockouts).
    // Not done on Keysanity since this lockout is a non-issue when Fire Keys can be found outside the temple.
    u8 keysanity = Randomizer_GetSettingValue(RSK_KEYSANITY) == RO_DUNGEON_ITEM_LOC_ANYWHERE ||
                   Randomizer_GetSettingValue(RSK_KEYSANITY) == RO_DUNGEON_ITEM_LOC_OVERWORLD ||
                   Randomizer_GetSettingValue(RSK_KEYSANITY) == RO_DUNGEON_ITEM_LOC_ANY_DUNGEON;
    if (!ResourceMgr_IsSceneMasterQuest(SCENE_FIRE_TEMPLE) && !keysanity) {
        gSaveContext.sceneFlags[SCENE_FIRE_TEMPLE].swch |= (1 << 0x17);
    }

    // Opens locked Water Temple door in vanilla to prevent softlocks.
    // West door on the middle level that leads to the water raising thing.
    // Happens in 3DS rando and N64 rando as well.
    if (!ResourceMgr_IsSceneMasterQuest(SCENE_WATER_TEMPLE)) {
        gSaveContext.sceneFlags[SCENE_WATER_TEMPLE].swch |= (1 << 0x15);
    }

    int doorOfTime = Randomizer_GetSettingValue(RSK_DOOR_OF_TIME);
    switch (doorOfTime) {
        case RO_DOOROFTIME_OPEN:
            Flags_SetEventChkInf(EVENTCHKINF_OPENED_THE_DOOR_OF_TIME);
            break;
    }

    if (Randomizer_GetSettingValue(RSK_GERUDO_FORTRESS) == RO_GF_CARPENTERS_FAST ||
        Randomizer_GetSettingValue(RSK_GERUDO_FORTRESS) == RO_GF_CARPENTERS_FREE) {
        Flags_SetEventChkInf(EVENTCHKINF_CARPENTERS_FREE(1));
        Flags_SetEventChkInf(EVENTCHKINF_CARPENTERS_FREE(2));
        Flags_SetEventChkInf(EVENTCHKINF_CARPENTERS_FREE(3));
        gSaveContext.sceneFlags[SCENE_THIEVES_HIDEOUT].swch |= (1 << 0x02); // heard yells and unlocked doors
        gSaveContext.sceneFlags[SCENE_THIEVES_HIDEOUT].swch |= (1 << 0x03);
        gSaveContext.sceneFlags[SCENE_THIEVES_HIDEOUT].swch |= (1 << 0x04);
        gSaveContext.sceneFlags[SCENE_THIEVES_HIDEOUT].swch |= (1 << 0x06);
        gSaveContext.sceneFlags[SCENE_THIEVES_HIDEOUT].swch |= (1 << 0x07);
        gSaveContext.sceneFlags[SCENE_THIEVES_HIDEOUT].swch |= (1 << 0x08);
        gSaveContext.sceneFlags[SCENE_THIEVES_HIDEOUT].swch |= (1 << 0x10);
        gSaveContext.sceneFlags[SCENE_THIEVES_HIDEOUT].swch |= (1 << 0x12);
        gSaveContext.sceneFlags[SCENE_THIEVES_HIDEOUT].swch |= (1 << 0x13);
        gSaveContext.sceneFlags[SCENE_THIEVES_HIDEOUT].collect |= (1 << 0x0A); // picked up keys
        gSaveContext.sceneFlags[SCENE_THIEVES_HIDEOUT].collect |= (1 << 0x0E);
        gSaveContext.sceneFlags[SCENE_THIEVES_HIDEOUT].collect |= (1 << 0x0F);
    }

    if (Randomizer_GetSettingValue(RSK_GERUDO_FORTRESS) == RO_GF_CARPENTERS_FREE) {
        Flags_SetEventChkInf(EVENTCHKINF_CARPENTERS_FREE(0));
        gSaveContext.sceneFlags[SCENE_THIEVES_HIDEOUT].swch |= (1 << 0x01); // heard yell and unlocked door
        gSaveContext.sceneFlags[SCENE_THIEVES_HIDEOUT].swch |= (1 << 0x05);
        gSaveContext.sceneFlags[SCENE_THIEVES_HIDEOUT].swch |= (1 << 0x11);
        gSaveContext.sceneFlags[SCENE_THIEVES_HIDEOUT].collect |= (1 << 0x0C); // picked up key

        Flags_SetRandomizerInf(RAND_INF_TH_ITEM_FROM_LEADER_OF_FORTRESS);
        if (!Randomizer_GetSettingValue(RSK_SHUFFLE_GERUDO_MEMBERSHIP_CARD)) {
            Item_Give(NULL, ITEM_GERUDO_CARD);
        }
    }

    // complete mask quest
    if (Randomizer_GetSettingValue(RSK_MASK_QUEST) == RO_MASK_QUEST_COMPLETED) {
        Flags_SetInfTable(INFTABLE_GATE_GUARD_PUT_ON_KEATON_MASK);
        Flags_SetEventChkInf(EVENTCHKINF_PAID_BACK_BUNNY_HOOD_FEE);

        Flags_SetRandomizerInf(RAND_INF_CHILD_TRADES_HAS_MASK_KEATON);
        Flags_SetRandomizerInf(RAND_INF_CHILD_TRADES_HAS_MASK_SKULL);
        Flags_SetRandomizerInf(RAND_INF_CHILD_TRADES_HAS_MASK_SPOOKY);
        Flags_SetRandomizerInf(RAND_INF_CHILD_TRADES_HAS_MASK_BUNNY);
        Flags_SetRandomizerInf(RAND_INF_CHILD_TRADES_HAS_MASK_GORON);
        Flags_SetRandomizerInf(RAND_INF_CHILD_TRADES_HAS_MASK_ZORA);
        Flags_SetRandomizerInf(RAND_INF_CHILD_TRADES_HAS_MASK_GERUDO);
        Flags_SetRandomizerInf(RAND_INF_CHILD_TRADES_HAS_MASK_TRUTH);

        gSaveContext.itemGetInf[3] |= 0x100;  // Sold Keaton Mask
        gSaveContext.itemGetInf[3] |= 0x200;  // Sold Skull Mask
        gSaveContext.itemGetInf[3] |= 0x400;  // Sold Spooky Mask
        gSaveContext.itemGetInf[3] |= 0x800;  // Bunny Hood related
        gSaveContext.itemGetInf[3] |= 0x8000; // Obtained Mask of Truth
    }
}
