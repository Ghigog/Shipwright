/**
 * Seven Sages - Phase 6: blue fire freezes what stands near it.
 *
 * Spec (docs/item-ability-overhaul.md, "Blue Fire"): a dropped blue flame leaves an area that
 * applies an ice trap to anything nearby, and the patch persists and can be re-bottled.
 *
 * The freeze is a SevenSagesAoeField using the deku nut's damage flag with **0 damage** - vanilla's
 * own way of saying "the effect is the stun, not the hit" - so enemies caught near the flame lock
 * up rather than take chip damage. Same mechanism as the ice arrow, different source.
 *
 * **Re-bottling is not ours.** SoH already ships it as `RebottleBlueFire`
 * (`Enhancements/QoL/RebottleBlueFire.cpp`), which offers the flame as a bottle catch whenever the
 * player is in range. It has been switched on in the Seven Sages enhancements preset rather than
 * reimplemented. Note the preset lives inside `soh.o2r`, so enabling it needs `GenerateSohOtr`, not
 * just a build.
 *
 * **Every blue flame gets the aura, not only dropped ones.** The naturally placed flames in the Ice
 * Cavern and the Shadow Temple are the same actor, so they freeze things too. That is deliberate:
 * a blue flame behaving differently depending on where it came from would be the surprising
 * outcome, and the player can already bottle those flames and re-drop them anyway.
 *
 * The field is refreshed on a shared frame boundary rather than per flame. A field is a pooled
 * collider allocation, so spawning one every frame would churn the play arena for no benefit; a
 * 25-frame field renewed every 20 frames overlaps enough to read as continuous, and stops within
 * about a second of the flame being bottled or destroyed.
 */
#include "soh/Enhancements/SevenSages/SevenSagesAoeField.h"

#include "soh/ShipInit.hpp"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
}

extern "C" PlayState* gPlayState;

namespace {

constexpr float BLUE_FIRE_RADIUS = 150.0f;
constexpr float BLUE_FIRE_HEIGHT = 150.0f; // total, centred

// Renew every REFRESH frames with a field that lives slightly longer, so there is no gap between
// one field expiring and the next appearing.
constexpr uint32_t BLUE_FIRE_REFRESH_FRAMES = 20;
constexpr int32_t BLUE_FIRE_FIELD_FRAMES = 25;

constexpr uint8_t BLUE_FIRE_DAMAGE = 0; // the effect is the freeze, not a hit

void SevenSagesBlueFireUpdate(void* actorPtr) {
    if (!GameInteractor::IsSaveLoaded(true) || gPlayState == nullptr || actorPtr == nullptr) {
        return;
    }
    if ((gPlayState->gameplayFrames % BLUE_FIRE_REFRESH_FRAMES) != 0) {
        return;
    }

    Actor* flame = static_cast<Actor*>(actorPtr);
    const Vec3f& pos = flame->world.pos;
    SevenSagesSpawnAoeField(gPlayState, pos.x, pos.y, pos.z, BLUE_FIRE_RADIUS, BLUE_FIRE_HEIGHT,
                            BLUE_FIRE_FIELD_FRAMES, SEVEN_SAGES_AOE_DMG_STUN, BLUE_FIRE_DAMAGE,
                            SEVEN_SAGES_AOE_VISUAL_NONE);
}

} // namespace

static void RegisterSevenSagesBlueFire() {
    COND_ID_HOOK(OnActorUpdate, ACTOR_EN_ICE_HONO, IS_RANDO, SevenSagesBlueFireUpdate);
}

static RegisterShipInitFunc sevenSagesBlueFireInitFunc(RegisterSevenSagesBlueFire, { "IS_RANDO" });
