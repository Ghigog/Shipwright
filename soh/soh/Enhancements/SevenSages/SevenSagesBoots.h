#ifndef SEVEN_SAGES_BOOTS_H
#define SEVEN_SAGES_BOOTS_H

/*
 * Seven Sages boots overhaul — shared predicates and tuning.
 *
 * Spec (docs/item-ability-overhaul.md, "Tunics, gauntlets, and boots"):
 *
 *   Iron Boots  — no movement speed penalty (removes vanilla's slow effect).
 *                 Resist all knockback. Walk through strong currents. Walk on
 *                 steep/slippery slopes without sliding.
 *   Hover Boots — slipperiness slightly reduced (not eliminated), boots grant
 *                 more speed, stacking with Bunny Hood. If still airborne at
 *                 the end of a hover and the player is holding a movement
 *                 direction, Link jumps instead of falling.
 *
 * Two of those clauses were already vanilla and needed no work:
 *
 *   - "Walk through strong currents" — every conveyor (which is what a current
 *     is: a `SurfaceType` conveyor speed on a water box or a floor) already
 *     excludes the Iron Boots, twice over, at z_player.c's conveyor arm and
 *     again at the point the push is applied.
 *   - "Slippery slopes" in the *ice floor* sense (`sFloorType == 5`) already
 *     excludes the Iron Boots. What was missing is the *steep* slope: the
 *     `SurfaceType` floor-effect-1 slide handled by `Player_HandleSlopes`,
 *     which vanilla applies regardless of boots.
 *
 * Like SevenSagesTunics.h, this header deliberately registers nothing. The
 * boots are a standing condition read by movement code that already exists, so
 * the work is in widening conditions at the sites that ask "which boots is Link
 * wearing right now?". Those sites are:
 *
 *   Iron:  z_player_lib.c  Player_SetBootData   drop the reduced speed/turn regs
 *          z_player.c      func_80837C0C        downgrade launching hits
 *          z_player.c      Player_HandleSlopes  no slide, no uphill drag
 *
 *   Hover: SpeedModifiers.cpp                   run-speed factor, folded into
 *                                               the existing Bunny Hood product
 *          z_player.c      Player_UpdateCommon  slip recovery rates
 *          z_player.c      func_8083AA10        jump at the end of a hover
 *
 * The one exception to "registers nothing" is the run-speed factor, and that is
 * the same fold-into-an-existing-predicate move the tunics made: `SpeedModifiers.cpp`
 * already owns the only VB_PLAYER_MODIFY_RUN_SPEED hook in the build and already
 * multiplies a Bunny Hood factor in place there, so the Hover Boots become one
 * more factor in that product rather than a second hook fighting it for the same
 * `*speedTarget`.
 *
 * `currentBoots` is read rather than `CUR_EQUIP_VALUE(EQUIP_TYPE_BOOTS)` — the
 * opposite of what SevenSagesTunics.h does, on purpose. Every vanilla check
 * these predicates sit beside reads `this->currentBoots`, and matching them
 * keeps "which boots" answering identically within a single frame's movement
 * update. (Player_SetBootData's local remap to PLAYER_BOOTS_IRON_UNDERWATER
 * does not touch the field, so the Iron predicate stays true underwater.)
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

struct Player;

// True when Link is wearing the Iron Boots in a randomizer save. Gates the
// removed speed penalty, the knockback resistance and the steep-slope grip.
bool SevenSagesIronBootsActive(const struct Player* player);

// True when Link is wearing the Hover Boots in a randomizer save. Gates the
// speed buff, the reduced slipperiness and the end-of-hover jump.
bool SevenSagesHoverBootsActive(const struct Player* player);

// Multiplier for the Hover Boots' ground run-speed target, or 1.0f when they
// are not worn. Applied multiplicatively alongside the Bunny Hood factor in
// SpeedModifiers.cpp, which is what makes the two stack.
//
// Note the Hover Boots' own base run cap is *lower* than the Kokiri Boots'
// (R_RUN_SPEED_LIMIT 550 vs 600, sBootData in z_player_lib.c), so a factor of
// F lands at F * 5.5 against a normal 6.0 - the buff relative to ordinary
// running is smaller than the number looks.
float SevenSagesHoverBootsRunSpeedFactor(const struct Player* player);

// Multiplier for how fast Link's actual velocity and facing catch up to the
// intended ones while the Hover Boots are sliding him around. Higher means
// grippier; the spec asks for "slightly reduced, not eliminated", so this is a
// modest number rather than an override.
//
// `otherSlipSourceActive` must be true when the same vanilla slip code is also
// being driven by something that is not the boots - the icy floor type, or the
// slippery-floor cheat - in which case this returns 1.0f. The Hover Boots are
// specified as less slippery; ice is not, and a player standing on ice in Hover
// Boots should still get ice.
float SevenSagesHoverBootsSlipGripFactor(const struct Player* player, bool otherSlipSourceActive);

#ifdef __cplusplus
}
#endif

#endif // SEVEN_SAGES_BOOTS_H
