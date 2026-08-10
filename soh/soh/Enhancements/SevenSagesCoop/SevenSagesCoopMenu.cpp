/**
 * Seven Sages co-op - the setup UI.
 *
 * Deliberately registers its own page under Network rather than adding widgets to
 * SohMenuEnhancements.cpp or Anchor's own Menu.cpp. Registering from here costs zero shared-file
 * edits, which is the property that keeps this module splittable onto its own branch later (see
 * CLAUDE.md and SevenSagesCoop.h).
 *
 * Lives under Network because everything on it is about the room you're playing with, and because
 * that is where the Anchor connection settings it depends on already are.
 */
#include "SevenSagesCoop.h"

#include "soh/SohGui/SohGui.hpp"
#include "soh/SohGui/SohMenu.h"
#include "soh/SohGui/UIWidgets.hpp"
#include "soh/cvar_prefixes.h"

#include <libultraship/bridge.h>

extern "C" {
#include "macros.h"
}

namespace SohGui {
extern std::shared_ptr<SohMenu> mSohMenu;
} // namespace SohGui

namespace {

// Indexed by RO_SAGE_*, matching the ring order in SevenSagesSelectMenu.cpp. Display strings only;
// sSageDefinitions in savefile.cpp remains the authoritative table.
const char* kSageNames[] = {
    "Rauru", "Saria", "Darunia", "Ruto", "Impa", "Nabooru", "Zelda",
};

// Ages come from the same table. Shown here because a mixed-age roster is legal but has a
// consequence worth surfacing at the moment the roster is picked - see the tooltip.
const char* kSageAges[] = {
    "Adult", "Child", "Adult", "Child", "Adult", "Adult", "Child",
};

void DrawRoster(WidgetInfo& info) {
    ImGui::TextWrapped("Tick every sage that will be played in this run, then generate. "
                       "Only the host needs to do this.");
    ImGui::Spacing();

    uint8_t roster = SevenSagesCoop_GetRoster();
    bool changed = false;

    for (uint8_t sage = 0; sage < ARRAY_COUNT(kSageNames); sage++) {
        bool claimed = (roster & (1 << sage)) != 0;
        ImGui::PushID(sage);
        if (ImGui::Checkbox(kSageNames[sage], &claimed)) {
            roster = claimed ? (uint8_t)(roster | (1 << sage)) : (uint8_t)(roster & ~(1 << sage));
            changed = true;
        }
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1, 1, 1, 0.45f), "(%s)", kSageAges[sage]);
        ImGui::PopID();
    }

    if (changed) {
        SevenSagesCoop_SetRoster(roster);
    }

    ImGui::Spacing();

    // The roster has to be RIGHT, not merely non-empty, and both ways of being wrong are worth
    // naming because they fail differently:
    //   - too few ticked: an unticked player's kit items are also placed in the world, so they find
    //     duplicates of things they already hold. Wasteful, never fatal.
    //   - too many ticked: an unplayed sage's kit leaves the pool without anyone holding it, so
    //     those items exist nowhere. That can strand progression and make a seed unbeatable.
    int claimedCount = 0;
    for (uint8_t sage = 0; sage < ARRAY_COUNT(kSageNames); sage++) {
        if (roster & (1 << sage)) {
            claimedCount++;
        }
    }

    if (claimedCount == 0) {
        ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "No sages ticked - generating as a solo run.");
    } else {
        ImGui::TextColored(ImVec4(0.8f, 1, 0.8f, 1), "%d sage%s in this run.", claimedCount,
                           claimedCount == 1 ? "" : "s");
    }
}

void RegisterSevenSagesCoopMenu() {
    WidgetPath path = { "Network", "Seven Sages Co-op", SECTION_COLUMN_1 };
    SohGui::mSohMenu->AddSidebarEntry("Network", path.sidebarName, 1);

    SohGui::mSohMenu->AddWidget(path, "Seven Sages Co-op", WIDGET_SEPARATOR_TEXT);
    SohGui::mSohMenu->AddWidget(path,
                                "Play one Seven Sages world together, each as a different sage.\n"
                                "\n"
                                "Anchor's own co-op converges everyone to one shared inventory. This "
                                "replaces that: the WORLD is shared - checks, flags, doors, entrances - "
                                "while each sage keeps their own kit, keys and hearts.\n"
                                "\n"
                                "Set up the Anchor connection on the Anchor page first.",
                                WIDGET_TEXT);

    SohGui::mSohMenu->AddWidget(path, "Enable Co-op Mode", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_GENERAL("SevenSages.Coop"))
        .Options(UIWidgets::CheckboxOptions().Tooltip(
            "Stops Anchor sharing inventory, keys, hearts and magic between players, and draws each "
            "teammate in their own sage's colours. Only affects Seven Sages saves - a plain "
            "Randomizer or vanilla file connected to Anchor behaves exactly as it always has."));

    path.column = SECTION_COLUMN_2;
    SohGui::mSohMenu->AddWidget(path, "Seed Roster", WIDGET_SEPARATOR_TEXT);
    SohGui::mSohMenu->AddWidget(path, "Generate Against Roster", WIDGET_CVAR_CHECKBOX)
        .CVar(CVAR_GENERAL("SevenSages.CoopUseRoster"))
        .Options(UIWidgets::CheckboxOptions().Tooltip(
            "When generating, remove EVERY ticked sage's starting kit from the item pool instead of "
            "only your own.\n"
            "\n"
            "Without this, teammates find duplicates of kit items they already started with. With "
            "it, tick exactly the sages who will actually play: ticking someone who isn't playing "
            "deletes their kit from the world without giving it to anyone, which can make the seed "
            "unbeatable."));
    SohGui::mSohMenu->AddWidget(path, "SevenSagesCoopRoster", WIDGET_CUSTOM)
        .CustomFunction(DrawRoster)
        .HideInSearch(true);
}

} // namespace

static RegisterMenuInitFunc sevenSagesCoopMenuInitFunc(RegisterSevenSagesCoopMenu);
