#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/ShipInit.hpp"

extern "C" {
#include "src/overlays/actors/ovl_En_Bom_Chu/z_en_bom_chu.h"

void EnBomChu_WaitForRelease(EnBomChu*, PlayState*);
void EnBomChu_Move(EnBomChu*, PlayState*);
void EnBomChu_Explode(EnBomChu*, PlayState*);
}

#define CVAR_BOMBCHU_DETONATE_NAME CVAR_ENHANCEMENT("BombchuRemoteDetonate")
#define CVAR_BOMBCHU_DETONATE_DEFAULT 0
#define CVAR_BOMBCHU_DETONATE_VALUE CVarGetInteger(CVAR_BOMBCHU_DETONATE_NAME, CVAR_BOMBCHU_DETONATE_DEFAULT)

// Pulling out a Bombchu detonates any Bombchu already running, so the item button doubles as a
// detonator. The trigger is the new chu sitting in EnBomChu_WaitForRelease (Link is holding it),
// not its spawn: the OnActorInit hook fires from inside Actor_Spawn, and EnBomChu_Explode spawns an
// EN_BOM of its own. Doing that re-entrantly would mutate the explosive list mid-spawn, so we wait
// for the first update instead. The pass is idempotent — an already-detonated chu has moved on to
// EnBomChu_WaitForKill, so re-running it every held frame is a no-op.
static void OnBombchuUpdate(void* refActor) {
    EnBomChu* heldChu = (EnBomChu*)refActor;

    if (heldChu->actionFunc != EnBomChu_WaitForRelease) {
        return;
    }

    Actor* actor = gPlayState->actorCtx.actorLists[ACTORCAT_EXPLOSIVE].head;
    while (actor != NULL) {
        // EnBomChu_Explode spawns an EN_BOM, which links in at the list head, so stash the next
        // pointer before it can run.
        Actor* next = actor->next;

        if ((actor != &heldChu->actor) && (actor->id == ACTOR_EN_BOM_CHU)) {
            EnBomChu* chu = (EnBomChu*)actor;
            if (chu->actionFunc == EnBomChu_Move) {
                EnBomChu_Explode(chu, gPlayState);
            }
        }

        actor = next;
    }
}

void RegisterBombchuRemoteDetonate() {
    COND_ID_HOOK(OnActorUpdate, ACTOR_EN_BOM_CHU, CVAR_BOMBCHU_DETONATE_VALUE, OnBombchuUpdate);
}

static RegisterShipInitFunc initFunc(RegisterBombchuRemoteDetonate, { CVAR_BOMBCHU_DETONATE_NAME });
