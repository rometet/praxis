#pragma once
// R29: one ordinary local pack inserted or removed by Minecraft's native
// Global Resources editor. Existing entries retain their exact relative order.
// This immutable analysis performs NO live ResourcePackManager or filesystem
// writes. Native owner must separately prove candidate identity and lifetime.
#include <algorithm>
#include <cstddef>
#include <span>
#include <string>
namespace praxis::companion::packs {
enum class NativeVanillaPackDelta {Unchanged=0,EnableOne=1,DisableOne=2,Unsupported=3};
struct NativeVanillaPackChange {
    NativeVanillaPackDelta delta{NativeVanillaPackDelta::Unsupported};
    std::size_t index{};
};
inline NativeVanillaPackChange nativeVanillaPackChange(
    std::span<std::string const> original,
    std::span<std::string const> selection) noexcept {
    if(original.size()==selection.size()
        && std::equal(original.begin(),original.end(),selection.begin()))
        return {NativeVanillaPackDelta::Unchanged,0};
    if(selection.size()==original.size()+1){
        auto n=original.size();
        for(std::size_t idx=0;idx<=n;++idx){
            if(std::equal(original.begin(),original.begin()+idx,selection.begin())
                &&std::equal(original.begin()+idx,original.end(),
                    selection.begin()+idx+1)
                &&std::find(original.begin(),original.end(),selection[idx])==original.end())
                return {NativeVanillaPackDelta::EnableOne,idx};
        }
    }
    if(original.size()==selection.size()+1){
        auto n=selection.size();
        for(std::size_t idx=0;idx<=n;++idx){
            if(std::equal(selection.begin(),selection.begin()+idx,original.begin())
                &&std::equal(selection.begin()+idx,selection.end(),
                    original.begin()+idx+1)
                &&std::find(selection.begin(),selection.end(),original[idx])==selection.end())
                return {NativeVanillaPackDelta::DisableOne,idx};
        }
    }
    return {NativeVanillaPackDelta::Unsupported,0};
}
inline NativeVanillaPackDelta nativeVanillaPackDelta(
    std::span<std::string const> original,
    std::span<std::string const> selection) noexcept {
    return nativeVanillaPackChange(original,selection).delta;
}
inline bool vanillaScopedCloneGrant(
    bool exactClone,bool multiplayer,bool levelPreserved,std::size_t levelCount,
    bool useGlobalStack,bool initialized,bool pending) noexcept {
    return exactClone&&!multiplayer&&levelPreserved&&levelCount==2
        &&useGlobalStack&&!initialized&&!pending;
}
}
