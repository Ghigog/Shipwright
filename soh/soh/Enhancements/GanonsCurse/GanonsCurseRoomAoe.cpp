#include "soh/Enhancements/GanonsCurse/GanonsCurseRoomAoe.h"

extern "C" {
#include "z64.h"
#include "macros.h"
}

void GanonsCurseForEachActorInRoom(PlayState* play, int category, const std::function<void(Actor*)>& fn) {
    Actor* actor = play->actorCtx.actorLists[category].head;
    while (actor != NULL) {
        fn(actor);
        actor = actor->next;
    }
}
