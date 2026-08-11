#include "soh/Network/Anchor/Anchor.h"
#include "soh/Enhancements/SevenSagesCoop/SevenSagesCoop.h"
#include "soh/Notification/Notification.h"
#include "soh/OTRGlobals.h"
#include "soh/Enhancements/randomizer/SeedContext.h"
#include <ship/Context.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <fstream>
#include <sstream>
#include <filesystem>

extern "C" {
#include "variables.h"
// Declared inside OTRGlobals.h's #ifndef __cplusplus block, so invisible to this file.
void Randomizer_ParseSpoiler(const char* fileLoc);
}

/**
 * SEVEN_SAGES_SEED
 *
 * Carries the host's generated spoiler file to everyone else in the team, so a co-op run needs no
 * manual file hand-off. The joiner receives it, drops it in their own Randomizer/ folder, and runs
 * the same ParseSpoiler path a dragged-in file would have taken.
 *
 * ── Why the whole spoiler and not the placements ────────────────────────────────────────────
 * Upstream already tried the other way: UpdateTeamState.cpp still carries its abandoned attempt at
 * transmitting item locations, entrances, MQ dungeons and trials field by field, with the comment
 * "currently it doesn't work :)". Re-serialising the world means re-implementing - and keeping in
 * step with - everything ParseSpoiler already does correctly.
 *
 * Shipping the file itself makes this a transport problem instead of a serialisation one. The load
 * path is the one SoH ships, tests and fixes; this packet only has to move bytes.
 *
 * ── On size ─────────────────────────────────────────────────────────────────────────────────
 * A spoiler runs ~110KB, far past the 512-byte socket reads. That is fine: Network::ReceiveFromServer
 * appends every read into `receivedData` and only dispatches on the '\0' delimiter
 * (Network.cpp:98-118), so a large payload simply spans several reads and is reassembled. The one
 * thing not verifiable from this repo is whether the relay server caps payload size - it is a
 * separate Go project - so a failure to arrive at all is the first thing to suspect if this
 * misbehaves.
 */

void Anchor::SendPacket_SevenSagesSeed() {
    // The path of the most recently generated spoiler. Written by SpoilerLog_Write
    // (spoiler_log.cpp:392) on every generation, so it is always the seed this client is actually
    // holding rather than whatever happens to be newest on disk.
    const std::string spoilerPath = CVarGetString(CVAR_GENERAL("SpoilerLog"), "");
    if (spoilerPath.empty()) {
        Notification::Emit({
            .message = "No generated seed to send - generate one first.",
        });
        return;
    }

    std::ifstream spoilerFile(Ship::Context::LocateFileAcrossAppDirs(spoilerPath, appShortName));
    if (!spoilerFile) {
        // Fall back to the raw path: LocateFileAcrossAppDirs resolves relative names against the
        // app dir, but the CVar already holds a usable "./Randomizer/x.json" for the common case.
        spoilerFile.open(spoilerPath);
    }
    if (!spoilerFile) {
        SPDLOG_ERROR("[Anchor] SEVEN_SAGES_SEED: could not open spoiler at {}", spoilerPath);
        Notification::Emit({
            .message = "Could not read the generated seed file.",
        });
        return;
    }

    std::stringstream buffer;
    buffer << spoilerFile.rdbuf();
    spoilerFile.close();

    const std::string spoiler = buffer.str();

    // Chunked, because the relay does not deliver a packet this size.
    //
    // Measured 2026-08-11: both clients logged the send (Queuing + Sending) and NEITHER logged a
    // receive, with the client send path doing a single unbounded SDLNet_TCP_Send
    // (Network.cpp:49-52). So the ~110KB payload leaves the client intact and dies at the server.
    // The server is a separate Go project and not inspectable from here, but a Go bufio.Scanner
    // defaults to a 64KB token limit, which this comfortably exceeds.
    //
    // 2KB of spoiler text per packet, not 32KB: the limit is unknown, JSON escaping inflates the
    // payload (every newline and quote in the file becomes two characters), and ~55 small packets
    // cost nothing on a link that carries per-frame player updates. Sized to survive a much
    // stricter cap than the one actually suspected.
    constexpr size_t kChunkSize = 2048;
    const size_t totalChunks = (spoiler.size() + kChunkSize - 1) / kChunkSize;

    for (size_t i = 0; i < totalChunks; i++) {
        nlohmann::json payload;
        payload["type"] = SEVEN_SAGES_SEED;
        payload["targetTeamId"] = CVarGetString(CVAR_REMOTE_ANCHOR("TeamId"), "default");
        payload["chunk"] = i;
        payload["chunks"] = totalChunks;
        payload["spoiler"] = spoiler.substr(i * kChunkSize, kChunkSize);
        // Suppressed from the debug log. Dumping every chunk wrote the whole spoiler to disk twice
        // over and pushed a single session's log past a megabyte, which buries everything else.
        payload["quiet"] = true;

        SendJsonToRemote(payload);
    }

    SPDLOG_INFO("[Anchor] SEVEN_SAGES_SEED: sent {} bytes in {} chunks", spoiler.size(), totalChunks);

    Notification::Emit({
        .message = "Seed sent to the room.",
    });
}

void Anchor::HandlePacket_SevenSagesSeed(nlohmann::json payload) {
    if (!payload.contains("spoiler")) {
        return;
    }

    const uint32_t clientId = payload.value("clientId", (uint32_t)0);
    const size_t chunkIndex = payload.value("chunk", (size_t)0);
    const size_t chunkCount = payload.value("chunks", (size_t)1);
    const std::string chunk = payload.at("spoiler").get<std::string>();

    // Reassembly buffer per sender. Keyed by client so two people sending at once cannot interleave
    // into one corrupt file - which is not hypothetical here, since both players generating is
    // exactly the mistake this whole feature exists to make impossible.
    static std::map<uint32_t, std::string> pending;

    if (chunkIndex == 0) {
        pending[clientId].clear();
        pending[clientId].reserve(chunkCount * 2048);
    } else if (!pending.contains(clientId)) {
        // Joined mid-transfer, or chunk 0 was lost. Nothing useful can be built from the remainder,
        // and silently keeping a partial file would be worse than waiting for a re-send.
        SPDLOG_WARN("[Anchor] SEVEN_SAGES_SEED: chunk {} with no transfer in progress - ignoring", chunkIndex);
        return;
    }

    pending[clientId] += chunk;

    if (chunkIndex + 1 < chunkCount) {
        return; // more to come
    }

    const std::string spoiler = pending[clientId];
    pending.erase(clientId);

    if (spoiler.empty()) {
        return;
    }

    const std::string senderName = clients.contains(clientId) ? clients[clientId].name : "A teammate";

    // Land it in this profile's own Randomizer/ folder, under a fixed name. Fixed rather than
    // hash-derived so repeated sends replace one file instead of accumulating near-identical
    // spoilers - the hash is inside the file, and the room list is what tells you whether the
    // worlds match.
    const std::string dir = Ship::Context::GetPathRelativeToAppDirectory("Randomizer");
    try {
        if (!std::filesystem::exists(dir)) {
            std::filesystem::create_directory(dir);
        }
    } catch (const std::exception& e) {
        SPDLOG_ERROR("[Anchor] SEVEN_SAGES_SEED: could not create {}: {}", dir, e.what());
        return;
    }

    const std::string path = dir + "/received-seed.json";
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) {
            SPDLOG_ERROR("[Anchor] SEVEN_SAGES_SEED: could not write {}", path);
            return;
        }
        out << spoiler;
    }

    // The same entry point a dragged-in file uses, which is the whole point of shipping the file
    // rather than the placements.
    Randomizer_ParseSpoiler(path.c_str());

    if (!Rando::Context::GetInstance()->IsSpoilerLoaded()) {
        SPDLOG_ERROR("[Anchor] SEVEN_SAGES_SEED: ParseSpoiler rejected the received file ({} bytes)", spoiler.size());
        Notification::Emit({
            .prefix = senderName,
            .message = "sent a seed, but it could not be loaded.",
        });
        return;
    }

    // From here the sage select screen must not generate over this world. Cleared again the moment
    // this client generates a seed of its own.
    SevenSagesCoop_SetHasReceivedSeed(true);

    SPDLOG_INFO("[Anchor] SEVEN_SAGES_SEED: loaded {} bytes from '{}'", spoiler.size(), senderName);
    Notification::Emit({
        .prefix = senderName,
        .message = "sent their seed - pick your sage and Start Randomizer.",
    });
}
