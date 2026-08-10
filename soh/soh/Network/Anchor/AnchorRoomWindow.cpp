#include "Anchor.h"
#include <ship/window/gui/IconsFontAwesome4.h>
#include "soh/OTRGlobals.h"
#include "soh/util.h"
#include "soh/Enhancements/randomizer/SeedContext.h"
#include "soh/Enhancements/SevenSagesCoop/SevenSagesCoop.h"

extern "C" {
#include "variables.h"
#include "functions.h"
#include "macros.h"
extern PlayState* gPlayState;
}

// Indexed by RO_SAGE_*, matching the ring order in SevenSagesSelectMenu.cpp. Display strings only -
// the authoritative table is sSageDefinitions in savefile.cpp.
static const char* kSevenSagesNames[] = {
    "Rauru", "Saria", "Darunia", "Ruto", "Impa", "Nabooru", "Zelda",
};

void AnchorRoomWindow::Draw() {
    if (!IsVisible() || !Anchor::Instance->isConnected) {
        return;
    }

    ImGui::PushStyleColor(ImGuiCol_WindowBg,
                          ImVec4(0, 0, 0, CVarGetFloat(CVAR_SETTING("Notifications.BgOpacity"), 0.5f)));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f);

    auto vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowViewport(vp->ID);

    ImGui::Begin("Anchor Room", nullptr,
                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
                     ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar);

    DrawElement();

    ImGui::End();

    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
}

void AnchorRoomWindow::DrawElement() {
    bool isGlobalRoom = (std::string("soh-global") == CVarGetString(CVAR_REMOTE_ANCHOR("RoomId"), ""));

    if (isGlobalRoom) {
        u32 activeClients = 0;
        for (auto& [clientId, client] : Anchor::Instance->clients) {
            if (client.online) {
                activeClients++;
            }
        }
        ImGui::Text("Players Online: %d", activeClients);
        return;
    }

    // First build a list of teams
    std::set<std::string> teams;
    for (auto& [clientId, client] : Anchor::Instance->clients) {
        teams.insert(client.teamId);
    }

    for (auto& team : teams) {
        if (teams.size() > 1) {
            ImGui::SeparatorText(team.c_str());
        }
        bool isOwnTeam = team == CVarGetString(CVAR_REMOTE_ANCHOR("TeamId"), "default");
        for (auto& [clientId, client] : Anchor::Instance->clients) {
            if (client.teamId != team) {
                continue;
            }

            ImGui::PushID(clientId);

            if (client.clientId == Anchor::Instance->roomState.ownerClientId) {
                ImGui::TextColored(ImVec4(1, 1, 0, 1), "%s", ICON_FA_GAVEL);
                ImGui::SameLine();
            }

            if (client.self) {
                ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "%s", CVarGetString(CVAR_REMOTE_ANCHOR("Name"), ""));
            } else if (!client.online) {
                ImGui::TextColored(ImVec4(1, 1, 1, 0.3f), "%s - offline", client.name.c_str());
                ImGui::PopID();
                continue;
            } else {
                ImGui::Text("%s", client.name.c_str());
            }

            // Seven Sages: who they are matters more than what they called themselves. Shown for
            // any client that reported a sage, so a mixed room still reads correctly.
            if (client.sage < ARRAY_COUNT(kSevenSagesNames)) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.9f, 0.8f, 1.0f, 0.9f), "(%s)", kSevenSagesNames[client.sage]);
            }

            if (Anchor::Instance->roomState.showLocationsMode == 2 ||
                (Anchor::Instance->roomState.showLocationsMode == 1 && isOwnTeam)) {
                if ((client.self ? Anchor::Instance->IsSaveLoaded() : client.isSaveLoaded)) {
                    ImGui::SameLine();
                    ImGui::TextColored(
                        ImVec4(1, 1, 1, 0.5f), "- %s",
                        SohUtils::GetSceneName(client.self ? gPlayState->sceneNum : client.sceneNum).c_str());
                }
            }

            if (Anchor::Instance->CanTeleportTo(client.clientId)) {
                ImGui::SameLine();
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
                if (ImGui::Button(ICON_FA_LOCATION_ARROW, ImVec2(20.0f, 20.0f))) {
                    Anchor::Instance->SendPacket_RequestTeleport(client.clientId);
                }
                ImGui::PopStyleVar();
            }

            if (client.clientVersion != Anchor::clientVersion) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1, 0, 0, 1), ICON_FA_EXCLAMATION_TRIANGLE);
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::Text("Incompatible version! Will not work together!");
                    ImGui::Text("Yours: %s", Anchor::clientVersion.c_str());
                    ImGui::Text("Theirs: %s", client.clientVersion.c_str());
                    ImGui::EndTooltip();
                }
            }
            // Seven Sages: compare the WORLD, not the seed string.
            //
            // `client.seed` is Hash(seedString) and is blind to settings, so two Seven Sages players
            // who typed the same seed string but picked different sages match here while holding
            // completely different item placements - the exact case this mod creates, and the one
            // the warning most needed to catch. The fingerprint hashes the placements themselves;
            // see SevenSagesCoop.h for why none of SoH's own seed values can be used. A 0 on either
            // side means "not comparable" (vanilla save, or a teammate on a build without this) and
            // is deliberately not a mismatch.
            uint32_t worldHash = SevenSagesCoop_GetWorldFingerprint();
            if (client.isSaveLoaded && Anchor::Instance->IsSaveLoaded() && client.online && !client.self &&
                worldHash != 0 && client.seedHash != 0 && client.seedHash != worldHash) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1, 0, 0, 1), ICON_FA_EXCLAMATION_TRIANGLE);
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::Text("Different world - NOT syncing with this player.");
                    ImGui::Text("Their item placement differs from yours: a different seed, or the");
                    ImGui::Text("same seed generated against a different sage roster.");
                    ImGui::Text("Yours: %u", worldHash);
                    ImGui::Text("Theirs: %u", client.seedHash);
                    ImGui::EndTooltip();
                }
            }
            ImGui::PopID();
        }
    }
}
