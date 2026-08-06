/**
 * Seven Sages - Magic items: Din's Fire also functions as a bomb, exploding nearby breakables.
 *
 * Spec (docs/item-ability-overhaul.md, "Magic items"): Din's Fire additionally explodes nearby
 * breakables the same way a bomb does.
 *
 * Din's Fire's own collider (z_magic_fire.c's sCylinderInit) already touches Player-type AC with
 * dmgFlags 0x00020000 - the fire damage flag enemies react to. Breakables (bushes, pots, bombable
 * walls) don't use an actor damage table at all; their AC bumper mask just gates on dmgFlags bit
 * 0x00000008, the same bit En_Bom's explosion collider carries (z_en_bom.c's sJntSphElementsInit).
 * Din's Fire doesn't have that bit set, so it currently passes through breakables untouched.
 *
 * OR-ing 0x00000008 into the existing dmgFlags is safe for enemy damage: CollisionCheck_ApplyDamage
 * (z_collision_check.c) indexes an actor's damage table by the position of the *highest* set bit in
 * the attacker's dmgFlags, and 0x00020000 (bit 17) stays higher than 0x00000008 (bit 3) once OR'd
 * together, so every enemy still resolves through the same fire-damage table entry as before - a
 * fire-immune enemy stays immune. The only new effect is the extra bit letting Din's Fire clear the
 * AC eligibility gate (z_collision_check.c:1451, `toucher.dmgFlags & bumper.dmgFlags`) for actors
 * that gate on the bomb bit specifically, which breakables do and standard enemies don't rely on.
 */
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/ShipInit.hpp"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
#include "overlays/actors/ovl_Magic_Fire/z_magic_fire.h"
}

namespace {

// Same flag En_Bom's explosion collider touches breakables with (z_en_bom.c's sJntSphElementsInit).
constexpr uint32_t DMG_FLAG_EXPLOSIVE = 0x00000008;

// Bombable *boulders* don't check that bit - they gate on `toucher.dmgFlags & 0x40000040`. The first
// attempt at the brown boulder (playtest 2026-08-06) added 0x40 here to match, which worked and also
// broke the tier split: Obj_Hamishi, the bronze hammer-only boulder, tests the *identical* mask
// (z_obj_hamishi.c:175), so Din's Fire started breaking those too.
//
// Nothing on this side can tell the two apart - they are different actors carrying the same flag -
// so the widening lives on the boulder instead: z_obj_bombiwa.c now also accepts DMG_FLAG_EXPLOSIVE,
// and Obj_Hamishi is left exactly as vanilla. Do not re-add a boulder bit here.

void OnMagicFireInit(void* actorPtr) {
    if (!GameInteractor::IsSaveLoaded(true)) {
        return;
    }
    MagicFire* magicFire = static_cast<MagicFire*>(actorPtr);
    magicFire->collider.info.toucher.dmgFlags |= DMG_FLAG_EXPLOSIVE;
}

} // namespace

static void RegisterSevenSagesDinsFireBomb() {
    COND_ID_HOOK(OnActorInit, ACTOR_MAGIC_FIRE, IS_RANDO, OnMagicFireInit);
}

static RegisterShipInitFunc sevenSagesDinsFireBombInitFunc(RegisterSevenSagesDinsFireBomb, { "IS_RANDO" });
