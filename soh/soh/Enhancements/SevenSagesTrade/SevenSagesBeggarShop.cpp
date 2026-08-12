/*
 * Seven Sages trade box - the beggar's shelf.
 *
 * The stash is presented as a shop you walk up to: a beggar standing at a set of shelves with the
 * stashed items sitting on them. Talk to her and you browse the shelf with the stick and take
 * something back with A. Press C at her with an item assigned and you leave that item instead.
 *
 * ── Why this is not En_Ossan ────────────────────────────────────────────────────────────────
 *
 * It was, and the spike proved a vanilla shop does run outside a shop room - shelves, cursor,
 * prices, item text and all. The reason it is gone is not that it failed but that keeping it cost
 * an override per feature: its draw had to be replaced to hide the shopkeeper, its camera taken
 * over (the browsing view is scene data, not actor behaviour), its `Actor_Find` shelf binding
 * worked around, and finally every per-item field on every En_GirlA rewritten each frame, because
 * En_GirlA's own item table is `static` (z_en_girla.c:167) and the one dynamic path it does have
 * routes through Randomizer_IdentifyShopItem - a shopsanity lookup keyed on the seed's spoiler
 * (OTRGlobals.cpp:2348), with no seam to hand it a stash item.
 *
 * Underneath all that, En_Ossan is a PURCHASE state machine: prices, rupee checks, sold-out,
 * mask borrowing, the Hylian Shield discount, eight fixed slots. Withdrawing your own property is
 * not a discounted purchase, and every one of those behaviours would have had to be suppressed.
 * At that point the only thing being reused was a cursor quad.
 *
 * ── What IS reused, and it is the parts that are actually parts ──────────────────────────────
 *
 *   En_Tana            the shelves, unmodified, spawned anywhere (see EnsureObjectLoaded).
 *   En_Hy BOJ_5        the beggar herself, unmodified.
 *   GetItemEntry_Draw  the one call that renders any item, including modded ones, from a
 *                      GetItemEntry - the same call En_GirlA ends its draw with
 *                      (z_en_girla.c:1466) and the same renderer as the item Link holds overhead.
 *   gSelectionCursorTex the shop selection cursor, drawn the way EnOssan_DrawCursor draws it.
 *   textId 0xFFFF      the documented silent-talk handshake. Player_StartTalking's own comment
 *                      says the player "will stand and look at the actor with no text appearing"
 *                      (z_player.c), which is exactly a browse mode: A reads as Speak, Player
 *                      consumes the press instead of rolling, and the actor holds him until it
 *                      releases with csAction 7.
 *
 * ── Object bookkeeping ──────────────────────────────────────────────────────────────────────
 *
 * En_Tana wants OBJECT_SHOP_DUNGEN and En_Hy wants OBJECT_BOJ and OBJECT_OS_ANIME, none of which
 * is resident in a field. That does not stop them rendering: SoH's DmaMgr_SendRequest1 is
 * `return 0;` with the real body #if 0'd out (z_std_dma.c:437), so no object is ever copied into
 * the bank and models resolve by OTR resource name. Registering an object is bookkeeping so the
 * actors' own Object_GetIndex / Object_IsLoaded gates pass, and Object_IsLoaded is just `id > 0`
 * (z_scene.c:131), so it takes effect the same frame.
 */
#include "SevenSagesStash.h"
#include "SevenSagesTrade.h"

#include "soh/ActorDB.h"
#include "soh/SohGui/SohGui.hpp"
#include "soh/SohGui/SohMenu.h"
#include "soh/SohGui/UIWidgets.hpp"
#include "soh/ShipInit.hpp"
#include "soh/OTRGlobals.h"
#include "soh/Network/Anchor/Anchor.h"
#include "soh/Notification/Notification.h"
#include "soh/Enhancements/custom-message/CustomMessageManager.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"

#include <spdlog/spdlog.h>

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
#include "objects/gameplay_keep/gameplay_keep.h" // gSelectionCursorTex

extern PlayState* gPlayState;

// Declared here rather than by including their headers: OTRGlobals.h and z_player.c's prototypes
// pull in far more than these four signatures, and functions.h is included by most of the tree, so
// adding to it costs a full rebuild (CLAUDE.md).
// OPEN_DISPS declares these itself at its point of use (macros.h:208), but a block-scope
// declaration in a C++ file resolves to the mangled name and fails to link against the C symbol.
// Declaring them extern "C" up here first makes the macro's redeclaration match.
void FrameInterpolation_RecordOpenChild(const void* a, int b);
void FrameInterpolation_RecordCloseChild(void);

GetItemEntry ItemTable_RetrieveEntry(s16 tableID, s16 getItemID);
u8 Randomizer_GetSettingValue(RandomizerSettingKey randoSettingKey);
s32 Player_GetItemOnButton(PlayState* play, s32 index);

// Defined in z_scene.c but never prototyped in functions.h. Declared here rather than added there
// for the reason CLAUDE.md gives: functions.h is included by most of the tree, so a one-line
// addition costs a full rebuild for a function only this file calls.
s32 Object_Spawn(ObjectContext* objectCtx, s16 objectId);
}

// ── Shared state ────────────────────────────────────────────────────────────────────────────
//
// Deliberately at file scope rather than on the actors: there is exactly one beggar in the world
// (see ClearShop) and the browse mode is a single global mode, so threading it through actor
// instances would be ceremony around a singleton.

namespace SohGui {
extern std::shared_ptr<SohMenu> mSohMenu;
} // namespace SohGui

namespace {

constexpr int16_t kEnHyBeggar = 5; // ENHY_TYPE_BOJ_5
constexpr int16_t kTanaWooden = 0;

// How close you have to be to talk to her, and to drop something with C.
constexpr float kUseRadius = 90.0f;

// Eight visible slots, laid out left to right so the stick moves through them in the order they
// appear. Upper tier first, then lower. Ranges match the shelf the vanilla store tables use
// (x -80..+80, y 52..76, z a little back) - z_en_ossan.c:203.
struct SlotOffset {
    float x;
    float y;
    float z;
};
constexpr SlotOffset kSlotOffsets[] = {
    { -80.0f, 76.0f, -6.0f }, { -50.0f, 76.0f, -14.0f }, { 50.0f, 76.0f, -14.0f }, { 80.0f, 76.0f, -6.0f },
    { -80.0f, 52.0f, -6.0f }, { -50.0f, 52.0f, -14.0f }, { 50.0f, 52.0f, -14.0f }, { 80.0f, 52.0f, -6.0f },
};
constexpr uint8_t kSlotsPerPage = (uint8_t)ARRAY_COUNT(kSlotOffsets);

// The customer's side of the shelf, read off the data rather than guessed: sItemShelfRot is
// { 0xEAAC x4, 0x1554 x4 } (z_en_ossan.c:145), i.e. -30 and +30 degrees splayed around yaw 0, an
// arrangement that only frames for a viewer standing at +Z. Every vanilla shop also places its
// En_Tana at rotY 0. Yaw 0 faces +Z, so the beggar keeps yaw 0 and looks out at the customer.
constexpr float kCameraHeight = 40.0f;
constexpr float kCameraDistance = 175.0f;

// How far toward the customer the invisible desk sits, so it beats the beggar's own talk offer.
constexpr float kDeskLead = 24.0f;

// Which way the whole rig faces. Captured at placement rather than fixed, because "put her by the
// pots" almost always also means "facing this way", and a shop facing a fence is no use.
//
// Everything below is authored in the rig's LOCAL space - +Z is the customer's side - and rotated
// into the world by this yaw. That is only possible because the slot layout is ours; En_Ossan could
// not be turned at all, since it offsets its items in raw world axes (z_en_ossan.c:450) and a
// rotated shelf left them hanging in the air beside it.
int16_t sYaw = 0;

/** Rotate a rig-local XZ offset into world space by sYaw. */
void RotateToWorld(float localX, float localZ, float* outX, float* outZ) {
    const float sin = Math_SinS(sYaw);
    const float cos = Math_CosS(sYaw);
    *outX = localX * cos + localZ * sin;
    *outZ = -localX * sin + localZ * cos;
}

// Stick deflection that counts as a deliberate nudge, and the frames to ignore afterwards, so one
// flick moves one slot rather than sweeping the shelf.
constexpr int16_t kStickThreshold = 30;
constexpr uint8_t kStickCooldownFrames = 6;

s16 sDeskActorId = -1;
s16 sSlotActorId = -1;

Actor* sDesk = nullptr;
Actor* sShelves = nullptr;
Actor* sBeggar = nullptr;
Actor* sSlots[kSlotsPerPage] = {};

bool sBrowsing = false;
uint8_t sCursor = 0;
uint8_t sPage = 0;
uint8_t sStickCooldown = 0;

// True between accepting the talk and the greeting textbox closing; browsing starts after.
bool sGreeting = false;

// The greeting's text id. Must not have 0x06 as its high byte: Actor_UpdateAll only exempts the
// talk actor from the talking freeze when `(player->actor.textId & 0xFF00) != 0x600`
// (z_actor.c:2621), and the desk being frozen mid-conversation is the one thing that would strand
// the player. 0x92xx is clear of vanilla (which stops in the 0x70xx range) and of the 0x91xx block
// rando uses for shop items.
constexpr uint16_t kGreetingTextId = 0x9200;

uint8_t PageCount() {
    const uint8_t count = SevenSagesStash_Count();
    if (count == 0) {
        return 1;
    }
    return (uint8_t)((count + kSlotsPerPage - 1) / kSlotsPerPage);
}

/** Stash index currently under slot `slot` on the current page, or -1 if that slot is empty. */
int16_t StashIndexForSlot(uint8_t slot) {
    const uint16_t index = (uint16_t)sPage * kSlotsPerPage + slot;
    if (index >= SevenSagesStash_Count()) {
        return -1;
    }
    return (int16_t)index;
}

/**
 * Register `objectId` in the current scene's object bank if it is not already there.
 *
 * Enrich World has the same helper and this is deliberately a copy rather than a call into it.
 * CLAUDE.md's split between the two mods is a folder copy - `git checkout mod/seven-sages --
 * EnrichWorld/` onto a branch off develop - and that only works while the dependency runs one way.
 * A call in this direction would leave the Enrich World branch building and this one broken.
 */
bool EnsureObjectLoaded(int16_t objectId) {
    if (gPlayState == nullptr || objectId <= 0 || objectId >= OBJECT_ID_MAX) {
        return false;
    }
    if (Object_GetIndex(&gPlayState->objectCtx, objectId) >= 0) {
        return true;
    }
    // Object_Spawn asserts rather than fails when the bank is full, and an assert compiled out in
    // a release build would corrupt the object arena instead. Refuse early.
    if (gPlayState->objectCtx.num >= OBJECT_EXCHANGE_BANK_MAX - 1) {
        return false;
    }
    Object_Spawn(&gPlayState->objectCtx, objectId);
    return Object_GetIndex(&gPlayState->objectCtx, objectId) >= 0;
}

} // namespace

// ── Drawing ─────────────────────────────────────────────────────────────────────────────────
//
// At file scope, NOT in the anonymous namespace above. OPEN_DISPS declares
// FrameInterpolation_RecordOpenChild at its point of use (macros.h:208); inside an anonymous
// namespace that declaration picks up internal linkage and fails to link. Every other C++ file in
// the tree that draws (nametag.cpp, kaleido.cpp) uses it at this scope for the same reason.

/** One stash item sitting on the shelf. `params` is the slot, not the stash index - see below. */
void SevenSagesSlotDraw(Actor* thisx, PlayState* play) {
    // The slot holds a POSITION, and the stash index is resolved fresh every frame from the
    // current page. Baking the stash index into the actor would mean respawning eight actors on
    // every page turn and on every withdrawal, and would leave a stale index behind whenever a
    // teammate's deposit arrives over the network mid-browse.
    const int16_t stashIndex = StashIndexForSlot((uint8_t)thisx->params);
    if (stashIndex < 0) {
        return;
    }

    const int16_t rg = SevenSagesStash_Get((uint8_t)stashIndex, nullptr);
    if (rg == RG_NONE) {
        return;
    }

    GetItemEntry entry = ItemTable_RetrieveEntry(MOD_RANDOMIZER, rg);
    GetItemEntry_Draw(play, entry);
}

/** The selection cursor, drawn the way EnOssan_DrawCursor draws it (z_en_ossan.c:2329). */
void SevenSagesDeskDraw(Actor* thisx, PlayState* play) {
    if (!sBrowsing || sSlots[sCursor] == nullptr) {
        return;
    }
    if (StashIndexForSlot(sCursor) < 0) {
        return; // cursor is over an empty slot; nothing to point at
    }

    s16 screenX;
    s16 screenY;
    Actor_GetScreenPos(play, sSlots[sCursor], &screenX, &screenY);

    const f32 z = 1.5f;
    const f32 w = 16.0f * z;
    const s32 dsdx = (s32)((1.0f / z) * 1024.0f);

    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_39Overlay(play->state.gfxCtx);
    gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 0, 255, 80, 255);
    gDPLoadTextureBlock_4b(OVERLAY_DISP++, gSelectionCursorTex, G_IM_FMT_IA, 16, 16, 0, G_TX_MIRROR | G_TX_WRAP,
                           G_TX_MIRROR | G_TX_WRAP, 4, 4, G_TX_NOLOD, G_TX_NOLOD);
    gSPTextureRectangle(OVERLAY_DISP++, (s32)((screenX - w) * 4.0f), (s32)((screenY - w) * 4.0f),
                        (s32)((screenX + w) * 4.0f), (s32)((screenY + w) * 4.0f), G_TX_RENDERTILE, 0, 0, dsdx, dsdx);
    CLOSE_DISPS(play->state.gfxCtx);
}

namespace {

// ── The shelf slots ─────────────────────────────────────────────────────────────────────────

void SlotInit(Actor* thisx, PlayState* play) {
    // 0.25 and a 24-unit lift are En_GirlA's own numbers for an item standing on a shop shelf
    // (z_en_girla.c:1395), so a stash item reads at the same size as a vanilla one.
    Actor_SetScale(thisx, 0.25f);
    thisx->shape.yOffset = 24.0f;
    thisx->gravity = 0.0f;
}

void SlotUpdate(Actor* thisx, PlayState* play) {
    // Slowly turning, so an item reads as an object on display rather than a decal. Matches the
    // idle rotation vanilla shop items have.
    thisx->shape.rot.y += 0x180;
}

void RegisterSlotActor() {
    if (sSlotActorId != -1) {
        return;
    }
    ActorDBInit entry = {
        "En_SevenSagesStashItem",
        "Seven Sages Stash Item",
        ACTORCAT_PROP,
        (ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED),
        OBJECT_GAMEPLAY_KEEP,
        sizeof(Actor),
        (ActorFunc)SlotInit,
        nullptr,
        (ActorFunc)SlotUpdate,
        (ActorFunc)SevenSagesSlotDraw,
        nullptr,
    };
    // Explicit narrowing: AddEntry hands back an s32 and actor ids are s16 everywhere they are
    // used. Left implicit this is a fatal C4244 on the Windows CI, which builds with /WX.
    sSlotActorId = (s16)ActorDB::Instance->AddEntry(entry).entry.id;
}

// ── The desk: talk offer, browse loop, camera ───────────────────────────────────────────────

/** Hand the player back. 7 is the csAction every vanilla actor uses to end one of these. */
void EndBrowse(Actor* thisx, PlayState* play) {
    Player* player = GET_PLAYER(play);

    sBrowsing = false;
    thisx->flags &= ~ACTOR_FLAG_TALK;
    player->actor.flags &= ~ACTOR_FLAG_TALK;
    Player_SetCsActionWithHaltedActors(play, thisx, 7);

    // z_play re-requests the viewpoint bgCam EVERY FRAME while unk_1242B is non-zero
    // (z_play.c:1223). Nothing here sets it, but leaving the release in place means a stray
    // viewpoint change from anything else cannot strand the camera on a field bgCam.
    play->unk_1242B = 0;
    Camera_RequestSetting(GET_ACTIVE_CAM(play), CAM_SET_NORMAL0);
}

/** Point the camera at the selected slot, or at the beggar when the shelf is empty. */
void HoldCamera(PlayState* play) {
    Vec3f at;

    if (sSlots[sCursor] != nullptr && StashIndexForSlot(sCursor) >= 0) {
        at = sSlots[sCursor]->world.pos;
    } else if (sBeggar != nullptr) {
        at = sBeggar->world.pos;
        at.y += 50.0f;
    } else {
        return;
    }

    float eyeOffsetX;
    float eyeOffsetZ;
    RotateToWorld(0.0f, kCameraDistance, &eyeOffsetX, &eyeOffsetZ);
    Vec3f eye = { at.x + eyeOffsetX, at.y + kCameraHeight, at.z + eyeOffsetZ };

    // See EndBrowse: hold this at 0 so z_play's per-frame Play_RequestViewpointBgCam cannot
    // overwrite the manual camera.
    play->unk_1242B = 0;
    Play_CameraSetAtEye(play, CAM_ID_MAIN, &at, &eye);
}

// ── Depositing with C ───────────────────────────────────────────────────────────────────────

// Tell the rest of the team the box changed.
//
// Every path that mutates the stash has to call this. The beggar did not, for her whole existence -
// only the old ImGui window did - so deposits and withdrawals at the shelf were invisible to
// teammates. A shared box that does not sync is just a private box with extra steps, which is the
// one thing this feature cannot be.
//
// Safe with Anchor absent or disconnected: single-player is the same code path, and simply keeps a
// stash that persists.
void BroadcastStash() {
    if (Anchor::Instance != nullptr && Anchor::Instance->isConnected) {
        Anchor::Instance->SendPacket_SevenSagesStash();
    }
}

void PlayRefusal() {
    Audio_PlaySoundGeneral(NA_SE_SY_ERROR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale,
                           &gSfxDefaultReverb);
}

/**
 * Deposit whatever is on a C button.
 *
 * Works two ways, and they are the same code: walk up to her and press C, or press C while
 * browsing. Leaving something is a one-press action and should not require a conversation first;
 * taking something back is a choice among several and does.
 *
 * ── Why the field press is safe again ───────────────────────────────────────────────────────
 *
 * It was briefly moved inside the conversation because pressing C near her seemed ambiguous -
 * firing an arrow at something behind the shelf appeared to also deposit. That diagnosis was
 * wrong. The bow is not depositable, so the press was never claimed and the arrow fired normally;
 * what actually happened was the ghost-button bug (SevenSagesTrade.cpp, UnassignFromButtons)
 * handing back an item that had never really left. With that fixed, an unclaimed press is exactly
 * a vanilla press, and only a genuinely depositable item is intercepted.
 */
// The three C buttons, and the index Player_GetItemOnButton wants for each (z_player.c:2516 -
// index 0 is B, 1..3 are C-left/down/right). B is not here: in browse mode B means leave.
struct CButton {
    uint16_t mask;
    int32_t buttonIndex;
};
constexpr CButton kCButtons[] = { { BTN_CLEFT, 1 }, { BTN_CDOWN, 2 }, { BTN_CRIGHT, 3 } };

void DepositHeldItem(PlayState* play, int32_t item) {
    // Every refusal below makes a noise. Out in the field a C press that we declined still did
    // something visible - it used the item - so silence was fine. In here the press has no other
    // meaning, and a deposit that silently does nothing is indistinguishable from a broken button.
    const int16_t rg = SevenSagesTrade_RandomizerGetForItemId((int16_t)item);
    if (rg == RG_NONE || SevenSagesTrade_CanDeposit(rg) != SEVEN_SAGES_TRADE_OK) {
        PlayRefusal();
        return;
    }

    if (!SevenSagesTrade_TakeItem(rg)) {
        PlayRefusal();
        return;
    }

    if (!SevenSagesStash_Add(rg, (uint8_t)Randomizer_GetSettingValue(RSK_SELECTED_SAGE))) {
        // Full box: give it straight back rather than destroying it. If even that is refused the
        // item would be gone, so the stash entry is restored instead - conservation over tidiness.
        if (!SevenSagesTrade_GiveItem(rg)) {
            SevenSagesStash_Add(rg, (uint8_t)Randomizer_GetSettingValue(RSK_SELECTED_SAGE));
            BroadcastStash();
        }
        PlayRefusal();
        return;
    }

    BroadcastStash();
    Audio_PlaySoundGeneral(NA_SE_SY_CORRECT_CHIME, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                           &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
}

/**
 * Keep the beggar out of the player's way, and send her talk offer to the desk.
 *
 * En_Hy BOJ_5 is a talking NPC - she is the vanilla beggar, with her own dialogue - and she offers
 * talk every frame she is on screen and in range (Npc_UpdateTalking, z_actor.c:4341). The desk
 * offers too, and Actor_OfferTalkExchange keeps whichever is closer, so MOST of the time the desk
 * wins on its 24-unit lead. But both live in ACTORCAT_NPC and the winner at any given moment
 * depends on which of the two updates last within the category, which depends on spawn order -
 * and when the beggar wins you get her textbox instead of the shelf, on top of a browse mode that
 * thinks it owns the player.
 *
 * Redirecting rather than clearing is what makes this order-independent: whichever of them wrote
 * `talkActor` this frame, by the end of the frame it names the desk.
 */
void RedirectBeggarTalk(void* actorPtr) {
    if (actorPtr != sBeggar || sBeggar == nullptr || sDesk == nullptr || gPlayState == nullptr) {
        return;
    }

    // Two targetable things standing on the same spot means Z flips between them and neither is
    // easy to drop. The beggar is what you look at; the desk is what you talk to. Cleared every
    // frame rather than once at spawn because En_Hy sets its own flags during its deferred Init,
    // so a single clear at spawn time would just be overwritten.
    sBeggar->flags &= ~ACTOR_FLAG_ATTENTION_ENABLED;

    Player* player = GET_PLAYER(gPlayState);
    if (player != nullptr && player->talkActor == sBeggar) {
        player->talkActor = sDesk;
        player->talkActorDistance = sDesk->xzDistToPlayer;
    }
}

/**
 * Take the item under the cursor back out of the stash.
 *
 * Immediate, and it stays in browse mode so several things can be collected in one visit. This
 * used to have to mark the item, release the player, and complete a frame later, because the give
 * was queued as a get-item cutscene and a halted player never reached the frame that ran it.
 * SevenSagesTrade_GiveItem applies the item directly now, so none of that dance is needed.
 */
void TakeSelected(Actor* thisx, PlayState* play) {
    const int16_t stashIndex = StashIndexForSlot(sCursor);
    if (stashIndex < 0) {
        PlayRefusal();
        return;
    }

    const int16_t rg = SevenSagesStash_Get((uint8_t)stashIndex, nullptr);
    if (rg == RG_NONE) {
        PlayRefusal();
        return;
    }

    // Give first, remove second, and believe the return value. If the give is refused the item
    // stays on the shelf - an item is never in neither place.
    if (!SevenSagesTrade_GiveItem(rg)) {
        PlayRefusal();
        return;
    }

    SevenSagesStash_RemoveAt((uint8_t)stashIndex);
    BroadcastStash();
    Audio_PlaySoundGeneral(NA_SE_SY_GET_ITEM, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                           &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);

    // That was the last one - same rule as arriving at an empty shelf, so leaving is automatic
    // rather than something the player has to work out.
    if (SevenSagesStash_Count() == 0) {
        EndBrowse(thisx, play);
        return;
    }

    // The shelf just got shorter. Keep the page inside what is left rather than past the end.
    if (sPage >= PageCount()) {
        sPage = (uint8_t)(PageCount() - 1);
    }
}

void ReadBrowseInput(Actor* thisx, PlayState* play) {
    Input* input = &play->state.input[0];

    if (CHECK_BTN_ALL(input->press.button, BTN_B)) {
        EndBrowse(thisx, play);
        return;
    }

    if (CHECK_BTN_ALL(input->press.button, BTN_A)) {
        TakeSelected(thisx, play);
        return;
    }

    for (const CButton& button : kCButtons) {
        if (CHECK_BTN_ALL(input->press.button, button.mask)) {
            DepositHeldItem(play, Player_GetItemOnButton(play, button.buttonIndex));
            return;
        }
    }

    // Z and R page, so the stick is left doing one job. Only offered when there is a second page.
    if (PageCount() > 1) {
        if (CHECK_BTN_ALL(input->press.button, BTN_R)) {
            sPage = (uint8_t)((sPage + 1) % PageCount());
            sCursor = 0;
        } else if (CHECK_BTN_ALL(input->press.button, BTN_Z)) {
            sPage = (uint8_t)((sPage + PageCount() - 1) % PageCount());
            sCursor = 0;
        }
    }

    if (sStickCooldown > 0) {
        sStickCooldown--;
        return;
    }

    const int16_t stickX = input->rel.stick_x;
    if (stickX > kStickThreshold && sCursor + 1 < kSlotsPerPage) {
        sCursor++;
        sStickCooldown = kStickCooldownFrames;
    } else if (stickX < -kStickThreshold && sCursor > 0) {
        sCursor--;
        sStickCooldown = kStickCooldownFrames;
    }
}

void DeskInit(Actor* thisx, PlayState* play) {
    // Invisible: the beggar standing in front of it is what the player sees and talks to. It still
    // needs a focus point and the talk flags, because it is the thing that actually offers talk.
    //
    // A real text id rather than the silent 0xFFFF. 0xFFFF gets you the browse hold for free -
    // Player stands still and no textbox appears - but "no textbox appears" is also why she had
    // nothing to say, and a shelf of silent objects with no prompt is not a conversation. With a
    // real id Player opens the greeting normally, and the hold is taken out afterwards by hand
    // (see DeskUpdate), which is the same csAction 1 the 0xFFFF path would have applied itself.
    thisx->textId = kGreetingTextId;
    thisx->focus.pos = thisx->world.pos;
    Actor_SetScale(thisx, 0.01f);
}

void DeskUpdate(Actor* thisx, PlayState* play) {
    Player* player = GET_PLAYER(play);
    if (player == nullptr) {
        return;
    }

    Actor_SetFocus(thisx, 40.0f);

    if (sBrowsing) {
        HoldCamera(play);
        ReadBrowseInput(thisx, play);
        return;
    }

    if (sGreeting) {
        HoldCamera(play);
        // Wait out the greeting, then take the player back off Player and into browse mode. The
        // textbox closing is what ends the vanilla talk, so the hold has to be re-applied here or
        // he simply walks away mid-conversation.
        if (Message_GetState(&play->msgCtx) == TEXT_STATE_CLOSING) {
            sGreeting = false;

            // Nothing on the shelf: let the textbox end the conversation and leave it there. The
            // player is already free at this point - the greeting ran as an ordinary vanilla talk
            // and closing it released him - so this is simply declining to take him captive again.
            // Browsing an empty shelf is a cursor over nothing with no way to tell that from a
            // broken screen, and B is the only thing that gets you out.
            if (SevenSagesStash_Count() == 0) {
                return;
            }

            sBrowsing = true;
            sCursor = 0;
            sPage = 0;
            sStickCooldown = 0;
            Player_SetCsActionWithHaltedActors(play, thisx, 1);
        }
        return;
    }

    if (Actor_WorldDistXZToActor(thisx, &player->actor) >= kUseRadius) {
        return;
    }

    if (Actor_ProcessTalkRequest(thisx, play)) {
        sGreeting = true;
    } else {
        Actor_OfferTalk(thisx, play, kUseRadius);
    }
}

void RegisterDeskActor() {
    if (sDeskActorId != -1) {
        return;
    }
    ActorDBInit entry = {
        "En_SevenSagesBeggarDesk",
        "Seven Sages Beggar Desk",
        ACTORCAT_NPC,
        // ATTENTION_ENABLED | FRIENDLY is the pair En_Kanban carries, and it is what makes A read
        // as Speak rather than Roll - without it Actor_OfferTalk has nothing to hang the prompt on.
        (ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED | ACTOR_FLAG_ATTENTION_ENABLED |
         ACTOR_FLAG_FRIENDLY),
        OBJECT_GAMEPLAY_KEEP,
        sizeof(Actor),
        (ActorFunc)DeskInit,
        nullptr,
        (ActorFunc)DeskUpdate,
        (ActorFunc)SevenSagesDeskDraw,
        nullptr,
    };
    sDeskActorId = (s16)ActorDB::Instance->AddEntry(entry).entry.id;
}

// ── Spawning ────────────────────────────────────────────────────────────────────────────────

struct BeggarSpot {
    int16_t sceneNum;
    float x;
    float y;
    float z;
    int16_t yaw;
};

// One beggar, in Lon Lon Ranch. The reasoning is worth keeping, because the instinct is to add more.
//
// Hyrule Field is hub-and-spoke: every destination hangs off the rim and the centre is empty, so
// early travel is always centre-out-and-back rather than lateral. A single point in the middle is
// therefore genuinely on the way to everything - and Lon Lon is the only thing actually inside the
// field.
//
// Considered and dropped:
//
//   Temple of Time     the age-handoff argument is real, but it sits close enough to Lon Lon that
//                      nothing lies between them. Two access points with no distance between them
//                      is one access point and a second thing to maintain.
//   Gerudo Valley      topologically the only true chokepoint in the game - one edge in, 426 checks
//                      behind it - but it is the wrong END of a long corridor, miles from Spirit
//                      Temple and useless as preparation. Requiem already solves that trip.
//   Zora's Domain      genuinely gates Jabu and Ice Cavern, but with Lon Lon carrying the general
//                      case it is an arbitrary exception rather than part of a pattern.
//   Dungeon mouths     what Resident Evil and Hollow Knight actually do - a box in every save room,
//                      a bench before every boss. Rejected because this box is SHARED. Coordination
//                      needs somewhere you go; a box at every door is a menu, and "who is holding
//                      the Hookshot" stops being a conversation the team ever has.
//
// MEASURED, not derived. Placed by hand in game and read back out; every previous value in this
// table was inferred from prop coordinates and every one of them was wrong - on the ranch roof, in
// the cucco pen, beside a liftable crate.
//
// It sits by the four OBJ_TSUBO at the back of the ranch (x 667, z -3122..-3218), which is the pot
// cluster that was meant all along. Yaw 0x4000 is a quarter turn, so she faces +X and the shelf
// stands behind her.
constexpr BeggarSpot kBeggarSpots[] = {
    { SCENE_LON_LON_RANCH, 669.3f, 0.0f, -3265.6f, 0x4000 },
};

// ── Hand placement, persisted ───────────────────────────────────────────────────────────────
//
// "Place the Beggar Here" used to spawn one and print a coordinate, which meant the placement
// evaporated on the next scene load and the bad table entry took over again. You cannot judge a
// spot you have to re-create every time you walk back to it.
//
// These CVars are written on placement and read on every scene load, so she stays put across
// reloads and restarts. An override for a scene REPLACES that scene's table row rather than adding
// to it, so there is never more than one of her (which ClearShop also enforces, but silently).
#define CVAR_BEGGAR_SCENE CVAR_ENHANCEMENT("SevenSagesBeggar.Scene")
#define CVAR_BEGGAR_X CVAR_ENHANCEMENT("SevenSagesBeggar.X")
#define CVAR_BEGGAR_Y CVAR_ENHANCEMENT("SevenSagesBeggar.Y")
#define CVAR_BEGGAR_Z CVAR_ENHANCEMENT("SevenSagesBeggar.Z")
#define CVAR_BEGGAR_YAW CVAR_ENHANCEMENT("SevenSagesBeggar.Yaw")

// -1 rather than 0: scene 0 is a real scene (the Deku Tree), so it cannot double as "unset".
constexpr int32_t kNoOverrideScene = -1;

/**
 * Remove any beggar already standing.
 *
 * At most one shop in the world, and this is load-bearing rather than tidiness: the browse state
 * above is a single global, so a second beggar would drive the same cursor and camera as the
 * first. (It is also what a second En_Ossan did in the previous design, for a different reason -
 * Actor_Find bound every shopkeeper to the FIRST En_Tana in the scene, which is what made a
 * hand-placed shop appear to teleport to the other side of the map.)
 */
void ClearShop() {
    if (gPlayState == nullptr) {
        return;
    }

    for (Actor* actor : { sDesk, sShelves, sBeggar }) {
        if (actor != nullptr) {
            Actor_Kill(actor);
        }
    }
    for (Actor*& slot : sSlots) {
        if (slot != nullptr) {
            Actor_Kill(slot);
        }
        slot = nullptr;
    }

    sDesk = sShelves = sBeggar = nullptr;
    sBrowsing = false;
    sCursor = sPage = sStickCooldown = 0;
    sGreeting = false;
}

bool SpawnShop(float x, float y, float z, int16_t yaw) {
    if (gPlayState == nullptr) {
        return false;
    }

    ClearShop();
    sYaw = yaw;

    if (!EnsureObjectLoaded(OBJECT_SHOP_DUNGEN) || !EnsureObjectLoaded(OBJECT_BOJ) ||
        !EnsureObjectLoaded(OBJECT_OS_ANIME)) {
        SPDLOG_ERROR("[SevenSages] beggar: object bank full, nothing spawned");
        return false;
    }

    RegisterDeskActor();
    RegisterSlotActor();
    if (sDeskActorId == -1 || sSlotActorId == -1) {
        return false;
    }

    ActorContext* ctx = &gPlayState->actorCtx;

    // Axis-aligned on purpose: the slot offsets below are world-axis, matching how vanilla lays a
    // shelf out, so a rotated shelf would leave its items hanging in the air beside it.
    sShelves = Actor_Spawn(ctx, gPlayState, ACTOR_EN_TANA, x, y, z, 0, sYaw, 0, kTanaWooden);
    sBeggar = Actor_Spawn(ctx, gPlayState, ACTOR_EN_HY, x, y, z, 0, sYaw, 0, kEnHyBeggar);

    // The desk sits toward the customer so it is what you walk up to and talk to; the beggar is
    // the thing you see. Both offer talk and Actor_OfferTalkExchange keeps whichever is closer
    // (z_actor.c:1962), so the lead has to be real rather than a tie.
    float deskX;
    float deskZ;
    RotateToWorld(0.0f, kDeskLead, &deskX, &deskZ);
    sDesk = Actor_Spawn(ctx, gPlayState, sDeskActorId, x + deskX, y, z + deskZ, 0, sYaw, 0, 0);

    for (uint8_t i = 0; i < kSlotsPerPage; i++) {
        float slotX;
        float slotZ;
        RotateToWorld(kSlotOffsets[i].x, kSlotOffsets[i].z, &slotX, &slotZ);
        sSlots[i] =
            Actor_Spawn(ctx, gPlayState, sSlotActorId, x + slotX, y + kSlotOffsets[i].y, z + slotZ, 0, sYaw, 0, i);
    }

    if (sShelves == nullptr || sBeggar == nullptr || sDesk == nullptr) {
        SPDLOG_ERROR("[SevenSages] beggar: shelves {} beggar {} desk {}", sShelves != nullptr, sBeggar != nullptr,
                     sDesk != nullptr);
        return false;
    }
    return true;
}

void SpawnForScene() {
    // OnSceneSpawnActors, not OnSceneInit: OnSceneInit fires before Actor_InitContext, whose first
    // act is memset(actorCtx, 0, ...) (z_actor.c:2541), so anything spawned there is erased a
    // hundred lines later while still logging that it spawned.
    if (gPlayState == nullptr) {
        return;
    }

    sDesk = sShelves = sBeggar = nullptr;
    for (Actor*& slot : sSlots) {
        slot = nullptr;
    }
    sBrowsing = false;
    sGreeting = false;

    const int16_t sceneNum = (int16_t)gPlayState->sceneNum;

    const int32_t overrideScene = CVarGetInteger(CVAR_BEGGAR_SCENE, kNoOverrideScene);
    if (overrideScene == sceneNum) {
        SpawnShop(CVarGetFloat(CVAR_BEGGAR_X, 0.0f), CVarGetFloat(CVAR_BEGGAR_Y, 0.0f),
                  CVarGetFloat(CVAR_BEGGAR_Z, 0.0f), (int16_t)CVarGetInteger(CVAR_BEGGAR_YAW, 0));
        return;
    }

    for (const BeggarSpot& spot : kBeggarSpots) {
        // A scene with an override never also gets its table row - see the note above the CVars.
        if (spot.sceneNum == sceneNum && spot.sceneNum != overrideScene) {
            SpawnShop(spot.x, spot.y, spot.z, spot.yaw);
        }
    }
}

/** Her greeting. Serves kGreetingTextId, which is ours and appears in no message table. */
void BuildGreeting(uint16_t* textId, bool* loadFromMessageTable) {
    // Button glyphs are ordinary characters in the NES font, not control codes:
    // \x9F is A, \xA7/\xA6/\xA8 are C-left/C-down/C-right, \xAA is the control stick
    // (nes_font_static.h:387-409). An earlier draft guessed \x9A-\x9C, which are not button
    // glyphs at all and would have rendered as three unrelated letters.
    // ── Her voice ───────────────────────────────────────────────────────────────────────────
    //
    // Matched to her actual vanilla line, which reads:
    //
    //     Please...with (C)...
    //     Please sell me the contents of a
    //     bottle...
    //     Please...with (C)...
    //
    // That is the whole register: desperate, fragmented, ellipsis everywhere, the same plea
    // repeated, and the button prompt bookending the line rather than stated once like an
    // instruction. She is not a shopkeeper explaining a service - she is begging, and the controls
    // happen to be part of the begging. Written first as a chatty grateful innkeeper, which was
    // wrong in a way worth recording: it read as helpful, and she is not helpful, she is pleading.
    //
    // \xA1 is the generic C button glyph, which is what vanilla uses here - not the three
    // directional ones. Any of the three C buttons works; the single glyph is how she says it.
    // \xAA is the control stick and \x9F the A button (nes_font_static.h:387-409). & is a newline.
    //
    // Same text id for all of them. The hook runs each time the box opens, so which line she uses
    // is decided here rather than needing an id and a COND_ID_HOOK per variant.
    static const char* kEmptyLines[] = {
        "Please...with \xA1...&Please leave something with me...&Please...with \xA1...",
        "Nothing...I have nothing...&Please...with \xA1...anything at all...&Please...",
        "Empty...it is so empty...&Please...with \xA1...&I will hold it...I will hold it...",
        "Please...with \xA1...&I have nothing to keep...nothing...&Please...with \xA1...",
    };

    static const char* kStockedLines[] = {
        "I kept it safe...so safe...&\xAA to look...\x9F to take it...&Please...more...with \xA1...",
        "Yours...it is all yours...&\xAA to look...\x9F to take it back...&Please...with \xA1...more...",
        "I touched nothing...nothing...&\xAA to look...\x9F to take what is yours...&Please...with \xA1...again...",
        "It waits...it still waits...&\xAA to look...\x9F to take it...&and please...with \xA1...please...",
    };

    // Advanced per conversation so she does not repeat herself back to back. Not random: a counter
    // reads the same every run, which is one less thing to wonder about when a line looks wrong.
    static uint8_t sLineIndex = 0;
    sLineIndex++;

    const char* text = SevenSagesStash_Count() == 0 ? kEmptyLines[sLineIndex % ARRAY_COUNT(kEmptyLines)]
                                                    : kStockedLines[sLineIndex % ARRAY_COUNT(kStockedLines)];
    CustomMessage msg(text, text, text);
    msg.AutoFormat();
    msg.LoadIntoFont();
    // Ours entirely - there is no vanilla message behind this id to fall back on.
    *loadFromMessageTable = false;
}

void RegisterBeggarShop() {
    COND_HOOK(OnSceneSpawnActors, IS_SEVENSAGES, SpawnForScene);

    // VB_EXECUTE_PLAYER_ACTION_FUNC fires at PLAYER level, before the action runs. That ordering is
    // the whole point: an actor polling the button in its own update is too late, because Player
    // updates first and has already rolled or used the item. Only depositable items are claimed, so
    // every other C press near her behaves exactly as it does anywhere else.
    COND_VB_SHOULD(VB_EXECUTE_PLAYER_ACTION_FUNC, IS_SEVENSAGES, {
        if (!*should || gPlayState == nullptr || sBeggar == nullptr || sBrowsing || sGreeting) {
            return;
        }

        Player* player = va_arg(args, Player*);
        if (player == nullptr || player->actor.id != ACTOR_PLAYER) {
            return;
        }
        if (Actor_WorldDistXZToActor(sBeggar, &player->actor) >= kUseRadius) {
            return;
        }

        Input* input = &gPlayState->state.input[0];
        for (const CButton& button : kCButtons) {
            if (!CHECK_BTN_ALL(input->press.button, button.mask)) {
                continue;
            }
            const int32_t item = Player_GetItemOnButton(gPlayState, button.buttonIndex);
            const int16_t rg = SevenSagesTrade_RandomizerGetForItemId((int16_t)item);
            // Silence rather than a refusal noise out here: an unclaimed press still does something
            // visible - it uses the item - so a beep would be reporting a failure that is not one.
            if (rg != RG_NONE && SevenSagesTrade_CanDeposit(rg) == SEVEN_SAGES_TRADE_OK) {
                DepositHeldItem(gPlayState, item);
                *should = false;
            }
            break;
        }
    });
    COND_ID_HOOK(OnOpenText, kGreetingTextId, IS_SEVENSAGES, BuildGreeting);
    COND_HOOK(OnActorUpdate, IS_SEVENSAGES, RedirectBeggarTalk);
}

} // namespace

// Drop the beggar at the player, for choosing the fixed spot by standing on it.
extern "C" void SevenSagesBeggarShop_SpawnAtPlayer(void) {
    if (gPlayState == nullptr) {
        return;
    }
    Player* player = GET_PLAYER(gPlayState);
    if (player == nullptr) {
        return;
    }

    const PosRot& world = player->actor.world;

    // She faces the way YOU are facing, so stand where she should stand and look the way she should
    // look. The customer side, the shelf, the camera and the eight slots all follow from that one
    // angle - see sYaw.
    const int16_t yaw = player->actor.shape.rot.y;

    if (!SpawnShop(world.pos.x, world.pos.y, world.pos.z, yaw)) {
        return;
    }

    CVarSetInteger(CVAR_BEGGAR_SCENE, gPlayState->sceneNum);
    CVarSetFloat(CVAR_BEGGAR_X, world.pos.x);
    CVarSetFloat(CVAR_BEGGAR_Y, world.pos.y);
    CVarSetFloat(CVAR_BEGGAR_Z, world.pos.z);
    CVarSetInteger(CVAR_BEGGAR_YAW, yaw);
    CVarSave();

    // On screen as well as in the log. The log is the wrong place for this on its own: spdlog
    // buffers to disk so the file stays empty until the game exits, and the in-game console is a
    // wall of ResourceManager [trace] lines that a single INFO disappears into. The whole point of
    // this button is to read four numbers off it.
    const std::string row =
        fmt::format("{{ SCENE_?, {:.1f}f, {:.1f}f, {:.1f}f, {} }}", world.pos.x, world.pos.y, world.pos.z, yaw);
    Notification::Emit({ .message = row, .remainingTime = 15 });

    // Logged in table-row shape too, because the end of this is a row in kBeggarSpots rather than a
    // CVar every player carries.
    SPDLOG_INFO("[SevenSages] beggar placed - scene {}: {}", gPlayState->sceneNum, row);
}

// Forget a hand placement and go back to the table.
extern "C" void SevenSagesBeggarShop_ClearPlacement(void) {
    CVarSetInteger(CVAR_BEGGAR_SCENE, kNoOverrideScene);
    CVarSave();
    SPDLOG_INFO("[SevenSages] beggar placement cleared");
}

namespace {
// ── Menu ────────────────────────────────────────────────────────────────────────────────────
//
// Two buttons, both for placing her. Nothing here is player-facing: the beggar spawns on her own
// and is used entirely in-world. These exist because picking a coordinate by reading prop tables
// was wrong four times running, and standing on the spot is the only method that worked.

void RegisterBeggarMenu() {
    // Same sidebar page the co-op setup registers, second column.
    WidgetPath path = { "Network", "Seven Sages Co-op", SECTION_COLUMN_2 };

    SohGui::mSohMenu->AddWidget(path, "Trade Box", WIDGET_SEPARATOR_TEXT);

    SohGui::mSohMenu->AddWidget(path, "Place the Beggar Here", WIDGET_BUTTON)
        .Callback([](WidgetInfo& info) { SevenSagesBeggarShop_SpawnAtPlayer(); })
        .Options(UIWidgets::ButtonOptions().Tooltip(
            "Move the beggar to where you stand, facing the way you face.\n"
            "\n"
            "The shelf goes behind her and the browse camera frames from in front, so stand where "
            "SHE should stand. The coordinate persists across reloads and is shown on screen."));

    SohGui::mSohMenu->AddWidget(path, "Forget Beggar Placement", WIDGET_BUTTON)
        .Callback([](WidgetInfo& info) { SevenSagesBeggarShop_ClearPlacement(); })
        .Options(
            UIWidgets::ButtonOptions().Tooltip("Discard a hand-placed beggar and go back to the built-in position.\n"
                                               "\n"
                                               "Takes effect on the next scene load."));
}

} // namespace

static RegisterShipInitFunc sevenSagesBeggarShopInitFunc(RegisterBeggarShop, { "IS_RANDO" });
static RegisterMenuInitFunc sevenSagesBeggarMenuInitFunc(RegisterBeggarMenu);
