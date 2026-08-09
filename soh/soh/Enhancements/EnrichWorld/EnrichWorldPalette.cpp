/**
 * Enrich World - the curated prop palette.
 *
 * Every entry names the object its model lives in. The placer filters on that at runtime via
 * Object_GetIndex against the *current* room, rather than a table generated from the asset
 * archive. That matters: a room's object list varies by scene header (child/adult, day/night),
 * so a baked table would be wrong for whichever header it wasn't generated from. Asking the
 * live object context is always right, and it self-corrects if upstream changes a scene.
 *
 * This is what makes the native-props rule structural instead of advisory - a prop whose object
 * isn't loaded simply doesn't appear in the list, so the silent Actor_Kill failure documented in
 * EnrichWorldProps.cpp can't be reached by hand-placing.
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

const std::vector<PropDef>& AllProps() {
    // OBJECT_GAMEPLAY_KEEP is always resident, so entries using it are available in every room.
    static const std::vector<PropDef> props = {
        // ---- Always available (gameplay_keep) ----
        { "Butterflies", ACTOR_OBJ_MURE, OBJECT_GAMEPLAY_KEEP, 4, "Pure ambience, no collision." },
        { "Bugs", ACTOR_OBJ_MURE, OBJECT_GAMEPLAY_KEEP, 3, "Pure ambience, no collision." },
        { "Fish", ACTOR_OBJ_MURE, OBJECT_GAMEPLAY_KEEP, 2, "Pure ambience. Wants water." },
        { "Flame (decorative)", ACTOR_EN_LIGHT, OBJECT_GAMEPLAY_KEEP, 0, "params & 0xF sets type/scale." },
        { "River sound", ACTOR_EN_RIVER_SOUND, OBJECT_GAMEPLAY_KEEP, 0, "Invisible ambient emitter." },
        { "Cluster - bushes (9)", ACTOR_OBJ_MURE2, OBJECT_GAMEPLAY_KEEP, 0,
          "Spawns 9 En_Kusa on an 80-unit circle. CHILDREN NEED THEIR OWN OBJECT - only use where "
          "a bush entry below is also available." },
        { "Cluster - bushes (12)", ACTOR_OBJ_MURE2, OBJECT_GAMEPLAY_KEEP, 1, "As above, 12 children." },
        { "Cluster - rocks (8)", ACTOR_OBJ_MURE2, OBJECT_GAMEPLAY_KEEP, 2,
          "Spawns 8 En_Ishi. Children need the field keep." },

        // ---- Overworld (gameplay_field_keep) ----
        { "Flowers", ACTOR_OBJ_HANA, OBJECT_GAMEPLAY_FIELD_KEEP, 0,
          "Best pure decoration - no collision, no flags, no drops." },
        { "Rock fragments", ACTOR_OBJ_HANA, OBJECT_GAMEPLAY_FIELD_KEEP, 1, "Small debris, cylinder r10 h18." },
        { "Bush (field)", ACTOR_OBJ_HANA, OBJECT_GAMEPLAY_FIELD_KEEP, 2, "Decorative only - cannot be cut." },
        { "Grass, cuttable", ACTOR_EN_KUSA, OBJECT_GAMEPLAY_FIELD_KEEP, 0, "Type 0 only. Types 1-2 need OBJECT_KUSA." },
        { "Rock, liftable (small)", ACTOR_EN_ISHI, OBJECT_GAMEPLAY_FIELD_KEEP, 0, "High nibble of byte 1 = drops." },
        { "Rock, liftable (silver)", ACTOR_EN_ISHI, OBJECT_GAMEPLAY_FIELD_KEEP, 1, "Large. Sets a switch flag." },
        { "Boulder, bronze", ACTOR_OBJ_HAMISHI, OBJECT_GAMEPLAY_FIELD_KEEP, 0, "Hammer-breakable." },

        // ---- Dungeon (gameplay_dangeon_keep) ----
        { "Pot (dungeon)", ACTOR_OBJ_TSUBO, OBJECT_GAMEPLAY_DANGEON_KEEP, 0, "Bit 8 clear = dungeon pot." },
        { "Pushable block", ACTOR_OBJ_OSHIHIKI, OBJECT_GAMEPLAY_DANGEON_KEEP, 0, "Structural, not decor." },

        // ---- Scene-specific ----
        { "Tree - conical, large", ACTOR_EN_WOOD02, OBJECT_WOOD02, 0, "" },
        { "Tree - conical, medium", ACTOR_EN_WOOD02, OBJECT_WOOD02, 1, "" },
        { "Tree - conical, small", ACTOR_EN_WOOD02, OBJECT_WOOD02, 2, "" },
        { "Tree - oval, green", ACTOR_EN_WOOD02, OBJECT_WOOD02, 0x0205,
          "The Hyrule Field default - high byte 0x02 is the vanilla drop table." },
        { "Bush - green, small", ACTOR_EN_WOOD02, OBJECT_WOOD02, 0x0B, "" },
        { "Bush - green, large", ACTOR_EN_WOOD02, OBJECT_WOOD02, 0x0C, "" },
        { "Bush (OBJECT_KUSA)", ACTOR_EN_KUSA, OBJECT_KUSA, 1, "Deku Tree, Dodongo's, Well, Kokiri." },
        { "Torch", ACTOR_OBJ_SYOKUDAI, OBJECT_SYOKUDAI, 0, "params >> 0xC: 0 gold, 1 timed, 2 wooden." },
        { "Signpost", ACTOR_EN_KANBAN, OBJECT_KANBAN, 0, "" },
        { "Gossip stone", ACTOR_EN_GS, OBJECT_GS, 0, "" },
        { "Pot (overworld)", ACTOR_OBJ_TSUBO, OBJECT_TSUBO, 0x0100, "Bit 8 set = overworld pot." },
        { "Crate, large", ACTOR_OBJ_KIBAKO2, OBJECT_KIBAKO2, 0, "" },
        { "Boulder, bombable", ACTOR_OBJ_BOMBIWA, OBJECT_BOMBIWA, 0, "" },
        { "Bomb flower", ACTOR_EN_BOMBF, OBJECT_BOMBF, 0, "" },
        { "Icicle", ACTOR_BG_ICE_TURARA, OBJECT_ICE_OBJECTS, 0, "" },
        { "Gravestone", ACTOR_BG_HAKA, OBJECT_HAKA, 0, "Graveyard and Lake Hylia only." },
    };
    return props;
}

bool IsPropAvailable(const PropDef& def) {
    if (gPlayState == nullptr) {
        return false;
    }
    return Object_GetIndex(&gPlayState->objectCtx, def.objectId) >= 0;
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
