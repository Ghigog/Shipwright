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

// Bombable *boulders* don't check that bit. Obj_Bombiwa (the brown boulder) gates on
// `toucher.dmgFlags & 0x40000040` (z_obj_bombiwa.c:130), so Din's Fire passed straight through one
// even with DMG_FLAG_EXPLOSIVE set - reported by playtest 2026-08-06.
//
// **Only bit 6 is added, deliberately, and 0x40000000 must not be.** The header comment above turns
// on CollisionCheck_ApplyDamage indexing an actor's damage table by the *highest* set bit of the
// attacker's dmgFlags: 0x40 is bit 6, safely below the existing 0x00020000 (bit 17), so every enemy
// still resolves through the same fire-damage entry and a fire-immune enemy stays immune. Bit 30
// would become the new highest bit and silently re-point every enemy's damage-table lookup.
constexpr uint32_t DMG_FLAG_BOMBABLE_BOULDER = 0x00000040;

void OnMagicFireInit(void* actorPtr) {
    if (!GameInteractor::IsSaveLoaded(true)) {
        return;
    }
    MagicFire* magicFire = static_cast<MagicFire*>(actorPtr);
    magicFire->collider.info.toucher.dmgFlags |= DMG_FLAG_EXPLOSIVE | DMG_FLAG_BOMBABLE_BOULDER;
}

} // namespace

static void RegisterSevenSagesDinsFireBomb() {
    COND_ID_HOOK(OnActorInit, ACTOR_MAGIC_FIRE, IS_RANDO, OnMagicFireInit);
}

static RegisterShipInitFunc sevenSagesDinsFireBombInitFunc(RegisterSevenSagesDinsFireBomb, { "IS_RANDO" });
