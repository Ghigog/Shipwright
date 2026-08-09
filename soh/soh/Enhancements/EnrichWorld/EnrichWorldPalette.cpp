/**
 * Enrich World - the curated prop palette.
 *
 * Every entry carries two object ids - see PropDef in EnrichWorld.h for why they differ. Both
 * are resolved against the *current* room via Object_GetIndex rather than a table baked from the
 * asset archive, because a room's object list varies by scene header (child/adult, day/night)
 * and a baked table would be wrong for whichever header it wasn't generated from.
 *
 * `requiredObjectId` is a hard gate: props that would Actor_Kill themselves here are dropped
 * from the list entirely, so the silent failure documented in EnrichWorldProps.cpp can't be
 * reached by hand-placing. `nativeObjectId` only sorts the list - non-native props still work in
 * SoH and stay placeable, just under a "not native" heading.
 *
 * Variants are separate entries rather than a raw params box, because "Tree - oval, green" is a
 * choice a person can make and 0x0205 isn't. Params stay editable in the placer for the cases
 * these defaults don't cover (mainly drop tables, the high byte on several of these actors).
 */
#include "soh/ActorDB.h"

#include <spdlog/fmt/fmt.h>

// After the headers above on purpose - see EnrichWorldPlacer.cpp for why z64.h has to come last.
#include "EnrichWorld.h"

extern "C" {
#include "functions.h"
#include "macros.h"
extern PlayState* gPlayState;
}

namespace EnrichWorld {

// Nothing has to be loaded for this prop to work. Spelled out so the table reads as a column.
static constexpr int16_t kNoObjectNeeded = OBJECT_ID_MAX;

const std::vector<PropDef>& AllProps() {
    // Columns: label, actor, native object, required object, params, note.
    // OBJECT_GAMEPLAY_KEEP is always resident, so entries native to it are native everywhere.
    static const std::vector<PropDef> props = {
        // ---- Native everywhere (gameplay_keep) ----
        { "Bugs", ACTOR_OBJ_MURE, OBJECT_GAMEPLAY_KEEP, kNoObjectNeeded, 3, "Pure ambience, no collision." },
        { "Fish", ACTOR_OBJ_MURE, OBJECT_GAMEPLAY_KEEP, kNoObjectNeeded, 2, "Pure ambience. Wants water." },
        { "Flame (decorative)", ACTOR_EN_LIGHT, OBJECT_GAMEPLAY_KEEP, kNoObjectNeeded, 0,
          "params & 0xF sets type/scale." },
        { "River sound", ACTOR_EN_RIVER_SOUND, OBJECT_GAMEPLAY_KEEP, kNoObjectNeeded, 0,
          "Invisible ambient emitter." },
        { "Fish (single)", ACTOR_EN_FISH, OBJECT_GAMEPLAY_KEEP, kNoObjectNeeded, 0,
          "One fish, rather than the shoal the Fish entry spawns. Wants water." },
        { "Bug (single)", ACTOR_EN_INSECT, OBJECT_GAMEPLAY_KEEP, kNoObjectNeeded, 0,
          "One bug, rather than the swarm the Bugs entry spawns." },
        { "Blue fire flame", ACTOR_EN_ICE_HONO, OBJECT_GAMEPLAY_KEEP, kNoObjectNeeded, 0, "" },
        { "Rupee pattern", ACTOR_OBJ_MURE3, OBJECT_GAMEPLAY_KEEP, kNoObjectNeeded, 0,
          "A formation of rupees. Collectible, not pure decoration." },

        // ---- Overworld (gameplay_field_keep) ----
        // Butterflies are Obj_Mure children (En_Butte), whose object is the field keep, not the
        // gameplay keep the spawner itself lives in.
        { "Butterflies", ACTOR_OBJ_MURE, OBJECT_GAMEPLAY_FIELD_KEEP, kNoObjectNeeded, 4,
          "Pure ambience, no collision." },
        { "Flowers", ACTOR_OBJ_HANA, OBJECT_GAMEPLAY_FIELD_KEEP, kNoObjectNeeded, 0,
          "Best pure decoration - no collision, no flags, no drops." },
        { "Rock fragments", ACTOR_OBJ_HANA, OBJECT_GAMEPLAY_FIELD_KEEP, kNoObjectNeeded, 1,
          "Small debris, cylinder r10 h18." },
        // Vanilla uses this as the castle-approach hiding bush, so ObjHana_Init Actor_Kills it
        // once EVENTCHKINF_OBTAINED_ZELDAS_LETTER is set (z_obj_hana.c:92). That is most of the
        // game, and all of it for an adult-starting file - hence the warning rather than a
        // quiet note. Kept in the palette because it is still the right prop for a child-era
        // scene; there is no way to suppress the self-kill without editing the actor.
        { "Bush (field, vanishes later)", ACTOR_OBJ_HANA, OBJECT_GAMEPLAY_FIELD_KEEP, kNoObjectNeeded, 2,
          "WARNING: disappears for good once you have Zelda's Letter - vanilla behaviour, not a "
          "bug in the placer. Decorative only, cannot be cut." },
        { "Grass, cuttable", ACTOR_EN_KUSA, OBJECT_GAMEPLAY_FIELD_KEEP, OBJECT_GAMEPLAY_FIELD_KEEP, 0,
          "Type 0 only. Types 1-2 need OBJECT_KUSA." },
        { "Rock, liftable (small)", ACTOR_EN_ISHI, OBJECT_GAMEPLAY_FIELD_KEEP, kNoObjectNeeded, 0,
          "High nibble of byte 1 = drops." },
        { "Rock, liftable (silver)", ACTOR_EN_ISHI, OBJECT_GAMEPLAY_FIELD_KEEP, kNoObjectNeeded, 1,
          "Large. Sets a switch flag." },
        { "Boulder, bronze", ACTOR_OBJ_HAMISHI, OBJECT_GAMEPLAY_FIELD_KEEP, kNoObjectNeeded, 0,
          "Hammer-breakable." },
        { "Butterfly (single)", ACTOR_EN_BUTTE, OBJECT_GAMEPLAY_FIELD_KEEP, kNoObjectNeeded, 0,
          "One butterfly, rather than the cloud the Butterflies entry spawns." },
        { "Beehive", ACTOR_OBJ_COMB, OBJECT_GAMEPLAY_FIELD_KEEP, kNoObjectNeeded, 0,
          "Breakable, drops from its collectible flag." },
        { "Grotto entrance", ACTOR_DOOR_ANA, OBJECT_GAMEPLAY_FIELD_KEEP, kNoObjectNeeded, 0,
          "A real, enterable grotto hole - scenery with gameplay attached." },

        // Obj_Mure2 rings its children on an 80-unit circle. The spawner is gameplay_keep, but
        // what lands on the ground is what the room has to support - En_Kusa type 0 for the
        // grass patches (which dies without the field keep), En_Ishi for the rocks.
        { "Grass patch - 9 in a ring", ACTOR_OBJ_MURE2, OBJECT_GAMEPLAY_FIELD_KEEP, OBJECT_GAMEPLAY_FIELD_KEEP, 0,
          "9 cuttable grass tufts, same prop as 'Grass, cuttable'." },
        { "Grass patch - 12 in a ring", ACTOR_OBJ_MURE2, OBJECT_GAMEPLAY_FIELD_KEEP, OBJECT_GAMEPLAY_FIELD_KEEP, 1,
          "12 cuttable grass tufts. The commonest vanilla cluster." },
        { "Rock patch - 8 in a ring", ACTOR_OBJ_MURE2, OBJECT_GAMEPLAY_FIELD_KEEP, kNoObjectNeeded, 2,
          "8 small liftable rocks, same prop as 'Rock, liftable (small)'." },

        // ---- Dungeon (gameplay_dangeon_keep) ----
        { "Pot (dungeon)", ACTOR_OBJ_TSUBO, OBJECT_GAMEPLAY_DANGEON_KEEP, OBJECT_GAMEPLAY_DANGEON_KEEP, 0,
          "Bit 8 clear = dungeon pot." },
        { "Pushable block", ACTOR_OBJ_OSHIHIKI, OBJECT_GAMEPLAY_DANGEON_KEEP, kNoObjectNeeded, 0,
          "Structural, not decor." },
        { "Crate, small liftable", ACTOR_OBJ_KIBAKO, OBJECT_GAMEPLAY_DANGEON_KEEP, kNoObjectNeeded, 0,
          "The one Link picks up and throws, unlike the large crate below." },
        { "Pot, flying", ACTOR_EN_TUBO_TRAP, OBJECT_GAMEPLAY_DANGEON_KEEP, kNoObjectNeeded, 0,
          "Launches itself at Link when he gets close." },

        // ---- Scene-specific: added 2026-08-09 from the vanilla usage ranking in
        // ../../../../enrich-world/data/scene-props.json. Each was read for whether it re-checks
        // its own object (none of these do, so none need a required object) and for its params
        // layout, which is in kParamsLayouts below where it isn't a single fixed value.
        { "Flagpole, red cloth", ACTOR_EN_HATA, OBJECT_HATA, kNoObjectNeeded, 0,
          "Vanilla's commonest pure decoration - 56 placements, all overworld." },
        { "Cucco", ACTOR_EN_NIW, OBJECT_NIW, kNoObjectNeeded, 0, "Pick it up, it flaps. Ranch and village." },
        { "Horse", ACTOR_EN_HORSE_NORMAL, OBJECT_HORSE_NORMAL, kNoObjectNeeded, 0,
          "Grazes in place. Lon Lon and Hyrule Field." },
        { "Fence, jumpable", ACTOR_BG_UMAJUMP, OBJECT_UMAJUMP, kNoObjectNeeded, 0, "The Lon Lon obstacle fence." },
        { "Gate, ranch", ACTOR_BG_INGATE, OBJECT_INGATE, kNoObjectNeeded, 0, "Ingo's gates." },
        { "Milk crate", ACTOR_BG_SPOT15_RRBOX, OBJECT_SPOT15_OBJ, kNoObjectNeeded, 0, "Liftable." },
        { "Shop shelves", ACTOR_EN_TANA, OBJECT_SHOP_DUNGEN, kNoObjectNeeded, 0, "" },
        { "Torch, golden", ACTOR_BG_PO_SYOKUDAI, OBJECT_SYOKUDAI, kNoObjectNeeded, 0,
          "The Poe Sisters' variant. High byte is the flame colour." },
        { "Hookshot target post", ACTOR_OBJ_HSBLOCK, OBJECT_D_HSBLOCK, kNoObjectNeeded, 0, "" },
        { "Red ice", ACTOR_BG_ICE_SHELTER, OBJECT_ICE_OBJECTS, kNoObjectNeeded, 0, "Melts to blue fire." },
        { "Cloud ring", ACTOR_BG_SPOT16_DOUGHNUT, OBJECT_EFC_DOUGHNUT, kNoObjectNeeded, 0,
          "Death Mountain's smoke ring. Pure effect, no collision." },
        { "Lava fountain", ACTOR_EFC_ERUPC, OBJECT_EFC_ERUPC, kNoObjectNeeded, 0, "Particle spout, no collision." },
        { "Windmill sails", ACTOR_BG_SPOT01_FUSYA, OBJECT_SPOT01_OBJECTS, kNoObjectNeeded, 0, "Turns on its own." },
        { "Skull jar, giant", ACTOR_BG_HAKA_TUBO, OBJECT_HAKA_OBJECTS, kNoObjectNeeded, 0, "" },
        { "Goron pot, large", ACTOR_BG_SPOT18_BASKET, OBJECT_SPOT18_OBJ, kNoObjectNeeded, 0, "" },
        { "Water spout", ACTOR_EN_SIOFUKI, OBJECT_SIOFUKI, kNoObjectNeeded, 0, "" },

        // ---- Scene-specific ----
        { "Tree - conical, large", ACTOR_EN_WOOD02, OBJECT_WOOD02, kNoObjectNeeded, 0, "" },
        { "Tree - conical, medium", ACTOR_EN_WOOD02, OBJECT_WOOD02, kNoObjectNeeded, 1, "" },
        { "Tree - conical, small", ACTOR_EN_WOOD02, OBJECT_WOOD02, kNoObjectNeeded, 2, "" },
        { "Tree - oval, green", ACTOR_EN_WOOD02, OBJECT_WOOD02, kNoObjectNeeded, 0x0205,
          "The Hyrule Field default - high byte 0x02 is the vanilla drop table." },
        { "Bush - green, small", ACTOR_EN_WOOD02, OBJECT_WOOD02, kNoObjectNeeded, 0x0B, "" },
        { "Bush - green, large", ACTOR_EN_WOOD02, OBJECT_WOOD02, kNoObjectNeeded, 0x0C, "" },
        { "Bush (OBJECT_KUSA)", ACTOR_EN_KUSA, OBJECT_KUSA, OBJECT_KUSA, 1, "Deku Tree, Dodongo's, Well, Kokiri." },
        { "Torch", ACTOR_OBJ_SYOKUDAI, OBJECT_SYOKUDAI, kNoObjectNeeded, 0,
          "params >> 0xC: 0 gold, 1 timed, 2 wooden." },
        { "Signpost", ACTOR_EN_KANBAN, OBJECT_KANBAN, kNoObjectNeeded, 0, "" },
        { "Gossip stone", ACTOR_EN_GS, OBJECT_GS, kNoObjectNeeded, 0, "" },
        { "Pot (overworld)", ACTOR_OBJ_TSUBO, OBJECT_TSUBO, OBJECT_TSUBO, 0x0100, "Bit 8 set = overworld pot." },
        { "Crate, large", ACTOR_OBJ_KIBAKO2, OBJECT_KIBAKO2, kNoObjectNeeded, 0, "" },
        { "Boulder, bombable", ACTOR_OBJ_BOMBIWA, OBJECT_BOMBIWA, kNoObjectNeeded, 0, "" },
        { "Bomb flower", ACTOR_EN_BOMBF, OBJECT_BOMBF, kNoObjectNeeded, 0, "" },
        { "Icicle", ACTOR_BG_ICE_TURARA, OBJECT_ICE_OBJECTS, kNoObjectNeeded, 0, "" },
        { "Gravestone", ACTOR_BG_HAKA, OBJECT_HAKA, kNoObjectNeeded, 0, "Graveyard and Lake Hylia only." },
    };
    return props;
}

/**
 * How each actor slices its params word.
 *
 * `variantMask` is the part that decides *which prop this is* - the palette entry owns it, and
 * the placer holds it fixed. Everything outside the mask is a genuine per-placement option (drop
 * tables, flock sizes, flag slots) and stays editable. Without this the two controls contradict
 * each other: "Butterflies" and "Fish" are the same actor differing only in bits 0-4, so editing
 * params turned one into the other, and the values in between are types the actor kills itself on.
 *
 * `variantCount` is how many variants the actor's own tables actually define, where the mask is a
 * plain 0..n-1 index into them. 0 means it isn't one - Obj_Tsubo's bit is a flag, En_Wood02
 * switches on the value rather than indexing - and no bounds check applies.
 *
 * Read off the actors themselves rather than inferred from the palette:
 *   Obj_Mure    type = params & 0x1F (z_obj_mure.c:95); ptn, svNum and chNum live above it
 *   Obj_Hana    sHanaParams[3] indexed `params & 3` (z_obj_hana.c:79, and in Draw at :113)
 *   En_Kusa     dLists[3] indexed `params & 3` (z_en_kusa.c:516); sObjectIds[3] at :263
 *   Obj_Mure2   D_80B9A818[3] indexed `params & 3` (z_obj_mure2.c:57 and six more)
 *   En_Ishi     type = params & 1; high nibble of byte 1 is the drop table
 *   Obj_Tsubo   object choice is `(params >> 8) & 1`
 *   En_Wood02   low byte is the tree/bush type, high byte the drop table
 *   En_Light    params & 0xF sets type and scale
 *   Obj_Syokudai  params >> 0xC picks gold/timed/wooden
 *
 * Actors absent from this table get variantMask 0: params stays fully editable, which is right
 * for the ones that treat it as flags or ignore it (signposts, crates, gossip stones). None of
 * them has an out-of-bounds variant table - that was checked actor by actor, not assumed.
 */
struct ActorParamsLayout {
    int16_t actorId;
    uint16_t variantMask;
    uint16_t variantCount;
    uint16_t usedMask; // every bit the actor reads at all; the rest change nothing
};

// usedMask is the union of every params bit each actor actually looks at, read off its source.
// Bits outside it are inert - editing them produced values that looked and behaved identically,
// which is what made the box appear full of duplicates. Where usedMask equals variantMask (or is
// zero) the prop has no options at all and the placer says so instead of offering a dead control.
//
// Zeroes are real findings, not gaps: Obj_Kibako2, En_Bombf, Bg_Ice_Turara, Bg_Haka and En_Kanban
// never reference params.
static const ActorParamsLayout kParamsLayouts[] = {
    // type 0x1F | svNum 0x60 | ptn 0x700 | chNum 0xF000
    { ACTOR_OBJ_MURE, 0x001F, 0, 0xF77F },
    // params & 3 and nothing else - no options whatsoever
    { ACTOR_OBJ_HANA, 0x0003, 3, 0x0003 },
    // type 3 | bug bit 0x10 | drop table 0xF00
    { ACTOR_EN_KUSA, 0x0003, 3, 0x0F13 },
    // type 3 | drop table 0xF00
    { ACTOR_OBJ_MURE2, 0x0003, 3, 0x0F03 },
    // size 1 | bugs 0x10 | no-snap 0x20 | switch flag 0xC0+0xF000 | drop table 0xF00
    { ACTOR_EN_ISHI, 0x0001, 2, 0xFFF1 },
    { ACTOR_OBJ_HAMISHI, 0x0000, 0, 0x003F },
    // dungeon/overworld bit 0x100 | switch flag 0x1F | drop table 0x7E00
    { ACTOR_OBJ_TSUBO, 0x0100, 0, 0x7F1F },
    { ACTOR_OBJ_OSHIHIKI, 0x000F, 0, 0x3FFF },
    // low byte the tree/bush type, high byte the drop table
    { ACTOR_EN_WOOD02, 0x00FF, 0, 0xFFFF },
    // gold/timed/wooden in 0xF000 | switch flag 0x3F | timer 0x3C0 | 0x400
    { ACTOR_OBJ_SYOKUDAI, 0xF000, 0, 0xF7FF },
    { ACTOR_EN_GS, 0x0000, 0, 0x3FFF },
    { ACTOR_OBJ_BOMBIWA, 0x0000, 0, 0x803F },
    { ACTOR_EN_LIGHT, 0x000F, 0, 0x0FFF },
    { ACTOR_EN_RIVER_SOUND, 0x0000, 0, 0xFFFF },
    { ACTOR_EN_KANBAN, 0x0000, 0, 0x0000 },
    { ACTOR_OBJ_KIBAKO2, 0x0000, 0, 0x0000 },
    { ACTOR_EN_BOMBF, 0x0000, 0, 0x0000 },
    { ACTOR_BG_ICE_TURARA, 0x0000, 0, 0x0000 },
    { ACTOR_BG_HAKA, 0x0000, 0, 0x0000 },

    // ---- Added with the 2026-08-09 palette expansion ----
    // Two of these carry the same out-of-bounds shape the crash fix was about, so they are the
    // reason variantCount is not vestigial:
    //   Obj_Mure3   spawnFuncs[3] indexed `(params >> 13) & 7` - indices 3-7 call a garbage
    //               FUNCTION POINTER, which is worse than the bad Gfx* that crashed Obj_Hana.
    //               sRupeeCounts[4] runs off the end the same way.
    //   Obj_Hsblock D_80B940C0[3], sCollisionHeaders[3] and sDLists[3] all indexed `params & 3`.
    { ACTOR_OBJ_MURE3, 0xE000, 3, 0xE03F },
    { ACTOR_OBJ_HSBLOCK, 0x0003, 3, 0x0023 },
    // params & 3 picks the bug's behaviour against a four-entry table, so no bounds issue; the
    // high bits are the Gold Skulltula soil flag it reports to.
    { ACTOR_EN_INSECT, 0x0003, 4, 0x1FFF },
    { ACTOR_EN_FISH, 0x0000, 0, 0x0000 },
    { ACTOR_EN_BUTTE, 0x0000, 0, 0x0001 },
    { ACTOR_EN_ICE_HONO, 0x0000, 0, 0x0000 },
    { ACTOR_OBJ_COMB, 0x0000, 0, 0x3F1F },
    { ACTOR_DOOR_ANA, 0x0000, 0, 0x0000 },
    { ACTOR_OBJ_KIBAKO, 0x0000, 0, 0x3F1F },
    { ACTOR_EN_TUBO_TRAP, 0x0000, 0, 0xFFFF },
    { ACTOR_EN_HATA, 0x0000, 0, 0x0000 },
    { ACTOR_EN_NIW, 0x0000, 0, 0x0000 },
    { ACTOR_EN_HORSE_NORMAL, 0x0000, 0, 0x00FF },
    { ACTOR_BG_UMAJUMP, 0x0000, 0, 0x0000 },
    { ACTOR_BG_INGATE, 0x0000, 0, 0x0003 },
    { ACTOR_BG_SPOT15_RRBOX, 0x0000, 0, 0x003F },
    { ACTOR_EN_TANA, 0x0000, 0, 0x0000 },
    { ACTOR_BG_PO_SYOKUDAI, 0x0000, 0, 0xFF00 },
    { ACTOR_BG_ICE_SHELTER, 0x0700, 0, 0x077F },
    { ACTOR_BG_SPOT16_DOUGHNUT, 0x0000, 0, 0x0000 },
    { ACTOR_EFC_ERUPC, 0x0000, 0, 0x0000 },
    { ACTOR_BG_SPOT01_FUSYA, 0x0000, 0, 0x0000 },
    { ACTOR_BG_HAKA_TUBO, 0x0000, 0, 0x003F },
    { ACTOR_BG_SPOT18_BASKET, 0x0000, 0, 0x3F3F },
    { ACTOR_EN_SIOFUKI, 0xF000, 0, 0xFFFF },
};

static const ActorParamsLayout* FindLayout(int16_t actorId) {
    for (const auto& layout : kParamsLayouts) {
        if (layout.actorId == actorId) {
            return &layout;
        }
    }
    return nullptr;
}

uint16_t VariantMask(int16_t actorId) {
    const ActorParamsLayout* layout = FindLayout(actorId);
    return layout == nullptr ? 0 : layout->variantMask;
}

// Fails closed: an actor with no entry above offers no options rather than a full 16-bit word of
// mostly-inert bits. Adding a palette entry for a new actor means adding its layout here too.
uint16_t OptionMask(int16_t actorId) {
    const ActorParamsLayout* layout = FindLayout(actorId);
    return layout == nullptr ? 0 : static_cast<uint16_t>(layout->usedMask & ~layout->variantMask);
}

bool AreParamsSafe(int16_t actorId, int16_t params) {
    const ActorParamsLayout* layout = FindLayout(actorId);
    if (layout == nullptr || layout->variantCount == 0) {
        return true;
    }
    return (params & layout->variantMask) < layout->variantCount;
}

bool IsPropNative(const PropDef& def) {
    if (gPlayState == nullptr) {
        return false;
    }
    return Object_GetIndex(&gPlayState->objectCtx, def.nativeObjectId) >= 0;
}

bool IsPropUsable(const PropDef& def) {
    if (gPlayState == nullptr) {
        return false;
    }
    if (def.requiredObjectId == kNoObjectNeeded) {
        return true;
    }
    return Object_GetIndex(&gPlayState->objectCtx, def.requiredObjectId) >= 0;
}

std::string ActorLabel(int16_t actorId, int16_t params) {
    for (const auto& def : AllProps()) {
        if (def.actorId == actorId && def.params == params) {
            return def.label;
        }
    }
    // Params were edited away from any palette default - fall back to the actor's own name.
    auto entry = ActorDB::Instance->RetrieveEntry(actorId);
    if (entry.entry.valid) {
        return fmt::format("{} (0x{:X})", entry.name, static_cast<uint16_t>(params));
    }
    return fmt::format("actor {} (0x{:X})", actorId, static_cast<uint16_t>(params));
}

} // namespace EnrichWorld
