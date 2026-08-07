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

namespace {

// Same flag En_Bom's explosion collider touches breakables with (z_en_bom.c's sJntSphElementsInit),
// spelled out here rather than taken from z64collision_check.h's DMG_* block, which carries an
// explicit "not to be used in code" note. SevenSagesDinsFireBomb.cpp defines the same constant for
// the same reason.
constexpr uint32_t DMG_FLAG_EXPLOSIVE = 0x00000008;

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

} // namespace

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
    SevenSagesSpawnAoeField(play, x, y, z, SHOCKWAVE_RADIUS, SHOCKWAVE_HEIGHT, 1, SHOCKWAVE_DMG_FLAGS, 0,
                            SEVEN_SAGES_AOE_VISUAL_NONE);
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
            // DMG_ENTRY packs damage in the low nibble and effect in the high one; index 0 is the
            // Deku Nut row.
            u8 dekuNutEntry = target->colChkInfo.damageTable->table[0];
            *damage = (f32)(dekuNutEntry & 0xF);
            target->colChkInfo.damageEffect = (dekuNutEntry >> 4) & 0xF;
        }
    });
}

static RegisterShipInitFunc sevenSagesMegatonHammerInitFunc(RegisterSevenSagesMegatonHammer, { "IS_RANDO" });
