#include "soh/Network/Anchor/Anchor.h"
#include "soh/Enhancements/SevenSagesCoop/SevenSagesCoop.h"
#include <nlohmann/json.hpp>
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/randomizer/randomizer_entrance.h"

/**
 * ENTRANCE_DISCOVERED
 */

void Anchor::SendPacket_EntranceDiscovered(u16 entranceIndex) {
    if (!IsSaveLoaded() || isProcessingIncomingPacket || !roomState.syncItemsAndFlags) {
        return;
    }

    nlohmann::json payload;
    payload["type"] = ENTRANCE_DISCOVERED;
    payload["targetTeamId"] = CVarGetString(CVAR_REMOTE_ANCHOR("TeamId"), "default");
    payload["entranceIndex"] = entranceIndex;
    payload["quiet"] = true;

    SendJsonToRemote(payload);
}

void Anchor::HandlePacket_EntranceDiscovered(nlohmann::json payload) {
    // Seven Sages co-op: refuse world state from a client in a different item placement. Their
    // flags describe a world this save does not have. See SevenSagesCoop.h.
    if (!ShouldAcceptWorldStateFrom(payload)) {
        return;
    }

    if (!IsSaveLoaded() || !roomState.syncItemsAndFlags) {
        return;
    }

    u16 entranceIndex = payload.at("entranceIndex").get<u16>();
    Entrance_SetEntranceDiscovered(entranceIndex, 1);
}
