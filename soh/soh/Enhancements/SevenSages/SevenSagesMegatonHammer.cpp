/**
 * Seven Sages - Phase 6: Megaton Hammer. AOE stun on the ground strike, and it breaks what a bomb
 * breaks. See SevenSagesMegatonHammer.h for the reasoning behind both halves.
 *
 * Like SevenSagesTunics.cpp and SevenSagesBoots.cpp this registers no hooks: the hammer is a
 * standing condition read from code that already exists, so the work is three small calls at sites
 * vanilla already has (the swing's dmgFlags assignment, the shockwave spawn, and the shared
 * "was I hit by an explosive" helper).
 */
#include "soh/Enhancements/SevenSages/SevenSagesMegatonHammer.h"
#include "soh/Enhancements/SevenSages/SevenSagesAoeField.h"

#include "soh/Enhancements/game-interactor/GameInteractor.h"
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

// The shockwave's reach. Larger than the elemental arrows' landing field (80), because this is a
// shockwave rolling out from a two-handed strike rather than an arrow's point of impact, and small
// enough that it stays a melee-range effect rather than a room clear - the room-wide case already
// has its own mechanism (SevenSagesRoomAoe.h) and songs are what pay for it.
constexpr float STUN_RADIUS = 120.0f;

// Total vertical extent, centred on the strike point (SevenSagesAoeField.h). Deliberately short:
// the shockwave travels along the ground, so something hovering well above Link should not be
// caught by it.
constexpr float STUN_HEIGHT = 80.0f;

// The breaker field's reach, separate from the stun's purely so it stays tunable on its own - this
// is the knob to turn if the collateral described below reads as too generous in play. Same value
// for now, so "what got stunned" and "what got smashed" are one mental model rather than two.
constexpr float BREAK_RADIUS = STUN_RADIUS;
constexpr float BREAK_HEIGHT = STUN_HEIGHT;

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
    // Lifetime 1 - the instantaneous case, through the same pooled mechanism a lingering fire field
    // uses. Damage 0 with the Deku Nut flag is vanilla's own idiom for "the effect is the stun".
    // Visual NONE because EffectSsBlast_SpawnWhiteShockwave has already been spawned at this exact
    // position by the caller; a second indicator on top of it would only muddy the strike.
    SevenSagesSpawnAoeField(play, x, y, z, STUN_RADIUS, STUN_HEIGHT, 1, SEVEN_SAGES_AOE_DMG_STUN, 0,
                            SEVEN_SAGES_AOE_VISUAL_NONE);

    // A SECOND field, carrying the explosive bit, so the shockwave also smashes pots, bushes and
    // crates the way a bomb does.
    //
    // It has to be a second field rather than another bit on the one above, and that is forced by
    // how damage resolves rather than being a style choice. CollisionCheck_ApplyDamage
    // (z_collision_check.c:3014) indexes an actor's DMG_ENTRY table by the position of the HIGHEST
    // set bit in the attacker's dmgFlags. The Deku Nut bit is bit 0 - the lowest there is - so ANY
    // other bit set alongside it wins, and the field stops being a stun at all: OR-ing the
    // explosive bit in would silently convert the stun into bomb damage. Nothing else can share a
    // collider with a stun. (This is the mirror image of Din's Fire, where OR-ing the explosive bit
    // in was safe precisely because its fire bit 17 stays higher.)
    //
    // Breakables do not care, because they have no damage table: their AC bumper is a pure
    // eligibility gate on dmgFlags, and pots (Obj_Tsubo, 0x4FC1FFFE), bushes (En_Kusa, 0x4FC00758)
    // and crates all accept the explosive bit. Note pots deliberately clear bit 0, so the stun
    // field could never have broken one no matter how it was tuned.
    //
    // ACCEPTED CONSEQUENCE: an enemy inside the radius takes its own bomb-table damage from this
    // field, on top of being stunned by the one above. There is no way to aim a collider at
    // scenery and not at enemies - the AT/AC type flags don't separate them, and 0 damage here is
    // ignored for anything that has a damage table. So "the shockwave smashes pots" and "the
    // shockwave deals bomb damage" are the same feature; BREAK_RADIUS is the dial.
    SevenSagesSpawnAoeField(play, x, y, z, BREAK_RADIUS, BREAK_HEIGHT, 1, DMG_FLAG_EXPLOSIVE, 0,
                            SEVEN_SAGES_AOE_VISUAL_NONE);
}
