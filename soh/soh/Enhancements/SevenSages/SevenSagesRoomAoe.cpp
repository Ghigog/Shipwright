#include "soh/Enhancements/SevenSages/SevenSagesRoomAoe.h"

extern "C" {
#include "z64.h"
#include "macros.h"
}

void SevenSagesForEachActorInRoom(PlayState* play, int category, const std::function<void(Actor*)>& fn) {
    Actor* actor = play->actorCtx.actorLists[category].head;
    while (actor != NULL) {
        fn(actor);
        actor = actor->next;
    }
}
