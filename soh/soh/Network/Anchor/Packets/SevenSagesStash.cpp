#include "soh/Network/Anchor/Anchor.h"
#include "soh/Enhancements/SevenSagesCoop/SevenSagesCoop.h"
#include "soh/Enhancements/SevenSagesTrade/SevenSagesStash.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

/**
 * SEVEN_SAGES_STASH
 *
 * The shared trade box. Carries the WHOLE stash on every change rather than an incremental
 * deposit/withdraw operation.
 *
 * ── Why whole-state and not operations ──────────────────────────────────────────────────────
 *
 * The relay is a dumb broadcast with no ordering guarantee and no server-side game knowledge, and
 * this project has already been bitten by assuming otherwise: seed chunks reassembled in arrival
 * order silently lost one to out-of-order delivery (multiplayer-anchor.md). An operation log would
 * need exactly that ordering to stay correct. Whole-state is self-correcting - any client that
 * receives a later state converges on it regardless of what it missed.
 *
 * Size is not a concern here the way it was for the seed. 24 entries of a 16-bit item id and an
 * 8-bit sage is well under a kilobyte of JSON, against the ~110KB payload that the relay silently
 * dropped. This is the reason SevenSagesStash.h fixes the capacity rather than growing on demand.
 *
 * ── The race this does NOT solve, and why that is acceptable for v1 ─────────────────────────
 *
 * Two players withdrawing the SAME item in the same instant will both receive it, and the later
 * broadcast wins - one item duplicated. `revision` is carried so a receiver can tell new state
 * from an echo of its own, but it is not a conflict resolver: there is no authority to arbitrate
 * between two clients that both incremented from the same base.
 *
 * This is accepted rather than overlooked. The box is deliberately asynchronous - decision 5 chose
 * one shared stash precisely so that trades never require both players present, and players of
 * different ages cannot even see each other. Two players standing at terminals in the same instant
 * reaching for the same item is the narrow case, not the normal one. Closing it properly needs an
 * authority the relay does not have (a claim/ack round trip), which is real work for a rare case
 * and belongs after the box has been played with.
 *
 * ── World guard ────────────────────────────────────────────────────────────────────────────
 *
 * requireVerified = true, because this is wholesale state rather than an incremental change. A
 * stash from a client in a different item placement describes items this world may not contain.
 */

void Anchor::SendPacket_SevenSagesStash() {
    if (!IsSaveLoaded() || !SevenSagesCoop_IsActive()) {
        return;
    }

    nlohmann::json payload;
    payload["type"] = SEVEN_SAGES_STASH;
    payload["targetTeamId"] = CVarGetString(CVAR_REMOTE_ANCHOR("TeamId"), "default");
    payload["addToQueue"] = true;
    payload["revision"] = SevenSagesStash_Revision();

    const uint8_t count = SevenSagesStash_Count();
    payload["count"] = count;

    nlohmann::json items = nlohmann::json::array();
    nlohmann::json depositors = nlohmann::json::array();
    for (uint8_t i = 0; i < count; i++) {
        uint8_t sage = 0;
        items.push_back(SevenSagesStash_Get(i, &sage));
        depositors.push_back(sage);
    }
    payload["items"] = items;
    payload["depositors"] = depositors;

    SendJsonToRemote(payload);
}

void Anchor::HandlePacket_SevenSagesStash(nlohmann::json payload) {
    // Wholesale state, so the strict form of the guard - see the header comment.
    if (!ShouldAcceptWorldStateFrom(payload, true)) {
        return;
    }

    if (!IsSaveLoaded() || !SevenSagesCoop_IsActive()) {
        return;
    }

    const uint32_t revision = payload.at("revision").get<uint32_t>();
    if (revision <= SevenSagesStash_Revision()) {
        // Our own broadcast coming back, or state older than what we already hold. Applying it
        // would undo a deposit the player just made in front of themselves.
        return;
    }

    const auto& items = payload.at("items");
    const auto& depositors = payload.at("depositors");

    // Trust the arrays' own length rather than the count field. They are what will actually be
    // iterated, and a count that disagrees with them is the shape of bug that reads out of bounds.
    size_t count = items.size();
    if (count > depositors.size()) {
        count = depositors.size();
    }
    if (count > SEVEN_SAGES_STASH_CAPACITY) {
        SPDLOG_WARN("[Anchor] SEVEN_SAGES_STASH: {} entries exceeds capacity, truncating", count);
        count = SEVEN_SAGES_STASH_CAPACITY;
    }

    SevenSagesStash_Clear();
    for (size_t i = 0; i < count; i++) {
        SevenSagesStash_Add(items[i].get<int16_t>(), depositors[i].get<uint8_t>());
    }

    // Adopt the sender's revision rather than the one our own Add calls just produced, so both
    // sides agree on what generation of the stash this is.
    SevenSagesStash_SetRevision(revision);
}
