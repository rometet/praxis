#pragma once
#include <array>
#include <cstdint>
#include <utility>

namespace praxis::companion::map {
// Stack-owned cache of copied values. Exact signed XYZ, bounded collision
// probes, no dynamic storage, no world-global lifetime or borrowed pointers.
template<class Value,std::size_t Size=256> class TickValueCache {
    static_assert(Size>0);
    struct Entry {int x{},y{},z{};bool used{};Value value;};
    std::array<Entry,Size> entries_{};
    std::uint32_t queries_{},hits_{};
public:
    template<class Query> Value const& get(int x,int y,int z,Query&& query) {
        auto hash=(std::uint32_t(x)*73856093u)^(std::uint32_t(y)*19349663u)^(std::uint32_t(z)*83492791u);
        std::size_t slot=hash%Size;
        for(unsigned probe=0;probe<8&&probe<Size;++probe) {
            auto& entry=entries_[(slot+probe)%Size];
            if(entry.used&&entry.x==x&&entry.y==y&&entry.z==z){++hits_;return entry.value;}
            if(!entry.used){slot=(slot+probe)%Size;break;}
        }
        ++queries_;
        auto value=std::forward<Query>(query)(x,y,z);
        entries_[slot]={x,y,z,true,std::move(value)};
        return entries_[slot].value;
    }
    std::uint32_t queries() const noexcept {return queries_;}
    std::uint32_t hits() const noexcept {return hits_;}
};
}
