/**
 * Seven Sages - Phase 3: talking overworld NPCs deliver hints about the
 * current seed.
 *
 * The hints themselves are real entries in `StaticData::staticHintInfoMap`
 * (static_data.cpp), generated alongside every other dedicated NPC hint, so
 * they are spoiler-logged, hint-tracked and serialized with the seed for free.
 * This file is only the delivery half: which actor speaks which hint.
 *
 * NPCs are matched by actor + scene + masked params rather than by text ID, so
 * one row covers a whole character instead of enumerating their dialogue.
 *
 * Only the *first* textbox of a conversation is replaced - see
 * IsConversationStart(). Anything that drives a flow (a purchase prompt, a
 * minigame offer, a yes/no question) is therefore left alone as long as it isn't
 * the opening line, because those carry choice control codes that a plain hint
 * message would destroy.
 */
#include <soh/OTRGlobals.h>
#include "soh/Enhancements/randomizer/randomizer.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"

#include <spdlog/spdlog.h>

extern "C" {
extern PlayState* gPlayState;
#include <macros.h>
#include <functions.h>
#include <variables.h>
#include "src/overlays/actors/ovl_En_Ossan/z_en_ossan.h"
}

namespace {

// A single addressable NPC. Most talking actors in OoT are one actor covering
// many distinct characters, selected by params - EN_HY alone is 21 different
// villagers (`params & 0x7F`, see z_en_hy.h). So identity is
// actor + scene + masked params, not actor + scene.
//
// paramsMask 0 means "any params": use it for actors that only ever represent
// one character (Talon, King Zora), and a real mask for everyone else.
//
// requiredTextId 0 means "no gate": the conversation-start check alone is
// enough. A nonzero value additionally requires the *current* textId the
// game was about to show to match exactly - needed for actors whose identity
// (actor+scene+params) doesn't change between two different conversation
// states, so matching on identity alone would fire on both. The Fire Temple
// caged Gorons are the first case of this: 0x3051 (still trapped) and 0x3052
// (just freed) are both fresh conversation starts for the same actor
// instance, and only the freed one should carry a hint.
struct NpcHintSpeaker {
    int16_t actorId;
    int16_t sceneNum;
    uint16_t paramsMask;
    uint16_t paramsValue;
    uint16_t requiredTextId;
    RandomizerHint hint;
};

// A shopkeeper's talk-to-owner line, identified by message ID. sceneNum -1 means
// "any scene".
//
// This is how shopkeepers are reached: EnOssan_Talk*Shopkeeper (z_en_ossan.c)
// delivers their conversational line through Message_ContinueTextbox, so it is
// never a conversation start and the actor table can never see it.
//
// A text ID alone is NOT a safe key - several of these IDs are reused elsewhere
// in the same scene for messages that drive a flow (0x9C is also the Lon Lon Milk
// purchase, 0x9D also a can't-buy refusal, 0x10BA also an EN_KO line). Matching
// one of those and replacing it softlocks the shop. So every row here is gated on
// IsShopOwnerTalking() as well; see that function.
struct NpcHintTextId {
    uint16_t textId;
    int16_t sceneNum;
    RandomizerHint hint;
};

// Generated from docs/npc-hints.md in the seven-sages repo - edit that file,
// not this table.
constexpr NpcHintTextId npcHintTextIds[] = {
// >>> SEVEN_SAGES_GENERATED: TEXT_IDS - edit data/npc-hints.json, not this
    { 0x3028, SCENE_GORON_SHOP, RH_NPC_SHOP_GORON },  // Shop Goron
    { 0x302D, SCENE_GORON_SHOP, RH_NPC_SHOP_GORON },  // Shop Goron
    { 0x300F, SCENE_GORON_SHOP, RH_NPC_SHOP_GORON },  // Shop Goron
    { 0x3057, SCENE_GORON_SHOP, RH_NPC_SHOP_GORON },  // Shop Goron
    { 0x305B, SCENE_GORON_SHOP, RH_NPC_SHOP_GORON },  // Shop Goron
    { 0x5046, SCENE_POTION_SHOP_KAKARIKO, RH_NPC_SHOP_KAK_POTION },  // Shop Kakariko Potion
    { 0x504E, SCENE_POTION_SHOP_KAKARIKO, RH_NPC_SHOP_KAK_POTION },  // Shop Kakariko Potion
    { 0x10BA, SCENE_KOKIRI_SHOP, RH_NPC_SHOP_KOKIRI },  // Shop Kokiri
    { 0x009C, SCENE_BAZAAR, RH_NPC_SHOP_BAZAAR_KAK },  // Shop Bazaar Kakariko
    { 0x009D, SCENE_BAZAAR, RH_NPC_SHOP_BAZAAR_MARKET },  // Shop Bazaar Market
    { 0x7076, SCENE_BOMBCHU_SHOP, RH_NPC_SHOP_BOMBCHU },  // Shop Bombchu
    { 0x504E, SCENE_POTION_SHOP_MARKET, RH_NPC_SHOP_MARKET_POTION },  // Shop Market Potion
    { 0x70AE, SCENE_HAPPY_MASK_SHOP, RH_NPC_SHOP_MASK },  // Shop Mask
    { 0x70A3, SCENE_HAPPY_MASK_SHOP, RH_NPC_SHOP_MASK },  // Shop Mask
    { 0x70A4, SCENE_HAPPY_MASK_SHOP, RH_NPC_SHOP_MASK },  // Shop Mask
    { 0x403A, SCENE_ZORA_SHOP, RH_NPC_SHOP_ZORA },  // Shop Zora
    { 0x403B, SCENE_ZORA_SHOP, RH_NPC_SHOP_ZORA },  // Shop Zora
// <<< SEVEN_SAGES_GENERATED: TEXT_IDS
};

// Generated from docs/npc-hints.md in the seven-sages repo - edit that file,
// not this table.
//
// Actors are picked to avoid anything an existing hook already claims: Saria
// (RSK_SARIA_HINT), Malon (RSK_MALON_HINT), Anju (RSK_CHICKENS_HINT) and
// Medigoron (TEXT_MEDIGORON, an EN_GO2) would all double-fire against ours.
constexpr NpcHintSpeaker npcHintSpeakers[] = {
// >>> SEVEN_SAGES_GENERATED: SPEAKERS - edit data/npc-hints.json, not this
    { ACTOR_EN_HEISHI4, SCENE_HYRULE_CASTLE, 0x00, 0, 0, RH_NPC_HC_GUARD },  // Hyrule Castle Guard
    { ACTOR_EN_MA1, SCENE_HYRULE_CASTLE, 0x00, 0, 0, RH_NPC_HC_MALON },  // Hyrule Castle Malon
    { ACTOR_EN_GO2, SCENE_DEATH_MOUNTAIN_TRAIL, 0x1F, 4, 0, RH_NPC_DMT_GORON_BOMB_FLOWER },  // Death Mountain Trail Goron Bomb Flower
    { ACTOR_EN_GO2, SCENE_DEATH_MOUNTAIN_TRAIL, 0x1F, 6, 0, RH_NPC_DMT_GORON_DC },  // Death Mountain Trail Goron Dc
    { ACTOR_EN_GO2, SCENE_DEATH_MOUNTAIN_TRAIL, 0x1F, 12, 0, RH_NPC_DMT_GORON_FAIRY },  // Death Mountain Trail Goron Fairy
    { ACTOR_EN_GO2, SCENE_DEATH_MOUNTAIN_TRAIL, 0x1F, 5, 0, RH_NPC_DMT_GORON_ROLLING },  // Death Mountain Trail Goron Rolling
    { ACTOR_EN_GE1, SCENE_GERUDO_VALLEY, 0xFF, 5, 0, RH_NPC_GV_GERUDO_FLOOR },  // Gerudo Valley Gerudo Floor
    { ACTOR_EN_TORYO, SCENE_GERUDO_VALLEY, 0x00, 0, 0, RH_NPC_GV_MUTOH },  // Gerudo Valley Mutoh
    { ACTOR_EN_GE2, SCENE_GERUDOS_FORTRESS, 0xFF, 2, 0, RH_NPC_GF_GERUDO_CARD_GIVER },  // Gerudo Fortress Gerudo Card Giver
    { ACTOR_EN_GE1, SCENE_GERUDOS_FORTRESS, 0xFF, 0, 0, RH_NPC_GF_GERUDO_GATE_GUARD },  // Gerudo Fortress Gerudo Gate Guard
    { ACTOR_EN_GE1, SCENE_GERUDOS_FORTRESS, 0xFF, 1, 0, RH_NPC_GF_GERUDO_GATE_OP },  // Gerudo Fortress Gerudo Gate Op
    { ACTOR_EN_GE1, SCENE_GERUDOS_FORTRESS, 0xFF, 4, 0, RH_NPC_GF_GERUDO_NORMAL },  // Gerudo Fortress Gerudo Normal
    { ACTOR_EN_GE2, SCENE_GERUDOS_FORTRESS, 0xFF, 0, 0, RH_NPC_GF_GERUDO_PATROL },  // Gerudo Fortress Gerudo Patrol
    { ACTOR_EN_GE2, SCENE_GERUDOS_FORTRESS, 0xFF, 1, 0, RH_NPC_GF_GERUDO_STATIONARY },  // Gerudo Fortress Gerudo Stationary
    { ACTOR_EN_GE1, SCENE_GERUDOS_FORTRESS, 0xFF, 70, 0, RH_NPC_GF_GERUDO_TG_GUARD },  // Gerudo Fortress Gerudo Tg Guard
    { ACTOR_EN_DAIKU, SCENE_THIEVES_HIDEOUT, 0x03, 0, 0, RH_NPC_TH_CARP_1TORCH },  // Thieves' Hideout carpenter 1Torch
    { ACTOR_EN_DAIKU, SCENE_THIEVES_HIDEOUT, 0x03, 1, 0, RH_NPC_TH_CARP_DEAD_END },  // Thieves' Hideout carpenter Dead End
    { ACTOR_EN_DAIKU, SCENE_THIEVES_HIDEOUT, 0x03, 2, 0, RH_NPC_TH_CARP_DOUBLE },  // Thieves' Hideout carpenter Double
    { ACTOR_EN_DAIKU, SCENE_THIEVES_HIDEOUT, 0x03, 3, 0, RH_NPC_TH_CARP_STEEP },  // Thieves' Hideout carpenter Steep
    { ACTOR_EN_DU, SCENE_GORON_CITY, 0x00, 0, 0, RH_NPC_GC_DARUNIA },  // Goron City Darunia
    { ACTOR_EN_GO2, SCENE_GORON_CITY, 0x1F, 7, 0, RH_NPC_GC_GORON_ENTRANCE },  // Goron City Goron Entrance
    { ACTOR_EN_GO, SCENE_GORON_CITY, 0x00, 0, 0, RH_NPC_GC_GORON_GENERIC },  // Goron City Goron Generic
    { ACTOR_EN_GO2, SCENE_GORON_CITY, 0x1F, 8, 0, RH_NPC_GC_GORON_ISLAND },  // Goron City Goron Island
    { ACTOR_EN_GO2, SCENE_GORON_CITY, 0x1F, 1, 0, RH_NPC_GC_GORON_LINK },  // Goron City Goron Link
    { ACTOR_EN_GO2, SCENE_GORON_CITY, 0x1F, 11, 0, RH_NPC_GC_GORON_LOST_WOODS },  // Goron City Goron Lost Woods
    { ACTOR_EN_GO2, SCENE_GORON_CITY, 0x1F, 9, 0, RH_NPC_GC_GORON_LOWEST },  // Goron City Goron Lowest
    { ACTOR_EN_GO2, SCENE_GORON_CITY, 0x1F, 0, 0, RH_NPC_GC_GORON_ROLLING_BIG },  // Goron City Goron Rolling Big
    { ACTOR_EN_GO2, SCENE_GORON_CITY, 0x1F, 10, 0, RH_NPC_GC_GORON_STAIRWELL },  // Goron City Goron Stairwell
    { ACTOR_EN_TK, SCENE_GRAVEKEEPERS_HUT, 0x00, 0, 0, RH_NPC_DAMPE_HUT },  // Dampe Hut
    { ACTOR_EN_TK, SCENE_GRAVEYARD, 0x00, 0, 0, RH_NPC_GY_DAMPE },  // Graveyard Dampe
    { ACTOR_EN_CS, SCENE_GRAVEYARD, 0x00, 0, 0, RH_NPC_GY_KID },  // Graveyard Kid
    { ACTOR_EN_GE1, SCENE_HAUNTED_WASTELAND, 0xFF, 4, 0, RH_NPC_HW_CHEST_TIP },  // Haunted Wasteland Chest Tip
    { ACTOR_EN_HS, SCENE_KAKARIKO_VILLAGE, 0x00, 0, 0, RH_NPC_KAK_CARPENTERS_SON },  // Kakariko Carpenters Son
    { ACTOR_EN_DAIKU_KAKARIKO, SCENE_KAKARIKO_VILLAGE, 0x03, 0, 0, RH_NPC_KAK_CARP_ICHIRO },  // Kakariko carpenter Ichiro
    { ACTOR_EN_DAIKU_KAKARIKO, SCENE_KAKARIKO_VILLAGE, 0x03, 1, 0, RH_NPC_KAK_CARP_JIRO },  // Kakariko carpenter Jiro
    { ACTOR_EN_DAIKU_KAKARIKO, SCENE_KAKARIKO_VILLAGE, 0x03, 2, 0, RH_NPC_KAK_CARP_SABOORO },  // Kakariko carpenter Sabooro
    { ACTOR_EN_DAIKU_KAKARIKO, SCENE_KAKARIKO_VILLAGE, 0x03, 3, 0, RH_NPC_KAK_CARP_SHIRO },  // Kakariko carpenter Shiro
    { ACTOR_EN_NIW_GIRL, SCENE_KAKARIKO_VILLAGE, 0x00, 0, 0, RH_NPC_KAK_CUCCO_GIRL },  // Kakariko Cucco Girl
    { ACTOR_EN_HY, SCENE_KAKARIKO_VILLAGE, 0x7F, 2, 0, RH_NPC_KAK_HY_AHG_2 },  // Kakariko Hylian Ahg 2
    { ACTOR_EN_HY, SCENE_KAKARIKO_VILLAGE, 0x7F, 7, 0, RH_NPC_KAK_HY_BJI_7 },  // Kakariko Hylian Bji 7
    { ACTOR_EN_HY, SCENE_KAKARIKO_VILLAGE, 0x7F, 10, 0, RH_NPC_KAK_HY_BOJ_10 },  // Kakariko Hylian Boj 10
    { ACTOR_EN_HY, SCENE_KAKARIKO_VILLAGE, 0x7F, 12, 0, RH_NPC_KAK_HY_BOJ_12 },  // Kakariko Hylian Boj 12
    { ACTOR_EN_HY, SCENE_KAKARIKO_VILLAGE, 0x7F, 9, 0, RH_NPC_KAK_HY_BOJ_9 },  // Kakariko Hylian Boj 9
    { ACTOR_EN_TORYO, SCENE_KAKARIKO_VILLAGE, 0x00, 0, 0, RH_NPC_KAK_MUTOH },  // Kakariko Mutoh
    { ACTOR_EN_ANI, SCENE_KAKARIKO_VILLAGE, 0x00, 0, 0, RH_NPC_KAK_ROOF_GUY },  // Kakariko Roof Guy
    { ACTOR_EN_HY, SCENE_KAKARIKO_CENTER_GUEST_HOUSE, 0x7F, 17, 0, RH_NPC_GUEST_HY_AHG_17 },  // Guest Hylian Ahg 17
    { ACTOR_EN_HY, SCENE_KAKARIKO_CENTER_GUEST_HOUSE, 0x7F, 2, 0, RH_NPC_GUEST_HY_AHG_2 },  // Guest Hylian Ahg 2
    { ACTOR_EN_HY, SCENE_KAKARIKO_CENTER_GUEST_HOUSE, 0x7F, 0, 0, RH_NPC_GUEST_HY_AOB },  // Guest Hylian Aob
    { ACTOR_EN_HY, SCENE_KAKARIKO_CENTER_GUEST_HOUSE, 0x7F, 7, 0, RH_NPC_GUEST_HY_BJI_7 },  // Guest Hylian Bji 7
    { ACTOR_EN_HY, SCENE_KAKARIKO_CENTER_GUEST_HOUSE, 0x7F, 18, 0, RH_NPC_GUEST_HY_BOB_18 },  // Guest Hylian Bob 18
    { ACTOR_EN_HY, SCENE_KAKARIKO_CENTER_GUEST_HOUSE, 0x7F, 9, 0, RH_NPC_GUEST_HY_BOJ_9 },  // Guest Hylian Boj 9
    { ACTOR_EN_HY, SCENE_IMPAS_HOUSE, 0x7F, 10, 0, RH_NPC_IMPAS_HY_BOJ_10 },  // Impas Hylian Boj 10
    { ACTOR_EN_FU, SCENE_WINDMILL_AND_DAMPES_GRAVE, 0x00, 0, 0, RH_NPC_WINDMILL_MAN },  // Windmill Man
    { ACTOR_EN_KO, SCENE_KOKIRI_FOREST, 0xFF, 12, 0, RH_NPC_FADO },  // Fado
    { ACTOR_EN_KO, SCENE_KNOW_IT_ALL_BROS_HOUSE, 0x00, 0, 0, RH_NPC_KNOW_IT_ALL_BROS },  // Know It All Bros
    { ACTOR_EN_KO, SCENE_KOKIRI_FOREST, 0xFF, 0, 0, RH_NPC_KOKIRI_0 },  // Kokiri 0
    { ACTOR_EN_KO, SCENE_KOKIRI_FOREST, 0xFF, 1, 0, RH_NPC_KOKIRI_1 },  // Kokiri 1
    { ACTOR_EN_KO, SCENE_KOKIRI_FOREST, 0xFF, 10, 0, RH_NPC_KOKIRI_10 },  // Kokiri 10
    { ACTOR_EN_KO, SCENE_KOKIRI_FOREST, 0xFF, 11, 0, RH_NPC_KOKIRI_11 },  // Kokiri 11
    { ACTOR_EN_KO, SCENE_KOKIRI_FOREST, 0xFF, 2, 0, RH_NPC_KOKIRI_2 },  // Kokiri 2
    { ACTOR_EN_KO, SCENE_KOKIRI_FOREST, 0xFF, 3, 0, RH_NPC_KOKIRI_3 },  // Kokiri 3
    { ACTOR_EN_KO, SCENE_KOKIRI_FOREST, 0xFF, 4, 0, RH_NPC_KOKIRI_4 },  // Kokiri 4
    { ACTOR_EN_KO, SCENE_KOKIRI_FOREST, 0xFF, 5, 0, RH_NPC_KOKIRI_5 },  // Kokiri 5
    { ACTOR_EN_KO, SCENE_KOKIRI_FOREST, 0xFF, 6, 0, RH_NPC_KOKIRI_6 },  // Kokiri 6
    { ACTOR_EN_KO, SCENE_KOKIRI_FOREST, 0xFF, 7, 0, RH_NPC_KOKIRI_7 },  // Kokiri 7
    { ACTOR_EN_KO, SCENE_KOKIRI_FOREST, 0xFF, 8, 0, RH_NPC_KOKIRI_8 },  // Kokiri 8
    { ACTOR_EN_KO, SCENE_KOKIRI_FOREST, 0xFF, 9, 0, RH_NPC_KOKIRI_9 },  // Kokiri 9
    { ACTOR_EN_KO, SCENE_TWINS_HOUSE, 0x00, 0, 0, RH_NPC_TWINS_HOUSE },  // Twins House
    { ACTOR_EN_MK, SCENE_LAKESIDE_LABORATORY, 0x00, 0, 0, RH_NPC_LH_PROFESSOR },  // Lake Hylia Professor
    { ACTOR_EN_IN, SCENE_LON_LON_RANCH, 0x00, 0, 0, RH_NPC_INGO },  // Ingo
    { ACTOR_EN_MA2, SCENE_LON_LON_RANCH, 0x00, 0, 0, RH_NPC_MALON_CORRAL },  // Malon Corral
    { ACTOR_EN_MA3, SCENE_LON_LON_RANCH, 0x00, 0, 0, RH_NPC_MALON_RANCH },  // Malon Ranch
    { ACTOR_EN_TA, SCENE_LON_LON_RANCH, 0x00, 0, 0, RH_NPC_TALON },  // Talon
    { ACTOR_EN_HEISHI2, SCENE_MARKET_GUARD_HOUSE, 0x00, 0, 0, RH_NPC_MARKET_GUARD_HOUSE },  // Market Guard House
    { ACTOR_EN_NIW_GIRL, SCENE_MARKET_DAY, 0x00, 0, 0, RH_NPC_MKT_CUCCO_GIRL },  // Market Cucco Girl
    { ACTOR_EN_TG, SCENE_MARKET_DAY, 0x00, 0, 0, RH_NPC_MKT_DANCERS },  // Market Dancers
    { ACTOR_EN_GO2, SCENE_MARKET_DAY, 0x1F, 13, 0, RH_NPC_MKT_GORON_BAZAAR },  // Market Goron Bazaar
    { ACTOR_EN_HEISHI2, SCENE_MARKET_DAY, 0x00, 0, 0, RH_NPC_MKT_GUARD },  // Market Guard
    { ACTOR_EN_MU, SCENE_MARKET_DAY, 0x00, 0, 0, RH_NPC_MKT_HAGGLERS },  // Market Hagglers
    { ACTOR_EN_HY, SCENE_MARKET_DAY, 0x7F, 13, 0, RH_NPC_MKT_HY_AHG_13 },  // Market Hylian Ahg 13
    { ACTOR_EN_HY, SCENE_MARKET_DAY, 0x7F, 2, 0, RH_NPC_MKT_HY_AHG_2 },  // Market Hylian Ahg 2
    { ACTOR_EN_HY, SCENE_MARKET_DAY, 0x7F, 20, 0, RH_NPC_MKT_HY_AHG_20 },  // Market Hylian Ahg 20
    { ACTOR_EN_HY, SCENE_MARKET_DAY, 0x7F, 4, 0, RH_NPC_MKT_HY_AHG_4 },  // Market Hylian Ahg 4
    { ACTOR_EN_HY, SCENE_MARKET_DAY, 0x7F, 0, 0, RH_NPC_MKT_HY_AOB },  // Market Hylian Aob
    { ACTOR_EN_HY, SCENE_MARKET_DAY, 0x7F, 6, 0, RH_NPC_MKT_HY_BBA },  // Market Hylian Bba
    { ACTOR_EN_HY, SCENE_MARKET_DAY, 0x7F, 15, 0, RH_NPC_MKT_HY_BJI_15 },  // Market Hylian Bji 15
    { ACTOR_EN_HY, SCENE_MARKET_DAY, 0x7F, 19, 0, RH_NPC_MKT_HY_BJI_19 },  // Market Hylian Bji 19
    { ACTOR_EN_HY, SCENE_MARKET_DAY, 0x7F, 14, 0, RH_NPC_MKT_HY_BOJ_14 },  // Market Hylian Boj 14
    { ACTOR_EN_HY, SCENE_MARKET_DAY, 0x7F, 16, 0, RH_NPC_MKT_HY_BOJ_16 },  // Market Hylian Boj 16
    { ACTOR_EN_HY, SCENE_MARKET_DAY, 0x7F, 3, 0, RH_NPC_MKT_HY_BOJ_3 },  // Market Hylian Boj 3
    { ACTOR_EN_HY, SCENE_MARKET_DAY, 0x7F, 5, 0, RH_NPC_MKT_HY_BOJ_5 },  // Market Hylian Boj 5
    { ACTOR_EN_HY, SCENE_MARKET_DAY, 0x7F, 11, 0, RH_NPC_MKT_HY_CNE_11 },  // Market Hylian Cne 11
    { ACTOR_EN_HY, SCENE_MARKET_DAY, 0x7F, 8, 0, RH_NPC_MKT_HY_CNE_8 },  // Market Hylian Cne 8
    { ACTOR_EN_HY, SCENE_MARKET_DAY, 0x7F, 1, 0, RH_NPC_MKT_HY_COB },  // Market Hylian Cob
    { ACTOR_EN_GUEST, SCENE_MARKET_DAY, 0x00, 0, 0, RH_NPC_MKT_MASK_CUSTOMER },  // Market Mask Customer
    { ACTOR_EN_KZ, SCENE_ZORAS_DOMAIN, 0x00, 0, 0, RH_NPC_ZD_KING_ZORA },  // Zora's Domain King Zora
    { ACTOR_EN_RU1, SCENE_ZORAS_DOMAIN, 0x00, 0, 0, RH_NPC_ZD_RUTO },  // Zora's Domain Ruto
    { ACTOR_EN_ZO, SCENE_ZORAS_DOMAIN, 0x3F, 0, 0, RH_NPC_ZD_ZORA_0 },  // Zora's Domain Zora 0
    { ACTOR_EN_ZO, SCENE_ZORAS_DOMAIN, 0x3F, 1, 0, RH_NPC_ZD_ZORA_1 },  // Zora's Domain Zora 1
    { ACTOR_EN_ZO, SCENE_ZORAS_DOMAIN, 0x3F, 2, 0, RH_NPC_ZD_ZORA_2 },  // Zora's Domain Zora 2
    { ACTOR_EN_ZO, SCENE_ZORAS_DOMAIN, 0x3F, 3, 0, RH_NPC_ZD_ZORA_3 },  // Zora's Domain Zora 3
    { ACTOR_EN_ZO, SCENE_ZORAS_DOMAIN, 0x3F, 4, 0, RH_NPC_ZD_ZORA_4 },  // Zora's Domain Zora 4
    { ACTOR_EN_ZO, SCENE_ZORAS_DOMAIN, 0x3F, 5, 0, RH_NPC_ZD_ZORA_5 },  // Zora's Domain Zora 5
    { ACTOR_EN_ZO, SCENE_ZORAS_FOUNTAIN, 0x3F, 6, 0, RH_NPC_ZF_ZORA_6 },  // Zora's Fountain Zora 6
    { ACTOR_EN_ZO, SCENE_ZORAS_FOUNTAIN, 0x3F, 7, 0, RH_NPC_ZF_ZORA_7 },  // Zora's Fountain Zora 7
    { ACTOR_EN_ZO, SCENE_ZORAS_RIVER, 0x3F, 8, 0, RH_NPC_ZR_ZORA_8 },  // Zora's River Zora 8
    { ACTOR_EN_GO2, SCENE_FIRE_TEMPLE, 0xFC00, 0x2000, 0x3071, RH_NPC_FT_GORON_PILLAR },  // Fire Temple Goron Pillar
    { ACTOR_EN_GO2, SCENE_FIRE_TEMPLE, 0xFC00, 0x1000, 0x3071, RH_NPC_FT_GORON_FLAME_DANCER },  // Fire Temple Goron Flame Dancer
    { ACTOR_EN_GO2, SCENE_FIRE_TEMPLE, 0xFC00, 0x0C00, 0x3071, RH_NPC_FT_GORON_FALLING_DOORS },  // Fire Temple Goron Falling Doors
    { ACTOR_EN_GO2, SCENE_FIRE_TEMPLE, 0xFC00, 0x2800, 0x3071, RH_NPC_FT_GORON_OCARINA },  // Fire Temple Goron Ocarina
    { ACTOR_EN_GO2, SCENE_FIRE_TEMPLE, 0xFC00, 0x2C00, 0x3071, RH_NPC_FT_GORON_HIDDEN_DOOR },  // Fire Temple Goron Hidden Door
// <<< SEVEN_SAGES_GENERATED: SPEAKERS
};

// True only for the opening textbox of a conversation.
//
// Both Message_StartTextbox() and Message_ContinueTextbox() call
// Message_OpenText() *before* they assign msgMode, so at hook time msgMode still
// holds the pre-open value - MSGMODE_NONE exactly when nothing was already being
// said. That makes this a free, stateless test for "first box", with no need to
// track the actor or watch for the conversation ending.
//
// Gating the actor table on this does two things: a multi-part conversation no
// longer repeats the hint on every box, and anything that drives a flow (a
// purchase prompt, a minigame offer, a yes/no question) is protected, since those
// arrive as continuations and carry choice control codes that a plain hint
// message would wipe out.
bool IsConversationStart() {
    return gPlayState->msgCtx.msgMode == MSGMODE_NONE;
}

// True only while an EnOssan is delivering its talk-to-owner line.
//
// EnOssan_ChooseTalkToOwner() sets stateFlag *before* calling into
// sShopkeeperTalkOwner[], so by the time that function's Message_ContinueTextbox
// reaches our hook the flag already reads OSSAN_STATE_TALKING_TO_SHOPKEEPER.
// Every other shop message - purchase prompts, refusals, milk fanfare - is sent
// by a helper that sets its state *after* the textbox call, so none of them can
// be mistaken for this one even when they share a text ID.
//
// player->talkActor holds for the whole shop conversation: it is only cleared
// when the player loses ACTOR_FLAG_TALK (z_player.c), which happens at the end of
// the interaction, not between the shop's internal states.
bool IsShopOwnerTalking() {
    Player* player = GET_PLAYER(gPlayState);
    if (player == nullptr || player->talkActor == nullptr || player->talkActor->id != ACTOR_EN_OSSAN) {
        return false;
    }
    return ((EnOssan*)player->talkActor)->stateFlag == OSSAN_STATE_TALKING_TO_SHOPKEEPER;
}

RandomizerHint FindHintForTextId(uint16_t textId) {
    if (!IsShopOwnerTalking()) {
        return RH_NONE;
    }
    for (const NpcHintTextId& entry : npcHintTextIds) {
        if (entry.textId == textId && (entry.sceneNum < 0 || entry.sceneNum == gPlayState->sceneNum)) {
            return entry.hint;
        }
    }
    return RH_NONE;
}

RandomizerHint FindHintForCurrentTalkActor(uint16_t textId) {
    Player* player = GET_PLAYER(gPlayState);
    if (player == nullptr || player->talkActor == nullptr) {
        return RH_NONE;
    }
    const Actor* talkActor = player->talkActor;
    for (const NpcHintSpeaker& speaker : npcHintSpeakers) {
        if (speaker.actorId != talkActor->id || speaker.sceneNum != gPlayState->sceneNum) {
            continue;
        }
        if (speaker.paramsMask != 0 && (talkActor->params & speaker.paramsMask) != speaker.paramsValue) {
            continue;
        }
        if (speaker.requiredTextId != 0 && speaker.requiredTextId != textId) {
            continue;
        }
        return speaker.hint;
    }
    // Unclaimed NPC. Logged so that walking around and talking to everyone
    // produces a ground-truth roster - actor/scene/params straight from the game
    // rather than guessed from scene data we can't read offline.
    SPDLOG_INFO("[SevenSages] unclaimed NPC: actor={} scene={} params=0x{:04X}", talkActor->id,
                gPlayState->sceneNum, static_cast<uint16_t>(talkActor->params));
    return RH_NONE;
}

} // namespace

void BuildNpcHintMessage(uint16_t* textId, bool* loadFromMessageTable) {
    // Shopkeepers first: their talk-to-owner line arrives mid-conversation via
    // Message_ContinueTextbox, so it is never a conversation start and the actor
    // table can never see it. Matched by text ID plus shop state - see
    // IsShopOwnerTalking(). Browsing and buying stay untouched.
    RandomizerHint hint = FindHintForTextId(*textId);
    const bool isShopOwnerLine = hint != RH_NONE;
    if (hint == RH_NONE && IsConversationStart()) {
        hint = FindHintForCurrentTalkActor(*textId);
    }
    if (hint == RH_NONE) {
        return;
    }
    // A hint whose target check fell out of the pool this seed leaves vanilla
    // dialogue alone rather than showing an empty textbox.
    auto hintEntry = OTRGlobals::Instance->gRandoContext->GetHint(hint);
    if (!hintEntry->IsEnabled()) {
        return;
    }

    GameInteractor::Instance->ExecuteHooks<GameInteractor::OnRandoHintRevealed>(hint);
    CustomMessage msg = hintEntry->GetHintMessage(MF_AUTO_FORMAT);
    // Shop-shelf subjects come back phrased as an instruction to the player
    // ("Buy Deku Nut"). Strip the imperative so an NPC remarking on a shelf still
    // scans as speech. Same cleanup the gossip stones do.
    msg.Replace("Buy ", "");
    msg.Replace("Acheter: ", "");
    msg.Replace(" kaufen ", "");
    msg.Replace(" kaufen", "");
    // A shop owner's line has to end with the EVENT control code, not the plain
    // END that CustomMessage appends. EnOssan_State_TalkingToShopkeeper only
    // advances on Message_GetState() == TEXT_STATE_EVENT, which needs
    // textboxEndType EVENT (z_message_PAL.c). And the message system will not
    // close the box itself either, because the shop sets YREG(31) while browsing.
    // Terminate with END here and both sides wait forever: the textbox stays up
    // with every input dead until the game is reset.
    if (isShopOwnerLine) {
        // CTRL_EVENT, spelled out because message_data_fmt.h is not in this TU and
        // CustomMessageManager.h #undefs the MESSAGE_* macros it would provide.
        constexpr char kMessageEvent = '\x0B';
        msg.Replace(CustomMessage::MESSAGE_END(), std::string(1, kMessageEvent));
    }
    msg.LoadIntoFont();
    *loadFromMessageTable = false;
}

void RegisterSevenSagesNpcHints() {
    COND_HOOK(OnOpenText, RAND_GET_OPTION(RSK_NPC_HINTS), BuildNpcHintMessage);
}

static RegisterShipInitFunc sevenSagesNpcHintsInitFunc(RegisterSevenSagesNpcHints, { "IS_RANDO" });
