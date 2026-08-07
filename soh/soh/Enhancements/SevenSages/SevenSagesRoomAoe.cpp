#include "soh/Enhancements/SevenSages/SevenSagesRoomAoe.h"

// Pulled in ahead of the extern "C" block on purpose. z64.h reaches <memory> transitively, and if
// that first happens *inside* extern "C" then libstdc++'s parallel-algorithm templates are declared
// with C linkage, which GCC rejects outright ("template with C linkage"). Including it here means
// the include guard makes the later, nested include a no-op. libc++ has no such header in that
// path, which is why macOS never complained.
#include <memory>

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
