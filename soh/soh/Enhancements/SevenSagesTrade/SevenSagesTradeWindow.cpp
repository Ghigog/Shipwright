/*
 * Seven Sages trade box - the deposit/withdraw UI.
 *
 * Registers its own GuiWindow through the Gui instance directly rather than being added in
 * SohGui.cpp. That costs zero shared-file edits, which is the property keeping this module
 * splittable onto its own branch (CLAUDE.md) - and it is a step better than Enrich World's placer
 * window, which does take the SohGui.cpp route and is listed there as a shared touch point.
 *
 * The window only draws while the player is standing at a terminal. It is not a menu you can open
 * from anywhere: the terminal is the access point, and the stash is only reachable through one.
 */

#include "SevenSagesTrade.h"
#include "SevenSagesStash.h"
#include "SevenSagesTerminal.h"

#include "soh/SohGui/SohGui.hpp"
#include "soh/SohGui/SohMenu.h"
#include "soh/SohGui/UIWidgets.hpp"
#include "soh/Network/Anchor/Anchor.h"
#include "soh/Notification/Notification.h"
#include "soh/Enhancements/randomizer/static_data.h"
#include "soh/Enhancements/randomizer/savefile.h"
#include "soh/ShipInit.hpp"

#include <libultraship/bridge.h>
#include <ship/window/gui/GuiWindow.h>
#include <libultraship/libultraship.h>

extern "C" {
#include "z64.h"
#include "macros.h"
#include "variables.h"

extern PlayState* gPlayState;
void SevenSagesTerminal_SpawnAtPlayer(void);
uint8_t Randomizer_GetSettingValue(RandomizerSettingKey randoSettingKey);
}

namespace SohGui {
extern std::shared_ptr<SohMenu> mSohMenu;
} // namespace SohGui

namespace {

// Indexed by RO_SAGE_*, same order as sSageDefinitions in savefile.cpp.
const char* kSageNames[] = {
    "Rauru", "Saria", "Darunia", "Ruto", "Impa", "Nabooru", "Zelda",
};

const char* SageName(uint8_t sage) {
    return sage < ARRAY_COUNT(kSageNames) ? kSageNames[sage] : "Someone";
}

const char* ItemName(int16_t randomizerGet) {
    return Rando::StaticData::RetrieveItem((RandomizerGet)randomizerGet).GetName().GetEnglish().c_str();
}

// Broadcast after every local change. Safe to call with Anchor absent or disconnected - a
// single-player run simply keeps a stash that persists, which is the same code path.
void BroadcastStash() {
    if (Anchor::Instance != nullptr && Anchor::Instance->isConnected) {
        Anchor::Instance->SendPacket_SevenSagesStash();
    }
}

// Everything the local player could put in the box right now, rebuilt each frame.
//
// Walking the tradeable table and asking CanDeposit per item is deliberate, rather than walking
// the inventory: the table is the authority on what can move, and driving the UI from the same
// predicate that enforces the rule means the two can never disagree about a given item.
void DrawDepositPane() {
    ImGui::TextWrapped("Yours to give");
    ImGui::Separator();

    bool anyOffered = false;

    for (int16_t rg = 1; rg < RG_MAX; rg++) {
        if (!SevenSagesTrade_IsTransferableObject(rg)) {
            continue;
        }

        const SevenSagesTradeVerdict verdict = SevenSagesTrade_CanDeposit(rg);
        if (verdict == SEVEN_SAGES_TRADE_NOT_HELD || verdict == SEVEN_SAGES_TRADE_NOT_AN_OBJECT) {
            // Not carrying it at all - showing every item in the game greyed out would bury the
            // handful that matter.
            continue;
        }

        anyOffered = true;
        ImGui::PushID(rg);

        if (verdict == SEVEN_SAGES_TRADE_OK) {
            if (ImGui::Button(ItemName(rg))) {
                if (!SevenSagesTrade_TakeItem(rg)) {
                    Notification::Emit({ .message = "Could not set that down." });
                } else if (!SevenSagesStash_Add(rg, (uint8_t)Randomizer_GetSettingValue(RSK_SELECTED_SAGE))) {
                    // Full box: give it straight back rather than destroying it. Conservation is
                    // the invariant this whole module is built around.
                    SevenSagesTrade_GiveItem(rg);
                    Notification::Emit({ .message = "The box is full." });
                } else {
                    BroadcastStash();
                }
            }
        } else {
            // Shown but refused, with the reason. A greyed slot that does not say why is the thing
            // the verdict enum exists to prevent.
            ImGui::BeginDisabled();
            ImGui::Button(ItemName(rg));
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextDisabled("%s", SevenSagesTrade_RefusalText(verdict));
        }

        ImGui::PopID();
    }

    if (!anyOffered) {
        ImGui::TextDisabled("Nothing you carry can be set down here.");
    }
}

void DrawWithdrawPane() {
    ImGui::TextWrapped("In the box");
    ImGui::Separator();

    const uint8_t count = SevenSagesStash_Count();
    if (count == 0) {
        ImGui::TextDisabled("Empty.");
        return;
    }

    for (uint8_t i = 0; i < count; i++) {
        uint8_t depositor = 0;
        const int16_t rg = SevenSagesStash_Get(i, &depositor);
        if (rg == RG_NONE) {
            continue;
        }

        ImGui::PushID(i);
        if (ImGui::Button(ItemName(rg))) {
            if (SevenSagesTrade_GiveItem(rg)) {
                SevenSagesStash_RemoveAt(i);
                BroadcastStash();
            } else {
                Notification::Emit({ .message = "Could not take that." });
            }
            ImGui::PopID();
            // The list just changed underneath the loop; stop drawing it this frame.
            return;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("left by %s", SageName(depositor));
        ImGui::PopID();
    }
}

class SevenSagesTradeWindow final : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;

    void InitElement() override {
    }
    void UpdateElement() override {
    }

    void DrawElement() override {
        // The terminal owns the open state, so walking away closes this even though the window
        // itself is what is visible.
        if (!SevenSagesTerminal_IsOpen()) {
            return;
        }

        ImGui::SetNextWindowSize(ImVec2(720, 420), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Seven Sages - Trade Box", nullptr, ImGuiWindowFlags_NoFocusOnAppearing)) {
            ImGui::TextWrapped("One box, shared by every sage. What you leave here waits for whoever "
                               "comes next - they need not be here now, or even in your age.");
            ImGui::Separator();

            if (ImGui::BeginTable("##tradePanes", 2,
                                  ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame)) {
                ImGui::TableNextColumn();
                DrawDepositPane();
                ImGui::TableNextColumn();
                DrawWithdrawPane();
                ImGui::EndTable();
            }

            ImGui::Separator();
            if (ImGui::Button("Close")) {
                SevenSagesTerminal_Close();
            }
        }
        ImGui::End();
    }
};

std::shared_ptr<SevenSagesTradeWindow> sWindow;

void RegisterSevenSagesTradeWindow() {
    if (sWindow != nullptr) {
        return;
    }

    auto context = Ship::Context::GetRawInstance();
    if (context == nullptr || context->GetWindow() == nullptr || context->GetWindow()->GetGui() == nullptr) {
        // Registration runs from a ShipInit func, which can fire before the Gui exists. Bailing is
        // correct rather than fatal - the next init pass picks it up.
        return;
    }

    sWindow = std::make_shared<SevenSagesTradeWindow>(CVAR_WINDOW("SevenSagesTradeBox"), "Seven Sages Trade Box");
    context->GetWindow()->GetGui()->AddGuiWindow(sWindow);
    sWindow->Show();
}

void RegisterSevenSagesTradeMenu() {
    // Same sidebar page the co-op setup already registers, second column - the trade box belongs
    // with the room it is played in.
    WidgetPath path = { "Network", "Seven Sages Co-op", SECTION_COLUMN_2 };

    SohGui::mSohMenu->AddWidget(path, "Trade Box", WIDGET_SEPARATOR_TEXT);
    SohGui::mSohMenu->AddWidget(path, "Place a Terminal Here", WIDGET_BUTTON)
        .Callback([](WidgetInfo& info) { SevenSagesTerminal_SpawnAtPlayer(); })
        .Options(UIWidgets::ButtonOptions().Tooltip(
            "Spawn a trade terminal at your feet.\n"
            "\n"
            "For testing placement without walking to one of the fixed terminals - the Fishing Pond "
            "coordinate in particular is not derived from scene data and may need moving by eye."));
}

} // namespace

static RegisterShipInitFunc sevenSagesTradeWindowInitFunc(RegisterSevenSagesTradeWindow);
static RegisterMenuInitFunc sevenSagesTradeMenuInitFunc(RegisterSevenSagesTradeMenu);
