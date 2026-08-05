/**
 * Seven Sages - Phase 6: Saria's Song as a temporary "climb anything" boost.
 *
 * docs/item-ability-overhaul.md: climb any surface for 20 seconds. Costs 24 magic (the
 * system-wide "songs cost magic" rule) via the shared deferred-request helper
 * (SevenSagesSongMagic.h) built for Song of Time.
 *
 * ── The vanilla Navi prompt chain is suppressed, not used as a gate ─────────────────────────
 * Playing Saria's Song in vanilla makes Navi force a textbox asking "talk to Saria?" and, if
 * declined, "talk to Navi instead?" (z_message_PAL.c sets a negative player->naviTextId, which
 * En_Elf's func_80A052F4/func_80A05208 state chain then walks). An earlier pass treated that
 * chain the way the warp songs treat their Yes/No prompt - buff only on declining both, vanilla
 * option on saying yes - and it was wrong for this song. A warp song's prompt is one question the
 * player is already used to answering; this is two questions between the player and the buff,
 * every single cast, and playtesting 2026-08-05 confirmed it reads as the song nagging rather
 * than doing anything. It is also lore-hostile here: the player *is* a sage, so "do you want to
 * talk to Saria?" is not a question this project can keep asking (docs/lore.md).
 *
 * So the buff is back on the song itself (OnOcarinaSongAction, as originally built and playtested
 * 2026-08-01) and the prompt chain is suppressed outright via VB_NAVI_ASK_TO_TALK_AFTER_SARIAS_SONG.
 * Nothing else about vanilla Saria's Song is touched. Its real reactors all go through
 * OCARINA_ACTION_CHECK_SARIA on the play-for-actor path, never through this prompt: Mido stepping
 * out of the Lost Woods passage (z_en_md.c, gated on SCENE_LOST_WOODS), Darunia's dance in Goron
 * City (z_en_du.c), the Lost Woods Skull Kid (z_en_skj.c), any En_Okarina_Tag spot placed with
 * this song's param, and Gossip Stones (z_en_gs.c, which checks the song id directly).
 *
 * Checked before suppressing, since both branches of the prompt did lead somewhere:
 * - "talk to Navi instead?" -> ElfMessage_GetCUpText, which returns 0 unless the scene loaded a
 *   cUpElfMsgs table, and only two of those exist in the whole game (z_scene.c's sNaviMsgFiles:
 *   Hyrule Field and Inside the Deku Tree), falling back to TEXT_NAVI_TRY_TO_KEEP_MOVING
 *   otherwise. It never carried Phase 4e's "where next" hint either - SevenSagesNaviGuide.cpp
 *   rewrites optional ElfMsg trigger volumes, a different path entirely. Nothing lost.
 * - "talk to Saria?" -> ElfMessage_GetSariaText, which under IS_RANDO with RSK_SARIA_HINT (on in
 *   the Seven Sages seed preset) returns TEXT_SARIAS_SONG_FOREST_SOUNDS for StaticHints.cpp to
 *   rewrite into the seed's Saria hint. That delivery IS given up here; talking to Saria in
 *   person in the Sacred Forest Meadow (TEXT_SARIA_SFM, hooked separately) still gives it.
 *
 * Rather than new climbable-surface logic, this toggles the existing "ClimbEverything" cheat
 * (see Cheats/ClimbEverything.cpp) on for the buff's duration and back off when it expires -
 * that cheat's CVAR_CHEAT("ClimbEverything") already gates every VB hook needed (surface/angle
 * climbability, the climb-vs-door-open disambiguation, ledge-slope handling), so there is
 * nothing left to build there.
 *
 * The prior CVar value is snapshotted before the buff forces it on and restored (not just
 * zeroed) when the buff ends, so a player who already has the cheat on manually from the
 * Enhancements menu doesn't have it silently turned off by this song. Known and accepted edge
 * case: if the player manually toggles the cheat mid-buff, the buff's own end will still
 * restore to whatever the value was at cast time, not the player's later manual change -
 * guarding against that would mean intercepting the menu checkbox itself, out of scope here.
 *
 * Duration is a plain frame countdown, not an absolute target against PlayState::gameplayFrames
 * - see SevenSagesEponasSong.cpp's header comment for why. 400 frames assumes the game's ~20Hz
 * logic tick rate (20 * 20s) - confirm empirically that the buff actually lasts ~20 real
 * seconds, and adjust SARIAS_SONG_BUFF_FRAMES if not.
 *
 * Replaying the song while already buffed refreshes the duration rather than re-snapshotting -
 * re-snapshotting on an already-active buff would capture "1" (our own forced value) as the
 * "prior" value instead of the real one, which would then fail to restore correctly.
 *
 * Bug found live 2026-08-01, testing Saria: setting the CVar alone did nothing - climbing never
 * activated. ClimbEverything.cpp's VB hooks are (re)registered only when something calls
 * ShipInit::Init(CVAR_CHEAT("ClimbEverything")), which is what the Enhancements-menu checkbox
 * does after every toggle (UIWidgets.cpp's CVarCheckbox); a bare CVarSetInteger doesn't trigger
 * that. Fixed by calling ShipInit::Init ourselves right after each CVarSetInteger, both turning
 * the buff on and restoring the prior value when it ends - deliberately not also calling
 * SaveConsoleVariablesNextFrame() the way the menu checkbox does, since this is a transient
 * buff state that should never get written to the user's saved settings.
 */
#include "soh/ShipInit.hpp"
#include "functions.h"
#include "macros.h"
#include "variables.h"
#include "soh/cvar_prefixes.h"
#include "soh/Enhancements/SevenSages/SevenSagesSongMagic.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"

extern "C" PlayState* gPlayState;

namespace {

constexpr s16 SARIAS_SONG_MAGIC_COST = 24;
constexpr s32 SARIAS_SONG_BUFF_FRAMES = 20 * 20;

s32 sBuffFramesRemaining = 0;
s32 sPriorClimbEverythingValue = 0;

void SetClimbEverything(s32 value) {
    CVarSetInteger(CVAR_CHEAT("ClimbEverything"), value);
    ShipInit::Init(CVAR_CHEAT("ClimbEverything"));
}

void SevenSagesSariasSongPlayed() {
    if (!GameInteractor::IsSaveLoaded(true)) {
        return;
    }
    if (gPlayState->msgCtx.lastPlayedSong != OCARINA_SONG_SARIAS) {
        return;
    }

    SevenSagesRequestSongMagic(SARIAS_SONG_MAGIC_COST, []() {
        if (sBuffFramesRemaining <= 0) {
            sPriorClimbEverythingValue = CVarGetInteger(CVAR_CHEAT("ClimbEverything"), 0);
            SetClimbEverything(1);
        }
        sBuffFramesRemaining = SARIAS_SONG_BUFF_FRAMES;
    });
}

void SevenSagesSariasSongFrameUpdate() {
    if (sBuffFramesRemaining <= 0) {
        return;
    }

    sBuffFramesRemaining--;
    if (sBuffFramesRemaining == 0) {
        SetClimbEverything(sPriorClimbEverythingValue);
    }
}

// Skip vanilla's "talk to Saria?" / "talk to Navi instead?" chain entirely - see the header
// comment. Unconditional under IS_RANDO: the buff above fires on every play of the song, so there
// is no branch of the prompt left that still leads anywhere this project wants the player to go.
void SevenSagesSariasSongOnVanillaBehavior(GIVanillaBehavior id, bool* should, va_list originalArgs) {
    if (id != VB_NAVI_ASK_TO_TALK_AFTER_SARIAS_SONG) {
        return;
    }
    *should = false;
}

} // namespace

static void RegisterSevenSagesSariasSong() {
    COND_HOOK(OnOcarinaSongAction, IS_RANDO, SevenSagesSariasSongPlayed);
    COND_HOOK(OnVanillaBehavior, IS_RANDO, SevenSagesSariasSongOnVanillaBehavior);
    COND_HOOK(OnGameFrameUpdate, IS_RANDO, SevenSagesSariasSongFrameUpdate);
}

static RegisterShipInitFunc sevenSagesSariasSongInitFunc(RegisterSevenSagesSariasSong, { "IS_RANDO" });
