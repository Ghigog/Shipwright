#include "soh/Enhancements/GanonsCurse/GanonsCurseRoomAoe.h"

extern "C" {
#include "z64.h"
#include "macros.h"
}

void GanonsCurseForEachEnemyInRoom(PlayState* play, const std::function<void(Actor*)>& fn) {
    Actor* actor = play->actorCtx.actorLists[ACTORCAT_ENEMY].head;
    while (actor != NULL) {
        fn(actor);
        actor = actor->next;
    }
}
