#pragma once

#ifndef ITEM_CATEGORY_ADJ_H
#define ITEM_CATEGORY_ADJ_H

#include "../item-tables/ItemTableTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

GetItemCategory Randomizer_AdjustItemCategory(GetItemEntry item);

// True for the health items that deserve a big chest (heart container, double defense), false for
// pieces of heart. See the definition for why this isn't folded into the category itself.
bool Randomizer_IsBigChestHealthItem(GetItemEntry item);

#ifdef __cplusplus
}
#endif

#endif
