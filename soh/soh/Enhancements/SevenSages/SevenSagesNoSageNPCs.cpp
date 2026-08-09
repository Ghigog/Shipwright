/**
 * Seven Sages - the sages are not standing around in Hyrule waiting to be talked to.
 *
 * The premise makes every sage a potential *player* (see seven-sages/docs/lore.md), so a sage who
 * is also an NPC handing the player a song is a contradiction the moment you pick that sage. Ruto
 * was the one genuinely load-bearing case and she is handled separately in
 * SevenSagesJabuWithoutRuto.cpp - she was a traversal mechanic, not a check-holder. Everything in
 * this file is the other case: sages who cost only randomizer *locations*, which
 * ItemLocation::SetExcludedOption handles (see the Seven Sages block in Context::FinalizeSettings,
 * settings.cpp) by making the fill place junk there. No location_access work is owed.
 *
 * ── Why ShouldActorInit and not OnActorInit ─────────────────────────────────────────────────
 * OnActorInit runs *after* the actor's own Init has already executed (z_actor.c:1259-1263), and
 * some of these Inits have side effects we specifically do not want: EnSa_Init's case 4 sets
 * gSaveContext.cutsceneTrigger and loads a cutscene segment before it returns. ShouldActorInit is
 * checked first, and returning false there means Init never runs and the engine kills the actor
 * itself - the same path the engine already uses for actors whose object failed to load.
 *
 * EnSa spawns a Kokiri fairy as a child actor. That fairy self-destructs when its parent's update
 * pointer goes NULL (z_en_elf.c:685-690), which is exactly what Actor_Kill leaves behind, so
 * nothing is stranded.
 *
 * ── Who is removed, and who is not ──────────────────────────────────────────────────────────
 * Saria (EN_SA) and Darunia (EN_DU) are pure NPCs and go entirely. EnSa already has an
 * "no reason to be in this scene" Actor_Kill branch of its own (z_en_sa.c:522), so removing her is
 * using a door the actor already has.
 *
 * Sheik (EN_XC) goes entirely, hint Sheiks included. The warp-song cutscenes still fire - they are
 * driven by the cutscene timeline in z_demo.c, not by the actor - they just play with nobody in
 * them, and their EventChkInf flags are set before the actor would have appeared, so they do not
 * repeat. The cost is RC_SHEIK_HINT_GC, a hint rather than an item location. RSK_SHEIK_LA_HINT is
 * already off in the Seven Sages preset, so no SHEIK_TYPE_RANDO Sheik was being spawned anyway.
 *
 * Nabooru (EN_NB) is only partly removed. NB_TYPE_CRAWLSPACE is the one talkable Nabooru, and
 * stock rando already kills it when Speak shuffle is off (hook_handlers.cpp:2702); this makes that
 * unconditional. NB_TYPE_DEMO02 is her Light Arrow / sealing cutscene appearance. NB_TYPE_KIDNAPPED
 * and NB_TYPE_KNUCKLE are deliberately left alone: that is the Spirit Temple Iron Knuckle fight and
 * the Twinrova abduction, a dungeon sequence rather than a conversation, and removing it is a much
 * bigger change than removing an NPC. She costs no locations either way.
 *
 * Rauru and Impa need nothing here and have no actor to remove *in the overworld*. Rauru (EN_RL,
 * OBJECT_RL) exists only inside the Chamber of Sages. Impa exists only as DEMO_IM inside the
 * castle courtyard cutscene, which the preset's Starting Zelda's Letter setting skips wholesale
 * (savefile.cpp:966). EnSa never spawns in Lost Woods to begin with (func_80AF5DFC returns 0
 * there), so the bridge Saria has no actor either.
 *
 * ── The story cutscenes are the preset's job, not this file's ───────────────────────────────
 * Everything that stages a sage *inside a cutscene* - the Chamber of Sages after every dungeon
 * (Demo_Sa, Demo_Du, Demo_IM, En_Ru2, En_Nb), Rauru at the Master Sword pull, Zelda and Impa
 * fleeing the castle, the Lost Woods bridge, Nabooru's capture - is already suppressed by stock
 * SoH behind one switch: TimeSavers.SkipCutscene.Story. The Seven Sages preset sets it to 1.
 *
 * That is deliberately a preset value and not a hook here. These are vanilla SoH skips that
 * happen to do exactly what the premise needs, and each one also fixes up the EventChkInf flags
 * its cutscene would have set - work this file would otherwise have to reproduce by hand.
 *
 * ⚠️ The switch is load-bearing, and it has been wrong once. Its *default* is IS_RANDO, so
 * reading the source alone suggests these are skipped in any rando seed. An explicit preset value
 * overrides that default. Phase 4a set Story to 0 to restore story cutscenes for lore reasons,
 * which silently put Rauru, the chamber sages and the castle-escape Impa back on screen; an
 * earlier version of this comment still claimed they were "already skipped in rando" and was
 * stale from that moment. Corrected 2026-08-09. If a sage reappears in a cutscene, check the
 * preset's Story value first - it is far more likely than a regression in this file.
 *
 * ── The one cutscene this file does suppress ────────────────────────────────────────────────
 * The Temple of Time Light Arrows cutscene is where Sheik unwraps and is Zelda, and it is the one
 * sage cutscene the Story skip does not cover. Story only answers VB_GIVE_ITEM_LIGHT_ARROW, which
 * withholds the *item* - the cutscene still plays, and Zelda is still in it. So the suppression
 * belongs here. VB_BE_ELIGIBLE_FOR_LIGHT_ARROWS (z_demo.c:2258) is the eligibility test for
 * triggering it, so answering it false means it never fires at all. RC_TOT_LIGHT_ARROWS_CUTSCENE
 * is excluded to match; the Light Arrows themselves are an ordinary pool item and are placed
 * somewhere else.
 */
#include "soh/OTRGlobals.h"
#include "soh/ShipInit.hpp"
#include "soh/Enhancements/game-interactor/GameInteractor.h"

extern "C" {
#include "z64.h"
#include "variables.h"
#include "src/overlays/actors/ovl_En_Nb/z_en_nb.h"
}

namespace {

void SevenSagesRemoveSageActor(void* actorRef, bool* result) {
    *result = false;
}

// Nabooru is the one partial case - only the talkable crawlspace Nabooru and her cutscene
// appearance go. See the header comment for why the Spirit Temple sequence stays.
void SevenSagesRemoveNabooru(void* actorRef, bool* result) {
    Actor* actor = static_cast<Actor*>(actorRef);
    s32 type = actor->params & 0xFF;

    if (type == NB_TYPE_CRAWLSPACE || type == NB_TYPE_DEMO02) {
        *result = false;
    }
}

void SevenSagesNoSageNPCsOnVanillaBehavior(GIVanillaBehavior id, bool* should, va_list originalArgs) {
    if (id != VB_BE_ELIGIBLE_FOR_LIGHT_ARROWS) {
        return;
    }
    *should = false;
}

void RegisterSevenSagesNoSageNPCs() {
    COND_ID_HOOK(ShouldActorInit, ACTOR_EN_SA, IS_SEVENSAGES, SevenSagesRemoveSageActor);
    COND_ID_HOOK(ShouldActorInit, ACTOR_EN_DU, IS_SEVENSAGES, SevenSagesRemoveSageActor);
    COND_ID_HOOK(ShouldActorInit, ACTOR_EN_XC, IS_SEVENSAGES, SevenSagesRemoveSageActor);
    COND_ID_HOOK(ShouldActorInit, ACTOR_EN_NB, IS_SEVENSAGES, SevenSagesRemoveNabooru);
    COND_HOOK(OnVanillaBehavior, IS_SEVENSAGES, SevenSagesNoSageNPCsOnVanillaBehavior);
}

} // namespace

static RegisterShipInitFunc sevenSagesNoSageNPCsInitFunc(RegisterSevenSagesNoSageNPCs, { "IS_RANDO" });
