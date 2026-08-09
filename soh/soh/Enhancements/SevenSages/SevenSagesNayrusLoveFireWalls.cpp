/**
 * Seven Sages - Magic items: Nayru's Love lets Link walk through Fire Temple's flame walls.
 *
 * Spec (docs/item-ability-overhaul.md, "Magic items"): Nayru's Love additionally lets you pass
 * through fire walls and other environmental hazards while active.
 *
 * Bg_Hidan_Firewall, Bg_Hidan_Fwbig, and Bg_Hidan_Curtain (the moving/rotating flame-wall obstacles
 * plus the doorway flame curtain in Fire Temple) block Link with a *physical* collision, not a
 * damage one: their collider is `AT_TYPE_ENEMY` (deals fire damage) layered with
 * `OC1_ON | OC1_TYPE_PLAYER` (object collision - shoves Link back, the same mechanism solid geometry
 * uses). Nayru's Love already gives near-total damage immunity in vanilla, so the fire damage is
 * already a non-issue; the missing piece is purely the OC shove-back, which immunity does nothing
 * for since it's a separate collision layer.
 *
 * Clearing OC1_ON on these colliders while the shield is up (and restoring it the instant it isn't)
 * is the whole fix - Link then walks through the same way he walks through any other actor with OC1
 * off. No damage-side changes needed.
 *
 * Bg_Hidan_Sekizou (the stationary flamethrower statue) also carries this same OC1|AT_TYPE_ENEMY
 * combination but is deliberately excluded: there, OC1 is the statue's own solid stone body blocking
 * Link from walking into the prop, and the fire is a separate projectile attack it shoots - not the
 * physical obstacle. Clearing it would let Link clip through solid statue geometry, which isn't what
 * "pass through fire" means.
 */
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/Enhancements/SevenSages/SevenSagesTunics.h"
#include "soh/ShipInit.hpp"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
#include "overlays/actors/ovl_Bg_Hidan_Firewall/z_bg_hidan_firewall.h"
#include "overlays/actors/ovl_Bg_Hidan_Fwbig/z_bg_hidan_fwbig.h"
#include "overlays/actors/ovl_Bg_Hidan_Curtain/z_bg_hidan_curtain.h"
}

namespace {

void SetFireWallPassable(Collider* collider) {
    if (!GameInteractor::IsSaveLoaded(true)) {
        return;
    }
    // Widened 2026-08-05 from "Nayru's Love is up" to the shared fire-protection
    // predicate, so the Red Tunic grants the same passthrough. See
    // SevenSagesTunics.h - the condition lives in one place now rather than
    // being duplicated per hazard.
    if (SevenSagesFireProtectionActive()) {
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

void OnCurtainUpdate(void* actorPtr) {
    SetFireWallPassable(&static_cast<BgHidanCurtain*>(actorPtr)->collider.base);
}

} // namespace

static void RegisterSevenSagesNayrusLoveFireWalls() {
    COND_ID_HOOK(OnActorUpdate, ACTOR_BG_HIDAN_FIREWALL, IS_SEVENSAGES, OnFirewallUpdate);
    COND_ID_HOOK(OnActorUpdate, ACTOR_BG_HIDAN_FWBIG, IS_SEVENSAGES, OnFwbigUpdate);
    COND_ID_HOOK(OnActorUpdate, ACTOR_BG_HIDAN_CURTAIN, IS_SEVENSAGES, OnCurtainUpdate);
}

static RegisterShipInitFunc sevenSagesNayrusLoveFireWallsInitFunc(RegisterSevenSagesNayrusLoveFireWalls,
                                                                  { "IS_RANDO" });
