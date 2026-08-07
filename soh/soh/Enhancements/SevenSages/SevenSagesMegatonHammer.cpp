/**
 * Seven Sages - Phase 6: Megaton Hammer. AOE stun on the ground strike, and it breaks what a bomb
 * breaks. See SevenSagesMegatonHammer.h for the reasoning behind both halves.
 *
 * Most of this is call sites rather than hooks, the way SevenSagesTunics.cpp and SevenSagesBoots.cpp
 * are: the hammer is a standing condition read from code that already exists, so three of the four
 * pieces are small calls at places vanilla already has (the swing's dmgFlags assignment, the
 * shockwave spawn, and the shared "was I hit by an explosive" helper). The fourth is a single
 * VB_MODIFY_RESOLVED_DAMAGE hook, which is what lets one AOE field stun enemies and smash scenery
 * at the same time - see the comment on it below.
 */
#include "soh/Enhancements/SevenSages/SevenSagesMegatonHammer.h"
#include "soh/Enhancements/SevenSages/SevenSagesAoeField.h"

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/ShipInit.hpp"
#include "soh/OTRGlobals.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
}

extern "C" PlayState* gPlayState;

namespace {

// Same flag En_Bom's explosion collider touches breakables with (z_en_bom.c's sJntSphElementsInit),
// spelled out here rather than taken from z64collision_check.h's DMG_* block, which carries an
// explicit "not to be used in code" note. SevenSagesDinsFireBomb.cpp defines the same constant for
// the same reason.
constexpr uint32_t DMG_FLAG_EXPLOSIVE = 0x00000008;

// The hammer's own two melee bits, from the header's damage-table discussion: bit 6 is a swing, bit
// 30 the jump strike. Used only to recognise the hammer's melee hit in the damage hook below.
constexpr uint32_t DMG_FLAG_HAMMER_SWING = 0x00000040;
constexpr uint32_t DMG_FLAG_HAMMER_JUMP = 0x40000000;

// How long after a hammer strike an enemy stays liftable.
//
// **This is a time window because OoT has no "knocked down" state to read.** Every generic stun in
// this game leaves a mark another system can test - a blue colour filter, a freezeTimer - and being
// knocked flat by a hammer leaves neither. What "flipped over" actually means is a per-enemy action
// state selected through that enemy's own damage table, where the effect nibble means whatever that
// actor decided it means; there is no cross-enemy value to compare against. So rather than infer the
// state, this records the CAUSE and trusts it for a few seconds.
//
// The cost of that is honest: an enemy that gets up early is liftable for the remainder of the
// window anyway. Three seconds is short enough that this reads as "grab it while it's down" rather
// than as a lasting property, and it is the same order as the knockdown itself.
constexpr int32_t KNOCKDOWN_FRAMES = 20 * 3;

// Eight is more enemies than one swing can plausibly flatten; a full table drops the record rather
// than evicting a live one, so the failure is "that one wasn't liftable", never a wrong grant.
constexpr int32_t MAX_KNOCKDOWNS = 8;

// **Why the shockwave field is spawned two frames late.**
//
// Vanilla already has a hammer-ground-strike AOE, and it is not a collider. func_80842A28 sets
// `play->actorCtx.unk_02 = 4` (z_player.c:9360), a broadcast that counts down one per frame in
// Actor_UpdateAll (z_actor.c:2605), and twelve actors poll it and react on their own - the Tektite
// flips onto its back, the Deku Baba and Skulltula drop, and so on.
//
// Every one of those reactions is written as the ELSE of the actor's damage check. EnTite_CheckDamage
// is the clearest case: `if (acFlags & AC_HIT) { ...damage... } else if (unk_02 != 0 && dist <= 400 &&
// grounded) { flip }` (z_en_tite.c:852-884). So an AC_HIT arriving in the same frame does not merely
// compete with the flip - it makes the flip branch unreachable. Our field was landing on the very
// frame of the strike and silently replacing every vanilla hammer reaction in the game with a Deku
// Nut stun. Reported from play as "tektites don't flip any more".
//
// Two frames of the four-frame window are given back to vanilla, so those actors do what they have
// always done, and the field then lands on everything else while the broadcast is still live. The
// delay is invisible at 20fps.
//
// Note this is a different failure from the melee-precedence one handled in the damage hook below,
// which is about which ATTACKER a target resolves. This one is about an actor never reaching its
// vanilla branch at all, and no amount of rewriting the resolved damage can fix it - AC_HIT alone is
// what diverts them.
constexpr int32_t SHOCKWAVE_DELAY_FRAMES = 2;

struct PendingShockwave {
    Vec3f pos;
    int32_t frames;
};

PendingShockwave sPending = { { 0.0f, 0.0f, 0.0f }, 0 };

struct Knockdown {
    // Compared, never dereferenced - the same rule SevenSagesThrownImpact.cpp and the Mirror
    // Shield's cooldown table follow. A freed actor's entry simply ages out, and a recycled address
    // can at worst offer one lift it should not have, for under three seconds.
    const Actor* actor;
    int32_t frames;
};

Knockdown sKnockdowns[MAX_KNOCKDOWNS];

void RecordKnockdown(const Actor* actor) {
    for (Knockdown& entry : sKnockdowns) {
        if (entry.actor == actor && entry.frames > 0) {
            entry.frames = KNOCKDOWN_FRAMES;
            return;
        }
    }
    for (Knockdown& entry : sKnockdowns) {
        if (entry.frames <= 0) {
            entry.actor = actor;
            entry.frames = KNOCKDOWN_FRAMES;
            return;
        }
    }
}

void TickKnockdowns() {
    for (Knockdown& entry : sKnockdowns) {
        if (entry.frames > 0) {
            entry.frames--;
        }
    }
}

// Whether the player's own hammer swing connected with this actor during THIS frame's
// CollisionCheck_AT.
//
// Reading the quads' resolved `at` pointer is safe in the one place this is called from and nowhere
// else. SevenSagesHammerCountsAsExplosive deliberately avoids the quads' AT_HIT state because it is
// asked from inside Actor_UpdateAll, where the answer depends on whether the player updated before
// the actor asking - a category-ordering detail. VB_MODIFY_RESOLVED_DAMAGE is different: it fires
// from CollisionCheck_Damage (z_play.c:1186), after CollisionCheck_AT has resolved every AT/AC pair
// for the frame, so `at` is final and the same for every caller.
bool MeleeQuadHit(const Actor* target) {
    if (gPlayState == nullptr) {
        return false;
    }
    Player* player = GET_PLAYER(gPlayState);
    for (const ColliderQuad& quad : player->meleeWeaponQuads) {
        if ((quad.base.atFlags & AT_HIT) && quad.base.at == target) {
            return true;
        }
    }
    return false;
}


// The shockwave's dmgFlags: the explosive bit, plus the Deku Nut bit (0x1).
//
// This one value carries the whole design of the field, so it is worth spelling out. It does three
// separate jobs, and each one is load-bearing:
//
//   1. ELIGIBILITY is the union of both bits, because CollisionCheck_NoSharedFlags
//      (z_collision_check.c:1450) is a plain AND against the target's bumper mask. The explosive
//      bit is what every breakable accepts - pots (0x4FC1FFFE), bushes (0x4FC00758), small crates
//      (0x4FC00748), cracked walls (0x48), the Fire Temple's explosive-only walls (0x8), and the
//      large crate via the bumper patch in z_obj_kibako2.c. The Deku Nut bit additionally reaches
//      any enemy that is stunnable but bomb-immune, which the explosive bit alone would miss.
//
//   2. RESOLUTION is the explosive row, because CollisionCheck_ApplyDamage indexes the damage table
//      by the position of the HIGHEST set bit, and bit 3 beats bit 0. That is what we want for
//      breakables (no damage table, they just need the hit to register) and NOT what we want for
//      enemies, which is what the hook below exists to correct.
//
//   3. IDENTIFICATION: a real bomb submits 0x00000008. Nothing in the game submits 0x00000009
//      (verified by search), so this value is a reliable signature for "this hit came from the
//      hammer's shockwave" inside VB_MODIFY_RESOLVED_DAMAGE, which sees dmgFlags and nothing else
//      that would distinguish us.
//
// Do not add a bit above 3 here. It would move the resolved row and turn the field into whatever
// that bit's weapon is.
constexpr uint32_t SHOCKWAVE_DMG_FLAGS = DMG_FLAG_EXPLOSIVE | 0x00000001;

// The shockwave's reach. Larger than the elemental arrows' landing field (80), because this is a
// shockwave rolling out from a two-handed strike rather than an arrow's point of impact, and small
// enough that it stays a melee-range effect rather than a room clear - the room-wide case already
// has its own mechanism (SevenSagesRoomAoe.h) and songs are what pay for it.
//
// STILL OPEN as of 2026-08-07: 120 sits at ~92% of Din's Fire's core sphere (325 * 0.4 scale, see
// z_magic_fire.c:133), which is not the "clearly smaller than Din's" the spec's area ordering
// wants. Left at 120 pending playtest rather than guessed at; ~80 is the likely landing spot.
constexpr float SHOCKWAVE_RADIUS = 120.0f;

// Total vertical extent, centred on the strike point (SevenSagesAoeField.h). Deliberately short:
// the shockwave travels along the ground, so something hovering well above Link should not be
// caught by it.
constexpr float SHOCKWAVE_HEIGHT = 80.0f;

void SevenSagesHammerKnockdownFrameUpdate() {
    if (!GameInteractor::IsSaveLoaded(true) || gPlayState == nullptr) {
        for (Knockdown& entry : sKnockdowns) {
            entry.actor = nullptr;
            entry.frames = 0;
        }
        sPending.frames = 0;
        return;
    }
    TickKnockdowns();

    if (sPending.frames > 0 && --sPending.frames == 0) {
        SevenSagesSpawnAoeField(gPlayState, sPending.pos.x, sPending.pos.y, sPending.pos.z, SHOCKWAVE_RADIUS,
                                SHOCKWAVE_HEIGHT, 1, SHOCKWAVE_DMG_FLAGS, 0, SEVEN_SAGES_AOE_VISUAL_NONE);
    }
}

} // namespace

bool SevenSagesHammerKnockedDown(const Actor* actor) {
    if (actor == nullptr) {
        return false;
    }
    for (const Knockdown& entry : sKnockdowns) {
        if (entry.frames > 0 && entry.actor == actor) {
            return true;
        }
    }
    return false;
}

bool SevenSagesHammerCountsAsExplosive(const Actor* attacker) {
    if (!IS_RANDO || attacker == nullptr || attacker->category != ACTORCAT_PLAYER) {
        return false;
    }
    const Player* player = reinterpret_cast<const Player*>(attacker);
    return player->heldItemAction == PLAYER_IA_HAMMER;
}

uint32_t SevenSagesHammerMeleeDmgFlags(const Player* player, uint32_t dmgFlags) {
    if (!IS_RANDO || player == nullptr || player->heldItemAction != PLAYER_IA_HAMMER) {
        return dmgFlags;
    }
    return dmgFlags | DMG_FLAG_EXPLOSIVE;
}

void SevenSagesHammerShockwave(PlayState* play, float x, float y, float z) {
    if (!IS_RANDO) {
        return;
    }
    // ONE field, not two. An earlier build spawned a stun field and a breaker field at the same
    // point, on the theory that a stun and an explosive bit cannot share a collider. They cannot -
    // but two colliders cannot share a target either, and that was the worse problem:
    // CollisionCheck_SetATvsAC does `acInfo->acHitInfo = atInfo` (z_collision_check.c:1740), a
    // plain assignment. Every AT/AC pair for the frame resolves in CollisionCheck_AT before
    // CollisionCheck_Damage runs at all (z_play.c:1180-1186), so a bumper hit by both fields keeps
    // only the one that registered last, and the stun was silently discarded on every enemy.
    //
    // Lifetime 1 - the instantaneous case, through the same pooled mechanism a lingering fire field
    // uses. Damage 0 because the resolved number comes from the target's own table, never from
    // here. Visual NONE because EffectSsBlast_SpawnWhiteShockwave has already been spawned at this
    // exact position by the caller; a second indicator on top of it would only muddy the strike.
    //
    // Queued rather than spawned, and the delay is the whole point - see SHOCKWAVE_DELAY_FRAMES.
    sPending.pos = { x, y, z };
    sPending.frames = SHOCKWAVE_DELAY_FRAMES;
}

static void RegisterSevenSagesMegatonHammer() {
    // Forces the shockwave to resolve as a Deku Nut hit against anything that has a damage table,
    // undoing the explosive row that its dmgFlags selected. This is what makes "AOE stun" and "AOE
    // reveal" one field instead of two mutually-destructive ones.
    //
    // Copying the target's OWN Deku Nut row rather than writing a fixed 0-damage stun is the point:
    // it gives each enemy exactly the reaction vanilla already assigned it to a Deku Nut, including
    // "nothing" for the ones vanilla made unstunnable (table entry 0). A flat stun would have made
    // the hammer better against those enemies than a Deku Nut is, which nobody asked for.
    //
    // Breakables are untouched by this: they have no damage table, so the guard skips them and
    // their hit stands as the explosive one that broke them. That asymmetry is the whole trick -
    // scenery reads the field as a bomb, enemies read it as a nut, from a single collider.
    COND_VB_SHOULD(VB_MODIFY_RESOLVED_DAMAGE, IS_RANDO, {
        Actor* target = va_arg(args, Actor*);
        f32* damage = va_arg(args, f32*);
        uint32_t dmgFlags = va_arg(args, uint32_t);

        if (dmgFlags == SHOCKWAVE_DMG_FLAGS && target != nullptr && target->colChkInfo.damageTable != nullptr) {
            // **The enemy the swing actually connected with resolves as a hammer hit, not a nut.**
            //
            // Only ONE attack survives per target: CollisionCheck_SetATvsAC ends with
            // `acInfo->acHitInfo = atInfo` (z_collision_check.c:1743), a plain assignment, so a
            // bumper touched by several ATs keeps whichever registered last. The shockwave field is
            // submitted from OnGameFrameUpdate (game.c:356), which runs after Play_Update has
            // finished Actor_UpdateAll, so it is always submitted after the player's melee quads and
            // therefore always wins. The hammer's own hit on that enemy is discarded before it can
            // resolve.
            //
            // That was invisible while the field was a sixth of its radius and rarely reached the
            // enemy in front of Link. Fixing the radius exposed it as a regression in play
            // (2026-08-07): tektites stopped flipping and just froze, because the swing's hit was
            // being replaced by the shockwave's stun.
            //
            // Rather than fight the submission order, the surviving collider is made to carry the
            // right answer: for the melee target, resolve through the hammer's own damage row
            // instead of the Deku Nut one. Row index is the position of the highest set bit in the
            // attacker's dmgFlags, so the hammer swing's bit 6 is table[6].
            const bool meleeTarget = MeleeQuadHit(target);
            const u8 entry = target->colChkInfo.damageTable->table[meleeTarget ? 6 : 0];
            *damage = (f32)(entry & 0xF);
            target->colChkInfo.damageEffect = (entry >> 4) & 0xF;

            // The knockdown record has to be made here too. It cannot key off the hammer's melee
            // dmgFlags reaching this hook, because by the reasoning above those flags never arrive -
            // the field's 0x9 is what the target resolves. This is the only point at which "the
            // swing hit this enemy" is both true and observable.
            if (meleeTarget && target->category == ACTORCAT_ENEMY) {
                RecordKnockdown(target);
            }
        }

        // A MELEE hammer hit on an enemy makes it liftable for a few seconds, which is what the
        // gauntlets read through SevenSagesHammerKnockedDown. Deliberately separate from the
        // shockwave case above: that one stuns through the target's own Deku Nut row and so already
        // leaves the blue filter the gauntlets recognise, whereas a swing that connects directly
        // leaves nothing to recognise at all. Requested from play 2026-08-07 - "it flips over and I
        // still can't pick it up".
        //
        // Testing the hammer's own bits means the swing's added explosive bit is irrelevant here,
        // and a real bomb (0x8 alone) never qualifies.
        if (target != nullptr && target->category == ACTORCAT_ENEMY &&
            (dmgFlags & (DMG_FLAG_HAMMER_SWING | DMG_FLAG_HAMMER_JUMP)) != 0) {
            RecordKnockdown(target);
        }
    });

    COND_HOOK(OnGameFrameUpdate, IS_RANDO, SevenSagesHammerKnockdownFrameUpdate);
}

static RegisterShipInitFunc sevenSagesMegatonHammerInitFunc(RegisterSevenSagesMegatonHammer, { "IS_RANDO" });
