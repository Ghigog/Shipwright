/**
 * Seven Sages - Phase 6: the Mirror Shield reflects light onto enemies and stuns them.
 *
 * Spec: docs/item-ability-overhaul.md, "Other" - "always reflects light when outside in the
 * overworld (ambient, not just deliberate aiming). Light shone on enemies stuns them (Deku Nut
 * style), generalizing the vanilla Iron-Knuckle-stun puzzle mechanic into an anywhere-outdoors
 * tool." Scoped 2026-08-07 with the author: raise the shield (R) and whatever is in front of Link
 * is stunned; it works wherever the shield is equipped, indoors included, and costs nothing but a
 * per-enemy cooldown.
 *
 * **There is no vanilla reflection to extend.** Vanilla's reflected light is Mir_Ray
 * (z_mir_ray.c), and it is not a shield feature at all: it is a scene actor with ten hard-coded
 * beam positions (sMirRayData - nine in the Spirit Temple, one in Ganon's Castle), each with a
 * fixed source and pool point. It asks whether Link is holding the mirror shield
 * (Player_HasMirrorShieldSetToDraw, z_player_lib.c:875) and reflects along its own authored line.
 * Nothing in the game reflects light from wherever Link happens to be standing, so this beam is new
 * work rather than a widened gate.
 *
 * **Why this is geometry and a freezeTimer rather than a collider.** Every other area effect in this
 * mod is a collider submitted through CollisionCheck (SevenSagesAoeField.h explains why at length),
 * and that is still the right default. It is the wrong tool here for two reasons:
 *
 *   1. A collider is all-or-nothing. It either exists for every target in its volume or for none,
 *      so a PER-ENEMY cooldown - the one thing keeping a free, unlimited stun from locking a fight
 *      down permanently - cannot be expressed with one. The cooldown is the balance decision; the
 *      pipeline is not.
 *   2. A beam is a shape the collision system does not really offer. Mir_Ray needs a quad AND a
 *      joint-sphere to describe one, both authored per beam from fixed endpoints. A cone test from
 *      Link's facing is four lines of maths and is exactly the shape the feature wants.
 *
 * The stun itself is Actor::freezeTimer, the universal field Deku Nuts, the boomerang and ice traps
 * all already use, applied the same way SevenSagesZeldasLullaby.cpp applies it. The known cost of
 * going around the damage tables is that an enemy vanilla authored as unstunnable (a Deku Nut row of
 * 0) is stunned by this anyway - the same trade Zelda's Lullaby already shipped with, and the
 * opposite of the choice the Megaton Hammer's shockwave made. Worth revisiting if it reads badly in
 * play on a specific enemy; it is a deliberate difference, not an oversight.
 *
 * The blue colour filter is set alongside the freeze because that is what a stun LOOKS like in this
 * game, and because SevenSagesGauntletLift.cpp's definition of "stunned" reads exactly that filter -
 * so a light-stunned enemy can then be picked up and thrown, which is the pairing the two items
 * ought to have.
 */
#include "soh/Enhancements/SevenSages/SevenSagesRoomAoe.h"

#include "soh/ShipInit.hpp"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"

#include <cmath>

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
}

extern "C" PlayState* gPlayState;

namespace {

// Reach. Comfortably past Din's Fire's core sphere (~130) and well short of a bow shot, so the
// shield is a tool for the fight in front of you rather than a way to disarm a room from the door.
constexpr f32 BEAM_RANGE = 400.0f;

// Half-angle of the cone, as a cosine so the test is one dot product. 20 degrees is wide enough to
// hold an enemy that is strafing and narrow enough that "shine it at him" is a thing the player
// aims rather than something that happens by standing still.
constexpr f32 BEAM_COS_HALF_ANGLE = 0.94f;

// How long a caught enemy stops acting. Deliberately shorter than Zelda's Lullaby's 15 seconds -
// that costs 24 magic a cast and this costs nothing.
constexpr u16 STUN_FRAMES = 20 * 4;

// Vanilla's own stun tint: colorFlag 0 is the blue filter (z64actor.h:301-309), which is what a
// Deku Nut leaves on its victims.
constexpr s16 STUN_COLOR_INTENSITY = 0x78;

// The per-enemy cooldown, measured from the moment the stun lands, so it covers the stun itself
// plus a few seconds of the enemy actually getting to act before the shield can catch it again.
constexpr int32_t COOLDOWN_FRAMES = STUN_FRAMES + 20 * 4;

// One slot per enemy the shield has caught recently. Sixteen is more enemies than any vanilla room
// holds; a full table simply refuses the stun rather than evicting a live cooldown, so an overflow
// shows up as "it stopped working" rather than as silent chain-stunning.
constexpr int32_t MAX_COOLDOWNS = 16;

struct Cooldown {
    // Compared, never dereferenced - see the note in SevenSagesThrownImpact.cpp. An entry whose
    // actor has been freed simply ages out, and the worst a recycled address can do is deny one
    // stun for a couple of seconds.
    const Actor* actor;
    int32_t frames;
};

Cooldown sCooldowns[MAX_COOLDOWNS];

bool OnCooldown(const Actor* actor) {
    for (const Cooldown& entry : sCooldowns) {
        if (entry.frames > 0 && entry.actor == actor) {
            return true;
        }
    }
    return false;
}

void StartCooldown(const Actor* actor) {
    for (Cooldown& entry : sCooldowns) {
        if (entry.frames <= 0) {
            entry.actor = actor;
            entry.frames = COOLDOWN_FRAMES;
            return;
        }
    }
}

void TickCooldowns() {
    for (Cooldown& entry : sCooldowns) {
        if (entry.frames > 0) {
            entry.frames--;
        }
    }
}

void ClearCooldowns() {
    for (Cooldown& entry : sCooldowns) {
        entry.actor = nullptr;
        entry.frames = 0;
    }
}

// Light does not go through walls. Without this the shield would stun everything in the next room
// over, which is both wrong and much stronger than the feature is meant to be. Only walls are
// tested: floors and ceilings between Link and something he can see would be a false positive on
// sloped ground.
bool BlockedByGeometry(const Vec3f& from, const Vec3f& to) {
    Vec3f hitPos;
    CollisionPoly* poly = nullptr;
    s32 bgId = 0;
    Vec3f a = from;
    Vec3f b = to;
    return BgCheck_EntityLineTest1(&gPlayState->colCtx, &a, &b, &hitPos, &poly, true, false, false, true, &bgId);
}

bool InBeam(const Player* player, const Actor* enemy, Vec3f* eyeOut) {
    // Chest height rather than the feet, so the line-of-sight test is not run from inside the floor
    // and so the beam misses things standing behind a low ledge.
    Vec3f eye = player->actor.world.pos;
    eye.y += 40.0f;
    *eyeOut = eye;

    f32 dx = enemy->world.pos.x - eye.x;
    f32 dy = enemy->world.pos.y - eye.y;
    f32 dz = enemy->world.pos.z - eye.z;
    f32 distSq = dx * dx + dy * dy + dz * dz;
    if (distSq > BEAM_RANGE * BEAM_RANGE || distSq < 1.0f) {
        return false;
    }

    // The cone is measured on the XZ plane against Link's facing. Height is left to the range check
    // alone: an enemy directly overhead is close enough to be worth catching, and vanilla's own
    // targeting is just as flat.
    f32 facingX = Math_SinS(player->actor.shape.rot.y);
    f32 facingZ = Math_CosS(player->actor.shape.rot.y);
    f32 flatLen = sqrtf(dx * dx + dz * dz);
    if (flatLen < 1.0f) {
        return false;
    }
    return ((dx / flatLen) * facingX + (dz / flatLen) * facingZ) >= BEAM_COS_HALF_ANGLE;
}

void SevenSagesMirrorShieldFrameUpdate() {
    if (!GameInteractor::IsSaveLoaded(true) || gPlayState == nullptr) {
        ClearCooldowns();
        return;
    }

    TickCooldowns();

    Player* player = GET_PLAYER(gPlayState);
    // Set-to-draw rather than merely owned: this is the same predicate Mir_Ray asks, and it is only
    // true once the shield is actually in Link's hand and facing outward. Paired with SHIELDING so
    // that carrying the mirror shield around is not itself a weapon - the player raises it.
    if (!Player_HasMirrorShieldSetToDraw(gPlayState) || !(player->stateFlags1 & PLAYER_STATE1_SHIELDING)) {
        return;
    }

    SevenSagesForEachActorInRoom(gPlayState, ACTORCAT_ENEMY, [player](Actor* enemy) {
        // A killed actor waiting to be deleted, and anything already held still by any other source
        // (a Deku Nut, an ice trap, a song), is left alone - re-freezing it would just reset a
        // longer stun to this shorter one.
        if (enemy->update == nullptr || enemy->freezeTimer != 0 || OnCooldown(enemy)) {
            return;
        }

        Vec3f eye;
        if (!InBeam(player, enemy, &eye) || BlockedByGeometry(eye, enemy->world.pos)) {
            return;
        }

        enemy->freezeTimer = STUN_FRAMES;
        Actor_SetColorFilter(enemy, 0, STUN_COLOR_INTENSITY, 0, STUN_FRAMES);
        SoundSource_PlaySfxAtFixedWorldPos(gPlayState, &enemy->world.pos, 20, NA_SE_EV_LIGHT_GATHER);
        StartCooldown(enemy);
    });
}

} // namespace

static void RegisterSevenSagesMirrorShield() {
    COND_HOOK(OnGameFrameUpdate, IS_RANDO, SevenSagesMirrorShieldFrameUpdate);
}

static RegisterShipInitFunc sevenSagesMirrorShieldInitFunc(RegisterSevenSagesMirrorShield, { "IS_RANDO" });
