/**
 * Ganon's Curse - per-sage cosmetic identity.
 *
 * Full spec and the reasoning behind every value: docs/sage-cosmetics.md.
 *
 * This is slice 1 of five: the infrastructure only. It does exactly what the code it replaces did
 * - recolor the Kokiri Tunic - but from a place that can grow, and at a moment that is actually
 * correct. Slices 2-5 (the rest of the colors, HUD layout, proportions/fairy, audio) hang off
 * ApplySageCosmetics below.
 *
 * ── Why this exists at all: CVars are global, saves are not ─────────────────────────────────
 * Every cosmetic and audio setting in SoH is a global config value living in
 * shipofharkinian.json, not save data. The previous implementation (Sram_SetSageTunicColor in
 * z_sram.c) wrote the tunic color once, at *file creation*, and its own comment acknowledged the
 * consequence: the color "just reflects whichever sage was most recently generated."
 *
 * That is survivable for one tunic. It is not survivable for the full spec - create a Saria file,
 * load your older Rauru file, and Rauru is wearing green, squeaking, playing a harp and has his
 * hearts in the wrong corner, with nothing on screen to say why. So the application moves to
 * OnLoadGame, keyed off the *loaded save's* sage.
 *
 * That works because RSK_SELECTED_SAGE is a real randomizer option registered in the logic option
 * group (settings.cpp), so it is serialized with the seed and restored per save file -
 * Randomizer_GetSettingValue is correct at load time, not just at generation time.
 *
 * The Sram_InitSave call site is kept as well, deliberately. Nothing was found that reads these
 * CVars between file creation and the first load (the one non-gameplay reader, the LED controller
 * color in OTRGlobals.cpp, is gated on gPlayState and so cannot run on the file-select screen), so
 * it looks redundant rather than load-bearing - but it costs one call and guards a path that may
 * simply not have been found.
 *
 * ── Ordering against SoH's own cosmetics hook ───────────────────────────────────────────────
 * CosmeticsEditor.cpp registers its own OnLoadGame handler which calls
 * ApplyOrResetCustomGfxPatches() + ApplyCustomCosmetics(). Hook execution order is registration
 * order, which is not something to depend on, so this file calls ApplyOrResetCustomGfxPatches
 * itself after writing CVars. That matters because cosmetic options come in two flavours: most
 * are read live every frame, but a significant set (Master Sword blade, Goron/Zora tunic, hearts,
 * magic, Farore's, Gerudo, Navi secondaries) are *display-list patches* which only get re-applied
 * when that function runs with manualChange = true. Writing those CVars without the patch pass
 * looks exactly like "I set the value and nothing happened".
 *
 * Note the upstream handler is gated on RandomizeCosmeticsGenModes == RANDOMIZE_OFF. Our preset
 * must keep that setting off regardless, or SoH's own randomize-all-cosmetics feature overwrites
 * every value this file sets.
 */
#include "soh/ShipInit.hpp"
#include "soh/OTRGlobals.h"
#include "soh/cvar_prefixes.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/cosmetics/CosmeticsEditor.h"
#include "soh/Enhancements/randomizer/savefile.h"
#include "soh/Enhancements/GanonsCurse/GanonsCurseSageCosmetics.h"

#include "z64save.h"
#include "variables.h"

#include <libultraship/bridge.h>

namespace {

// Master switch for the whole per-sage presentation layer. Default on - a player who wants sage
// select but the vanilla look/HUD/voice turns this off and gets exactly stock SoH behavior.
// Deliberately a single gate rather than one per category: the categories are a coherent identity,
// and seven half-applied sages is a worse state than either extreme.
constexpr const char* CVAR_SAGE_COSMETICS = CVAR_ENHANCEMENT("GanonsCurse.SageCosmetics");

bool SageCosmeticsEnabled() {
    return CVarGetInteger(CVAR_SAGE_COSMETICS, 1) != 0;
}

// Recolor the Kokiri Tunic - what every sage actually starts wearing, since none of the kits equip
// the Goron/Zora tunic directly, those are just carried for later.
//
// The worn tunic is read live in z_player_lib.c every frame, so this needs no patch pass of its
// own; the get-item tunic *model* is a display-list patch, but shares this same CVar, which is why
// ApplySageCosmetics runs the patch pass once at the end rather than per-option.
void ApplyTunicColors() {
    uint8_t r = 0, g = 0, b = 0;
    Randomizer_GetSageTunicColor(&r, &g, &b);

    Color_RGB8 kokiri = { r, g, b };
    CVarSetColor24(CVAR_COSMETIC("Link.KokiriTunic.Value"), kokiri);
    CVarSetInteger(CVAR_COSMETIC("Link.KokiriTunic.Changed"), 1);

    // Slice 2 adds the derived Goron (darker) and Zora (lighter) shades here.
}

} // namespace

extern "C" void GanonsCurse_ApplySageCosmetics() {
    if (!IS_RANDO || !SageCosmeticsEnabled()) {
        return;
    }

    // -1 means no sage override applies to this file. Reusing the home-entrance accessor as the
    // "is there a sage at all" probe keeps the sage definition table the single source of truth -
    // there is deliberately no second copy of "which sages exist" in this file.
    if (Randomizer_GetSageHomeEntrance() == -1) {
        return;
    }

    ApplyTunicColors();

    // Push display-list-patched options through in one pass. See the header comment for why this
    // cannot be left to CosmeticsUpdateTick: its per-frame call passes manualChange = false, which
    // skips every patch whose rainbow CVar is unset - i.e. all of ours.
    ApplyOrResetCustomGfxPatches(true);
}

/**
 * Registered unconditionally at boot, and deliberately NOT gated on IS_RANDO the way every other
 * GanonsCurse module is. That difference is load-bearing, not an oversight.
 *
 * `ShipInit::Init("IS_RANDO")` - the thing that re-runs IS_RANDO-gated registration - is itself
 * called from inside an OnLoadGame hook (hook_handlers.cpp). So an IS_RANDO-gated OnLoadGame hook
 * gets *registered while OnLoadGame is mid-flight*: ExecuteHooks is range-for iterating
 * RegisteredGameHooks::functions, which is a std::unordered_map, and registration inserts into
 * that same map. An insert that triggers a rehash invalidates every iterator, including the one
 * the loop is holding - undefined behaviour, and even in the benign case whether our hook runs on
 * this first load is left to bucket ordering.
 *
 * The other modules get away with the IS_RANDO gate because they hook things that fire long after
 * load (ocarina songs, room AOE). This one hooks OnLoadGame itself, which is exactly the colliding
 * case. Registering once at boot and testing IS_RANDO inside the body sidesteps it: the hook
 * exists before any load can happen, and IS_RANDO / the randomizer options are both valid by the
 * time the body runs.
 */
static void RegisterGanonsCurseSageCosmetics() {
    COND_HOOK(OnLoadGame, true, [](int32_t fileNum) { GanonsCurse_ApplySageCosmetics(); });
}

static RegisterShipInitFunc ganonsCurseSageCosmeticsInitFunc(RegisterGanonsCurseSageCosmetics);
