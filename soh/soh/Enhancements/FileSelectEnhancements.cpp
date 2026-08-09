#include "FileSelectEnhancements.h"

#include "soh/OTRGlobals.h"
#include "soh/SohGui/SohModals.h"
#include "soh/SohGui/SohGui.hpp"
#include "soh/Enhancements/Presets/Presets.h"

#include <array>
#include <string>

std::array<std::string, LANGUAGE_MAX> RandomizerSettingsMenuText[RSM_MAX] = {
    {
        // English
        "Start Randomizer",
        // German
        "Randomizer starten",
        // French
        "Commencer le Randomizer",
    },
    {
        // English
        "Generate New Randomizer Seed",
        // German
        "Neuen Randomizer Seed generieren",
        // French
        "Générer une nouvelle seed pour le Randomizer",
    },
    {
        // English
        "Open Randomizer Settings",
        // German
        "Randomizer Optionen öffnen",
        // French
        "Ouvrir les paramètres du Randomizer",
    },
    {
        // English
        "Generating...",
        // German
        "Generiere...",
        // French
        "Génération en cours...",
    },
    { // English
      "No randomizer seed loaded.\nPlease generate one first"
#if defined(__WIIU__) || defined(__SWITCH__)
      ".",
#else
      ",\nor drop a spoiler log on the game window.",
#endif
      // German
      "Kein Randomizer Seed gefunden.\nBitte generiere zuerst einen"
#if defined(__WIIU__) || defined(__SWITCH__)
      ".",
#else
      ",\noder ziehe ein Spoiler Log\nauf das Spielfenster.",
#endif
      // French
      "Aucune Seed de Randomizer actuellement disponible.\nGénérez-en une dans les \"Randomizer Settings\""
#if (defined(__WIIU__) || defined(__SWITCH__))
      "."
#else
      "\nou glissez un spoilerlog sur la fenêtre du jeu."
#endif
    },
};

const char* SohFileSelect_GetSettingText(uint8_t optionIndex, uint8_t language) {
    return RandomizerSettingsMenuText[optionIndex][language].c_str();
}

void SohFileSelect_ShowPresetMenu() {
    SohGui::ShowEscMenu();
    CVarSetString(CVAR_SETTING("Menu.ActiveHeader"), "Settings");
    CVarSetString(CVAR_SETTING("Menu.SettingsSidebarSection"), "Presets");
    CVarSetInteger(CVAR_GENERAL("HasSeenPresetModal"), 1);
}

void SohFileSelect_DismissPresetModal() {
    CVarSetInteger(CVAR_GENERAL("HasSeenPresetModal"), 1);
}

// Seven Sages ships as two presets that both have to be applied before the seed is generated: the
// rando settings shape the seed itself, the enhancements shape how it plays. Applying one and
// missing the other doesn't error - it produces a run that's subtly wrong rather than obviously
// broken, which is far worse to debug from a player's chair. So the quest applies both itself
// rather than asking. This used to be a "Set up Seven Sages?" modal, from back when the mod rode on
// the plain Randomizer entry and had no way to know you meant it; the dedicated quest type is that
// signal, and the confirmation became a click with only one sensible answer.
//
// Deliberately called where the player confirms the Seven Sages quest (z_file_choose.c), NOT where
// the vanilla preset modal fires. That one runs on "Start Randomizer", by which point the seed
// already exists and applying rando settings would change nothing about it.
//
// Unconditional, every time the quest is confirmed - not once-ever behind a "configured" flag. A
// player who applies "Enhancements - Curated Randomizer" for a normal rando run in between would
// otherwise come back to Seven Sages in exactly the half-configured state described above, with
// nothing left to tell them. The cost is that hand-tweaked enhancements don't survive starting
// another Seven Sages file, which is the cheaper failure: it's visible, and the ESC menu undoes it.
static const char* kSevenSagesPresets[] = {
    "Rando Seed Settings - Seven Sages",
    "Enhancements - Seven Sages",
};

extern "C" void SohFileSelect_ApplySevenSagesPresets() {
    for (const char* preset : kSevenSagesPresets) {
        // Empty section list means "every section the preset declares". applyPreset handles the
        // randomizer-section refresh (UpdateAllOptions / UpdateMenuTricks) internally.
        applyPreset(preset, {});
    }
}

void SohFileSelect_ShowPresetModal() {
    if (CVarGetInteger(CVAR_GENERAL("HasSeenPresetModal"), 0)) {
        return;
    }
    std::shared_ptr<SohModalWindow> modal = static_pointer_cast<SohModalWindow>(
        Ship::Context::GetRawInstance()->GetWindow()->GetGui()->GetGuiWindow("Modal Window"));
    if (modal->IsPopupOpen("Take a look at our presets!")) {
        modal->DismissPopup();
    } else {
        modal->RegisterPopup("Take a look at our presets!",
                             "\nHey there! Ship comes with a ton of options, but none of them are on by default,\n"
                             "even in randomizer. If you haven't already, we highly recommend applying the\n"
                             "\"Enhancements - Curated Randomizer\" preset for a great, curated out of the\n"
                             "box rando experience.\n"
                             "\n"
                             "Afterwards, consider taking a look at the rest of the ESC menu to further tweak\n"
                             "the experience to your liking!\n",
                             "Cool, show me the presets!", "Got it, just let me play!", SohFileSelect_ShowPresetMenu,
                             SohFileSelect_DismissPresetModal);
    }
}
