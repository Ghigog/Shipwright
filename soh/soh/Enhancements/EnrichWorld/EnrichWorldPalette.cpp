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
    int16_t variantMask;
    int16_t variantCount;
};

static const ActorParamsLayout kParamsLayouts[] = {
    { ACTOR_OBJ_MURE, 0x1F, 0 },   { ACTOR_OBJ_HANA, 0x03, 3 },     { ACTOR_EN_KUSA, 0x03, 3 },
    { ACTOR_OBJ_MURE2, 0x03, 3 },  { ACTOR_EN_ISHI, 0x01, 2 },      { ACTOR_OBJ_TSUBO, 0x0100, 0 },
    { ACTOR_EN_WOOD02, 0x00FF, 0 }, { ACTOR_EN_LIGHT, 0x000F, 0 },  { ACTOR_OBJ_SYOKUDAI, (int16_t)0xF000, 0 },
};

static const ActorParamsLayout* FindLayout(int16_t actorId) {
    for (const auto& layout : kParamsLayouts) {
        if (layout.actorId == actorId) {
            return &layout;
        }
    }
    return nullptr;
}

int16_t VariantMask(int16_t actorId) {
    const ActorParamsLayout* layout = FindLayout(actorId);
    return layout == nullptr ? 0 : layout->variantMask;
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
