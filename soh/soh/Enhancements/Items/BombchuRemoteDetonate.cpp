#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/ShipInit.hpp"

extern "C" {
#include "src/overlays/actors/ovl_En_Bom_Chu/z_en_bom_chu.h"
#include "macros.h"

void EnBomChu_Move(EnBomChu*, PlayState*);
void EnBomChu_Explode(EnBomChu*, PlayState*);
}

#define CVAR_BOMBCHU_DETONATE_NAME CVAR_ENHANCEMENT("BombchuRemoteDetonate")
#define CVAR_BOMBCHU_DETONATE_DEFAULT 0
#define CVAR_BOMBCHU_DETONATE_VALUE CVarGetInteger(CVAR_BOMBCHU_DETONATE_NAME, CVAR_BOMBCHU_DETONATE_DEFAULT)

// Z-target a running Bombchu and press B to set it off from any range.
//
// This extends vanilla rather than fighting it: a Bombchu already explodes when a sword connects
// (the AC_HIT check in EnBomChu_Move), and already flags itself targetable the moment it starts
// moving. Lock-on is what makes B unambiguous here — plain "B detonates everything" would fire on
// every combat swing, which is exactly when a chu is most likely to be running.
//
// Runs on OnGameStateMainStart, which fires before gameState->main and therefore before
// Actor_UpdateAll. That ordering is load-bearing: ACTORCAT_PLAYER (2) updates ahead of
// ACTORCAT_EXPLOSIVE (3), so a hook on the chu itself would be too late to stop Link from
// jump-slashing at the chu he just detonated and eating the blast. Getting in ahead of the whole
// update pass lets us consume the B press instead.
static void OnGameStateMainStartBombchuDetonate() {
    if (!GameInteractor::IsSaveLoaded() || gPlayState == NULL) {
        return;
    }

    Input* input = &gPlayState->state.input[0];
    if (!CHECK_BTN_ALL(input->press.button, BTN_B)) {
        return;
    }

    Actor* focus = GET_PLAYER(gPlayState)->focusActor;
    if ((focus == NULL) || (focus->id != ACTOR_EN_BOM_CHU)) {
        return;
    }

    // focusActor is cleared once its actor stops updating, but confirm it is still linked before
    // treating the pointer as a live chu.
    bool isLive = false;
    for (Actor* actor = gPlayState->actorCtx.actorLists[ACTORCAT_EXPLOSIVE].head; actor != NULL; actor = actor->next) {
        if (actor == focus) {
            isLive = true;
            break;
        }
    }
    if (!isLive) {
        return;
    }

    EnBomChu* chu = (EnBomChu*)focus;
    if (chu->actionFunc != EnBomChu_Move) {
        return;
    }

    EnBomChu_Explode(chu, gPlayState);

    // Swallow the press so B detonates instead of swinging. cur is left alone so releasing B stays
    // consistent for anything mid-charge.
    input->press.button &= ~BTN_B;
}

void RegisterBombchuRemoteDetonate() {
    COND_HOOK(OnGameStateMainStart, CVAR_BOMBCHU_DETONATE_VALUE, OnGameStateMainStartBombchuDetonate);
}

static RegisterShipInitFunc initFunc(RegisterBombchuRemoteDetonate, { CVAR_BOMBCHU_DETONATE_NAME });
