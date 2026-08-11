#include "Anchor.h"
#include <nlohmann/json.hpp>
#include "soh/OTRGlobals.h"
#include "soh/Enhancements/nametag.h"
#include "soh/ObjectExtension/ObjectExtension.h"
#include "soh/Enhancements/randomizer/randomizer.h"
#include "soh/Enhancements/SevenSagesCoop/SevenSagesCoop.h"
#include "soh/Notification/Notification.h"
#include <map>

extern "C" {
#include "variables.h"
#include "functions.h"
extern PlayState* gPlayState;
}

// MARK: - Overrides

void Anchor::Enable() {
    Network::Enable(CVarGetString(CVAR_REMOTE_ANCHOR("Host"), "anchor.hm64.org"),
                    CVarGetInteger(CVAR_REMOTE_ANCHOR("Port"), 43383));
    ownClientId = CVarGetInteger(CVAR_REMOTE_ANCHOR("LastClientId"), 0);
    roomState.ownerClientId = 0;
}

void Anchor::Disable() {
    Network::Disable();

    clients.clear();
    RefreshClientActors();
}

void Anchor::OnConnected() {
    SendPacket_Handshake();
    RegisterHooks();

    if (IsSaveLoaded()) {
        SendPacket_RequestTeamState();

        // Seven Sages trade box: announce our stash on connect, so a client joining an established
        // run is not left with an empty box until somebody happens to make the next deposit.
        //
        // Every client doing this is what makes it work without a request/response round trip. The
        // packet carries a revision and receivers keep the higher one, so whoever holds the newest
        // stash wins regardless of who announced first. A joiner with a fresh save is at revision 0
        // and loses to everyone, which is the desired direction.
        SendPacket_SevenSagesStash();
    }
}

void Anchor::OnDisconnected() {
    RegisterHooks();
}

void Anchor::ProcessOutgoingPackets() {
    // Copy all queued packets while holding the lock, then send them after releasing
    std::queue<nlohmann::json> packetsToSend;
    {
        std::lock_guard<std::mutex> lock(outgoingPacketQueueMutex);
        packetsToSend.swap(outgoingPacketQueue);
    }

    // Send packets without holding the lock
    while (!packetsToSend.empty()) {
        nlohmann::json payload = packetsToSend.front();
        packetsToSend.pop();

        if (!payload.contains("quiet")) {
            SPDLOG_DEBUG("[Anchor] Sending payload:\n{}", payload.dump());
        }
        Network::SendJsonToRemote(payload);
    }
}

void Anchor::SendJsonToRemote(nlohmann::json payload) {
    if (!isConnected) {
        return;
    }

    payload["clientId"] = ownClientId;
    if (!payload.contains("quiet")) {
        SPDLOG_DEBUG("[Anchor] Queuing payload:\n{}", payload.dump());
    }

    if (payload["type"] == HANDSHAKE) {
        Network::SendJsonToRemote(payload);
        return;
    }

    // Queue the packet to be sent on the network thread
    std::lock_guard<std::mutex> lock(outgoingPacketQueueMutex);
    outgoingPacketQueue.push(payload);
}

void Anchor::OnIncomingJson(nlohmann::json payload) {
    // If it doesn't contain a type, it's not a valid payload
    if (!payload.contains("type")) {
        return;
    }

    // If it's not a quiet payload, log it
    if (!payload.contains("quiet")) {
        SPDLOG_DEBUG("[Anchor] Received payload:\n{}", payload.dump());
    }

    std::string packetType = payload["type"].get<std::string>();

    // Ignore packets from mismatched clients, except for ALL_CLIENT_STATE, UPDATE_CLIENT_STATE, and PLAYER_UPDATE
    if (packetType != ALL_CLIENT_STATE && packetType != UPDATE_CLIENT_STATE && packetType != PLAYER_UPDATE) {
        if (payload.contains("clientId")) {
            uint32_t clientId = payload["clientId"].get<uint32_t>();
            if (clients.contains(clientId) && clients[clientId].clientVersion != clientVersion) {
                return;
            }
        }
    }

    // Queue all packets to be processed on the game thread
    std::lock_guard<std::mutex> lock(incomingPacketQueueMutex);
    incomingPacketQueue.push(payload);
}

void Anchor::ProcessIncomingPacketQueue() {
    // Copy all queued packets while holding the lock, then process them after releasing
    std::queue<nlohmann::json> packetsToProcess;
    {
        std::lock_guard<std::mutex> lock(incomingPacketQueueMutex);
        packetsToProcess.swap(incomingPacketQueue);
    }

    // Process packets without holding the lock
    while (!packetsToProcess.empty()) {
        nlohmann::json payload = packetsToProcess.front();
        packetsToProcess.pop();

        std::string packetType = payload["type"].get<std::string>();

        isProcessingIncomingPacket = true;

        try {
            // packetType here is a string so we can't use a switch statement
            if (packetType == ALL_CLIENT_STATE)
                HandlePacket_AllClientState(payload);
            else if (packetType == DAMAGE_PLAYER)
                HandlePacket_DamagePlayer(payload);
            else if (packetType == DISABLE_ANCHOR)
                HandlePacket_DisableAnchor(payload);
            else if (packetType == ENTRANCE_DISCOVERED)
                HandlePacket_EntranceDiscovered(payload);
            else if (packetType == GAME_COMPLETE)
                HandlePacket_GameComplete(payload);
            else if (packetType == GIVE_ITEM)
                HandlePacket_GiveItem(payload);
            else if (packetType == OCARINA_SFX)
                HandlePacket_OcarinaSfx(payload);
            else if (packetType == PLAYER_UPDATE)
                HandlePacket_PlayerUpdate(payload);
            else if (packetType == PLAYER_SFX)
                HandlePacket_PlayerSfx(payload);
            else if (packetType == UPDATE_TEAM_STATE)
                HandlePacket_UpdateTeamState(payload);
            else if (packetType == REQUEST_TEAM_STATE)
                HandlePacket_RequestTeamState(payload);
            else if (packetType == REQUEST_TELEPORT)
                HandlePacket_RequestTeleport(payload);
            else if (packetType == SERVER_MESSAGE)
                HandlePacket_ServerMessage(payload);
            else if (packetType == SEVEN_SAGES_SEED)
                HandlePacket_SevenSagesSeed(payload);
            else if (packetType == SEVEN_SAGES_STASH)
                HandlePacket_SevenSagesStash(payload);
            else if (packetType == SET_CHECK_STATUS)
                HandlePacket_SetCheckStatus(payload);
            else if (packetType == SET_FLAG)
                HandlePacket_SetFlag(payload);
            else if (packetType == TELEPORT_TO)
                HandlePacket_TeleportTo(payload);
            else if (packetType == UNSET_FLAG)
                HandlePacket_UnsetFlag(payload);
            else if (packetType == UPDATE_BEANS_COUNT)
                HandlePacket_UpdateBeansCount(payload);
            else if (packetType == UPDATE_CLIENT_STATE)
                HandlePacket_UpdateClientState(payload);
            else if (packetType == UPDATE_ROOM_STATE)
                HandlePacket_UpdateRoomState(payload);
            else if (packetType == UPDATE_DUNGEON_ITEMS)
                HandlePacket_UpdateDungeonItems(payload);
        } catch (const std::exception& e) {
            SPDLOG_ERROR("[Anchor] Exception while processing incoming packet {}", e.what());
            SPDLOG_ERROR("[Anchor] Packet: {}", payload.dump());
        }

        isProcessingIncomingPacket = false;
    }
}

// MARK: - Misc/Helpers

// Kills all existing anchor actors and respawns them with the new client data

struct DummyPlayerClientId {
    uint32_t clientId = 0;
};
static ObjectExtension::Register<DummyPlayerClientId> DummyPlayerClientIdRegister;

uint32_t Anchor::GetDummyPlayerClientId(const Actor* actor) {
    const DummyPlayerClientId* clientId = ObjectExtension::GetInstance().Get<DummyPlayerClientId>(actor);
    return clientId != nullptr ? clientId->clientId : 0;
}

void Anchor::SetDummyPlayerClientId(const Actor* actor, uint32_t clientId) {
    ObjectExtension::GetInstance().Set<DummyPlayerClientId>(actor, DummyPlayerClientId{ clientId });
}

void Anchor::RefreshClientActors() {
    if (!IsSaveLoaded()) {
        return;
    }

    Actor* actor = gPlayState->actorCtx.actorLists[ACTORCAT_NPC].head;

    while (actor != NULL) {
        if (actor->id == ACTOR_EN_OE2 && actor->update == DummyPlayer_Update) {
            NameTag_RemoveAllForActor(actor);
            Actor_Kill(actor);
        }
        actor = actor->next;
    }

    for (auto& [clientId, client] : clients) {
        if (!client.online || client.self) {
            continue;
        }

        // Seven Sages co-op: child and adult are separate dimensions. You only see players who are
        // currently your own age, and time travel is what moves you between them.
        //
        // This is flavour with a real bug underneath it. Child and adult Hyrule are the SAME scene
        // id with different setups, so an unfiltered Anchor already renders a cross-age teammate
        // standing in geometry that does not exist in your version of the room - walking through
        // walls, floating over terrain that is only there for them. Filtering fixes that and
        // explains it in-world at the same time.
        //
        // Re-runs are already handled at both ends: HandlePacket_PlayerUpdate raises
        // shouldRefreshActors when a REMOTE age changes, and the OnPlayerUpdate hook does the same
        // when the LOCAL player's age changes (HookHandlers.cpp).
        if (!SevenSagesCoop_ShouldSeeAge(client.linkAge)) {
            // Clear the pointer, don't just skip the spawn. Every dummy actor was Actor_Kill'ed at
            // the top of this function, so leaving client.player set would leave it aimed at a dead
            // actor - and both readers (the compass icons in HookHandlers.cpp and OCARINA_SFX's
            // positional audio) guard only on the pointer being non-null, which a stale pointer
            // passes. A player in the other dimension keeps sending PLAYER_UPDATE and OCARINA_SFX,
            // so those readers stay live for exactly the clients being skipped here.
            client.player = nullptr;
            continue;
        }

        spawningDummyPlayerForClientId = clientId;
        // We are using a hook `ShouldActorInit` to override the init/update/draw/destroy functions of the Player we
        // spawn We quickly store a mapping of "index" to clientId, then within the init function we use this to get the
        // clientId and store it on player->zTargetActiveTimer (unused s32 for the dummy) for convenience
        auto dummy =
            Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_PLAYER, client.posRot.pos.x, client.posRot.pos.y,
                        client.posRot.pos.z, client.posRot.rot.x, client.posRot.rot.y, client.posRot.rot.z, 0);
        client.player = (Player*)dummy;
    }
    spawningDummyPlayerForClientId = 0;
}

bool Anchor::ShouldAcceptWorldStateFrom(const nlohmann::json& payload, bool requireVerified) {
    // Unverifiable sender. Permissive by default so vanilla Anchor behaviour is unchanged, but
    // refused outright for a wholesale snapshot while co-op is on - see the header.
    const bool coopStrict = requireVerified && SevenSagesCoop_IsActive();

    if (!payload.contains("clientId")) {
        return !coopStrict;
    }

    const uint32_t clientId = payload.value("clientId", (uint32_t)0);
    const auto it = clients.find(clientId);
    if (it == clients.end()) {
        return !coopStrict;
    }

    // A sender who reports no fingerprint at all cannot be shown to share our world. Same argument.
    if (coopStrict && it->second.seedHash == 0) {
        return false;
    }

    if (SevenSagesCoop_ShouldAcceptWorldStateFrom(it->second.seedHash)) {
        return true;
    }

    // Say so, once per client per mismatched world.
    //
    // Dropping the packets silently is correct but indistinguishable from "co-op is broken": the
    // only other signal is a red triangle in the room list that has to be hovered. Playtesting on
    // 2026-08-11 hit exactly this - a joiner generated their own seed instead of loading the host's
    // spoiler, the two of them played entirely different worlds for twenty minutes, and the visible
    // symptom was "the chests aren't syncing".
    //
    // Keyed on the remote's fingerprint as well as their id, so reconnecting onto the CORRECT world
    // clears the way for a fresh warning if they later diverge again.
    // Log, not an on-screen notification. This fires whenever a room contains a client on another
    // world, which includes the ordinary case of somebody still setting up at file select, and a
    // toast for that is noise during exactly the minutes the player is busiest. The room list's red
    // triangle is the visible signal; this is the one that survives into a bug report.
    static std::map<uint32_t, uint32_t> warnedClients;
    const auto warned = warnedClients.find(clientId);
    if (warned == warnedClients.end() || warned->second != it->second.seedHash) {
        warnedClients[clientId] = it->second.seedHash;
        SPDLOG_WARN("[Anchor] '{}' is on a different world (theirs {}, ours {}) - refusing world state",
                    it->second.name, it->second.seedHash, SevenSagesCoop_GetWorldFingerprint());
    }

    return false;
}

bool Anchor::IsSaveLoaded() {
    if (gPlayState == nullptr) {
        return false;
    }

    if (GET_PLAYER(gPlayState) == nullptr) {
        return false;
    }

    if (gSaveContext.fileNum < 0 || gSaveContext.fileNum > 2) {
        return false;
    }

    if (gSaveContext.gameMode != GAMEMODE_NORMAL) {
        return false;
    }

    return true;
}
