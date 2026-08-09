/**
 * Seven Sages - Jabu-Jabu's Belly works without Ruto.
 *
 * Ruto is the one sage who is genuinely load-bearing: in vanilla she is not a check-holder in
 * Jabu, she is the traversal mechanic, and the randomizer logic models that as
 * LOGIC_JABU_RUTO_IN_1F. This project removes sages from the world (the player *is* one), so her
 * two jobs in the dungeon have to be given to something else. See seven-sages/docs/lore.md.
 *
 * ── Job 1: the fork-corridor weight switches (Bg_Bdan_Switch) ───────────────────────────────
 * Vanilla wants the player standing on the switch AND holding Ruto specifically, for 6 frames
 * (z_bg_bdan_switch.c's func_8086DA1C). Only the held-actor identity half is behind
 * VB_JABU_SWITCH_BE_WEIGHED_DOWN; standing on it and the 6-frame settle are untouched.
 *
 * What counts as heavy here is deliberately generous - any carryable at all (Jabu's own pots,
 * a crate, a bomb, and Ruto herself if she is ever still around), plus Iron Boots and the
 * Megaton Hammer. The point is that the *puzzle* survives - you still have to bring weight onto
 * the switch - without the answer being a specific person. Both underwater and dry Iron Boots
 * count, since this room floods.
 *
 * ── Job 2: the Big Octo platform (Bg_Bdan_Objects) ──────────────────────────────────────────
 * This one has no weight mechanic at all, which is why "just put a crate where Ruto stands"
 * doesn't work. Ruto and the platform talk over a private channel: the platform's `cameraSetting`
 * field is reused as a message register (BgBdanObjects_Get/SetContactRu1, z_bg_bdan_objects.c:78)
 * and the sequence is scripted -
 *
 *   1. platform waits for Ruto to signal "aboard"           -> raises, one-point cutscene
 *   2. at the top it signals back                            -> waits again
 *   3. Ruto signals a second time                            -> THIS is what spawns Big Octo
 *
 * so without her there is no raise and, more importantly, no Big Octo at all. Both waits are now
 * behind VBs and answered here instead:
 *
 *   - Boarding becomes DynaPolyActor_IsPlayerOnTop, the engine-standard test already used by a
 *     sibling platform in the same actor (BgBdanObjects_WaitForPlayerOnTop). Vanilla's own
 *     `xzDistToPlayer < 250.0f` guard is untouched and still applies.
 *   - The Big Octo spawn waits on the timer that function is already running for its earthquake
 *     effect, rather than on Ruto's second message. It counts 30 down to 0 and stays there, so
 *     the transition happens a frame after the quake, in the same beat vanilla used.
 *
 * Everything downstream is unchanged - SetContactRu1(this, 4) still fires, so INFTABLE_146 is
 * still set and re-entering the room still finds the fight already in progress.
 *
 * ── Why no solver work is owed ──────────────────────────────────────────────────────────────
 * This only widens access: the same entrances that require LOGIC_JABU_RUTO_IN_1F also require
 * CanKillEnemy(RE_BIG_OCTO) (jabujabus_belly.cpp:208-214), and that half is untouched, so Big
 * Octo is still the real gate and it is still modelled. Player capability becomes a strict
 * superset of what the generator assumed, which is the additive-only case the project's
 * 2026-08-03 decision covers (docs/item-ability-overhaul.md).
 */
#include "soh/OTRGlobals.h"
#include "soh/ShipInit.hpp"
#include "soh/Enhancements/game-interactor/GameInteractor.h"

extern "C" {
#include "z64.h"
#include "variables.h"
#include "functions.h"
#include "macros.h"
#include "src/overlays/actors/ovl_Bg_Bdan_Objects/z_bg_bdan_objects.h"
#include "src/overlays/actors/ovl_Bg_Bdan_Switch/z_bg_bdan_switch.h"
extern PlayState* gPlayState;
}

namespace {

// Anything that plausibly reads as "there is weight on this switch." Kept deliberately broad -
// see the header comment. heldActor covers every liftable actor including pots, crates, bombs
// and Ruto herself, so this stays a superset of vanilla's condition rather than a replacement.
bool PlayerIsHeavy() {
    Player* player = GET_PLAYER(gPlayState);
    if (player->heldActor != NULL) {
        return true;
    }
    // Both iron-boot states: this room floods, and the underwater variant is a separate value.
    if (player->currentBoots == PLAYER_BOOTS_IRON || player->currentBoots == PLAYER_BOOTS_IRON_UNDERWATER) {
        return true;
    }
    return player->heldItemAction == PLAYER_IA_HAMMER;
}

void SevenSagesJabuOnVanillaBehavior(GIVanillaBehavior id, bool* should, va_list originalArgs) {
    va_list args;
    va_copy(args, originalArgs);

    switch (id) {
        case VB_JABU_SWITCH_BE_WEIGHED_DOWN: {
            *should = *should || PlayerIsHeavy();
            break;
        }
        case VB_JABU_OCTO_PLATFORM_BE_BOARDED: {
            BgBdanObjects* platform = va_arg(args, BgBdanObjects*);
            *should = *should || DynaPolyActor_IsPlayerOnTop(&platform->dyna);
            break;
        }
        case VB_JABU_OCTO_PLATFORM_SPAWN_BIG_OCTO: {
            BgBdanObjects* platform = va_arg(args, BgBdanObjects*);
            *should = *should || platform->timer == 0;
            break;
        }
        default:
            break;
    }

    va_end(args);
}

void RegisterSevenSagesJabuWithoutRuto() {
    COND_HOOK(OnVanillaBehavior, IS_SEVENSAGES, SevenSagesJabuOnVanillaBehavior);
}

} // namespace

static RegisterShipInitFunc sevenSagesJabuWithoutRutoInitFunc(RegisterSevenSagesJabuWithoutRuto, { "IS_RANDO" });
