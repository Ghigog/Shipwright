/**
 * Seven Sages - Magic items: Nayru's Love lets Link walk through Fire Temple's flame walls.
 *
 * Spec (docs/item-ability-overhaul.md, "Magic items"): Nayru's Love additionally lets you pass
 * through fire walls and other environmental hazards while active.
 *
 * Bg_Hidan_Firewall and Bg_Hidan_Fwbig (the moving/rotating flame-wall obstacles in Fire Temple)
 * block Link with a *physical* collision, not a damage one: their collider is `AT_TYPE_ENEMY` (deals
 * fire damage) layered with `OC1_ON | OC1_TYPE_PLAYER` (object collision - shoves Link back, the
 * same mechanism solid geometry uses). Nayru's Love already gives near-total damage immunity in
 * vanilla, so the fire damage is already a non-issue; the missing piece is purely the OC shove-back,
 * which immunity does nothing for since it's a separate collision layer.
 *
 * Clearing OC1_ON on these two colliders while the shield is up (and restoring it the instant it
 * isn't) is the whole fix - Link then walks through the same way he walks through any other actor
 * with OC1 off. No damage-side changes needed.
 */
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/ShipInit.hpp"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
#include "overlays/actors/ovl_Bg_Hidan_Firewall/z_bg_hidan_firewall.h"
#include "overlays/actors/ovl_Bg_Hidan_Fwbig/z_bg_hidan_fwbig.h"
}

namespace {

void SetFireWallPassable(Collider* collider) {
    if (!GameInteractor::IsSaveLoaded(true)) {
        return;
    }
    if (gSaveContext.nayrusLoveTimer != 0) {
        collider->ocFlags1 &= ~OC1_ON;
    } else {
        collider->ocFlags1 |= OC1_ON;
    }
}

void OnFirewallUpdate(void* actorPtr) {
    SetFireWallPassable(&static_cast<BgHidanFirewall*>(actorPtr)->collider.base);
}

void OnFwbigUpdate(void* actorPtr) {
    SetFireWallPassable(&static_cast<BgHidanFwbig*>(actorPtr)->collider.base);
}

} // namespace

static void RegisterSevenSagesNayrusLoveFireWalls() {
    COND_ID_HOOK(OnActorUpdate, ACTOR_BG_HIDAN_FIREWALL, IS_RANDO, OnFirewallUpdate);
    COND_ID_HOOK(OnActorUpdate, ACTOR_BG_HIDAN_FWBIG, IS_RANDO, OnFwbigUpdate);
}

static RegisterShipInitFunc sevenSagesNayrusLoveFireWallsInitFunc(RegisterSevenSagesNayrusLoveFireWalls,
                                                                  { "IS_RANDO" });
