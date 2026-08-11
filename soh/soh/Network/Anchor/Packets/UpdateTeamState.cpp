#include "soh/Network/Anchor/Anchor.h"
#include "soh/Network/Anchor/JsonConversions.hpp"
#include <nlohmann/json.hpp>
#include "soh/OTRGlobals.h"
#include "soh/Notification/Notification.h"
#include "soh/Enhancements/randomizer/randomizer.h"
#include "soh/Enhancements/SevenSagesCoop/SevenSagesCoop.h"
#include "soh/Enhancements/SevenSages/SevenSagesTempHearts.h"

extern "C" {
#include "variables.h"
extern PlayState* gPlayState;
}

/**
 * UPDATE_TEAM_STATE
 *
 * Pushes the current save state to the server for other teammates to use.
 *
 * Fires when the server passes on a REQUEST_TEAM_STATE packet, or when this client saves the game
 *
 * When sending this packet we will assume that the team queue has been emptied for this client, so the queue
 * stored in the server will be cleared.
 *
 * When receiving this packet, if there is items in the team queue, we will play them back in order.
 */

void Anchor::SendPacket_UpdateTeamState() {
    if (!IsSaveLoaded() || !roomState.syncItemsAndFlags) {
        return;
    }

    json payload;
    payload["type"] = UPDATE_TEAM_STATE;
    payload["targetTeamId"] = CVarGetString(CVAR_REMOTE_ANCHOR("TeamId"), "default");

    // Assume the team queue has been emptied, so clear it
    payload["queue"] = json::array();

    payload["state"] = gSaveContext;

    // Seven Sages: strip the Sun's Song temporary hearts out of the snapshot.
    //
    // SaveManager::SaveFile wraps its own snapshot in Suspend/Restore (SaveManager.cpp:1250-1260)
    // so the buff never reaches the save file. This packet is not covered by that pair: it is sent
    // from the OnSaveFile hook, which fires at the END of SaveFileThreaded (SaveManager.cpp:1226)
    // on the save thread, by which point the main thread has already run Restore. So the
    // gSaveContext just serialised still has the buff applied, and the inflated healthCapacity
    // would reach the whole team as permanent hearts.
    //
    // Patched in the JSON rather than by calling Suspend/Restore here, and that distinction
    // matters: those two share a file-scope static and their header requires them to be paired
    // within one frame, so calling them from this thread would race the main thread's own pair and
    // briefly move the live player's health as a side effect. Reading the pool is safe; moving it
    // is not.
    //
    // Unconditional rather than co-op-gated - temp hearts inflating a teammate's real heart
    // capacity is wrong in a vanilla Anchor run too.
    //
    // Only healthCapacity needs patching: `health` is not part of this packet's SaveContext
    // serialization at all (JsonConversions.hpp:164-179), so current HP never crosses the wire.
    int16_t ownCapacity = 0;
    if (SevenSagesPeekTempHeartCapacity(&ownCapacity)) {
        payload["state"]["healthCapacity"] = ownCapacity;
    }

    // manually update current scene flags
    payload["state"]["sceneFlags"][gPlayState->sceneNum * 4] = gPlayState->actorCtx.flags.chest;
    payload["state"]["sceneFlags"][gPlayState->sceneNum * 4 + 1] = gPlayState->actorCtx.flags.swch;
    payload["state"]["sceneFlags"][gPlayState->sceneNum * 4 + 2] = gPlayState->actorCtx.flags.clear;
    payload["state"]["sceneFlags"][gPlayState->sceneNum * 4 + 3] = gPlayState->actorCtx.flags.collect;

    // The commented out code below is an attempt at sending the entire randomizer seed over, in hopes that a player
    // doesn't have to generate the seed themselves Currently it doesn't work :)
    if (IS_RANDO) {
        auto randoContext = Rando::Context::GetInstance();

        payload["state"]["rando"] = json::object();
        payload["state"]["rando"]["itemLocations"] = json::array();
        for (int i = 0; i < RC_MAX; i++) {
            payload["state"]["rando"]["itemLocations"][i] = json::array();
            // payload["state"]["rando"]["itemLocations"][i]["rgID"] =
            // randoContext->GetItemLocation(i)->GetPlacedRandomizerGet();
            payload["state"]["rando"]["itemLocations"][i][0] = randoContext->GetItemLocation(i)->GetCheckStatus();
            payload["state"]["rando"]["itemLocations"][i][1] = (u8)randoContext->GetItemLocation(i)->GetIsSkipped();

            // if (randoContext->GetItemLocation(i)->GetPlacedRandomizerGet() == RG_ICE_TRAP) {
            //     payload["state"]["rando"]["itemLocations"][i]["fakeRgID"] =
            //     randoContext->GetItemOverride(i).LooksLike();
            //     payload["state"]["rando"]["itemLocations"][i]["trickName"] = json::object();
            //     payload["state"]["rando"]["itemLocations"][i]["trickName"]["english"] =
            //     randoContext->GetItemOverride(i).GetTrickName().GetEnglish();
            //     payload["state"]["rando"]["itemLocations"][i]["trickName"]["french"] =
            //     randoContext->GetItemOverride(i).GetTrickName().GetFrench();
            // }
            // if (randoContext->GetItemLocation(i)->HasCustomPrice()) {
            //     payload["state"]["rando"]["itemLocations"][i]["price"] =
            //     randoContext->GetItemLocation(i)->GetPrice();
            // }
        }

        // auto entranceCtx = randoContext->GetEntranceShuffler();
        // for (int i = 0; i < ENTRANCE_OVERRIDES_MAX_COUNT; i++) {
        //     payload["state"]["rando"]["entrances"][i] = json::object();
        //     payload["state"]["rando"]["entrances"][i]["type"] = entranceCtx->entranceOverrides[i].type;
        //     payload["state"]["rando"]["entrances"][i]["index"] = entranceCtx->entranceOverrides[i].index;
        //     payload["state"]["rando"]["entrances"][i]["destination"] = entranceCtx->entranceOverrides[i].destination;
        //     payload["state"]["rando"]["entrances"][i]["override"] = entranceCtx->entranceOverrides[i].override;
        //     payload["state"]["rando"]["entrances"][i]["overrideDestination"] =
        //     entranceCtx->entranceOverrides[i].overrideDestination;
        // }

        // payload["state"]["rando"]["seed"] = json::array();
        // for (int i = 0; i < randoContext->hashIconIndexes.size(); i++) {
        //     payload["state"]["rando"]["seed"][i] = randoContext->hashIconIndexes[i];
        // }
        // payload["state"]["rando"]["inputSeed"] = randoContext->GetSeedString();
        // payload["state"]["rando"]["finalSeed"] = randoContext->GetSeed();

        // payload["state"]["rando"]["randoSettings"] = json::array();
        // for (int i = 0; i < RSK_MAX; i++) {
        //     payload["state"]["rando"]["randoSettings"][i] =
        //     randoContext->GetOption((RandomizerSettingKey(i))).GetSelectedOptionIndex();
        // }

        // payload["state"]["rando"]["masterQuestDungeonCount"] = randoContext->GetDungeons()->CountMQ();
        // payload["state"]["rando"]["masterQuestDungeons"] = json::array();
        // for (int i = 0; i < randoContext->GetDungeons()->GetDungeonListSize(); i++) {
        //     payload["state"]["rando"]["masterQuestDungeons"][i] = randoContext->GetDungeon(i)->IsMQ();
        // }
        // for (int i = 0; i < randoContext->GetTrials()->GetTrialListSize(); i++) {
        //     payload["state"]["rando"]["requiredTrials"][i] = randoContext->GetTrial(i)->IsRequired();
        // }
    }

    SendJsonToRemote(payload);
}

void Anchor::SendPacket_ClearTeamState(std::string teamId) {
    json payload;
    payload["type"] = UPDATE_TEAM_STATE;
    payload["targetTeamId"] = teamId;
    payload["queue"] = json::array();
    payload["state"] = json::object();
    SendJsonToRemote(payload);
}

void Anchor::HandlePacket_UpdateTeamState(nlohmann::json payload) {
    // Seven Sages co-op: refuse world state from a client in a different item placement. Their
    // flags describe a world this save does not have. See SevenSagesCoop.h.
    //
    // requireVerified, unlike the incremental flag packets: this is a whole-world snapshot the
    // server stores per team and replays to whoever asks, so an unverifiable one is precisely the
    // dangerous case rather than a harmless edge. Playtest 2026-08-11 - a player created a brand
    // new file, joined a room that still held a snapshot from an earlier session, and found Mido's
    // chests already opened by nobody.
    if (!ShouldAcceptWorldStateFrom(payload, /* requireVerified */ true)) {
        return;
    }

    if (!roomState.syncItemsAndFlags) {
        return;
    }

    isHandlingUpdateTeamState = true;
    // This can happen in between file select and the game starting, so we can't use this check, but we need to ensure
    // we be careful to wrap PlayState usage in this check
    //
    // if (!IsSaveLoaded()) {
    //     return;
    // }

    if (payload.contains("state")) {
        SaveContext loadedData = payload["state"].get<SaveContext>();

        // Seven Sages co-op: every capacity stat below is personal, not team property.
        //
        // This block is easy to read as "just hearts and magic", but the magic meter is literally
        // an item in Rauru's kit (RSK_STARTING_MAGIC_METER, savefile.cpp's sage table), so copying
        // it from a teammate dissolves a kit distinction on its own - before the inventory
        // overwrite further down ever runs. Heart containers, double magic, double defence and the
        // Biggoron sword state are all the same kind of thing: something one player earned.
        //
        // gSaveContext.ship.quest is NOT skipped. It carries the quest id that IS_SEVENSAGES tests,
        // and everyone in the room is on the same quest by construction - a mismatched client is
        // refused before it gets here (see the seed hash check in HandlePacket_UpdateClientState).
        if (!SevenSagesCoop_ShouldSuppressItemSync()) {
            gSaveContext.healthCapacity = loadedData.healthCapacity;
            gSaveContext.magicLevel = loadedData.magicLevel;
            gSaveContext.magicCapacity = loadedData.magicCapacity;
            gSaveContext.magic = static_cast<s8>(loadedData.magicCapacity);
            gSaveContext.isMagicAcquired = loadedData.isMagicAcquired;
            gSaveContext.isDoubleMagicAcquired = loadedData.isDoubleMagicAcquired;
            gSaveContext.isDoubleDefenseAcquired = loadedData.isDoubleDefenseAcquired;
            gSaveContext.bgsFlag = loadedData.bgsFlag;
            gSaveContext.swordHealth = loadedData.swordHealth;
        }
        gSaveContext.ship.quest = loadedData.ship.quest;

        for (int i = 0; i < 124; i++) {
            if (i == SCENE_WATER_TEMPLE) {
                // Keep water temple water level flags
                u32 mask = (1 << 0x1C) | (1 << 0x1D) | (1 << 0x1E);
                loadedData.sceneFlags[i].swch =
                    (loadedData.sceneFlags[i].swch & ~mask) | (gSaveContext.sceneFlags[i].swch & mask);
            }

            if (i == SCENE_FOREST_TEMPLE) {
                // Keep forest temple elevator flag
                u32 mask = (1 << 0x1B);
                loadedData.sceneFlags[i].swch =
                    (loadedData.sceneFlags[i].swch & ~mask) | (gSaveContext.sceneFlags[i].swch & mask);
            }

            if (i == SCENE_GANONS_TOWER_COLLAPSE_EXTERIOR) {
                // Keep collapse timer flag
                u32 mask = (1 << 0x17);
                loadedData.sceneFlags[i].swch =
                    (loadedData.sceneFlags[i].swch & ~mask) | (gSaveContext.sceneFlags[i].swch & mask);
            }

            gSaveContext.sceneFlags[i] = loadedData.sceneFlags[i];
            if (IsSaveLoaded() && gPlayState->sceneNum == i) {
                gPlayState->actorCtx.flags.chest = loadedData.sceneFlags[i].chest;
                gPlayState->actorCtx.flags.swch = loadedData.sceneFlags[i].swch;
                gPlayState->actorCtx.flags.clear = loadedData.sceneFlags[i].clear;
                gPlayState->actorCtx.flags.collect = loadedData.sceneFlags[i].collect;
            }
        }

        for (int i = 0; i < 14; i++) {
            gSaveContext.eventChkInf[i] |= loadedData.eventChkInf[i];
        }

        for (int i = 0; i < 4; i++) {
            gSaveContext.itemGetInf[i] |= loadedData.itemGetInf[i];
        }

        // Skip last row of infTable, don't want to sync swordless flag
        for (int i = 0; i < 29; i++) {
            gSaveContext.infTable[i] |= loadedData.infTable[i];
        }

        for (int i = 0; i < ceil((RAND_INF_MAX + 15) / 16); i++) {
            gSaveContext.ship.randomizerInf[i] |= loadedData.ship.randomizerInf[i];
        }

        for (int i = 0; i < 6; i++) {
            gSaveContext.gsFlags[i] |= loadedData.gsFlags[i];
        }

        gSaveContext.ship.stats.firstInput = loadedData.ship.stats.firstInput;
        gSaveContext.ship.stats.fileCreatedAt = loadedData.ship.stats.fileCreatedAt;

        // Ensure ganon barrier state matches trials
        if (gSaveContext.eventChkInf[10] & 0x2000 && gSaveContext.eventChkInf[11] & 0xFC00) {
            gSaveContext.eventChkInf[12] |= 0x8;
        }

        // Restore master sword state
        // Disabling this for now, not really sure I understand why I did this in the past
        // u8 hasMasterSword = CHECK_OWNED_EQUIP(EQUIP_TYPE_SWORD, 1);
        // if (hasMasterSword) {
        //     loadedData.inventory.equipment |= 0x2;
        // } else {
        //     loadedData.inventory.equipment &= ~0x2;
        // }

        // Restore bottle contents (unless it's ruto's letter)
        for (int i = 0; i < 4; i++) {
            if (gSaveContext.inventory.items[SLOT_BOTTLE_1 + i] != ITEM_NONE &&
                gSaveContext.inventory.items[SLOT_BOTTLE_1 + i] != ITEM_LETTER_RUTO) {
                loadedData.inventory.items[SLOT_BOTTLE_1 + i] = gSaveContext.inventory.items[SLOT_BOTTLE_1 + i];
            }
        }

        // Restore ammo if it's non-zero, unless it's beans
        for (int i = 0; i < ARRAY_COUNT(gSaveContext.inventory.ammo); i++) {
            if (gSaveContext.inventory.ammo[i] != 0 && i != SLOT(ITEM_BEAN) && i != SLOT(ITEM_BEAN + 1)) {
                loadedData.inventory.ammo[i] = gSaveContext.inventory.ammo[i];
            }
        }

        // Seven Sages co-op: the wholesale inventory overwrite is the single line that most defines
        // Anchor's "everyone converges to one bag" model, and the one this mod exists to reject.
        // Each sage's inventory stays their own; what the team shares is the world above.
        //
        // The exception is Knowledge - medallions, spiritual stones and songs. Those are progress
        // markers rather than objects (decision 4), and they are OR-merged rather than assigned so
        // this can only ever ADD to what the player knows. GIVE_ITEM carries them live; this is the
        // path that catches a player up on everything the team learned while they were away.
        if (!SevenSagesCoop_ShouldSuppressItemSync()) {
            gSaveContext.inventory = loadedData.inventory;
        } else {
            gSaveContext.inventory.questItems |=
                (loadedData.inventory.questItems & SEVEN_SAGES_COOP_KNOWLEDGE_QUEST_MASK);
        }

        // The commented out code below is an attempt at sending the entire randomizer seed over, in hopes that a player
        // doesn't have to generate the seed themselves Currently it doesn't work :)
        if (IS_RANDO && payload["state"].contains("rando")) {
            auto randoContext = Rando::Context::GetInstance();

            for (int i = 0; i < RC_MAX; i++) {
                auto itemLocation = payload["state"]["rando"].at("itemLocations").at(i);
                // randoContext->GetItemLocation(i)->RefPlacedItem() =
                // itemLocation.at("rgID").get<RandomizerGet>();
                OTRGlobals::Instance->gRandoContext->GetItemLocation(i)->SetCheckStatus(
                    itemLocation.at(0).get<RandomizerCheckStatus>());
                OTRGlobals::Instance->gRandoContext->GetItemLocation(i)->SetIsSkipped(itemLocation.at(1).get<u8>());

                // if (itemLocation.contains("fakeRgID")) {
                //     randoContext->overrides.emplace(static_cast<RandomizerCheck>(i),
                //     Rando::ItemOverride(static_cast<RandomizerCheck>(i),
                //     itemLocation.at("fakeRgID").get<RandomizerGet>()));
                //     randoContext->GetItemOverride(i).GetTrickName().english =
                //     itemLocation.at("trickName").at("english").get<std::string>();
                //     randoContext->GetItemOverride(i).GetTrickName().french =
                //     itemLocation.at("trickName").at("french").get<std::string>();
                // }
                // if (itemLocation.contains("price")) {
                //     u16 price = itemLocation.at("price"].get<u16>();
                //     if (price > 0) {
                //         randoContext->GetItemLocation(i)->SetCustomPrice(price);
                //     }
                // }
            }

            // auto entranceCtx = randoContext->GetEntranceShuffler();
            // for (int i = 0; i < ENTRANCE_OVERRIDES_MAX_COUNT; i++) {
            //     entranceCtx->entranceOverrides[i].type =
            //     payload["state"]["rando"]["entrances"][i]["type"].get<u16>(); entranceCtx->entranceOverrides[i].index
            //     = payload["state"]["rando"]["entrances"][i]["index"].get<s16>();
            //     entranceCtx->entranceOverrides[i].destination =
            //     payload["state"]["rando"]["entrances"][i]["destination"].get<s16>();
            //     entranceCtx->entranceOverrides[i].override =
            //     payload["state"]["rando"]["entrances"][i]["override"].get<s16>();
            //     entranceCtx->entranceOverrides[i].overrideDestination =
            //     payload["state"]["rando"]["entrances"][i]["overrideDestination"].get<s16>();
            // }

            // for (int i = 0; i < randoContext->hashIconIndexes.size(); i++) {
            //     randoContext->hashIconIndexes[i] = payload["state"]["rando"]["seed"][i].get<u8>();
            // }
            // randoContext->GetSettings()->SetSeedString(payload["state"]["rando"]["inputSeed"].get<std::string>());
            // randoContext->GetSettings()->SetSeed(payload["state"]["rando"]["finalSeed"].get<u32>());

            // for (int i = 0; i < RSK_MAX; i++) {
            //     randoContext->GetOption(RandomizerSettingKey(i)).SetSelectedIndex(payload["state"]["rando"]["randoSettings"][i].get<u8>());
            // }

            // randoContext->GetDungeons()->ClearAllMQ();
            // for (int i = 0; i < randoContext->GetDungeons()->GetDungeonListSize(); i++) {
            //     if (payload["state"]["rando"]["masterQuestDungeons"][i].get<bool>()) {
            //         randoContext->GetDungeon(i)->SetMQ();
            //     }
            // }

            // randoContext->GetTrials()->SkipAll();
            // for (int i = 0; i < randoContext->GetTrials()->GetTrialListSize(); i++) {
            //     if (payload["state"]["rando"]["requiredTrials"][i].get<bool>()) {
            //         randoContext->GetTrial(i)->SetAsRequired();
            //     }
            // }
        }

        Notification::Emit({
            .message = "Save updated from team",
        });
    }

    if (payload.contains("queue")) {
        std::lock_guard<std::mutex> lock(incomingPacketQueueMutex);
        for (auto& item : payload["queue"]) {
            nlohmann::json itemPayload = nlohmann::json::parse(item.get<std::string>());
            incomingPacketQueue.push(itemPayload);
        }
    }
    isHandlingUpdateTeamState = false;
}
