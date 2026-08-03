/**
 * Seven Sages - Phase 4e: Navi as the "where next" guide.
 *
 * In vanilla, Navi's forced dialogue is the game's soft quest marker - she tells you where the
 * story goes next. Under a randomizer that role is vacant, and Phase 4a's SkipForcedDialog = 2 put
 * her back on screen with nothing useful to say. This gives her the job back: she names the region
 * worth investigating next, derived from this seed's own logic.
 *
 * This is deliberately NOT built on Phase 3's mechanism. Phase 3 hints are static - one NPC, one
 * subject fixed at authoring time. Navi is the opposite: one speaker whose subject changes as the
 * player progresses. She is, narrowly, the only genuinely dynamic hint in the project.
 *
 * ── What she is told, and what she says ──────────────────────────────────────────────────────
 * Region only. No item, no check, no reason. "Why don't you go check Lake Hylia?" is the entire
 * hint. Same spoiler containment as Phase 3: the item is read to *pick* a region and never leaves
 * the selection code below.
 *
 * This does not collide with Phase 3's "AREA hints must be earned" rule despite naming a region.
 * A Phase 3 AREA hint answers "where is the Master Sword?" - it resolves a specific item the player
 * is hunting. Navi answers "where should I go?" and names no item, so she can't substitute for one.
 *
 * ── Where "worth going" comes from ──────────────────────────────────────────────────────────
 * ROADMAP.md's 4e spec named `ctx->playthroughLocations` (the generator's sphere-ordered
 * playthrough) as the progression-relevance source. **That data does not exist at runtime.** It is
 * a generation-time working set, cleared right after the spoiler log is written
 * (3drando/spoiler_log.cpp:364) and again before Playthrough_Init returns (playthrough.cpp:83),
 * with the spoiler write itself unconditional - so no settings combination preserves it. It is not
 * in the save file either, so it would be gone after a reload regardless.
 *
 * The substitute is the placed item's own advancement flag - the same flag the generator uses to
 * build those spheres in the first place, read live off checks the player hasn't collected yet.
 * That is strictly better for this purpose: sphere data is a static snapshot of the *intended*
 * route, while this reflects what actually still matters *now*, which is the question Navi is
 * answering.
 *
 * ── "Nearest" means region-graph hops, not world distance ────────────────────────────────────
 * Regions live in different scenes with no shared coordinate space, and under entrance shuffle the
 * graph is per-seed anyway - which is exactly what we want, since "close" should mean "few
 * transitions away in THIS seed". BFS over Region::exits from the player's current region, first
 * qualifying region wins.
 *
 * ── Cost ────────────────────────────────────────────────────────────────────────────────────
 * ReachabilitySearch is not cheap (the check tracker wraps its own call in a performance timer and
 * logs the duration). It is therefore run at most once per scene entry and cached, never per Navi
 * trigger. Note this deliberately does NOT piggyback on the check tracker's
 * InternalRecalculateAvailableChecks: that is gated behind the `EnableAvailableChecks` tracker
 * CVar, which is off by default, and a story feature must not depend on a user-facing tracker
 * toggle.
 */
#include "soh/OTRGlobals.h"
#include "soh/ShipInit.hpp"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/custom-message/CustomMessageManager.h"

#include "soh/Enhancements/randomizer/randomizerTypes.h"
#include "soh/Enhancements/randomizer/randomizer.h"
#include "soh/Enhancements/randomizer/static_data.h"
#include "soh/Enhancements/randomizer/location_access.h"
#include "soh/Enhancements/randomizer/entrance.h"
#include "soh/Enhancements/randomizer/3drando/fill.hpp"

#include <deque>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

extern "C" {
extern PlayState* gPlayState;
#include <macros.h>
#include <functions.h>
#include <variables.h>
#include "src/overlays/actors/ovl_Elf_Msg/z_elf_msg.h"
}

namespace {

// The region Navi last suggested, recomputed on scene entry. RR_NONE means "nothing to add".
RandomizerRegion sSuggestedRegion = RR_NONE;
// Set the moment a forced-Navi trigger fires, carrying the textId vanilla is about to open so the
// OnOpenText hook can recognise exactly that message and no other. See RewriteNaviMessage.
bool sNaviTalkPending = false;
uint16_t sPendingNaviTextId = 0;

// The player's current region, derived the same way the check tracker derives it: walk backwards
// from the current entrance index to the first entrance in this scene that the shuffler actually
// has a mapping for (e.g. ENTR_DEKU_TREE_0_1 is not mapped but ENTR_DEKU_TREE_ENTRANCE is), then
// take that entrance's connected region. Returns RR_NONE when nothing maps.
RandomizerRegion GetCurrentRegion() {
    if (gPlayState == nullptr) {
        return RR_NONE;
    }
    int16_t entranceIndex = gPlayState->nextEntranceIndex;
    if (entranceIndex < 0 || entranceIndex >= ENTR_MAX) {
        return RR_NONE;
    }
    const int8_t scene = gEntranceTable[entranceIndex].scene;
    for (; entranceIndex >= 0 && gEntranceTable[entranceIndex].scene == scene; entranceIndex--) {
        const auto entrance = Rando::EntranceShuffler::GetEntranceByIndex(entranceIndex);
        if (entrance != nullptr) {
            return entrance->GetOriginalConnectedRegionKey();
        }
    }
    return RR_NONE;
}

RandoAgeTime GetCurrentAgeTime() {
    if (LINK_IS_CHILD) {
        return IS_DAY ? RAT_CHILD_DAY : RAT_CHILD_NIGHT;
    }
    return IS_DAY ? RAT_ADULT_DAY : RAT_ADULT_NIGHT;
}

// Every check the player hasn't collected that still holds a progression item. This is the
// "worth going" filter - see the header comment for why it replaces the spec's playthrough data.
std::vector<RandomizerCheck> CollectProgressionTargets() {
    const auto& ctx = Rando::Context::GetInstance();
    std::vector<RandomizerCheck> targets;
    targets.reserve(RC_MAX);

    for (auto& location : Rando::StaticData::GetLocationTable()) {
        const RandomizerCheck rc = location.GetRandomizerCheck();
        Rando::ItemLocation* itemLocation = ctx->GetItemLocation(rc);
        if (itemLocation == nullptr || itemLocation->HasObtained()) {
            continue;
        }
        const RandomizerGet placed = itemLocation->GetPlacedRandomizerGet();
        if (placed == RG_NONE) {
            continue;
        }
        if (!Rando::StaticData::RetrieveItem(placed).IsAdvancement()) {
            continue;
        }
        targets.push_back(rc);
    }
    return targets;
}

// Breadth-first over the runtime region graph, which already has this seed's entrance overrides
// applied, so hop counts are per-seed rather than per-vanilla-map. Returns the nearest region that
// holds a reachable progression check, skipping the region the player is standing in - being told
// to go where you already are is worse than silence. RR_NONE if there is nothing to say.
RandomizerRegion FindNearestRelevantRegion(RandomizerRegion from,
                                           const std::unordered_set<uint16_t>& relevantRegions) {
    if (from == RR_NONE || relevantRegions.empty()) {
        return RR_NONE;
    }

    std::unordered_set<uint16_t> visited = { static_cast<uint16_t>(from) };
    std::deque<RandomizerRegion> queue = { from };

    while (!queue.empty()) {
        const RandomizerRegion current = queue.front();
        queue.pop_front();

        // Deliberately tested on dequeue rather than enqueue so `from` itself is excluded by the
        // ordering alone - it is the only region already in `visited` before the loop starts.
        if (current != from && relevantRegions.count(static_cast<uint16_t>(current)) > 0) {
            return current;
        }

        Region* region = RegionTable(current);
        if (region == nullptr) {
            continue;
        }
        for (Rando::Entrance& exit : region->exits) {
            const RandomizerRegion next = exit.GetConnectedRegionKey();
            if (next == RR_NONE || visited.count(static_cast<uint16_t>(next)) > 0) {
                continue;
            }
            visited.insert(static_cast<uint16_t>(next));
            queue.push_back(next);
        }
    }
    return RR_NONE;
}

// The one expensive call. Runs on scene entry only; everything else reads sSuggestedRegion.
void RecalculateNaviSuggestion(int16_t sceneNum) {
    sSuggestedRegion = RR_NONE;

    if (!GameInteractor::IsSaveLoaded()) {
        return;
    }
    const RandomizerRegion currentRegion = GetCurrentRegion();
    if (currentRegion == RR_NONE) {
        return;
    }

    const std::vector<RandomizerCheck> targets = CollectProgressionTargets();
    if (targets.empty()) {
        return; // Everything that matters is already collected - she has nothing to add.
    }

    const auto& ctx = Rando::Context::GetInstance();

    // Not optional. ReachabilitySearch works through the `logic` GLOBAL (location_access.h:20), not
    // through ctx - it calls logic->Reset(false) before doing anything - so the caller has to point
    // that global at this seed's Logic first. RegionTable_Init() happens to set it too, but relying
    // on that would make this depend on nobody having reassigned it since save load. The check
    // tracker does the same assignment for the same reason (randomizer_check_tracker.cpp:2280).
    //
    // Side effect worth knowing: this leaves logic->CalculatingAvailableChecks true, which changes
    // how location_access/item.cpp treat unidentified checks. That is pre-existing shared behaviour
    // - the tracker's own call leaves it in exactly the same state - and every entry into
    // ReachabilitySearch re-Resets it, but it is why this runs on scene entry rather than
    // mid-gameplay.
    logic = ctx->GetLogic();

    const std::vector<RandomizerCheck> reachable =
        ReachabilitySearch(targets, RG_NONE, true, currentRegion, GetCurrentAgeTime());

    std::unordered_set<uint16_t> relevantRegions;
    for (const RandomizerCheck rc : reachable) {
        Rando::ItemLocation* itemLocation = ctx->GetItemLocation(rc);
        if (itemLocation == nullptr) {
            continue;
        }
        const RandomizerRegion parent = itemLocation->GetParentRegionKey();
        if (parent != RR_NONE) {
            relevantRegions.insert(static_cast<uint16_t>(parent));
        }
    }

    sSuggestedRegion = FindNearestRelevantRegion(currentRegion, relevantRegions);
}

std::string GetRegionAreaName(RandomizerRegion regionKey) {
    Region* region = RegionTable(regionKey);
    if (region == nullptr) {
        return "";
    }
    const RandomizerArea area = region->GetFirstArea();
    if (area == RA_NONE) {
        return "";
    }
    const auto nameEntry = Rando::StaticData::areaNames.find(static_cast<uint32_t>(area));
    if (nameEntry == Rando::StaticData::areaNames.end()) {
        return "";
    }
    return Rando::StaticData::hintTextTable[nameEntry->second].GetClear().GetForCurrentLanguage(MF_CLEAN);
}

// Arm the rewrite. Left at *should = true on purpose: vanilla still does all its own work (sets
// player->naviTextId, points Navi at this trigger), and we only swap what the resulting textbox
// says. Capturing the exact textId here is what keeps the OnOpenText hook below from touching any
// other message - it is far more precise than guessing at the ElfMsg id range.
void ArmNaviRewrite(bool* should, ElfMsg* naviTalk) {
    if (naviTalk == nullptr || !*should) {
        return;
    }
    // Mirrors ElfMsg_GetMessageId (z_elf_msg.c:112), which is static to that file. Player takes
    // ABS() of the signed form before opening the box, so the magnitude is what we match on.
    sPendingNaviTextId = (naviTalk->actor.params & 0xFF) + 0x100;
    sNaviTalkPending = true;
}

void RewriteNaviMessage(uint16_t* textId, bool* loadFromMessageTable) {
    if (!sNaviTalkPending || *textId != sPendingNaviTextId) {
        return;
    }
    sNaviTalkPending = false;

    const std::string area = sSuggestedRegion == RR_NONE ? "" : GetRegionAreaName(sSuggestedRegion);

    // Vanilla's line here is a stale quest marker under a randomizer ("go to the Deku Tree"), so
    // it is replaced either way - with the suggestion when there is one, and with an in-character
    // "nothing to add" when there isn't. Silence is not an option: the trigger has already fired
    // and Navi is on screen expecting to speak.
    CustomMessage msg(area.empty() ? "Hey! I don't have a feel for where to go next...&Let's just look around!"
                                   : "Hey! Why don't you go check " + area + "?");
    // AutoFormat() before LoadIntoFont() is mandatory - see docs/custom-text-safety.md. Skipping it
    // leaves the message unterminated and Message_Decode runs off the end of the buffer.
    msg.AutoFormat();
    msg.LoadIntoFont();
    *loadFromMessageTable = false;
}

void SevenSagesNaviOnVanillaBehavior(GIVanillaBehavior id, bool* should, va_list originalArgs) {
    if (id != VB_NAVI_TALK) {
        return;
    }
    va_list args;
    va_copy(args, originalArgs);
    ElfMsg* naviTalk = va_arg(args, ElfMsg*);
    va_end(args);
    ArmNaviRewrite(should, naviTalk);
}

void RegisterSevenSagesNaviGuide() {
    COND_HOOK(OnSceneInit, IS_RANDO, RecalculateNaviSuggestion);
    COND_HOOK(OnVanillaBehavior, IS_RANDO, SevenSagesNaviOnVanillaBehavior);
    COND_HOOK(OnOpenText, IS_RANDO, RewriteNaviMessage);
}

} // namespace

static RegisterShipInitFunc sevenSagesNaviGuideInitFunc(RegisterSevenSagesNaviGuide, { "IS_RANDO" });
