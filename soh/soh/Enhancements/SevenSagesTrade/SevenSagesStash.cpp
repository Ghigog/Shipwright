/*
 * Seven Sages trade box - the stash's storage and persistence.
 *
 * See SevenSagesStash.h for the shape of the thing and why it keeps its own storage rather than a
 * field on gSaveContext.
 */

#include "SevenSagesStash.h"

#include "soh/SaveManager.h"
#include "soh/ShipInit.hpp"
#include "soh/Enhancements/randomizer/randomizerTypes.h"

#include <spdlog/spdlog.h>

namespace {

struct StashEntry {
    int16_t randomizerGet;
    uint8_t depositorSage;
};

StashEntry sEntries[SEVEN_SAGES_STASH_CAPACITY];
uint8_t sCount = 0;

void SaveStash(SaveContext* saveContext, int sectionID, bool fullSave) {
    SaveManager::Instance->SaveData("count", sCount);
    SaveManager::Instance->SaveArray("items", SEVEN_SAGES_STASH_CAPACITY, [](size_t i) {
        SaveManager::Instance->SaveData("", sEntries[i].randomizerGet);
    });
    SaveManager::Instance->SaveArray("depositors", SEVEN_SAGES_STASH_CAPACITY, [](size_t i) {
        SaveManager::Instance->SaveData("", sEntries[i].depositorSage);
    });
}

void LoadStash() {
    SaveManager::Instance->LoadData("count", sCount);
    SaveManager::Instance->LoadArray("items", SEVEN_SAGES_STASH_CAPACITY, [](size_t i) {
        SaveManager::Instance->LoadData("", sEntries[i].randomizerGet);
    });
    SaveManager::Instance->LoadArray("depositors", SEVEN_SAGES_STASH_CAPACITY, [](size_t i) {
        SaveManager::Instance->LoadData("", sEntries[i].depositorSage);
    });

    // A count past capacity means a corrupt or hand-edited save. Clamping rather than trusting it
    // keeps every later loop in bounds; the alternative is reading past the array on the first
    // terminal the player opens.
    if (sCount > SEVEN_SAGES_STASH_CAPACITY) {
        SPDLOG_WARN("[SevenSages] stash count {} exceeds capacity, clamping", sCount);
        sCount = SEVEN_SAGES_STASH_CAPACITY;
    }
}

// Runs at file creation and again on every load, which is what makes it the right place to reset:
// the stash must never carry entries from the previous file into a new one. `isDebug` is the
// debug-save flag and is irrelevant here - an empty box is correct either way.
void InitStash(bool isDebug) {
    SevenSagesStash_Clear();
}

void RegisterSevenSagesStash() {
    // AddSaveFunction must run exactly once per name. ShipInit::InitAll() is re-run when a preset
    // is applied - which is precisely what happens when the player picks the Seven Sages quest at
    // file select - so this would otherwise register a second time mid-session.
    static bool sRegistered = false;
    if (sRegistered) {
        return;
    }
    sRegistered = true;

    SaveManager::Instance->AddInitFunction(InitStash);
    SaveManager::Instance->AddSaveFunction("sevenSagesStash", 1, SaveStash, true, SECTION_PARENT_NONE);
    SaveManager::Instance->AddLoadFunction("sevenSagesStash", 1, LoadStash);
}

} // namespace

extern "C" uint8_t SevenSagesStash_Count(void) {
    return sCount;
}

extern "C" int16_t SevenSagesStash_Get(uint8_t index, uint8_t* outDepositorSage) {
    if (index >= sCount) {
        return RG_NONE;
    }
    if (outDepositorSage != nullptr) {
        *outDepositorSage = sEntries[index].depositorSage;
    }
    return sEntries[index].randomizerGet;
}

extern "C" bool SevenSagesStash_Add(int16_t randomizerGet, uint8_t depositorSage) {
    if (sCount >= SEVEN_SAGES_STASH_CAPACITY) {
        return false;
    }
    sEntries[sCount].randomizerGet = randomizerGet;
    sEntries[sCount].depositorSage = depositorSage;
    sCount++;
    return true;
}

extern "C" bool SevenSagesStash_RemoveAt(uint8_t index) {
    if (index >= sCount) {
        return false;
    }
    for (uint8_t i = index; i + 1 < sCount; i++) {
        sEntries[i] = sEntries[i + 1];
    }
    sCount--;
    sEntries[sCount].randomizerGet = RG_NONE;
    sEntries[sCount].depositorSage = 0;
    return true;
}

extern "C" void SevenSagesStash_Clear(void) {
    for (uint8_t i = 0; i < SEVEN_SAGES_STASH_CAPACITY; i++) {
        sEntries[i].randomizerGet = RG_NONE;
        sEntries[i].depositorSage = 0;
    }
    sCount = 0;
}

// No IS_RANDO condition: the save section has to be registered on every launch regardless of quest,
// or a Seven Sages file saved in one session would find no handler for its stash in the next.
static RegisterShipInitFunc sevenSagesStashInitFunc(RegisterSevenSagesStash);
