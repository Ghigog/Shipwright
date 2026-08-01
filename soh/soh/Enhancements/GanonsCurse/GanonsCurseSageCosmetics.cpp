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

#include "soh/Enhancements/randomizer/randomizerTypes.h"
#include "soh/Enhancements/randomizer/randomizerEnums.h"

#include "z64save.h"
#include "variables.h"

#include <libultraship/bridge.h>
#include <algorithm>
#include <cmath>

extern "C" uint8_t Randomizer_GetSettingValue(RandomizerSettingKey randoSettingKey);

namespace {

// Master switch for the whole per-sage presentation layer. Default on - a player who wants sage
// select but the vanilla look/HUD/voice turns this off and gets exactly stock SoH behavior.
// Deliberately a single gate rather than one per category: the categories are a coherent identity,
// and seven half-applied sages is a worse state than either extreme.
constexpr const char* CVAR_SAGE_COSMETICS = CVAR_ENHANCEMENT("GanonsCurse.SageCosmetics");

bool SageCosmeticsEnabled() {
    return CVarGetInteger(CVAR_SAGE_COSMETICS, 1) != 0;
}

// ── Color helpers ───────────────────────────────────────────────────────────────────────────
// Every derived color in the spec is one of these three operations applied to a sage's single
// identity color, rather than 60 hand-written triples. Changing the "how much darker is a Goron
// tunic" feel is a one-number edit here, not a re-edit of seven rows.

struct Rgb {
    uint8_t r, g, b;

    Color_RGB8 ToColor() const {
        return Color_RGB8{ r, g, b };
    }
};

uint8_t ScaleChannel(uint8_t c, float f) {
    return static_cast<uint8_t>(std::clamp(std::lround(c * f), 0L, 255L));
}

uint8_t LightenChannel(uint8_t c, float f) {
    return static_cast<uint8_t>(std::clamp(std::lround(c + (255.0f - c) * f), 0L, 255L));
}

uint8_t BlendChannel(uint8_t a, uint8_t b, float aWeight) {
    return static_cast<uint8_t>(std::clamp(std::lround(a * aWeight + b * (1.0f - aWeight)), 0L, 255L));
}

Rgb Darker(Rgb c, float f) {
    return { ScaleChannel(c.r, f), ScaleChannel(c.g, f), ScaleChannel(c.b, f) };
}

Rgb Lighter(Rgb c, float f) {
    return { LightenChannel(c.r, f), LightenChannel(c.g, f), LightenChannel(c.b, f) };
}

// Sage identity tinted toward a vanilla effect color. Keeps a spell recognisable as fire/wind/love
// while still reading as the sage's - "black with a hint of red" and so on.
Rgb Tinted(Rgb sage, Rgb vanilla, float sageWeight) {
    return { BlendChannel(sage.r, vanilla.r, sageWeight), BlendChannel(sage.g, vanilla.g, sageWeight),
             BlendChannel(sage.b, vanilla.b, sageWeight) };
}

// ── CVar helpers ────────────────────────────────────────────────────────────────────────────
// Set and Clear are a matched pair and both call sites matter. Every option this file touches must
// be either set or explicitly cleared on every apply, because these are global CVars: a sage who
// simply *doesn't mention* the Master Sword blade would otherwise inherit whichever blade color the
// previously-loaded sage set. Cleared, not set-to-vanilla-white, so the game falls back through its
// own `.Changed` gate to real vanilla behavior rather than to a hardcoded guess at what vanilla is.

void SetCosmeticColor(const char* valueCvar, const char* changedCvar, Rgb color) {
    Color_RGB8 c = color.ToColor();
    CVarSetColor24(valueCvar, c);
    CVarSetInteger(changedCvar, 1);
}

void ClearCosmeticColor(const char* valueCvar, const char* changedCvar, const char* rainbowCvar) {
    CVarClear(valueCvar);
    CVarClear(changedCvar);
    if (rainbowCvar != nullptr) {
        CVarClear(rainbowCvar);
    }
}

#define COSMETIC_SET(id, color) SetCosmeticColor(CVAR_COSMETIC(id ".Value"), CVAR_COSMETIC(id ".Changed"), color)
#define COSMETIC_CLEAR(id) ClearCosmeticColor(CVAR_COSMETIC(id ".Value"), CVAR_COSMETIC(id ".Changed"), CVAR_COSMETIC(id ".Rainbow"))

// ── Per-sage data ───────────────────────────────────────────────────────────────────────────
// Only what cannot be derived from `base` lives here. See docs/sage-cosmetics.md for the reasoning
// behind each value; this table is the spec, transcribed.
//
// `base` is the sage's whole visual identity: tunic, hearts, HUD buttons and every spell tint come
// out of it. Note Rauru, Zelda and Darunia's bases differ from the values in savefile.cpp's
// sSageDefinitions - see the note on kSagePalette's use below.

struct SagePalette {
    uint8_t sage;
    Rgb base;

    Rgb magic;         // magic meter fill; MagicActive is derived from it
    bool magicRainbow; // Zelda only - overrides `magic` for the idle fill

    bool recolorBlade; // false = leave the Master Sword vanilla (Impa, Rauru)
    Rgb blade;
    bool recolorTrail; // false = leave the trail vanilla white
    Rgb trail;
    int32_t trailDuration; // 2 = the minimum the engine allows, our stand-in for "no trail"

    bool darkHearts; // Rauru: near-black hearts need a light border to stay legible
};

// Vanilla spell colors, the tint targets for the blend above (CosmeticsEditor.cpp's defaults).
constexpr Rgb kDinsVanillaPrimary = { 255, 200, 0 };
constexpr Rgb kDinsVanillaSecondary = { 255, 0, 0 };
constexpr Rgb kFaroresVanillaPrimary = { 255, 255, 0 };
constexpr Rgb kFaroresVanillaSecondary = { 100, 200, 0 };
constexpr Rgb kNayrusVanillaPrimary = { 170, 255, 255 };
constexpr Rgb kNayrusVanillaSecondary = { 0, 100, 255 };

// How much of the sage's identity survives the tint. Primary carries the sage, secondary carries
// enough of the element that Din's still reads as fire even when the sage is black.
constexpr float kSpellPrimarySageWeight = 0.70f;
constexpr float kSpellSecondarySageWeight = 0.45f;

constexpr float kGoronTunicDarken = 0.55f;
constexpr float kZoraTunicLighten = 0.45f;
constexpr float kDoubleDefenseDarken = 0.55f;
constexpr float kMagicActiveLighten = 0.35f;

// Button tiers. All darker than the sage's base, but separated from each other so B/A/C stay
// tellable apart by brightness now that the vanilla blue/green/amber cue is gone.
constexpr float kButtonBDarken = 0.55f;
constexpr float kButtonADarken = 0.70f;
constexpr float kButtonCDarken = 0.85f;

constexpr int32_t kTrailDurationMin = 2;
constexpr int32_t kTrailDurationDefault = 4;

// Zelda's rainbow cycle length is 360 * RainbowSpeed frames, so a LARGER value is SLOWER. The
// editor's own slider caps at 1.0 (~6s); the runtime has no such limit, and the spec asks for
// "very slow". ~30s at 20fps-equivalent hue ticks.
constexpr float kZeldaRainbowSpeed = 5.0f;

constexpr SagePalette kSagePalette[] = {
    // Rauru - black. Base lifted off pure-black {25,25,25} to {45,45,45} so the Goron (darker) and
    // Zora (lighter) tunics have somewhere to go; at the old value all three collapsed together.
    // Magic is white for time. Hearts near-black, hence darkHearts.
    { RO_SAGE_RAURU,
      { 45, 45, 45 },
      { 245, 245, 245 },
      false,
      false,
      {},
      false,
      {},
      kTrailDurationMin,
      true },
    // Saria - green. Magic is an earthy red-brown. Wooden sword, and no trail.
    { RO_SAGE_SARIA,
      { 110, 225, 70 },
      { 165, 105, 70 },
      false,
      true,
      { 105, 120, 60 },
      false,
      {},
      kTrailDurationMin,
      false },
    // Darunia - fire. Base moved off pure red {190,30,30} to red-orange, which is what resolved the
    // spec's own conflict between "red tunic" and "orange hearts/sword": now everything agrees, and
    // he stops colliding with the several other red things on screen.
    { RO_SAGE_DARUNIA,
      { 215, 75, 20 },
      { 150, 15, 15 },
      false,
      true,
      { 230, 120, 25 },
      true,
      { 250, 215, 70 },
      kTrailDurationDefault,
      false },
    // Ruto - water. Zora metal blade: dark blue-grey, with a blue trail.
    { RO_SAGE_RUTO,
      { 40, 130, 220 },
      { 40, 130, 220 },
      false,
      true,
      { 95, 110, 125 },
      true,
      { 90, 150, 225 },
      kTrailDurationDefault,
      false },
    // Impa - shadow. Magic is "black"; a true {0,0,0} bar is invisible against the HUD, so this is
    // a very dark violet instead, which reads black in motion but still has an edge.
    { RO_SAGE_IMPA,
      { 130, 60, 170 },
      { 35, 20, 50 },
      false,
      false,
      {},
      false,
      {},
      kTrailDurationMin,
      false },
    // Nabooru - spirit. Golden blade, yellow trail. Magic is poison green.
    { RO_SAGE_NABOORU,
      { 235, 190, 20 },
      { 40, 95, 35 },
      false,
      true,
      { 200, 165, 45 },
      true,
      { 245, 225, 110 },
      kTrailDurationDefault,
      false },
    // Zelda - light. Base pulled off pure white {235,235,235} to {225,225,225} for the same
    // headroom reason as Rauru, in the other direction. Her magic is rainbow, so `magic` here only
    // seeds the derived MagicActive color.
    { RO_SAGE_ZELDA,
      { 225, 225, 225 },
      { 225, 225, 225 },
      true,
      true,
      { 255, 250, 205 },
      true,
      { 170, 90, 225 },
      kTrailDurationDefault,
      false },
};

const SagePalette* FindSagePalette(uint8_t sage) {
    for (const SagePalette& palette : kSagePalette) {
        if (palette.sage == sage) {
            return &palette;
        }
    }
    return nullptr;
}

// ── Apply ───────────────────────────────────────────────────────────────────────────────────

// Kokiri is what every sage actually starts wearing (no kit equips Goron/Zora directly, those are
// just carried for later), so it takes the base color and the other two are its shades.
//
// Worn tunic colors are read live in z_player_lib.c every frame; the get-item tunic *models* are
// display-list patches sharing these same CVars, which is why ApplySageCosmetics runs one patch
// pass at the end rather than per-option.
void ApplyTunics(const SagePalette& p) {
    COSMETIC_SET("Link.KokiriTunic", p.base);
    COSMETIC_SET("Link.GoronTunic", Darker(p.base, kGoronTunicDarken));
    COSMETIC_SET("Link.ZoraTunic", Lighter(p.base, kZoraTunicLighten));
}

// Master Sword only - it stands in for each sage's own magic sword. Kokiri and Biggoron blades stay
// vanilla for everyone.
//
// There is no way to switch a trail off: the duration slider floors at 2 and the trail's alpha is
// deliberately preserved rather than settable. "No trail" is therefore the minimum duration. Note
// the duration override is global across trail types (stick, hammer, both other blades) but exempts
// boomerang and bombchu, which keep their own.
void ApplySword(const SagePalette& p) {
    if (p.recolorBlade) {
        COSMETIC_SET("Swords.MasterBlade", p.blade);
    } else {
        COSMETIC_CLEAR("Swords.MasterBlade");
    }

    if (p.recolorTrail) {
        COSMETIC_SET("Trails.MasterSword", p.trail);
    } else {
        COSMETIC_CLEAR("Trails.MasterSword");
    }

    CVarSetInteger(CVAR_COSMETIC("Trails.Duration.Value"), p.trailDuration);
    CVarSetInteger(CVAR_COSMETIC("Trails.Duration.Changed"), 1);
}

// Hearts take the base color, Double Defence hearts its darker shade. Magic takes its own color
// since it represents the sage's power rather than their person.
void ApplyHeartsAndMagic(const SagePalette& p) {
    COSMETIC_SET("Consumable.Hearts", p.base);
    COSMETIC_SET("Consumable.DDHearts", Darker(p.base, kDoubleDefenseDarken));

    // Rauru's hearts are near-black and vanish against the HUD without an outline. Everyone else
    // gets the vanilla dark border back.
    if (p.darkHearts) {
        COSMETIC_SET("Consumable.HeartBorder", Rgb{ 235, 235, 235 });
    } else {
        COSMETIC_CLEAR("Consumable.HeartBorder");
    }

    // MagicActive is the portion of the bar draining *right now* - not a rare state, it fires on
    // every spin attack, Din's charge and spell cast. Left alone it would flash vanilla yellow
    // mid-cast on every sage, so it tracks the sage's magic color.
    COSMETIC_SET("Consumable.MagicActive", Lighter(p.magic, kMagicActiveLighten));

    if (p.magicRainbow) {
        // CosmeticsUpdateTick rewrites the value every frame while rainbow is on, so only the gate
        // flags matter here.
        CVarSetInteger(CVAR_COSMETIC("Consumable.Magic.Changed"), 1);
        CVarSetInteger(CVAR_COSMETIC("Consumable.Magic.Rainbow"), 1);
        CVarSetFloat(CVAR_COSMETIC("RainbowSpeed"), kZeldaRainbowSpeed);
    } else {
        CVarClear(CVAR_COSMETIC("Consumable.Magic.Rainbow"));
        // Global, and shared with any other rainbow option - so it must go back to default for
        // every non-Zelda sage, or her slow cycle leaks into the next file.
        CVarClear(CVAR_COSMETIC("RainbowSpeed"));
        COSMETIC_SET("Consumable.Magic", p.magic);
    }

    // Consumable_MagicInfinite is deliberately untouched: it only renders with the infinite-magic
    // cheat, which our preset does not enable. (Note it is spelled with an underscore upstream,
    // outside the CVAR_COSMETIC dotted convention, so it would not respond to the macro anyway.)
}

void ApplySpells(const SagePalette& p) {
    COSMETIC_SET("Magic.DinsPrimary", Tinted(p.base, kDinsVanillaPrimary, kSpellPrimarySageWeight));
    COSMETIC_SET("Magic.DinsSecondary", Tinted(p.base, kDinsVanillaSecondary, kSpellSecondarySageWeight));
    COSMETIC_SET("Magic.FaroresPrimary", Tinted(p.base, kFaroresVanillaPrimary, kSpellPrimarySageWeight));
    COSMETIC_SET("Magic.FaroresSecondary", Tinted(p.base, kFaroresVanillaSecondary, kSpellSecondarySageWeight));
    COSMETIC_SET("Magic.NayrusPrimary", Tinted(p.base, kNayrusVanillaPrimary, kSpellPrimarySageWeight));
    COSMETIC_SET("Magic.NayrusSecondary", Tinted(p.base, kNayrusVanillaSecondary, kSpellSecondarySageWeight));
}

// All buttons darker than the sage's base, in three tiers so they stay distinguishable from each
// other. D-pad is set to white explicitly rather than left alone, so it stays white even if a
// preset or a previous session moved it.
void ApplyHudButtons(const SagePalette& p) {
    Rgb b = Darker(p.base, kButtonBDarken);
    Rgb a = Darker(p.base, kButtonADarken);
    Rgb c = Darker(p.base, kButtonCDarken);

    // Rauru's base is already charcoal, so the three tiers land on 25/32/38 - indistinguishable
    // from each other and from the HUD behind them. Spread upward instead, preserving the
    // B-dark -> C-light ordering. No general per-channel floor, because flooring would wash the
    // blue out of Darunia's orange and Nabooru's gold.
    if (p.darkHearts) {
        b = { 35, 35, 35 };
        a = { 55, 55, 55 };
        c = { 80, 80, 80 };
    }

    COSMETIC_SET("HUD.BButton", b);
    COSMETIC_SET("HUD.AButton", a);
    COSMETIC_SET("HUD.StartButton", a);
    COSMETIC_SET("HUD.CButtons", c);
    COSMETIC_SET("HUD.Dpad", Rgb{ 255, 255, 255 });

    // The four individual C-button *color* options stay cleared so they inherit HUD.CButtons.
    // Their *position* CVars are separate and read independently - that is what lets slice 3 split
    // Zelda's C buttons across two corners without touching color.
    COSMETIC_CLEAR("HUD.CUpButton");
    COSMETIC_CLEAR("HUD.CDownButton");
    COSMETIC_CLEAR("HUD.CLeftButton");
    COSMETIC_CLEAR("HUD.CRightButton");
}

// World state, identical for every sage: the Kokiri mourn Link, the Gerudo honour Nabooru's
// ascension. Applied here rather than in the preset's cosmetics block (as docs originally
// suggested) because a preset only takes effect when the player explicitly applies it, whereas
// this needs to be true in every run without ceremony.
void ApplyWorldNpcColors() {
    COSMETIC_SET("NPC.Kokiri", Rgb{ 55, 55, 60 });
    COSMETIC_SET("NPC.Gerudo", Rgb{ 200, 165, 45 });
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
