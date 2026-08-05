/**
 * Seven Sages - Mido never blocks the way into the Sacred Forest Meadow.
 *
 * Vanilla stations Mido across the Lost Woods passage and makes you play Saria's Song at him
 * before he'll move (EnMd_Init picks EnMd_BlockPath, z_en_md.c). That gate is written for Link:
 * the errand boy of the Kokiri making the outsider prove Saria vouched for him. In this project
 * the player is a sage - and may literally *be* Saria - so a Kokiri demanding proof of Saria's
 * friendship before letting them into their own meadow reads as nonsense.
 *
 * Overriding VB_MIDO_BLOCK_LOST_WOODS_PATH to false sends him to the end of his path at spawn
 * (EnMd_SetMovedPos, the exact spot vanilla walks him to after he relents) instead of into
 * EnMd_BlockPath, so the passage is open from the first visit. He is still there, still talkable
 * - he just isn't in the way. Deliberately not a despawn: he's a Kokiri who lives there, and the
 * Phase 3 hint system may yet want him as a speaker.
 *
 * Only the Lost Woods Mido is touched. The Kokiri Forest one (the sword-and-shield gate) has its
 * own conditions in the same if-chain and its own existing overrides in the randomizer's handler
 * (VB_MIDO_SPAWN / VB_MOVE_MIDO_IN_KOKIRI_FOREST, both driven by the Closed Forest setting, which
 * the Seven Sages seed preset already sets to Off).
 *
 * No solver work is owed. This only *adds* access - and the logic already assumes child Link gets
 * through regardless (lost_woods.cpp:62, `logic->IsChild || logic->CanUse(RG_SARIAS_SONG) || the
 * backflip trick`), so the only change is that adults reach RR_LW_BEYOND_MIDO without the song.
 * That is a strict superset of what the generator modelled, which is the additive-only case the
 * project's 2026-08-03 decision explicitly covers (docs/item-ability-overhaul.md).
 */
#include "soh/OTRGlobals.h"
#include "soh/ShipInit.hpp"
#include "soh/Enhancements/game-interactor/GameInteractor.h"

extern "C" {
#include "z64.h"       // IS_RANDO
#include "variables.h" // gSaveContext, which IS_RANDO reads
}

namespace {

void SevenSagesMidoOnVanillaBehavior(GIVanillaBehavior id, bool* should, va_list originalArgs) {
    if (id != VB_MIDO_BLOCK_LOST_WOODS_PATH) {
        return;
    }
    *should = false;
}

void RegisterSevenSagesMidoStandsAside() {
    COND_HOOK(OnVanillaBehavior, IS_RANDO, SevenSagesMidoOnVanillaBehavior);
}

} // namespace

static RegisterShipInitFunc sevenSagesMidoStandsAsideInitFunc(RegisterSevenSagesMidoStandsAside, { "IS_RANDO" });
