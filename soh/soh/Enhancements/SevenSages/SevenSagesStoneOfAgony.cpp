/**
 * Seven Sages - Phase 6: the Stone of Agony gets a voice, and a wider sense of what a secret is.
 *
 * Spec (docs/item-ability-overhaul.md, "Utility items"): "Makes a sound whenever near an invisible
 * object, or anything that would normally require the Lens of Truth to notice."
 *
 * Vanilla already has most of the machinery and it is better shaped than it looks:
 *
 *   - Actors that count as secrets call Actor_SetClosestSecretDistance (z_actor.c) every frame,
 *     which keeps the single closest one in `player->closestSecretDistSq`.
 *   - Player_DetectRumbleSecrets (z_player.c) turns that distance into a rate: it accumulates
 *     `200000 - dist^2 * 5` per frame into `player->unk_6A0` and fires when that crosses a
 *     threshold. So the trigger repeats FASTER the closer you are, and stops entirely past ~200
 *     units where the term clamps to zero. That is already a proximity detector; vanilla just
 *     expresses it as rumble alone.
 *   - The firing edge is even already a hook, VB_RUMBLE_FOR_SECRET.
 *
 * So this file is two small things rather than a detector of its own:
 *
 *   1. A sound on the existing firing edge. Because the edge repeats at a rate set by distance,
 *      the beep speeds up as you close in with no rate logic here at all. The rumble is left
 *      running - `should` is read, never written.
 *   2. A wider definition of "secret". Vanilla's set is only hidden grottos (Door_Ana) and chests
 *      (En_Box) - those are the only two actors in the game that call the helper. The spec's set
 *      is anything the Lens of Truth is for, so every actor carrying ACTOR_FLAG_REACT_TO_LENS is
 *      fed into the same `closestSecretDistSq` the vanilla path uses, which means rumble and sound
 *      can never disagree about what they are reacting to.
 *
 * ON THE BREADTH OF (2), deliberately: the flag is taken at face value, with no filter on the
 * room's `lensMode`. That mode decides which DIRECTION the lens works in - `LENS_MODE_SHOW_ACTORS`
 * rooms hide their lens actors until you look, `LENS_MODE_HIDE_ACTORS` rooms show things that the
 * lens then reveals as fake (fake walls). Both are "you need the Lens to know the truth here",
 * which is what the spec asks for, so both ping. A fake wall looks exactly like a wall; pinging on
 * it is the feature, not over-reach.
 *
 * Range needs no gating of its own. The vanilla accumulator's term goes to zero at dist^2 > 40000,
 * i.e. 200 units, so anything further away contributes nothing no matter how many of them there
 * are.
 *
 * NOT DONE HERE: the spec's other half, "reveals all hidden grottos on the map". That half now
 * overlaps with the Mask of Truth (SevenSagesMaskOfTruth.cpp, built 2026-08-06), which already
 * draws world markers on every grotto in the scene, and the two specs were written eleven days
 * apart without reference to each other. Flagged for a decision rather than built twice.
 */
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

// A soft repeating tick rather than a one-shot "found it" chime, chosen because the trigger it
// hangs off repeats - and repeats faster the closer you get. A chime that says "found" would be
// claiming something new every time it fired. This is the one line to change if it grates in play.
constexpr uint16_t SECRET_SFX = NA_SE_SY_METRONOME;

void SevenSagesStoneOfAgonySenseLensActor(void* actorPtr) {
    if (!GameInteractor::IsSaveLoaded(true) || gPlayState == nullptr) {
        return;
    }
    Actor* actor = static_cast<Actor*>(actorPtr);
    if (!(actor->flags & ACTOR_FLAG_REACT_TO_LENS)) {
        return;
    }
    // Feeds the same field the two vanilla secret actors write, from the same point in the frame
    // (actor update time), so it inherits vanilla's ordering against the player's own reset rather
    // than needing to reason about it.
    Actor_SetClosestSecretDistance(actor, gPlayState);
}

} // namespace

static void RegisterSevenSagesStoneOfAgony() {
    // Reads the vanilla condition without changing it: when the rumble fires, so does the sound.
    // The quest-item check lives in Player_DetectRumbleSecrets around this, so there is no need to
    // re-test for the Stone here - this hook is only reached by a player who has it.
    COND_VB_SHOULD(VB_RUMBLE_FOR_SECRET, IS_SEVENSAGES, {
        [[maybe_unused]] Player* player = va_arg(args, Player*);
        if (*should) {
            Audio_PlaySoundGeneral(SECRET_SFX, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                   &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        }
    });

    COND_HOOK(OnActorUpdate, IS_SEVENSAGES, SevenSagesStoneOfAgonySenseLensActor);
}

static RegisterShipInitFunc sevenSagesStoneOfAgonyInitFunc(RegisterSevenSagesStoneOfAgony, { "IS_RANDO" });
