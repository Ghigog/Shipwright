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
 * the buff on and restoring the prior value when it ends.
 *
 * ── Ending the buff is four paths, not one (fixed 2026-08-09) ───────────────────────────────
 *
 * The timer expiring used to be the ONLY thing that restored the CVar, which made every other way
 * of ending a buff a way of leaving a cheat switched on permanently. This is a cheat the player
 * also owns from the Enhancements menu, it is global rather than per-save, and nothing in the game
 * would ever turn it back off - so "quit while Saria's Song is up" meant climbing every wall in
 * every file, in this session and every session after it, until the player found the checkbox.
 *
 * All four now route through EndClimbBuff(), which is idempotent so each can fire blind:
 *
 *   timer expiry          the ordinary case, in the frame update
 *   save unloaded         the frame update's own guard - catches quitting to the file select,
 *                         where frames keep running
 *   any file load         RegisterSevenSagesSariasSong(), which ShipInit re-runs on OnLoadGame.
 *                         Necessary on top of the above because loading a non-Seven-Sages file
 *                         UNREGISTERS the frame update - the hook that would otherwise clean up
 *   next boot             the same call at startup, reading the on-disk breadcrumb
 *
 * The last one is why a CVar gets written to disk here despite this being transient buff state.
 * Not calling CVarSave() is not protection: anything else that saves the config while the buff is
 * live (opening the menu and toggling something is enough) writes ClimbEverything=1 out anyway,
 * and then only a record of what it was before can undo it. See CVAR_CLIMB_FORCED below.
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

// Persistent breadcrumb recording that the buff - not the player - is what turned the cheat on,
// and what it was set to beforehand. Stored as prior + 1 so that "no buff active" (absent/0) stays
// distinguishable from "buff active, prior value was 0", which is the overwhelmingly common case.
//
// This exists because the buff drives a CVar the player also owns, and every way of ending a buff
// except the timer used to be a way of leaving it on forever. Restoring on the paths we control
// (below) fixes quitting, loading another file and exiting normally; it cannot fix the process
// dying mid-buff. Only something on disk can, so this goes to disk deliberately and is undone at
// boot. CVAR_GENERAL, so applyPreset's whole-block overwrite of gRandoSettings/gCheats can't erase
// the record of a cheat it is simultaneously overwriting.
constexpr const char* CVAR_CLIMB_FORCED = CVAR_GENERAL("SevenSages.ClimbEverythingForced");

void SetClimbEverything(s32 value) {
    CVarSetInteger(CVAR_CHEAT("ClimbEverything"), value);
    ShipInit::Init(CVAR_CHEAT("ClimbEverything"));
}

void BeginClimbBuff() {
    sPriorClimbEverythingValue = CVarGetInteger(CVAR_CHEAT("ClimbEverything"), 0);
    CVarSetInteger(CVAR_CLIMB_FORCED, sPriorClimbEverythingValue + 1);
    CVarSave();
    SetClimbEverything(1);
}

// Idempotent, and safe to call when no buff is running - which is the point, because every caller
// below is a "this might be stale" path rather than a known-active one.
//
// Prefers the on-disk breadcrumb over sPriorClimbEverythingValue: after a crash-and-relaunch the
// in-memory value is gone and the breadcrumb is the only surviving record of what to restore.
void EndClimbBuff() {
    const s32 marker = CVarGetInteger(CVAR_CLIMB_FORCED, 0);
    if (marker == 0 && sBuffFramesRemaining <= 0) {
        return;
    }

    const s32 prior = marker != 0 ? marker - 1 : sPriorClimbEverythingValue;
    sBuffFramesRemaining = 0;
    sPriorClimbEverythingValue = 0;

    CVarClear(CVAR_CLIMB_FORCED);
    SetClimbEverything(prior);
    CVarSave();
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
            BeginClimbBuff();
        }
        sBuffFramesRemaining = SARIAS_SONG_BUFF_FRAMES;
    });
}

void SevenSagesSariasSongFrameUpdate() {
    // Leaving the save behind ends the buff, and this is the branch that catches quitting to the
    // file select - frames keep running there, so it fires before any new file can be loaded.
    // Without it the cheat stayed on across the rest of the session and into every other save.
    if (!GameInteractor::IsSaveLoaded(true)) {
        EndClimbBuff();
        return;
    }

    if (sBuffFramesRemaining <= 0) {
        return;
    }

    sBuffFramesRemaining--;
    if (sBuffFramesRemaining == 0) {
        EndClimbBuff();
    }
}

// Skip vanilla's "talk to Saria?" / "talk to Navi instead?" chain entirely - see the header
// comment. Unconditional under IS_SEVENSAGES: the buff above fires on every play of the song, so there
// is no branch of the prompt left that still leads anywhere this project wants the player to go.
void SevenSagesSariasSongOnVanillaBehavior(GIVanillaBehavior id, bool* should, va_list originalArgs) {
    if (id != VB_NAVI_ASK_TO_TALK_AFTER_SARIAS_SONG) {
        return;
    }
    *should = false;
}

} // namespace

static void RegisterSevenSagesSariasSong() {
    // Unconditional, and before the COND_HOOKs rather than inside one. This function runs at boot
    // and again on every OnLoadGame (see the RegisterShipInitFunc below), which makes it both
    // recovery points at once:
    //
    //   at boot     - undo a buff the process died in the middle of, from the on-disk breadcrumb
    //   on load     - end a buff left over from the previous file, INCLUDING when the file being
    //                 loaded is vanilla or plain Randomizer, where the COND_HOOKs below are about
    //                 to be unregistered and the frame update would never run again to do it
    //
    // That second case is why this cannot live in the frame update alone: unregistering a hook is
    // not the same as ending what it started.
    EndClimbBuff();

    COND_HOOK(OnOcarinaSongAction, IS_SEVENSAGES, SevenSagesSariasSongPlayed);
    COND_HOOK(OnVanillaBehavior, IS_SEVENSAGES, SevenSagesSariasSongOnVanillaBehavior);
    COND_HOOK(OnGameFrameUpdate, IS_SEVENSAGES, SevenSagesSariasSongFrameUpdate);
}

static RegisterShipInitFunc sevenSagesSariasSongInitFunc(RegisterSevenSagesSariasSong, { "IS_RANDO" });
