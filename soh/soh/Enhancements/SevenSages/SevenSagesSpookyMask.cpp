/**
 * Seven Sages - the Spooky Mask makes minor mobs flee from Link while it is worn.
 *
 * This is option 3 from docs/item-ability-overhaul.md, resolved 2026-08-05: a generic flee,
 * with no per-actor code. The reason that decision exists is worth restating, because the
 * obvious approach is a trap.
 *
 * **There is no shared "flee" behaviour in OoT to hook.** Flee exists, but hand-authored per
 * actor - the Poe version (z_en_poh.c:313-320) morphs to that actor's own fleeAnim, turns
 * 180 degrees, sets speedXZ, and hands control to a dedicated action func. Reproducing that
 * per enemy type is more than six bespoke retargeting jobs, and there are more than six minor
 * mobs. So instead this steers actors directly - point them away, push them - and never
 * touches their action funcs.
 *
 * The honest cost of that, spelled out in the doc and repeated here so it is not mistaken for
 * a bug: fleeing enemies keep playing their normal walk or idle animation, and actors that
 * drive their own position every frame (fliers, jumpers, anything that re-sets speedXZ) will
 * fight this or ignore it. The plan is to judge it in play and hand-author only the mobs that
 * look wrong.
 *
 * **The allowlist is the part that has to be right, and it is why this is not a sweep over
 * ACTORCAT_ENEMY.** That category is not a list of enemies: all 73 members were enumerated on
 * 2026-08-05 and it contains En_Elf (Navi and every healing fairy), live bombs and bomb
 * flowers, adult Zelda, the Lost Woods minigame scrubs, invisible spawner and timer actors,
 * a boss, Dark Link, Iron Knuckle, and a pile of pure projectiles. A blanket sweep would send
 * Navi fleeing and shove armed bombs around the room.
 *
 * Deliberately excluded even though they are enemies: the mini-boss tier (Moblin, Gerudo
 * fighter, Lizalfos, Wolfos, Stalfos, Big Octo), En_Skj (Skull Kid is a trade interaction),
 * En_Attack_Niw (the cucco swarm is a punishment mechanic and should stay punishing),
 * En_Po_Field (Big Poes are farmed on Epona for RSK_BIG_POE_COUNT) and En_Po_Sisters
 * (required fight). Anchored enemies - Deku Baba, Beamos, Freezard, wall and ceiling
 * Skulltulas, Wallmaster, Peahat, the Jabu tentacles - are simply absent from the list; they
 * would not move anyway, so excluding them explicitly would be noise.
 */
// No extern "C" block around these, deliberately. GameInteractor_Hooks.h and the libultraship
// headers reachable from macros.h are template-using C++ headers; reaching one from inside an
// extern "C" block gives its templates C linkage and the compiler rejects them outright
// ("templates must have C++ linkage"). SevenSagesSkullMask.cpp orders its includes the same
// way for the same reason.
#include "soh/ShipInit.hpp"
#include "SevenSagesRoomAoe.h"

#include <unordered_set>

#include "functions.h"
#include "macros.h"
#include "variables.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"

extern "C" PlayState* gPlayState;

namespace {

// Chosen to read as "they scatter as you approach" rather than "they teleport away when you
// touch them". Roughly two of Link's dash-lengths; the AOE bursts elsewhere in the mod use
// 150-250, but those are instantaneous hits where this is a standing aura.
constexpr float FLEE_RADIUS = 400.0f;

// The speed vanilla's own Poe flee uses (z_en_poh.c:317). Matching it means the one enemy
// with a real authored flee moves at the same rate whether it is fleeing for its own reasons
// or for ours.
constexpr float FLEE_SPEED = 5.0f;

// Free-moving minor mobs only - see the header comment for why this is an explicit list.
const std::unordered_set<int16_t>& FleeingActors() {
    static const std::unordered_set<int16_t> actors = {
        ACTOR_EN_FIREFLY,  // Keese
        ACTOR_EN_CROW,     // Guay
        ACTOR_EN_TITE,     // Tektite
        ACTOR_EN_REEBA,    // Leever
        ACTOR_EN_NY,       // Spike
        ACTOR_EN_EIYER,    // Stinger
        ACTOR_EN_WEIYER,   // Stinger
        ACTOR_EN_BB,       // Bubble
        ACTOR_EN_BILI,     // Biri
        ACTOR_EN_VALI,     // Bari
        ACTOR_EN_TP,       // Tailpasaran
        ACTOR_EN_SB,       // Shellblade
        ACTOR_EN_SKB,      // Stalchild
        ACTOR_EN_DODOJR,   // Baby Dodongo
        ACTOR_EN_DODONGO,  // Dodongo
        ACTOR_EN_BW,       // Torch Slug
        ACTOR_EN_RR,       // Like Like
        ACTOR_EN_OKUTA,    // Octorok
        ACTOR_EN_AM,       // Armos
        ACTOR_EN_RD,       // ReDead / Gibdo
        ACTOR_EN_POH,      // Poe
        ACTOR_EN_FLOORMAS, // Floormaster
        ACTOR_EN_BUBBLE,   // Shabom
        ACTOR_EN_GOMA,     // Gohma larva
        ACTOR_EN_DEKUNUTS, // Mad Scrub
    };
    return actors;
}

bool IsWearingSpookyMask() {
    Player* player = GET_PLAYER(gPlayState);
    return player != nullptr && player->currentMask == PLAYER_MASK_SPOOKY;
}

void SevenSagesSpookyMaskFrameUpdate() {
    if (gPlayState == nullptr || !IsWearingSpookyMask()) {
        return;
    }

    Player* player = GET_PLAYER(gPlayState);
    if (player == nullptr) {
        return;
    }

    SevenSagesForEachActorInRoom(gPlayState, ACTORCAT_ENEMY, [&](Actor* actor) {
        if (actor == nullptr || !FleeingActors().count(actor->id)) {
            return;
        }
        // xzDistToPlayer is maintained by the engine each frame, so this costs nothing extra
        // and stays correct for actors that moved since the room walk began.
        if (actor->xzDistToPlayer > FLEE_RADIUS) {
            return;
        }

        // Yaw from the player to the actor is, by definition, the direction away from the
        // player - so this needs no 180 degree flip of its own.
        const s16 awayYaw = Math_Vec3f_Yaw(&player->actor.world.pos, &actor->world.pos);
        actor->world.rot.y = awayYaw;
        actor->shape.rot.y = awayYaw;
        actor->speedXZ = FLEE_SPEED;
    });
}

} // namespace

static void RegisterSevenSagesSpookyMask() {
    // OnPlayerUpdate rather than OnActorUpdate: this is a standing aura keyed off the mask, so
    // it wants one sweep per frame, not a callback per actor that would then have to re-check
    // the mask and the player each time.
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnPlayerUpdate>(
        []() { SevenSagesSpookyMaskFrameUpdate(); });
}

static RegisterShipInitFunc sevenSagesSpookyMaskInitFunc(RegisterSevenSagesSpookyMask, { "IS_RANDO" });
