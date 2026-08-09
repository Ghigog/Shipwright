#include "item_category_adj.h"
#include "z64item.h"
#include "variables.h"
#include "macros.h"
#include "functions.h"

GetItemCategory Randomizer_AdjustItemCategory(GetItemEntry item) {
    GetItemCategory category = item.getItemCategory;

    // Downgrade bombchus to lesser if the player already has bombchus
    if (INV_CONTENT(ITEM_BOMBCHU) == ITEM_BOMBCHU &&
        ((item.modIndex == MOD_RANDOMIZER && item.getItemId == RG_PROGRESSIVE_BOMBCHU_BAG) ||
         (item.modIndex == MOD_NONE &&
          (item.getItemId == GI_BOMBCHUS_5 || item.getItemId == GI_BOMBCHUS_10 || item.getItemId == GI_BOMBCHUS_20)))) {
        category = ITEM_CATEGORY_LESSER;
    }

    // Downgrade bottles to lesser if the player already has a bottle
    if ((item.modIndex == MOD_RANDOMIZER && item.getItemId >= RG_BOTTLE_WITH_RED_POTION &&
         item.getItemId <= RG_BOTTLE_WITH_POE) ||
        (item.modIndex == MOD_NONE && (item.getItemId == GI_BOTTLE || item.getItemId == GI_MILK_BOTTLE))) {
        if (gSaveContext.inventory.items[SLOT_BOTTLE_1] != ITEM_NONE) {
            category = ITEM_CATEGORY_LESSER;
        }
    }

    // Downgrade keys to junk if the player already has skeleton key
    if (category == ITEM_CATEGORY_SMALL_KEY && Flags_GetRandomizerInf(RAND_INF_HAS_SKELETON_KEY)) {
        category = ITEM_CATEGORY_JUNK;
    }

    return category;
}

// Seven Sages: heart containers and double defense are major items - they get a big chest, and they
// keep their full item-get animation under the "skip junk animations" time saver. They stay
// ITEM_CATEGORY_HEALTH - the category picks the chest *skin*, and the heart skin is still the right
// thing to show - so this is a separate predicate rather than a recategorization to MAJOR.
// Pieces of heart are deliberately excluded and stay small/skippable.
// Depends only on the item itself, never on live inventory state, so unlike
// Randomizer_AdjustItemCategory it is safe to call at any time.
bool Randomizer_IsMajorHealthItem(GetItemEntry item) {
    if (item.getItemCategory != ITEM_CATEGORY_HEALTH) {
        return false;
    }

    if (item.modIndex == MOD_RANDOMIZER) {
        return item.getItemId == RG_DOUBLE_DEFENSE || item.getItemId == RG_HEART_CONTAINER;
    }

    return item.getItemId == GI_HEART_CONTAINER || item.getItemId == GI_HEART_CONTAINER_2;
}
