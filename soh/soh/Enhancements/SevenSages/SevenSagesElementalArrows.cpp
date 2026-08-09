/**
 * Seven Sages - Phase 6: elemental arrows.
 *
 * Spec: docs/item-ability-overhaul.md, "Arrows". Built on SevenSagesAoeField, so all three go
 * through the normal damage pipeline rather than applying anything directly.
 *
 *   - **Fire Arrow**: leaves a lingering fire field where it lands. The field IS the damage over
 *     time - vanilla has no ignite status for enemies, so anything standing in the fire keeps
 *     getting hit and anything that walks out stops. Din's Fire's damage flag.
 *   - **Ice Arrow**: leaves a stun field where it lands. Uses the deku nut's flag with **0 damage**,
 *     which is how vanilla expresses "the effect is the stun, not the hit" - so enemies caught in
 *     it freeze rather than take chip damage.
 *   - **Light Arrow**: no field. Re-scoped 2026-08-03 away from the spec's "blind enemies in an
 *     area" to a straight ultimate - far more damage for far more magic. The blind was dropped as
 *     unbuildable within vanilla's vocabulary; a stun field would just have duplicated the ice
 *     arrow.
 *
 * **The light arrow's cost has a hard ceiling that is not a balance opinion.** `logic.cpp:1008`
 * requires `CanUse(RG_LIGHT_ARROWS)` to kill Ganon, so light arrows are mandatory to finish a
 * seed. Worse, the failure is silent: `z_player.c` falls back to `arrowType = ARROW_NORMAL` when
 * `Magic_RequestChange` declines, so an unaffordable light arrow fires as an ordinary one rather
 * than refusing. Price it so a full meter still yields several shots.
 */
#include "soh/Enhancements/SevenSages/SevenSagesAoeField.h"

#include "soh/ShipInit.hpp"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
#include "overlays/actors/ovl_En_Arrow/z_en_arrow.h"
}

extern "C" PlayState* gPlayState;

namespace {

// Fire: a patch of ground that keeps burning. 3 seconds at OoT's 20fps logic rate.
constexpr float FIRE_RADIUS = 150.0f; // ~2 bomb blasts (a bomb is 72)
constexpr float FIRE_HEIGHT = 150.0f; // total, centred - 75 up and 75 down
constexpr int32_t FIRE_LIFETIME_FRAMES = 3 * 20;
constexpr uint8_t FIRE_DAMAGE = 1;

// Ice: shorter, because a stun that outlasts the enemy's own stun timer just re-applies itself and
// the enemy never gets to act. Damage is 0 deliberately - see the header.
constexpr float ICE_RADIUS = 150.0f;
constexpr float ICE_HEIGHT = 150.0f;
constexpr int32_t ICE_LIFETIME_FRAMES = 2 * 20;
constexpr uint8_t ICE_DAMAGE = 0;

// Light: vanilla is 8 magic. 24 is the same "expensive" tier the songs and the gauntlet doors use,
// and still leaves 2 shots on a single bar and 4 on a double - deliberately enough for the Ganon
// fight, per the ceiling described in the header.
constexpr int16_t LIGHT_MAGIC_COST = 24;

// A MULTIPLIER, not a damage value. Damage comes from the target's own damageTable, so it cannot be
// set from the attacker at all (see VB_MODIFY_RESOLVED_DAMAGE). Scaling preserves each enemy's
// relative resistance and keeps immune enemies (table entry 0) immune, which setting a flat number
// would not. 6x turns a Wolfos's light-arrow entry of 2 into 12 against its 8 health - a one-shot -
// while a tougher or more resistant enemy still takes proportionally more.
constexpr float LIGHT_DAMAGE_MULTIPLIER = 6.0f;

// The light arrow's dmgFlags bit, from EnArrow's own dmgFlags[] table indexed by ArrowType
// (z_en_arrow.c:83, z_en_arrow.h:31). ARROW_LIGHT is index 5. Getting this wrong is silent and
// looks exactly like the feature not working: 0x00000800 is index 3, the FIRE arrow, which had the
// multiplier landing on fire hits while light arrows stayed at their vanilla table value.
// The damage table is indexed by this flag's BIT POSITION - bit 13 for 0x2000, which is the
// "Light arrow" row of every enemy's table.
constexpr uint32_t LIGHT_ARROW_DMG_FLAG = 0x00002000;

void SevenSagesElementalArrowImpact(void* arrowPtr) {
    if (!GameInteractor::IsSaveLoaded(true) || gPlayState == nullptr || arrowPtr == nullptr) {
        return;
    }

    Actor* arrow = static_cast<Actor*>(arrowPtr);
    const Vec3f& pos = arrow->world.pos;

    switch (arrow->params) {
        case ARROW_FIRE:
            SevenSagesSpawnAoeField(gPlayState, pos.x, pos.y, pos.z, FIRE_RADIUS, FIRE_HEIGHT,
                                    FIRE_LIFETIME_FRAMES, SEVEN_SAGES_AOE_DMG_FIRE, FIRE_DAMAGE,
                                    SEVEN_SAGES_AOE_VISUAL_FIRE);
            break;
        case ARROW_ICE:
            SevenSagesSpawnAoeField(gPlayState, pos.x, pos.y, pos.z, ICE_RADIUS, ICE_HEIGHT, ICE_LIFETIME_FRAMES,
                                    SEVEN_SAGES_AOE_DMG_STUN, ICE_DAMAGE, SEVEN_SAGES_AOE_VISUAL_ICE);
            break;
        default:
            // Light arrows leave no field, and every other arrow type is vanilla.
            break;
    }
}



} // namespace

static void RegisterSevenSagesElementalArrows() {
    COND_HOOK(OnArrowImpact, IS_SEVENSAGES, SevenSagesElementalArrowImpact);

    // Light arrow damage. Has to happen here rather than on the arrow, because the attacker's
    // toucher.damage is ignored whenever the target has a damageTable - which every enemy does.
    COND_VB_SHOULD(VB_MODIFY_RESOLVED_DAMAGE, IS_SEVENSAGES, {
        [[maybe_unused]] Actor* target = va_arg(args, Actor*);
        f32* damage = va_arg(args, f32*);
        uint32_t dmgFlags = va_arg(args, uint32_t);

        if (dmgFlags == LIGHT_ARROW_DMG_FLAG) {
            *damage *= LIGHT_DAMAGE_MULTIPLIER;
        }
    });

    // Vanilla charges sMagicArrowCosts[] - 4/4/8 for fire/ice/light - at the moment the arrow is
    // nocked. Returning false suppresses that entirely so the light arrow can be charged at its own
    // rate; fire and ice keep vanilla's price by letting the default through.
    COND_VB_SHOULD(VB_PLAYER_ARROW_MAGIC_CONSUMPTION, IS_SEVENSAGES, {
        [[maybe_unused]] Player* player = va_arg(args, Player*);
        int32_t magicArrowType = va_arg(args, int32_t);
        [[maybe_unused]] int8_t* arrowType = va_arg(args, int8_t*);

        // magicArrowType is arrowType - ARROW_FIRE, so 2 is the light arrow.
        if (magicArrowType == 2) {
            *should = false;
            if (!Magic_RequestChange(gPlayState, LIGHT_MAGIC_COST, MAGIC_CONSUME_NOW)) {
                // Mirror vanilla's own fallback rather than inventing a refusal: an unaffordable
                // magic arrow fires as a normal one.
                *arrowType = ARROW_NORMAL;
            }
        }
    });
}

static RegisterShipInitFunc sevenSagesElementalArrowsInitFunc(RegisterSevenSagesElementalArrows, { "IS_RANDO" });
