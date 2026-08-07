/**
 * Seven Sages - Phase 6: the Deku Shield survives fire and carries it. See
 * SevenSagesDekuShieldFire.h for the design, the enumeration of what the flame lights, and why the
 * flame is not a collider.
 */
#include "soh/Enhancements/SevenSages/SevenSagesDekuShieldFire.h"

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

bool sAflame = false;

// The lit Deku Stick's own flame particle parameters (z_player.c, Player_UpdateBurningDekuStick):
// same upward drift, same yellow-to-red ramp. Matching them exactly is the point - a player who has
// lit a stick should recognise this as the same fire on a different object.
Vec3f sFlameVelocity = { 0.0f, 0.5f, 0.0f };
Vec3f sFlameAccel = { 0.0f, 0.5f, 0.0f };
Color_RGBA8 sFlamePrimColor = { 255, 255, 100, 255 };
Color_RGBA8 sFlameEnvColor = { 255, 50, 0, 0 };

// The stick scales its flame down as it burns out (`temp * 200.0f`). This one never burns out, so
// it holds at the stick's full-strength value.
//
// s16 rather than float because func_8002836C takes s16: as a float this narrowed implicitly at the
// call site, which MSVC reports as C4244 and CI promotes to an error under /WX. 200 is exactly
// representable either way, so the effect is unchanged.
constexpr s16 FLAME_SCALE = 200;

Player* CurrentPlayer() {
    if (!GameInteractor::IsSaveLoaded(true) || gPlayState == nullptr) {
        return nullptr;
    }
    return GET_PLAYER(gPlayState);
}

bool HasDekuShield(const Player* player) {
    return player != nullptr && player->currentShield == PLAYER_SHIELD_DEKU;
}

// Where the shield is right now. Vanilla parents the shield to the sheath limb while it is stowed
// on Link's back and to the left hand while he is actively blocking, so the flame has to follow the
// same two positions or it detaches from the object it is supposedly burning on.
const Vec3f& ShieldPos(const Player* player) {
    const bool inHand = (player->stateFlags1 & PLAYER_STATE1_SHIELDING) != 0;
    return player->bodyPartsPos[inHand ? PLAYER_BODYPART_L_HAND : PLAYER_BODYPART_SHEATH];
}

void SevenSagesDekuShieldFrameUpdate() {
    Player* player = CurrentPlayer();
    if (player == nullptr) {
        sAflame = false;
        return;
    }

    if (!sAflame) {
        return;
    }

    // Doused by water, and lost with the shield itself. PLAYER_STATE1_IN_WATER is the swimming
    // state rather than "standing in a puddle", which is the line we want: wading through shallow
    // water in Zora's Domain should not cost you the flame you crossed the room to fetch.
    if (!HasDekuShield(player) || (player->stateFlags1 & PLAYER_STATE1_IN_WATER)) {
        sAflame = false;
        return;
    }

    Vec3f pos = ShieldPos(player);
    func_8002836C(gPlayState, &pos, &sFlameVelocity, &sFlameAccel, &sFlamePrimColor, &sFlameEnvColor, FLAME_SCALE, 0,
                  8);
}

} // namespace

bool SevenSagesDekuShieldIsAflame(void) {
    return sAflame && HasDekuShield(CurrentPlayer());
}

bool SevenSagesDekuShieldFlamePos(float* x, float* y, float* z) {
    Player* player = CurrentPlayer();
    if (!HasDekuShield(player)) {
        return false;
    }
    const Vec3f& pos = ShieldPos(player);
    *x = pos.x;
    *y = pos.y;
    *z = pos.z;
    return true;
}

bool SevenSagesDekuShieldIgnite(void) {
    if (sAflame || !HasDekuShield(CurrentPlayer())) {
        return false;
    }
    sAflame = true;
    return true;
}

static void RegisterSevenSagesDekuShieldFire() {
    // `*should` arrives carrying vanilla's own condition (`currentShield == PLAYER_SHIELD_DEKU`),
    // so this only fires when a Deku Shield really was about to be destroyed. It survives, and
    // catches instead - one hook covering both of func_8083819C's call sites, the fire-attack block
    // and Link burning.
    COND_VB_SHOULD(VB_BURN_SHIELD, IS_RANDO, {
        if (*should) {
            *should = false;
            SevenSagesDekuShieldIgnite();
        }
    });

    COND_HOOK(OnGameFrameUpdate, IS_RANDO, SevenSagesDekuShieldFrameUpdate);
}

static RegisterShipInitFunc sevenSagesDekuShieldFireInitFunc(RegisterSevenSagesDekuShieldFire, { "IS_RANDO" });
