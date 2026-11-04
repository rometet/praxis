#pragma once
#include <array>
#include <cstdint>
#include <string_view>
namespace praxis::companion {
inline constexpr bool fastPlacementFullCube(std::string_view id) noexcept {
    // Explicit allowlist: don't accelerate oriented/interactive or thin blocks.
    // Expand only after world tests. Include the most-used building blocks.
    constexpr std::array<std::string_view,19> types{{
        "minecraft:stone","minecraft:cobblestone","minecraft:dirt",
        "minecraft:grass_block","minecraft:deepslate","minecraft:cobbled_deepslate",
        "minecraft:stone_bricks","minecraft:bricks","minecraft:glass",
        "minecraft:obsidian","minecraft:netherrack","minecraft:end_stone",
        "minecraft:oak_planks","minecraft:spruce_planks","minecraft:birch_planks",
        "minecraft:acacia_planks","minecraft:cherry_planks",
        "minecraft:smooth_stone","minecraft:white_concrete"
    }};
    for(auto candidate:types)if(candidate==id)return true;
    // Other unrotated ordinary full building cubes, with no block-state
    // orientation. Exclude suffixes such as stairs, walls, slabs, doors,
    // trapdoors, logs, observers and redstone machinery.
    constexpr std::array<std::string_view,7> suffixes{{
        "_planks","_wool","_concrete","_terracotta",
        "_stained_glass","_bricks","_glass"
    }};
    if(!id.starts_with("minecraft:") || id.ends_with("_glazed_terracotta"))return false;
    for(auto suffix:suffixes)
        if(id.size()>suffix.size() && id.ends_with(suffix))return true;
    constexpr std::array<std::string_view,16> extra{{
        "minecraft:andesite","minecraft:polished_andesite",
        "minecraft:granite","minecraft:polished_granite",
        "minecraft:diorite","minecraft:polished_diorite",
        "minecraft:calcite","minecraft:tuff","minecraft:polished_tuff",
        "minecraft:chiseled_tuff","minecraft:blackstone","minecraft:polished_blackstone",
        "minecraft:sandstone","minecraft:red_sandstone",
        "minecraft:mud_bricks","minecraft:quartz_block"
    }};
    for(auto name:extra)if(name==id)return true;
    // Ordinary mineral storage cubes and ores. Generic _block is NOT a
    // whitelist: piston, command blocks, honey, bone, hay etc. stay excluded.
    constexpr std::array<std::string_view,17> storage{{
        "iron_block","gold_block","diamond_block","emerald_block",
        "lapis_block","redstone_block","coal_block","netherite_block",
        "copper_block","raw_iron_block","raw_gold_block",
        "raw_copper_block","amethyst_block","quartz_block",
        "moss_block","clay","packed_mud"
    }};
    auto const shortId=id.substr(sizeof("minecraft:")-1);
    for(auto candidate:storage)if(candidate==shortId)return true;
    constexpr std::array<std::string_view,17> ores{{
        "coal_ore","iron_ore","copper_ore","gold_ore",
        "redstone_ore","lapis_ore","diamond_ore","emerald_ore",
        "deepslate_coal_ore","deepslate_iron_ore",
        "deepslate_copper_ore","deepslate_gold_ore",
        "deepslate_redstone_ore","deepslate_lapis_ore",
        "deepslate_diamond_ore","deepslate_emerald_ore",
        "nether_gold_ore"
    }};
    for(auto candidate:ores)if(candidate==shortId)return true;
    return false;
}
// Prefer the placed-block ID if available; ItemStack::getTypeName can be
// reported differently from Block::getTypeName in some Bedrock builds.
inline constexpr bool fastPlacementEligibleHeld(
    std::string_view itemId,std::string_view blockId,
    bool hasBlockRef,bool isBlockPlanter,bool nullOrEmpty) noexcept {
    if(nullOrEmpty || (!hasBlockRef && !isBlockPlanter))return false;
    return fastPlacementFullCube(itemId) || fastPlacementFullCube(blockId);
}
// Multiplayer authoritative block confirmation is not currently observed by
// Praxis. Its 50ms client-side prediction path caused ghost blocks on BDS;
// do not accelerate when any multiplayer signal is present.
inline constexpr bool fastPlacementSafeNetworkScope(
    bool isMultiplayerClient,bool isInMultiplayerGame) noexcept {
    return !isMultiplayerClient && !isInMultiplayerGame;
}
// FastRightClick cannot repeat block placement in multiplayer (the
// independent safety gate in FastClick.cpp explicitly disallows it).
// Hence only OFFLINE fast-right advanced placement competes with
// FastBlockPlacement's native continuation. Do not silently veto the BDS
// experiment merely because the unrelated checkbox remains enabled.
inline constexpr bool fastPlacementCompetingBlockRepeat(
    bool multiplayer,bool fastRightEnabled,bool fastRightBlockPlacement) noexcept {
    return !multiplayer && fastRightEnabled && fastRightBlockPlacement;
}
// Remote BDS trial: at most one accelerated *native continuation* every
// three real client simulation ticks (150 ms at 20 TPS), with no repeated
// support+face target, even when render ticks accumulate many HitResults.
// This is NOT proof that a server accepts one placement per three ticks.
struct FastPlacementRemoteFence {
    std::uint64_t generation{},lastTick{};
    int lastX{},lastY{},lastZ{},lastFace{-1};
    bool reserve(std::uint64_t gen,std::uint64_t tick,int x,int y,int z,int face) noexcept {
        if(!gen||!tick||face<0||face>=6)return false;
        if(generation!=gen) {reset();generation=gen;}
        if(lastTick && (tick<=lastTick || tick-lastTick<3))return false;
        if(lastTick && x==lastX && y==lastY && z==lastZ && face==lastFace)return false;
        lastTick=tick;lastX=x;lastY=y;lastZ=z;lastFace=face;
        return true;
    }
    void reset() noexcept {
        generation=lastTick=0;
        lastX=lastY=lastZ=0;lastFace=-1;
    }
};
struct FastPlacementAdmission {
    bool moduleEnabled{}, focused{}, nativeInput{}, held{}, creative{};
    bool fullCube{}, supportSolid{}, interactive{}, modifierHeld{}, competing{};
};
inline constexpr bool fastPlacementAdmitted(FastPlacementAdmission const& a) noexcept {
    return a.moduleEnabled && a.focused && a.nativeInput && a.held && a.creative
        && a.fullCube && a.supportSolid && !a.interactive
        && !a.modifierHeld && !a.competing;
}
// The historical confirmed Max=2 policy is a safety bound, not a claim that
// the native client/server accepts two real transactions in one tick.
struct FastPlacementBudget {
    std::uint64_t generation{},tick{};
    int used{};
    bool consume(std::uint64_t world,std::uint64_t nativeTick) noexcept {
        if(!nativeTick)return false;
        if(generation!=world||tick!=nativeTick){generation=world;tick=nativeTick;used=0;}
        if(used>=2)return false;
        ++used;return true;
    }
    void reset() noexcept {generation=tick=0;used=0;}
};
} // namespace praxis::companion
