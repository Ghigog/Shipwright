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
 * reached by hand-placing.
 *
 * `nativeObjectId` sorts the list and is registered in the bank on demand, but it does NOT decide
 * whether a prop renders. SoH's DmaMgr_SendRequest1 is a no-op, so objects are never copied into
 * the bank at all and every model resolves by OTR resource name - a prop draws correctly in a
 * scene that has never heard of its object. Registering the object still matters for the actors
 * that gate themselves on Object_GetIndex.
 *
 * When a prop does not appear, the cause is almost always the actor killing itself in Init on a
 * condition the room cannot satisfy - a scene setup layer, an event flag, Link's age, the scene
 * number. Bg_Toki_Hikari wants the Door of Time opened; Bg_Spot09_Obj wants a specific
 * age/carpenter state; En_Yabusame_Mark wanted setup layer 4 and was dropped for it. The placer
 * marks these [dead] in its placed list rather than trying to prove survivability up front.
 *
 * A separate and nastier screen: actors that index the *scene's* waterbox array. A room's
 * collision header carries however many waterboxes that scene was authored with, and in SoH an
 * empty one leaves colHeader->waterBoxes null (CollisionHeaderFactory hands over vector::data()
 * on an empty vector). Every engine reader guards on numWaterBoxes == 0 first; these actors do
 * not, because in their home scene the box is always there. Outside it they touch null:
 *
 *   Bg_Spot01_Idomizu  writes waterBoxes[0].ySurface every update - the Kakariko well's water
 *                      level IS that field, so it cannot be made scene-independent.
 *   Bg_Mizu_Bwall      reads waterBoxes[2].ySurface to pick its alpha - hardcoded to the Water
 *                      Temple's third box.
 *
 * Both are dropped. Note this class fails on *update*, not on Init, so it survives the [dead]
 * marker and every other screen here - the placer's list showed a healthy prop right up until
 * it entered the update culling volume and took the process down. That is why the screen is a
 * table entry rather than something checked at spawn.
 *
 * A third way to be wrong, and the one that costs the most time: the entry renders exactly what
 * the actor draws, and the actor is not the thing its name suggests. Bg_Menkuri_Eye was listed
 * as "Eye statue". It is not a statue - it is the glowing eye *decal* that GTG's eye switches
 * wear, one small XLU quad (gGTGEyeStatueEyeDL) at 0.1 scale. The statue under it is scene
 * geometry that no actor carries. Worse, its dormant state draws at env alpha 0, so away from a
 * set switch flag it is a faint smudge rather than anything. It was dropped; when a prop looks
 * wrong, read what its Draw actually emits before assuming the model failed to resolve.
 *
 * Variants are separate entries rather than a raw params box, because "Tree - oval, green" is a
 * choice a person can make and 0x0205 isn't. Params stay editable in the placer for the cases
 * these defaults don't cover (mainly drop tables, the high byte on several of these actors).
 */
#include "soh/ActorDB.h"

#include <spdlog/fmt/fmt.h>
#include <cstring>

// After the headers above on purpose - see EnrichWorldPlacer.cpp for why z64.h has to come last.
#include "EnrichWorld.h"

extern "C" {
#include "functions.h"
#include "macros.h"
extern PlayState* gPlayState;

// Defined in z_scene.c but never prototyped in functions.h, which the rest of the object API
// does live in. Declared here rather than added there on purpose: functions.h is included by
// most of the tree, so a one-line addition to it costs a full rebuild (see CLAUDE.md) for a
// function only this file calls.
s32 Object_Spawn(ObjectContext* objectCtx, s16 objectId);
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
        // En_River_Sound types 0, 4 and 5 - the three the enum leaves unnamed - read
        // play->setupPathList[params >> 8] every frame with no null check
        // (z_en_river_sound.c:176), so they only work in a scene that has the path they name.
        // Type 0 was this palette's default and crashed on placement. The named types below all
        // emit from the actor's own position and never touch a path; the layout table locks the
        // whole word so neither the type nor the path index can be edited into one that does.
        { "Sound - small waterfall", ACTOR_EN_RIVER_SOUND, OBJECT_GAMEPLAY_KEEP, kNoObjectNeeded, 1, "" },
        { "Sound - large waterfall", ACTOR_EN_RIVER_SOUND, OBJECT_GAMEPLAY_KEEP, kNoObjectNeeded, 3, "" },
        { "Sound - lava bubbling", ACTOR_EN_RIVER_SOUND, OBJECT_GAMEPLAY_KEEP, kNoObjectNeeded, 2, "" },
        { "Sound - dripping water", ACTOR_EN_RIVER_SOUND, OBJECT_GAMEPLAY_KEEP, kNoObjectNeeded, 8, "" },
        { "Sound - fountain", ACTOR_EN_RIVER_SOUND, OBJECT_GAMEPLAY_KEEP, kNoObjectNeeded, 9, "" },
        { "Sound - market crowd", ACTOR_EN_RIVER_SOUND, OBJECT_GAMEPLAY_KEEP, kNoObjectNeeded, 0x0A, "" },
        { "Sound - torch crackling", ACTOR_EN_RIVER_SOUND, OBJECT_GAMEPLAY_KEEP, kNoObjectNeeded, 0x14, "" },
        { "Sound - cow mooing", ACTOR_EN_RIVER_SOUND, OBJECT_GAMEPLAY_KEEP, kNoObjectNeeded, 0x15, "" },
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
        // params 0xE deliberately, not 0. A cucco sets this->path = 1 the first time it settles
        // (z_en_niw.c:677) and then walks play->setupPathList[0] - fine in Kakariko, a null
        // dereference in a scene with no paths. 0xE is the one variant that branch skips, so the
        // path is never engaged and the cucco stays put wherever it is dropped.
        { "Cucco", ACTOR_EN_NIW, OBJECT_NIW, kNoObjectNeeded, 0x0E, "Pick it up, it flaps. Stays where placed." },
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

        // ---- Machinery, traps and structures: added 2026-08-09 ----
        //
        // Sourced the same way as the block above, but taking the puzzle mechanisms and dungeon
        // furniture the first pass skipped, because breadth is worth more here than restraint.
        // Every one was screened against the three failure modes this mod has actually hit:
        // re-checking its own object, indexing a table by params, and dereferencing
        // play->setupPathList. Anything matching was excluded rather than guarded, so none of
        // these needs a required object or a params layout - and with no layout entry
        // OptionMask returns 0, which means their params stay pinned at the value below.
        //
        // Several are scene-coupled by nature (a Fire Temple elevator wants a Fire Temple), so
        // most will sit under "not native to this room" outside their home scene. That is the
        // point of the grouping rather than a reason to leave them out.
        { "Platform, falling", ACTOR_BG_GANON_OTYUKA, OBJECT_GANON, kNoObjectNeeded, 0, "" },
        { "Fire wall, proximity", ACTOR_BG_HIDAN_FIREWALL, OBJECT_HIDAN_OBJECTS, kNoObjectNeeded, 0, "" },
        { "Spike trap, sliding", ACTOR_EN_TRAP, OBJECT_TRAP, kNoObjectNeeded, 0, "" },
        { "Rubble, ruined", ACTOR_DEMO_GJ, OBJECT_GJ, kNoObjectNeeded, 0, "" },
        { "Weather trigger", ACTOR_EN_WEATHER_TAG, OBJECT_GAMEPLAY_KEEP, kNoObjectNeeded, 0, "" },
        { "Gerudo Valley structures", ACTOR_BG_SPOT09_OBJ, OBJECT_SPOT09_OBJ, kNoObjectNeeded, 0, "" },
        { "Block, clear", ACTOR_BG_GND_DARKMEIRO, OBJECT_DEMO_KEKKAI, kNoObjectNeeded, 0, "" },
        { "Shadow Temple trap", ACTOR_BG_HAKA_TRAP, OBJECT_HAKA_OBJECTS, kNoObjectNeeded, 0, "" },
        { "Statue, hammer", ACTOR_BG_HIDAN_DALM, OBJECT_HIDAN_OBJECTS, kNoObjectNeeded, 0, "" },
        { "Ice platform", ACTOR_BG_SPOT08_ICEBLOCK, OBJECT_SPOT08_OBJ, kNoObjectNeeded, 0, "" },
        { "Window, stained glass", ACTOR_BG_TOKI_HIKARI, OBJECT_TOKI_OBJECTS, kNoObjectNeeded, 0, "" },
        { "Platform, stone (fire)", ACTOR_BG_HIDAN_SIMA, OBJECT_HIDAN_OBJECTS, kNoObjectNeeded, 0, "" },
        // Bg_Spot01_Idomizu (well water) dropped - see the waterbox note in the file header.
        { "Block stop", ACTOR_OBJ_BLOCKSTOP, OBJECT_GAMEPLAY_KEEP, kNoObjectNeeded, 0, "" },
        { "Water vortex", ACTOR_EN_STREAM, OBJECT_STREAM, kNoObjectNeeded, 0, "" },
        { "Drawbridge", ACTOR_BG_SPOT00_HANEBASI, OBJECT_SPOT00_OBJECTS, kNoObjectNeeded, 0, "" },
        { "Bombable wall, flat", ACTOR_BG_BOMBWALL, OBJECT_GAMEPLAY_FIELD_KEEP, kNoObjectNeeded, 0, "" },
        { "Windmill machinery", ACTOR_BG_RELAY_OBJECTS, OBJECT_RELAY_OBJECTS, kNoObjectNeeded, 0, "" },
        { "Waterfall, Zora's", ACTOR_BG_SPOT07_TAKI, OBJECT_SPOT07_OBJECT, kNoObjectNeeded, 0, "" },
        { "Bombable wall, desert", ACTOR_BG_SPOT11_BAKUDANKABE, OBJECT_SPOT11_OBJ, kNoObjectNeeded, 0, "" },
        { "Spike platform, huge", ACTOR_BG_HIDAN_HROCK, OBJECT_HIDAN_OBJECTS, kNoObjectNeeded, 0, "" },
        // Bg_Mizu_Bwall dropped - same waterbox reason, see the file header. The other four
        // bombable walls below are safe; only the Water Temple one reads the waterbox array.
        { "Wall, climbable sliding", ACTOR_BG_JYA_ZURERUKABE, OBJECT_JYA_OBJ, kNoObjectNeeded, 0, "" },
        // Bg_Menkuri_Eye dropped - it was mislabelled "Eye statue" and is not one. See below.
        { "Drawbridge, broken", ACTOR_BG_SPOT00_BREAK, OBJECT_SPOT00_BREAK, kNoObjectNeeded, 0, "" },
        { "Bombable wall, fountain", ACTOR_BG_SPOT08_BAKUDANKABE, OBJECT_SPOT08_OBJ, kNoObjectNeeded, 0, "" },
        { "Block, stone (fire)", ACTOR_BG_HIDAN_ROCK, OBJECT_HIDAN_OBJECTS, kNoObjectNeeded, 0, "" },
        { "Flamethrower, spinning", ACTOR_BG_HIDAN_RSEKIZOU, OBJECT_HIDAN_OBJECTS, kNoObjectNeeded, 0, "" },
        { "Block, silver", ACTOR_BG_JYA_BLOCK, OBJECT_GAMEPLAY_DANGEON_KEEP, kNoObjectNeeded, 0, "" },
        { "Boulder, rolling", ACTOR_BG_JYA_GOROIWA, OBJECT_GOROIWA, kNoObjectNeeded, 0, "" },
        { "Ceiling hole, webbed", ACTOR_BG_GND_SOULMEIRO, OBJECT_DEMO_KEKKAI, kNoObjectNeeded, 0, "" },
        { "Bombable wall, crater", ACTOR_BG_SPOT17_BAKUDANKABE, OBJECT_SPOT17_OBJ, kNoObjectNeeded, 0, "" },
        { "Block puzzle spawner", ACTOR_OBJ_MAKEOSHIHIKI, OBJECT_GAMEPLAY_DANGEON_KEEP, kNoObjectNeeded, 0, "" },
        { "Flamethrower statue", ACTOR_BG_HIDAN_SEKIZOU, OBJECT_HIDAN_OBJECTS, kNoObjectNeeded, 0, "" },
        { "Whirlpool", ACTOR_BG_MIZU_UZU, OBJECT_MIZU_OBJECTS, kNoObjectNeeded, 0, "" },
        { "Elevator, stone", ACTOR_BG_JYA_1FLIFT, OBJECT_JYA_OBJ, kNoObjectNeeded, 0, "" },
        { "Ice block, pushable", ACTOR_BG_ICE_OBJECTS, OBJECT_ICE_OBJECTS, kNoObjectNeeded, 0, "" },
        { "Wall, false stone", ACTOR_BG_MENKURI_NISEKABE, OBJECT_MENKURI_OBJECTS, kNoObjectNeeded, 0, "" },
        { "Wall, false (castle)", ACTOR_BG_GND_NISEKABE, OBJECT_DEMO_KEKKAI, kNoObjectNeeded, 0, "" },
        { "Ice block, square", ACTOR_BG_GND_ICEBLOCK, OBJECT_DEMO_KEKKAI, kNoObjectNeeded, 0, "" },
        { "Well stone", ACTOR_BG_SPOT01_IDOSOKO, OBJECT_SPOT01_MATOYA, kNoObjectNeeded, 0, "" },
        { "Epona, young", ACTOR_EN_HORSE_LINK_CHILD, OBJECT_HORSE_LINK_CHILD, kNoObjectNeeded, 0, "" },
        { "Platform, collapsing", ACTOR_OBJ_LIFT, OBJECT_D_LIFT, kNoObjectNeeded, 0, "" },
        { "Elevator, fire", ACTOR_BG_HIDAN_SYOKU, OBJECT_HIDAN_OBJECTS, kNoObjectNeeded, 0, "" },
        { "Elevator, hookshot", ACTOR_BG_HIDAN_FSLIFT, OBJECT_HIDAN_OBJECTS, kNoObjectNeeded, 0, "" },
        { "Bombable rock wall", ACTOR_BG_JYA_BOMBIWA, OBJECT_JYA_OBJ, kNoObjectNeeded, 0, "" },
        { "Grate, sliding", ACTOR_BG_JYA_AMISHUTTER, OBJECT_JYA_OBJ, kNoObjectNeeded, 0, "" },
        { "Fire trap, statue eye", ACTOR_EN_HONOTRAP, OBJECT_GAMEPLAY_DANGEON_KEEP, kNoObjectNeeded, 0, "" },
        { "Ring platform, rotating", ACTOR_BG_MENKURI_KAITEN, OBJECT_MENKURI_OBJECTS, kNoObjectNeeded, 0, "" },
        { "Lava platform, sinking", ACTOR_BG_GND_FIREMEIRO, OBJECT_DEMO_KEKKAI, kNoObjectNeeded, 0, "" },
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
    // Locked shut: the low byte is the sound type (three values of which dereference a scene
    // path) and the high byte IS that path index. Neither is safe to hand to a free-form box.
    { ACTOR_EN_RIVER_SOUND, 0xFFFF, 0, 0xFFFF },
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
    // Both of these are locked shut rather than given options, because their editable bits are
    // what turn on path following - and a path index is only meaningful in the scene that
    // defines it. En_Niw's variant is the guard (see its palette note); En_Horse_Normal follows
    // a path when (params & 0xF0) == 0x10, reading setupPathList[params & 0xF]
    // (z_en_horse_normal.c:290). Neither is reachable with the word held to the palette default.
    { ACTOR_EN_NIW, 0x00FF, 0, 0x00FF },
    { ACTOR_EN_HORSE_NORMAL, 0x00FF, 0, 0x00FF },
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

/**
 * Which subheading a prop sits under. Grouped by what the thing *is* to someone dressing a
 * scene, not by which object it ships in - the object grouping is already carried by the
 * native / not-native split, and repeating it here would say nothing new.
 *
 * Order here is the order they appear in the dropdown, so it runs from what you reach for most
 * (ground cover) to what you reach for least (machinery).
 */
static const char* const kGroupOrder[] = {
    "Plants & ground", "Rocks & breakables", "Creatures", "Fire & light",
    "Water & ice",     "Structures",         "Machinery",  "Sound & effects",
    "Other",
};

static const struct {
    int16_t actorId;
    const char* group;
} kGroups[] = {
    { ACTOR_EN_WOOD02, "Plants & ground" },        { ACTOR_OBJ_HANA, "Plants & ground" },
    { ACTOR_EN_KUSA, "Plants & ground" },          { ACTOR_OBJ_MURE2, "Plants & ground" },
    { ACTOR_OBJ_COMB, "Plants & ground" },         { ACTOR_DOOR_ANA, "Plants & ground" },

    { ACTOR_EN_ISHI, "Rocks & breakables" },       { ACTOR_OBJ_HAMISHI, "Rocks & breakables" },
    { ACTOR_OBJ_BOMBIWA, "Rocks & breakables" },   { ACTOR_OBJ_TSUBO, "Rocks & breakables" },
    { ACTOR_OBJ_KIBAKO, "Rocks & breakables" },    { ACTOR_OBJ_KIBAKO2, "Rocks & breakables" },
    { ACTOR_EN_BOMBF, "Rocks & breakables" },      { ACTOR_BG_HAKA_TUBO, "Rocks & breakables" },
    { ACTOR_BG_SPOT18_BASKET, "Rocks & breakables" }, { ACTOR_BG_SPOT15_RRBOX, "Rocks & breakables" },
    { ACTOR_EN_TUBO_TRAP, "Rocks & breakables" },  { ACTOR_BG_BOMBWALL, "Rocks & breakables" },
    { ACTOR_BG_JYA_BOMBIWA, "Rocks & breakables" },
    { ACTOR_BG_SPOT08_BAKUDANKABE, "Rocks & breakables" },
    { ACTOR_BG_SPOT11_BAKUDANKABE, "Rocks & breakables" },
    { ACTOR_BG_SPOT17_BAKUDANKABE, "Rocks & breakables" }, { ACTOR_DEMO_GJ, "Rocks & breakables" },

    { ACTOR_OBJ_MURE, "Creatures" },               { ACTOR_EN_FISH, "Creatures" },
    { ACTOR_EN_INSECT, "Creatures" },              { ACTOR_EN_BUTTE, "Creatures" },
    { ACTOR_EN_NIW, "Creatures" },                 { ACTOR_EN_HORSE_NORMAL, "Creatures" },
    { ACTOR_EN_HORSE_LINK_CHILD, "Creatures" },

    { ACTOR_EN_LIGHT, "Fire & light" },            { ACTOR_OBJ_SYOKUDAI, "Fire & light" },
    { ACTOR_BG_PO_SYOKUDAI, "Fire & light" },      { ACTOR_EN_ICE_HONO, "Fire & light" },
    { ACTOR_EFC_ERUPC, "Fire & light" },           { ACTOR_BG_TOKI_HIKARI, "Fire & light" },

    { ACTOR_BG_ICE_TURARA, "Water & ice" },        { ACTOR_BG_ICE_SHELTER, "Water & ice" },
    { ACTOR_EN_SIOFUKI, "Water & ice" },           { ACTOR_EN_STREAM, "Water & ice" },
    { ACTOR_BG_MIZU_UZU, "Water & ice" },          { ACTOR_BG_SPOT07_TAKI, "Water & ice" },
    { ACTOR_BG_ICE_OBJECTS, "Water & ice" },
    { ACTOR_BG_GND_ICEBLOCK, "Water & ice" },      { ACTOR_BG_SPOT08_ICEBLOCK, "Water & ice" },

    { ACTOR_EN_KANBAN, "Structures" },             { ACTOR_EN_GS, "Structures" },
    { ACTOR_BG_HAKA, "Structures" },               { ACTOR_EN_HATA, "Structures" },
    { ACTOR_BG_UMAJUMP, "Structures" },            { ACTOR_BG_INGATE, "Structures" },
    { ACTOR_EN_TANA, "Structures" },               { ACTOR_OBJ_HSBLOCK, "Structures" },
    { ACTOR_BG_SPOT09_OBJ, "Structures" },         { ACTOR_BG_SPOT00_HANEBASI, "Structures" },
    { ACTOR_BG_SPOT00_BREAK, "Structures" },       { ACTOR_BG_SPOT01_IDOSOKO, "Structures" },
    { ACTOR_BG_HIDAN_DALM, "Structures" },
    { ACTOR_BG_MENKURI_NISEKABE, "Structures" },   { ACTOR_BG_GND_NISEKABE, "Structures" },
    { ACTOR_BG_JYA_ZURERUKABE, "Structures" },     { ACTOR_BG_GND_SOULMEIRO, "Structures" },
    { ACTOR_BG_HIDAN_ROCK, "Structures" },         { ACTOR_BG_JYA_BLOCK, "Structures" },
    { ACTOR_BG_GND_DARKMEIRO, "Structures" },      { ACTOR_OBJ_OSHIHIKI, "Structures" },
    { ACTOR_OBJ_MAKEOSHIHIKI, "Structures" },      { ACTOR_OBJ_BLOCKSTOP, "Structures" },

    { ACTOR_BG_HIDAN_SIMA, "Machinery" },          { ACTOR_BG_HIDAN_HROCK, "Machinery" },
    { ACTOR_BG_HIDAN_SYOKU, "Machinery" },         { ACTOR_BG_HIDAN_FSLIFT, "Machinery" },
    { ACTOR_BG_JYA_1FLIFT, "Machinery" },          { ACTOR_BG_MENKURI_KAITEN, "Machinery" },
    { ACTOR_BG_JYA_AMISHUTTER, "Machinery" },      { ACTOR_OBJ_LIFT, "Machinery" },
    { ACTOR_BG_GANON_OTYUKA, "Machinery" },        { ACTOR_BG_GND_FIREMEIRO, "Machinery" },
    { ACTOR_BG_HIDAN_FIREWALL, "Machinery" },      { ACTOR_BG_HIDAN_SEKIZOU, "Machinery" },
    { ACTOR_BG_HIDAN_RSEKIZOU, "Machinery" },      { ACTOR_EN_HONOTRAP, "Machinery" },
    { ACTOR_EN_TRAP, "Machinery" },                { ACTOR_BG_HAKA_TRAP, "Machinery" },
    { ACTOR_BG_JYA_GOROIWA, "Machinery" },         { ACTOR_BG_RELAY_OBJECTS, "Machinery" },
    { ACTOR_BG_SPOT01_FUSYA, "Machinery" },

    { ACTOR_EN_RIVER_SOUND, "Sound & effects" },   { ACTOR_EN_WEATHER_TAG, "Sound & effects" },
    { ACTOR_BG_SPOT16_DOUGHNUT, "Sound & effects" }, { ACTOR_OBJ_MURE3, "Sound & effects" },
};

const char* PropGroup(int16_t actorId) {
    for (const auto& row : kGroups) {
        if (row.actorId == actorId) {
            return row.group;
        }
    }
    return "Other";
}

// strcmp rather than pointer equality: kGroups and kGroupOrder spell the same names as separate
// string literals, and whether the compiler pools those into one address is not guaranteed.
int PropGroupRank(const char* group) {
    const int count = static_cast<int>(sizeof(kGroupOrder) / sizeof(kGroupOrder[0]));
    for (int i = 0; i < count; i++) {
        if (std::strcmp(kGroupOrder[i], group) == 0) {
            return i;
        }
    }
    return count;
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

bool IsInPalette(int16_t actorId) {
    for (const auto& def : AllProps()) {
        if (def.actorId == actorId) {
            return true;
        }
    }
    return false;
}

int16_t NativeObjectForActor(int16_t actorId) {
    for (const auto& def : AllProps()) {
        if (def.actorId == actorId) {
            return def.nativeObjectId;
        }
    }
    return kNoObjectNeeded;
}

bool EnsureObjectLoaded(int16_t objectId) {
    if (gPlayState == nullptr || objectId <= 0 || objectId >= OBJECT_ID_MAX) {
        return false;
    }
    if (Object_GetIndex(&gPlayState->objectCtx, objectId) >= 0) {
        return true; // already resident, which is the common case
    }

    // Object_Spawn asserts rather than fails when the bank is full, and an assert that is
    // compiled out in a release build would corrupt the object arena instead. Refuse early: a
    // prop that doesn't appear is a far better outcome than heap damage. OBJECT_EXCHANGE_BANK_MAX
    // is 128 and a busy vanilla room uses well under twenty, so this is headroom rather than a
    // real constraint - Lon Lon Ranch loads ten.
    if (gPlayState->objectCtx.num >= OBJECT_EXCHANGE_BANK_MAX - 1) {
        return false;
    }

    Object_Spawn(&gPlayState->objectCtx, objectId);
    return Object_GetIndex(&gPlayState->objectCtx, objectId) >= 0;
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
