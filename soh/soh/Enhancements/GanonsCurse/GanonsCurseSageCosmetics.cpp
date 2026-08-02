/**
 * Ganon's Curse - per-sage cosmetic identity.
 *
 * Full spec and the reasoning behind every value: docs/sage-cosmetics.md.
 *
 * Slices 1 and 2 of five are built: the infrastructure, and every color. Slices 3-5 (HUD layout,
 * proportions and the random fairy, audio) hang off ApplySageCosmetics below.
 *
 * ── Derived, not transcribed ────────────────────────────────────────────────────────────────
 * Each sage has ONE identity color. Tunics, hearts, Double Defence hearts, all four HUD button
 * tiers and every spell tint are computed from it by the shade/tint helpers below, rather than
 * written out as ~60 triples. Retuning how a Goron tunic feels is a one-number edit here instead
 * of seven table rows, and the values cannot drift out of agreement with each other.
 *
 * The other half of that discipline is that every option this file touches is explicitly SET or
 * CLEARED on each apply. These are global CVars: a sage who merely fails to mention the Master
 * Sword blade inherits whichever blade color the previously-loaded sage set. "Say nothing" is not
 * a safe default here; there is no such thing as leaving an option alone.
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
// PosType (ORIGINAL_LOCATION / ANCHOR_LEFT / ... / ANCHOR_TO_LIFE_METER), used by the HUD layout
// table below. Not pulled in by CosmeticsEditor.h.
#include "soh/Enhancements/cosmetics/cosmeticsTypes.h"
#include "soh/Enhancements/randomizer/savefile.h"
#include "soh/Enhancements/GanonsCurse/GanonsCurseSageCosmetics.h"

// randomizerTypes.h pulls in randomizerEnums.h, which is an X-macro header with no include guard
// of its own - including both here re-expands every enum and fails to compile. One is enough.
#include "soh/Enhancements/randomizer/randomizerTypes.h"

#include "z64save.h"
#include "variables.h"

#include <libultraship/bridge.h>
#include <algorithm>
#include <cmath>
#include <string>

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

uint8_t ClampChannel(long v) {
    return static_cast<uint8_t>(std::clamp(v, 0L, 255L));
}

uint8_t BlendChannel(uint8_t a, uint8_t b, float aWeight) {
    return ClampChannel(std::lround(a * aWeight + b * (1.0f - aWeight)));
}

uint8_t MaxChannel(Rgb c) {
    return std::max(c.r, std::max(c.g, c.b));
}

uint8_t MinChannel(Rgb c) {
    return std::min(c.r, std::min(c.g, c.b));
}

/**
 * Shade a color toward black or white by a proportion `t`, with an optional guarantee that the
 * result differs from the input by at least `minDelta` on some channel.
 *
 * The floor exists because a proportional shade has no room left at the extremes, which is exactly
 * where two of our sages live. Zelda's base is near-white: shifting it 45% toward white moves it 13
 * points, so her Kokiri and Zora tunics come out as two near-identical greys. Rauru's near-black
 * base has the same problem in the other direction. Nudging the base colors (which the spec did)
 * was not enough on its own - white and black are walls, not slopes.
 *
 * The floor is measured on the channel with the most room, and applied by scaling `t` up rather
 * than by clamping channels individually. That matters: a per-channel floor would drag the low
 * channels of a saturated color up on their own and wash the hue out - it would turn Darunia's
 * orange grey. Scaling `t` keeps the shift proportional, so hue is preserved and the floor only
 * ever engages for colors that are already close to the extreme. In practice it fires for exactly
 * two of the twenty-one tunics: Rauru's Goron and Zelda's Zora.
 */
Rgb TowardBlack(Rgb c, float t, int32_t minDelta = 0) {
    uint8_t room = MaxChannel(c);
    if (minDelta > 0 && room > 0 && room * t < minDelta) {
        t = std::min(1.0f, static_cast<float>(minDelta) / room);
    }
    return { ClampChannel(std::lround(c.r - c.r * t)), ClampChannel(std::lround(c.g - c.g * t)),
             ClampChannel(std::lround(c.b - c.b * t)) };
}

Rgb TowardWhite(Rgb c, float t, int32_t minDelta = 0) {
    int32_t room = 255 - MinChannel(c);
    if (minDelta > 0 && room > 0 && room * t < minDelta) {
        t = std::min(1.0f, static_cast<float>(minDelta) / room);
    }
    return { ClampChannel(std::lround(c.r + (255 - c.r) * t)), ClampChannel(std::lround(c.g + (255 - c.g) * t)),
             ClampChannel(std::lround(c.b + (255 - c.b) * t)) };
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

// Shade amounts, as "how far toward the extreme", so 0.45 means 45% of the way to black/white.
constexpr float kGoronTunicShade = 0.45f;
constexpr float kZoraTunicShade = 0.45f;
constexpr float kDoubleDefenseShade = 0.45f;
constexpr float kMagicActiveShade = 0.35f;

// Minimum perceptible difference between a shade and the color it came from. Applied only where
// the requirement is "must not look like the base" - the tunics, Double Defence hearts, and the
// draining half of the magic bar.
constexpr int32_t kMinShadeSeparation = 28;

// Button tiers. All darker than the sage's base, but separated from each other so B/A/C stay
// tellable apart by brightness now that the vanilla blue/green/amber cue is gone. No separation
// floor here: these need to differ from *each other*, which three distinct amounts already
// guarantee, and a floor would perturb the tiers of the mid-tone sages for no benefit. The one base
// too dark for this to work (Rauru) is hand-set instead - see ApplyHudButtons.
constexpr float kButtonBShade = 0.45f;
constexpr float kButtonAShade = 0.30f;
constexpr float kButtonCShade = 0.15f;

// A magic color this light has no headroom left, so lightening it again produces a near-identical
// bar and the drain becomes invisible. Above this, derive MagicActive downward instead.
constexpr uint8_t kMagicActiveFlipThreshold = 200;

constexpr int32_t kTrailDurationMin = 2;
constexpr int32_t kTrailDurationDefault = 4;

// Named rather than written inline at the call sites: COSMETIC_SET is a macro, and a braced
// initializer's commas would be parsed as extra macro arguments.
constexpr Rgb kWhite = { 255, 255, 255 };
constexpr Rgb kLightHeartBorder = { 235, 235, 235 };
// The Kokiri in mourning for Link; the Gerudo in gold for Nabooru's ascension.
constexpr Rgb kKokiriMourning = { 55, 55, 60 };
constexpr Rgb kGerudoGold = { 200, 165, 45 };
// Rauru's hand-set button tiers - see ApplyHudButtons for why they are not derived.
constexpr Rgb kRauruButtonB = { 35, 35, 35 };
constexpr Rgb kRauruButtonA = { 55, 55, 55 };
constexpr Rgb kRauruButtonC = { 80, 80, 80 };

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

/**
 * The color of the magic bar's draining half.
 *
 * Normally a lighter version of the sage's magic color. But Rauru's magic is white, and lightening
 * white gets you white - his drain would have been invisible, which is worse than leaving it
 * vanilla yellow. So for colors that are already near the top, this shades downward instead. The
 * direction is what carries the "this is being spent" read; which direction doesn't matter.
 */
Rgb DeriveMagicActive(Rgb magic) {
    if (MinChannel(magic) > kMagicActiveFlipThreshold) {
        return TowardBlack(magic, kMagicActiveShade, kMinShadeSeparation);
    }
    return TowardWhite(magic, kMagicActiveShade, kMinShadeSeparation);
}

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
    COSMETIC_SET("Link.GoronTunic", TowardBlack(p.base, kGoronTunicShade, kMinShadeSeparation));
    COSMETIC_SET("Link.ZoraTunic", TowardWhite(p.base, kZoraTunicShade, kMinShadeSeparation));
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
    COSMETIC_SET("Consumable.DDHearts", TowardBlack(p.base, kDoubleDefenseShade, kMinShadeSeparation));

    // Rauru's hearts are near-black and vanish against the HUD without an outline. Everyone else
    // gets the vanilla dark border back.
    if (p.darkHearts) {
        COSMETIC_SET("Consumable.HeartBorder", kLightHeartBorder);
    } else {
        COSMETIC_CLEAR("Consumable.HeartBorder");
    }

    // MagicActive is the portion of the bar draining *right now* - not a rare state, it fires on
    // every spin attack, Din's charge and spell cast. Left alone it would flash vanilla yellow
    // mid-cast on every sage, so it tracks the sage's magic color.
    COSMETIC_SET("Consumable.MagicActive", DeriveMagicActive(p.magic));

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
    Rgb b = TowardBlack(p.base, kButtonBShade);
    Rgb a = TowardBlack(p.base, kButtonAShade);
    Rgb c = TowardBlack(p.base, kButtonCShade);

    // Rauru's base is already charcoal, so the three tiers land on 25/32/38 - indistinguishable
    // from each other and from the HUD behind them. Spread upward instead, preserving the
    // B-dark -> C-light ordering. No general per-channel floor, because flooring would wash the
    // blue out of Darunia's orange and Nabooru's gold.
    if (p.darkHearts) {
        b = kRauruButtonB;
        a = kRauruButtonA;
        c = kRauruButtonC;
    }

    COSMETIC_SET("HUD.BButton", b);
    COSMETIC_SET("HUD.AButton", a);
    COSMETIC_SET("HUD.StartButton", a);
    COSMETIC_SET("HUD.CButtons", c);
    COSMETIC_SET("HUD.Dpad", kWhite);

    // The four individual C-button *color* options stay cleared so they inherit HUD.CButtons.
    // Their *position* CVars are separate and read independently - that is what lets slice 3 split
    // Zelda's C buttons across two corners without touching color.
    COSMETIC_CLEAR("HUD.CUpButton");
    COSMETIC_CLEAR("HUD.CDownButton");
    COSMETIC_CLEAR("HUD.CLeftButton");
    COSMETIC_CLEAR("HUD.CRightButton");
}

// ── HUD layout ──────────────────────────────────────────────────────────────────────────────
/**
 * Each sage rearranges the HUD. All of it goes through SoH's existing anchor system - PosType
 * picks an edge to measure from, PosX/PosY give the offset - so there is no new drawing code here,
 * only CVars.
 *
 * Three things about that system are not obvious and all three cost a wrong guess:
 *
 *  1. **The life meter is driven by `HUD.HeartsCount`, not `HUD.Hearts`.** The latter is color and
 *     row length only. Worse, the two are mixed within one element: position reads
 *     `HUD.HeartsCount.PosType/PosX/PosY` while the margin toggle reads `HUD.Hearts.UseMargins`
 *     (z_lifemeter.c:340-378).
 *
 *  2. **Hearts have a +70 baked into their X.** `getHealthMeterXOffset` returns `PosX + 70` under
 *     every anchor mode, so PosX = -70 is flush against the left edge, not 0. The editor's own
 *     slider bottoms out at -125, which is how you can tell negative values are the intended
 *     domain rather than a hack.
 *
 *  3. **`ANCHOR_TO_LIFE_METER` (5) is magic-bar-only.** `getHealthMeterXOffset` handles PosTypes
 *     0-4 and falls off the end of the function with no return for 5. The editor never offers it
 *     for hearts so nobody has hit it, but we write these CVars directly and could. Never set 5 on
 *     anything but the magic bar.
 *
 * X values below are in the 320x240 virtual HUD space. Under ANCHOR_LEFT/ANCHOR_RIGHT they are
 * measured from that edge and stay put as the window widens; under ANCHOR_NONE they are absolute,
 * and because the projection is centred, X = 160 is the true horizontal centre at *every* aspect
 * ratio - which is what makes the centred layouts below work in widescreen. Y is always absolute.
 *
 * These are starting values. They are meant to be tuned by eye in game, which is exactly why they
 * are a flat table rather than something clever.
 */
struct HudPos {
    int32_t type;
    int32_t x;
    int32_t y;
};

// The vanilla item-button cluster's X positions, measured from the right edge. Reused verbatim
// wherever a sage keeps the buttons on the right, so "same order" costs nothing to preserve.
constexpr int32_t kRightB = 160;
constexpr int32_t kRightA = 186;
constexpr int32_t kRightCLeft = 227;
constexpr int32_t kRightCDown = 249;
constexpr int32_t kRightCUp = 254;
constexpr int32_t kRightCRight = 271;
constexpr int32_t kRightDpad = 271;

// The same cluster translated to hug the left edge, preserving the internal left-to-right order so
// C-left stays left of C-right. Mirroring instead would have swapped them, which reads as a bug
// when the labels imply direction.
constexpr int32_t kLeftB = 10;
constexpr int32_t kLeftA = 36;
constexpr int32_t kLeftCLeft = 77;
constexpr int32_t kLeftCDown = 99;
constexpr int32_t kLeftCUp = 104;
constexpr int32_t kLeftCRight = 121;
constexpr int32_t kLeftDpad = 121;

// Ruto's D-pad, moved off kLeftDpad (2026-08-01, requested): kLeftDpad shares C-right's X, which is
// the same "D-pad stacked onto the C-buttons" look Rauru's flip fixed. This gives Ruto's D-pad its
// own column near the A/B buttons on the far left instead. Not folded into kLeftDpad itself because
// Nabooru still used that constant at the time.
constexpr int32_t kRutoDpadX = 8;

// Ruto's buttons needed to move up to clear the rupee counter (2026-08-01, requested), but the Y
// values (kBottomB etc.) are shared with Saria, who didn't ask for this and was reported as "nearly
// there" the same round - moving the shared constant would have re-broken her. Applied as a
// per-element offset in Ruto's row instead of a new set of six constants.
constexpr int32_t kRutoButtonLift = 15;

// Nabooru's D-pad, same fix as Ruto's above but a round later - her post-redesign D-pad reused
// kLeftDpad (=kLeftCRight) again, same "stacked with C-buttons" bug, reported as "put the dpad on
// the left side" (it was technically ANCHOR_LEFT already, just visually glued to the C-cluster
// rather than reading as its own far-left column).
constexpr int32_t kNabooruDpadX = 8;

// Rauru's own arrangement: Impa's top-right cluster reflected about the screen's vertical centre,
// so the vanilla left-to-right reading order reverses (D-pad and C-buttons at the far left, A/B
// nearest the hearts instead of nearest the edge).
//
// Fourth playtest (2026-08-01): the previous derivation used `u = 320 - v` (v = Impa's ANCHOR_RIGHT
// value) and rendered wrong - A and B drew as two overlapping circles and the whole cluster sat ~30
// units right of where it should, leaving a gap between it and the D-pad. That formula mirrors each
// element's *anchor point*, but an anchor point is not the element: what has to be reflected is the
// on-screen box, and no two of these elements place their box at the anchor the same way. From the
// draw code, each renders at [P + a, P + a + w] where P is the resolved position:
//
//   B        a=0   w=30   plain rect, gButtonBackgroundTex at 32 * 0.95 (z_parameter.c:4164)
//   A        a=8   w=29   matrix draw: Matrix_Translate(-137 + P), quad -15..+14 (z_parameter.c:4965,
//                         Interface_InitVertices) - model x maps to virtual x + 160, hence the +8
//   C-l/d/r  a=0   w=27   plain rects, R_ITEM_BTN_WIDTH(1..3) (z_construct.c:569-572)
//   C-up     a=-7  w=32   the Navi *label* is the widest part, drawn at P - LabelX_Navi(7) and 32
//                         wide; the button icon itself is only P..P+16 (z_parameter.c:4248-4263)
//   D-pad    a=0   w=32   plain rect, gDPadTex (z_parameter.c:5632)
//
// Reflecting the box rather than the anchor gives `u = 320 - v - 2a - w`, which reproduces every
// one of Impa's spans exactly, measured from the opposite edge:
//   B    160 ->130 (130..160)   A     186 ->89 (97..126)   C-up   254 ->48 (41..73)
//   C-l  227 ->66  (66..93)     C-d   249 ->44 (44..71)    C-r    271 ->22 (22..49)
//   Dpad 271 ->17  (17..49)
// The old A(142..171)/B(160..190) overlap disappears because A's a=8 is now accounted for on both
// sides of the reflection instead of shifting A 16 units into B.
//
// A literal reflection swaps left and right, though, which would put C-right left of C-left - the
// same bug fixed two rounds ago. So C-left and C-right's *derived* values are swapped back once
// more, same reasoning as before: their names carry direction, so their relative order must survive
// even though every other element's does get mirrored.
constexpr int32_t kRauruDpadX = 17;
constexpr int32_t kRauruCLeftX = 22;
constexpr int32_t kRauruCDownX = 44;
constexpr int32_t kRauruCUpX = 48;
constexpr int32_t kRauruCRightX = 66;
constexpr int32_t kRauruAX = 89;
constexpr int32_t kRauruBX = 130;

// Vanilla cluster Y, and the same shifted to the bottom of the screen.
constexpr int32_t kTopB = 17, kTopA = 9, kTopCLeft = 18, kTopCDown = 34, kTopCUp = 16, kTopCRight = 18;
// Shifted down another 15 (2026-08-01, requested for Saria - "plenty of unused space" below them).
// Shared with Ruto, which wasn't the one asked about, but the request was about empty space
// specifically at the bottom of the screen, which applies equally to both.
constexpr int32_t kBottomB = 187, kBottomA = 179, kBottomCLeft = 188, kBottomCDown = 204, kBottomCUp = 186,
                  kBottomCRight = 188;
// Impa and Rauru stack hearts, magic, buttons and D-pad in one corner, so their buttons drop below
// the meters rather than sitting on top of them.
//
// Found in playtest (2026-08-01): the old values (B=62, A=54, CUp=61, CLeft=63, CRight=63, CDown=79)
// didn't actually clear the magic bar. ANCHOR_TO_LIFE_METER only knows about the hearts' own row
// height (getHealthMeterYOffset), not where any button is placed, so nothing kept them apart. Pushed
// down once to clear it, then pushed back up once more on request ("bunch everything up").
//
// Reverted (2026-08-01): a further push-up round and the HUD scale halving that went with it were
// both rolled back on request ("it was better before" / "undo the 50% size reduction") - back to
// the values from the bunch-up round, computed against the *default* HUD.HeartsCount.Scale (0.7),
// not the halved 0.35 the third round assumed.
//
// Pushed up again (2026-08-01, "again I think we can push everything up") - both sages asked for
// this in the same round they asked for the D-pad to come back down, so the two moved apart
// slightly rather than staying locked together like the earlier rounds.
constexpr int32_t kStackedA = 56, kStackedCUp = 63, kStackedB = 64, kStackedCLeft = 65, kStackedCRight = 65,
                  kStackedCDown = 81;

// Rauru and Impa's magic PosY, and their shared D-pad Y.
// Magic PosY -16: R_MAGIC_BAR_SMALL_Y(34) - 2 - 16 + getHealthMeterYOffset(8 + 0.7*15 = 18.5) = 34.5,
// bottom edge ~50.5, clear of kStackedA(56) above.
//
// D-pad Y unified (2026-08-01, requested): Rauru previously had its own separate D-pad Y; Impa's
// was reported as "in the right place" at the time, so Rauru took Impa's value instead of tracking
// its own. Both then asked for the D-pad to come down again this round (80 -> 100) - kept shared.
constexpr int32_t kStackedMagicPosY = -16;
constexpr int32_t kImpaRauruDpadY = 100;

// Impa and Rauru's hearts, pushed up from 8 (2026-08-01, requested: "the hearts are a bit too far
// down... maybe there's a margin in the way"). It wasn't a margin - UseMargins is explicitly 0 for
// this element in ApplyHudLayout. It's that the hearts carry a baked-in vertical offset of their
// own, the mirror of the +70 baked into their X: HealthMeter_Draw positions each heart with
// `(-94 + offsetY) * -1` against a frame whose centre is 120 (z_lifemeter.c:626), so the drawn
// centre lands at PosY + 26, not PosY. PosY = 8 therefore drew them at 34 with the row's top edge
// at ~26 - a quarter of the way down the HUD's 240-unit height before the first pixel of heart.
// -10 puts the centre at 16 and the top edge at ~8.5, a real top margin instead of a hidden one.
//
// The magic bar follows automatically (ANCHOR_TO_LIFE_METER reads getHealthMeterYOffset, which is
// this value + one row's height), so the meter pair moves as a block and their spacing is unchanged.
// The buttons below deliberately do NOT follow - only the hearts were reported as sitting too low.
constexpr int32_t kStackedHeartsY = -10;

// Hearts: -62 sits just off the left edge once the +70 is applied; 130 hugs the right, allowing
// ~110px for a ten-heart row.
constexpr int32_t kHeartsLeftX = -62;
constexpr int32_t kHeartsRightX = 130;
// Zelda only. Changed from 35 (2026-08-01, requested): rather than centre the *current* few hearts
// (which drifts as the row grows toward its 10-heart max), align the row's left edge with the magic
// bar's left edge instead - a fixed reference that doesn't get "better" or "worse" centred as hearts
// are gained. The +70 baked into getHealthMeterXOffset means the hearts' actual drawn start is
// kHeartsCentreX+70, so to match kMagicCentreX(96): 96-70 = 26.
constexpr int32_t kHeartsCentreX = 26;

// The bar is a start cap + a magicCapacity-wide fill + an end cap (z_parameter.c:3554-3564), so its
// on-screen width is `magicCapacity + 16`, not a fixed size. Single magic (capacity 48) is 64px wide
// and centres at X=128; double magic (capacity 96) is 112px wide and centres at X=104. One static
// PosX can't be exact for both. Nudged further left after the first in-game look (2026-08-01) - still
// live-tunable, see docs/sage-cosmetics.md section 9.
constexpr int32_t kMagicCentreX = 96;

// ANCHOR_LEFT only. Bug found in playtest (2026-08-01): ANCHOR_LEFT and ANCHOR_RIGHT are NOT mirror
// images of each other the way every other sage's kLeft*/kRight* pairs (which use vanilla-derived
// values) made it look. OTRGlobals.cpp:2198-2204 -
//   ANCHOR_LEFT:  screenX = PosX + (160 - 120*aspect)   - PosX grows RIGHTWARD from the left edge.
//   ANCHOR_RIGHT: screenX = PosX + (120*aspect - 160)   - PosX ALSO grows rightward, just from a
//                                                          different (rightward-shifted) baseline.
// A small PosX under ANCHOR_RIGHT does NOT hug the right edge - it lands near screen centre (worked
// out from the formula: 26 + 120*aspect - 160 is ~146 at a 16:9-ish aspect, i.e. just left of centre
// X=160). Only a *large* PosX (matching the vanilla-derived kRightB/kRightA/kRightCRight values used
// elsewhere) actually sits near the right edge. This constant is correct for the three ANCHOR_LEFT
// elements below (C-up, C-left were reported fine); the three ANCHOR_RIGHT elements need
// kZeldaRightCornerX instead.
constexpr int32_t kZeldaCornerX = 26;

// The ANCHOR_RIGHT counterpart - reuses the same value already proven to hug the right edge
// elsewhere (kRightCRight/kRightDpad).
constexpr int32_t kZeldaRightCornerX = 271;

// Darunia's magic bar is centred at the *top*, which is also where the vanilla B button lives
// (X 160). True top-centre and an untouched B button cannot both have that space, so the bar drops
// below the button row instead of beside it. This is a softer version of the fallback the spec
// anticipated ("if magic can't go in top centre... anchor beneath it") - it stays centred and
// clearly top-of-screen, rather than being demoted to hanging off the hearts like everyone else's.
constexpr int32_t kMagicTopCentreY = 50;

// D-pad above the bottom button cluster rather than below it: below would put its lower two icons
// past the bottom edge, since the D-pad's own icon offsets spread -8..+24 from this value. Nudged
// down from 120 (2026-08-01, requested for Ruto). Only Ruto uses this now - Nabooru's redesign
// (mirror of Darunia) dropped its use of it.
constexpr int32_t kBottomDpadAbove = 135;

// Ruto's minimap, lifted off vanilla (2026-08-01, requested): with Ruto's hearts moved to Y=195
// (custom, lower than vanilla's own hearts position), the still-vanilla-positioned overworld map
// (base Y 164, tall enough to reach into the Y 195+ band) started overlapping them. PosY=-40 lifts
// it clear without needing the full -140 push Saria's top-right placement uses.
constexpr HudPos kMinimapRutoLift = { ANCHOR_RIGHT, 0, -40 };

struct SageHudLayout {
    uint8_t sage;
    HudPos hearts;
    HudPos magic;
    HudPos bButton;
    HudPos aButton;
    HudPos cUp;
    HudPos cDown;
    HudPos cLeft;
    HudPos cRight;
    HudPos dpad;
    // The map overlay (HUD.Minimap) - two separate draw paths read it: the dungeon room-map
    // (z_map_exp.c:779-888) AND a full overworld map covering nearly every outdoor scene
    // (z_map_exp.c:890-1030, missed on first read - it's not dungeon-only the way the first pass
    // through this file suggested). Left at ORIGINAL_LOCATION for every sage except where requested.
    HudPos minimap;
    // Added 2026-08-01 (requested): no sage set HUD.StartButton at all before this, so it sat
    // wherever the last-loaded config left it. Every row below points it at that sage's own cUp
    // position - Start (the pause menu) is never on screen at the same time as gameplay HUD/Navi,
    // so reusing C-up's slot can't collide with anything.
    HudPos startButton;
};

// Bug found in playtest (2026-08-01): {ANCHOR_RIGHT, 0, 0} reproduced the vanilla position exactly,
// which read as "the map hasn't moved." Both draw paths ADD PosX/PosY to a hardcoded vanilla base
// (R_DGN_MINIMAP_X/Y = 204/140, R_OW_MINIMAP_X/Y = 238/164 - z_construct.c:439-479) instead of
// treating them as an absolute/edge-relative position the way every other HUD element does, so
// PosY=0 leaves it at Y=140-164 (still low), not Y=0. PosY=-140 cancels most of that (dungeon lands
// at Y=0, overworld at Y=24, close enough given the two bases are only 24 apart). Left PosX at 0 -
// both bases already put it right-of-centre once ANCHOR_RIGHT's real formula is worked through (see
// kZeldaRightCornerX's comment for that derivation), so X didn't look like the broken half of this.
// -140 confirmed correct in direction (playtest 2026-08-01) but too far - it now sits flush against
// the very top with no margin at all. Eased back to -110 (map now sits ~30 lower, i.e. with room
// above it) per request.
constexpr HudPos kMinimapTopRight = { ANCHOR_RIGHT, 0, -110 };
constexpr HudPos kMinimapOriginal = { ORIGINAL_LOCATION, 0, 0 };

// Bug found in playtest (2026-08-01): giving Start the exact same PosX/PosY as C-up still rendered
// it visibly off, for every sage that had it set explicitly. Root cause found in
// z_parameter.c:3946-3960 - unlike every other button, the Start button's own draw code subtracts
// `(Start_BTN_Scale * 13)` from BOTH PosX and PosY before positioning it, with no equivalent
// subtraction on the C-up side. At the default Start_BTN_Scale (0.75, since ApplyHudScale's halving
// is reverted - see section 12), that's 0.75*13 = 9.75, rounded to 10. The offsets below carry that
// +10 as their baseline, so Start lands on C-up rather than ~10px short on both axes.
//
// Also fixed: Darunia and Zelda had Start at ORIGINAL_LOCATION to "match" their own ORIGINAL_LOCATION
// C-up - but ORIGINAL_LOCATION means "this element's own hardcoded vanilla spot", which is NOT the
// same screen position as another element's ORIGINAL_LOCATION spot. Start's vanilla spot is
// nowhere near C-up's (confirmed in playtest - "the start menu is all the way over here"). Both now
// get C-up's real vanilla coordinates (kRightCUp/kTopCUp) explicitly, plus the offset above.
//
// Split into separate X and Y offsets (2026-08-01, requested: "a smidge to the left and pushed up...
// I guess aligning it with C-up is not as easy as I thought"). Landing Start *on* C-up turned out to
// be the wrong target: the two elements are not the same size. The button icon is 24 wide
// (32 * 0.75) and does line up with C-up's 16-wide icon under the old symmetric +10, but Start also
// draws a "Return"/"Save" action label - 49 x 16, centred on the icon (z_parameter.c:4218-4228 with
// actionVtx[4..7]) - and that label is what the eye actually reads. It is three times C-up's width,
// so aligning the icons buries the label in the middle of the C-button row, which is what showed up
// in game.
//
// So the offsets below are aimed at the label, not the icon: 10 units left and 16 up from the old
// value. For a vanilla-position C-cluster that puts the label's box at y ~3.5-19.5, clearing the
// C-button row (which starts at y 18) instead of sitting across it, and shifts it off C-right's
// column.
//
// One offset serves all seven sages because the cluster's internal geometry is identical for all
// seven: kStackedA/CUp/B/CLeft/CRight/CDown are exactly kTop* + 47, so Rauru and Impa's cluster is
// the vanilla cluster translated bodily down the screen, not a tighter one. Start lands the same
// distance from its C-up everywhere, and the residual overlap (2.75 units into the A button, 3.75
// into C-up) is the same for every sage. If that ever stops being true, this offset stops being
// one-size-fits-all - it is only safe while the kStacked*/kTop* delta stays uniform.
constexpr int32_t kStartOffsetX = 0;
constexpr int32_t kStartOffsetY = -6;
constexpr HudPos kRauruStartButton = { ANCHOR_LEFT, kRauruCUpX + kStartOffsetX, kStackedCUp + kStartOffsetY };
constexpr HudPos kSariaStartButton = { ANCHOR_RIGHT, kRightCUp + kStartOffsetX, kBottomCUp + kStartOffsetY };
constexpr HudPos kDaruniaStartButton = { ANCHOR_RIGHT, kRightCUp + kStartOffsetX, kTopCUp + kStartOffsetY };
// Ruto's own Start value is computed inline in its row instead of here, since it has to account
// for kRutoButtonLift (C-up's Y there is kBottomCUp - kRutoButtonLift, not kBottomCUp itself).
constexpr HudPos kImpaStartButton = { ANCHOR_RIGHT, kRightCUp + kStartOffsetX, kStackedCUp + kStartOffsetY };

constexpr SageHudLayout kSageHudLayouts[] = {
    // Rauru - everything top-left, horizontally reversed (2026-08-01): D-pad alone at the far edge,
    // then the C-buttons, then A/B closest to the hearts - see kRauru* above for why.
    { RO_SAGE_RAURU,
      { ANCHOR_LEFT, kHeartsLeftX, kStackedHeartsY },
      { ANCHOR_TO_LIFE_METER, 0, kStackedMagicPosY },
      { ANCHOR_LEFT, kRauruBX, kStackedB },
      { ANCHOR_LEFT, kRauruAX, kStackedA },
      { ANCHOR_LEFT, kRauruCUpX, kStackedCUp },
      { ANCHOR_LEFT, kRauruCDownX, kStackedCDown },
      { ANCHOR_LEFT, kRauruCLeftX, kStackedCLeft },
      { ANCHOR_LEFT, kRauruCRightX, kStackedCRight },
      { ANCHOR_LEFT, kRauruDpadX, kImpaRauruDpadY },
      kMinimapOriginal,
      kRauruStartButton },
    // Saria - hearts bottom-left with the magic bar hung off them, buttons bottom-right, D-pad
    // sitting just above the buttons rather than below as in vanilla. Minimap moved to top-right
    // (2026-08-01, requested) - nothing else in this layout occupies that corner.
    //
    // Magic PosY fixed (2026-08-01) - was rendering off-screen. ANCHOR_TO_LIFE_METER only knows how
    // to add PosY *below* the hearts; with hearts bottom-anchored at Y=200, the old PosY=14 computed
    // to Y=256.5 (with HeartsScale halved to 0.35 - see ApplyHudScale), past the 240-tall screen
    // entirely. A large negative PosY is needed to put the bar above the hearts instead: target
    // ~Y170, so PosY = 170 - (32 + 200 + 0.35*15) = 170 - 237.25 = -67.
    { RO_SAGE_SARIA,
      { ANCHOR_LEFT, kHeartsLeftX, 200 },
      // Magic and D-pad both nudged down a bit (2026-08-01, requested) - magic PosY -67 -> -50
      // (bar Y ~175.5 -> ~192.5, still ~7.5px clear of the hearts at 200), D-pad 130 -> 145.
      { ANCHOR_TO_LIFE_METER, 0, -50 },
      { ANCHOR_RIGHT, kRightB, kBottomB },
      { ANCHOR_RIGHT, kRightA, kBottomA },
      { ANCHOR_RIGHT, kRightCUp, kBottomCUp },
      { ANCHOR_RIGHT, kRightCDown, kBottomCDown },
      { ANCHOR_RIGHT, kRightCLeft, kBottomCLeft },
      { ANCHOR_RIGHT, kRightCRight, kBottomCRight },
      { ANCHOR_RIGHT, kRightDpad, 145 },
      kMinimapTopRight,
      kSariaStartButton },
    // Darunia - hearts top-left, magic centred at the top, buttons and D-pad left alone.
    { RO_SAGE_DARUNIA,
      { ANCHOR_LEFT, kHeartsLeftX, 8 },
      { ANCHOR_NONE, kMagicCentreX, kMagicTopCentreY },
      { ORIGINAL_LOCATION, 0, 0 },
      { ORIGINAL_LOCATION, 0, 0 },
      { ORIGINAL_LOCATION, 0, 0 },
      { ORIGINAL_LOCATION, 0, 0 },
      { ORIGINAL_LOCATION, 0, 0 },
      { ORIGINAL_LOCATION, 0, 0 },
      { ORIGINAL_LOCATION, 0, 0 },
      kMinimapOriginal,
      kDaruniaStartButton },
    // Ruto - meters bottom-right, buttons and D-pad bottom-left. D-pad pulled further left
    // (2026-08-01, requested) - see kRutoDpadX above for why it's not just kLeftDpad. D-pad Y also
    // nudged down (kBottomDpadAbove), and minimap lifted off vanilla to clear the hearts (both
    // requested 2026-08-01, see kMinimapRutoLift above).
    //
    // Magic PosY fixed for the same off-screen bug as Saria's, worked out separately since Ruto's
    // hearts sit at a slightly different Y (195, not 200): target ~Y165,
    // PosY = 165 - (32 + 195 + 0.35*15) = 165 - 232.25 = -67.
    { RO_SAGE_RUTO,
      { ANCHOR_RIGHT, kHeartsRightX, 195 },
      { ANCHOR_TO_LIFE_METER, 0, -67 },
      { ANCHOR_LEFT, kLeftB, kBottomB - kRutoButtonLift },
      { ANCHOR_LEFT, kLeftA, kBottomA - kRutoButtonLift },
      { ANCHOR_LEFT, kLeftCUp, kBottomCUp - kRutoButtonLift },
      { ANCHOR_LEFT, kLeftCDown, kBottomCDown - kRutoButtonLift },
      { ANCHOR_LEFT, kLeftCLeft, kBottomCLeft - kRutoButtonLift },
      { ANCHOR_LEFT, kLeftCRight, kBottomCRight - kRutoButtonLift },
      { ANCHOR_LEFT, kRutoDpadX, kBottomDpadAbove },
      kMinimapRutoLift,
      { ANCHOR_LEFT, kLeftCUp + kStartOffsetX, kBottomCUp - kRutoButtonLift + kStartOffsetY } },
    // Impa - everything top-right. The most crowded layout of the seven; expect this one to need
    // the most tuning.
    { RO_SAGE_IMPA,
      { ANCHOR_RIGHT, kHeartsRightX, kStackedHeartsY },
      { ANCHOR_TO_LIFE_METER, 0, kStackedMagicPosY },
      { ANCHOR_RIGHT, kRightB, kStackedB },
      { ANCHOR_RIGHT, kRightA, kStackedA },
      { ANCHOR_RIGHT, kRightCUp, kStackedCUp },
      { ANCHOR_RIGHT, kRightCDown, kStackedCDown },
      { ANCHOR_RIGHT, kRightCLeft, kStackedCLeft },
      { ANCHOR_RIGHT, kRightCRight, kStackedCRight },
      // Reported as "in the right place" - Rauru's D-pad now shares this same value instead of
      // tracking its own (kImpaRauruDpadY).
      { ANCHOR_RIGHT, kRightDpad, kImpaRauruDpadY },
      kMinimapOriginal,
      kImpaStartButton },
    // Nabooru - rebuilt as a mirror of Darunia (2026-08-01), replacing the old bottom-right/
    // bottom-left layout that turned out to be an accidental duplicate of Ruto's. Darunia is
    // hearts-left/magic-top-centre/buttons-vanilla-right, so mirroring it means hearts-right (custom
    // X, same Y as Darunia), magic stays centred (mirroring a centred X doesn't move it), and the
    // vanilla-right buttons flip to vanilla-left - kLeft* for X (preserves internal L-R order, same
    // fix as Rauru's C-buttons above) paired with kTop* for Y (the vanilla Y values themselves,
    // matching Darunia's untouched ORIGINAL_LOCATION Y instead of the bottom-of-screen kBottom* set).
    { RO_SAGE_NABOORU,
      { ANCHOR_RIGHT, kHeartsRightX, 8 },
      { ANCHOR_NONE, kMagicCentreX, kMagicTopCentreY },
      { ANCHOR_LEFT, kLeftB, kTopB },
      { ANCHOR_LEFT, kLeftA, kTopA },
      { ANCHOR_LEFT, kLeftCUp, kTopCUp },
      { ANCHOR_LEFT, kLeftCDown, kTopCDown },
      { ANCHOR_LEFT, kLeftCLeft, kTopCLeft },
      { ANCHOR_LEFT, kLeftCRight, kTopCRight },
      // Darunia's D-pad is ORIGINAL_LOCATION with no tracked X/Y of its own, so there's no exact
      // vanilla Y to mirror here - 55 is the vanilla D-pad Y quoted in section 9 of the docs.
      // Reported as fine, so only the X changed (kLeftDpad -> kNabooruDpadX, see that constant).
      { ANCHOR_LEFT, kNabooruDpadX, 55 },
      kMinimapOriginal,
      { ANCHOR_LEFT, kLeftCUp + kStartOffsetX, kTopCUp + kStartOffsetY } },
    // Zelda - the most scattered layout: meters centred top and bottom, A and B in the lower
    // corners.
    //
    // C-buttons and D-pad reverted to ORIGINAL_LOCATION (2026-08-01, requested) - the split-corners
    // design (C-up/C-left top-left, C-down/C-right top-right) was reported as still messy even after
    // the ANCHOR_RIGHT fix below, so this drops back to vanilla's own default C-button/D-pad
    // placement rather than continuing to hand-tune four more positions.
    //
    // (Kept for history: B, C-down and C-right - the three ANCHOR_RIGHT elements - were using
    // kZeldaCornerX, which is only correct for ANCHOR_LEFT; under ANCHOR_RIGHT that value put them
    // near screen *centre*, not the right edge. See kZeldaRightCornerX's comment above for the
    // formula. Still relevant for B, which keeps its custom position below.)
    //
    // A/B: Y 200 -> 175 -> 150 -> 165 - 150 turned out to be too high, 165 is the new middle ground.
    // Hearts: kHeartsCentreX changed to align the heart row's left edge with the magic bar's, so the
    // "heart block" is anchored to something that doesn't shift as the row grows - see that
    // constant's comment above.
    { RO_SAGE_ZELDA,
      { ANCHOR_NONE, kHeartsCentreX, 10 },
      { ANCHOR_NONE, kMagicCentreX, 220 },
      { ANCHOR_RIGHT, kZeldaRightCornerX, 165 },
      { ANCHOR_LEFT, kZeldaCornerX, 165 },
      { ORIGINAL_LOCATION, 0, 0 },
      { ORIGINAL_LOCATION, 0, 0 },
      { ORIGINAL_LOCATION, 0, 0 },
      { ORIGINAL_LOCATION, 0, 0 },
      { ORIGINAL_LOCATION, 0, 0 },
      kMinimapOriginal,
      { ANCHOR_RIGHT, kRightCUp + kStartOffsetX, kTopCUp + kStartOffsetY } },
};

const SageHudLayout* FindSageHudLayout(uint8_t sage) {
    for (const SageHudLayout& layout : kSageHudLayouts) {
        if (layout.sage == sage) {
            return &layout;
        }
    }
    return nullptr;
}

void SetHudPos(const char* baseCvar, const HudPos& pos) {
    std::string posType = std::string(baseCvar) + ".PosType";
    std::string posX = std::string(baseCvar) + ".PosX";
    std::string posY = std::string(baseCvar) + ".PosY";

    CVarSetInteger(posType.c_str(), pos.type);
    if (pos.type == ORIGINAL_LOCATION) {
        // Offsets are ignored in this mode, but a stale value left behind by the previously loaded
        // sage would resurface the moment anything switched the mode back.
        CVarClear(posX.c_str());
        CVarClear(posY.c_str());
    } else {
        CVarSetInteger(posX.c_str(), pos.x);
        CVarSetInteger(posY.c_str(), pos.y);
    }
}

void ApplyHudLayout(const SageHudLayout& layout) {
    // Position and margins are split across two different CVar roots for the life meter:
    // HUD.HeartsCount for position, HUD.Hearts for margins. Not a typo.
    SetHudPos(CVAR_COSMETIC("HUD.HeartsCount"), layout.hearts);
    CVarSetInteger(CVAR_COSMETIC("HUD.Hearts.UseMargins"), 0);

    SetHudPos(CVAR_COSMETIC("HUD.MagicBar"), layout.magic);
    SetHudPos(CVAR_COSMETIC("HUD.BButton"), layout.bButton);
    SetHudPos(CVAR_COSMETIC("HUD.AButton"), layout.aButton);
    SetHudPos(CVAR_COSMETIC("HUD.CUpButton"), layout.cUp);
    SetHudPos(CVAR_COSMETIC("HUD.CDownButton"), layout.cDown);
    SetHudPos(CVAR_COSMETIC("HUD.CLeftButton"), layout.cLeft);
    SetHudPos(CVAR_COSMETIC("HUD.CRightButton"), layout.cRight);
    SetHudPos(CVAR_COSMETIC("HUD.Dpad"), layout.dpad);
    SetHudPos(CVAR_COSMETIC("HUD.Minimap"), layout.minimap);
    SetHudPos(CVAR_COSMETIC("HUD.StartButton"), layout.startButton);

    // Every element above is positioned explicitly, so the global margin offsets must not also be
    // applied on top - they would drag everything off by the margin amount.
    for (const char* cvar : { CVAR_COSMETIC("HUD.MagicBar"), CVAR_COSMETIC("HUD.BButton"),
                              CVAR_COSMETIC("HUD.AButton"), CVAR_COSMETIC("HUD.CUpButton"),
                              CVAR_COSMETIC("HUD.CDownButton"), CVAR_COSMETIC("HUD.CLeftButton"),
                              CVAR_COSMETIC("HUD.CRightButton"), CVAR_COSMETIC("HUD.Dpad"),
                              CVAR_COSMETIC("HUD.Minimap"), CVAR_COSMETIC("HUD.StartButton") }) {
        CVarSetInteger((std::string(cvar) + ".UseMargins").c_str(), 0);
    }
}

// Halves every HUD button/hearts element that has a Scale CVar (requested 2026-08-01). Applied
// unconditionally, not per-sage - it's a global "everything smaller" preference, not part of any
// sage's identity. Two elements have NO Scale CVar anywhere in this codebase and can't be resized
// this way: HUD.AButton and HUD.Dpad. That's an engine gap, not something fixable from cosmetics -
// flagging it rather than silently leaving them full-size with no explanation.
// Reverted (2026-08-01, requested: "undo the 50% size reduction until we figure out all the
// layouts"). This still runs every load so any 0.475/0.435/etc value already written to a save's
// shipofharkinian.json from testing gets explicitly cleared back to the engine's own default rather
// than silently left stale - a plain CVarClear is what actually undoes a persisted CVar; simply
// removing the call site would not have.
//
// Left disabled rather than deleted: the halved values are commented out below so re-enabling this
// is a one-line uncomment once the layouts themselves are settled, not a re-derivation.
void ApplyHudScale() {
    CVarClear(CVAR_COSMETIC("HUD.BButton.Scale"));
    CVarClear(CVAR_COSMETIC("HUD.StartButton.Scale"));
    CVarClear(CVAR_COSMETIC("HUD.CLeftButton.Scale"));
    CVarClear(CVAR_COSMETIC("HUD.CRightButton.Scale"));
    CVarClear(CVAR_COSMETIC("HUD.CUpButton.Scale"));
    CVarClear(CVAR_COSMETIC("HUD.CDownButton.Scale"));
    CVarClear(CVAR_COSMETIC("HUD.HeartsCount.Scale"));

    // CVarSetFloat(CVAR_COSMETIC("HUD.BButton.Scale"), 0.475f);       // default 0.95
    // CVarSetFloat(CVAR_COSMETIC("HUD.StartButton.Scale"), 0.375f);   // default 0.75
    // CVarSetFloat(CVAR_COSMETIC("HUD.CLeftButton.Scale"), 0.435f);   // default 0.87
    // CVarSetFloat(CVAR_COSMETIC("HUD.CRightButton.Scale"), 0.435f);  // default 0.87
    // CVarSetFloat(CVAR_COSMETIC("HUD.CUpButton.Scale"), 0.25f);      // default 0.5
    // CVarSetFloat(CVAR_COSMETIC("HUD.CDownButton.Scale"), 0.435f);   // default 0.87
    // CVarSetFloat(CVAR_COSMETIC("HUD.HeartsCount.Scale"), 0.35f);    // default 0.7
}

// World state, identical for every sage: the Kokiri mourn Link, the Gerudo honour Nabooru's
// ascension. Applied here rather than in the preset's cosmetics block (as docs originally
// suggested) because a preset only takes effect when the player explicitly applies it, whereas
// this needs to be true in every run without ceremony.
void ApplyWorldNpcColors() {
    COSMETIC_SET("NPC.Kokiri", kKokiriMourning);
    COSMETIC_SET("NPC.Gerudo", kGerudoGold);
}

} // namespace

extern "C" void GanonsCurse_ApplySageCosmetics() {
    if (!IS_RANDO || !SageCosmeticsEnabled()) {
        return;
    }

    // -1 means no sage override applies to this file. Reusing the home-entrance accessor as the
    // "is there a sage at all" probe avoids a second copy of "which sages exist" gating the entry
    // point, and it stays correct if the sage list ever changes.
    if (Randomizer_GetSageHomeEntrance() == -1) {
        return;
    }

    const SagePalette* palette = FindSagePalette(Randomizer_GetSettingValue(RSK_SELECTED_SAGE));
    if (palette == nullptr) {
        return;
    }

    ApplyTunics(*palette);
    ApplySword(*palette);
    ApplyHeartsAndMagic(*palette);
    ApplySpells(*palette);
    ApplyHudButtons(*palette);
    ApplyWorldNpcColors();

    // Layout is a separate table from the palette: it is pure geometry, has no relationship to the
    // sage's colors, and is the part most likely to be retuned by eye.
    const SageHudLayout* layout = FindSageHudLayout(palette->sage);
    if (layout != nullptr) {
        ApplyHudLayout(*layout);
    }

    // Global, not per-sage: applies regardless of which layout was found above.
    ApplyHudScale();

    // Push display-list-patched options through in one pass. See the header comment for why this
    // cannot be left to CosmeticsUpdateTick: its per-frame call passes manualChange = false, which
    // skips every patch whose rainbow CVar is unset - i.e. all of ours. Several options set above
    // are patch-based (Master Sword blade, Goron/Zora tunic, hearts, magic, Farore's, Gerudo), so
    // without this they would silently not appear.
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
