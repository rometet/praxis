#pragma once
#include <cstdint>
#include <map>
#include <set>
#include <vector>
namespace praxis::companion::inventory {
enum class RequestResult { Idle, Waiting, Accepted, Rejected, Untracked, TimedOut };
// One operation owns the barrier until it is explicitly released. Responses
// are observations of vanilla requests; this policy never creates a request.
class RequestBarrier {
public:
    bool begin(std::uint64_t generation, std::uint64_t now) {
        if (active_ || !generation || exhausted_ || seen_.size()>=65536) return false;
        active_=true; generation_=generation; started_=now; captured_=failed_=ambiguous_=false; ids_.clear(); early_.clear(); return true;
    }
    void capture(std::vector<std::int64_t> const& ids) {
        if (!active_ || captured_) return;
        for (auto id:ids) {
            if(ids_.contains(id))continue;
            if(ids_.size()>=128||seen_.size()>=65536){ambiguous_=exhausted_=true;continue;}
            // Packets do not carry our world/job epoch. Never let a reused
            // native ID make an old duplicate look like a new job's ACK.
            if(!seen_.insert(id).second)ambiguous_=true;
            auto const value=early_.contains(id)?early_[id]:0;
            ids_[id]=value;if(value<0)failed_=true;
        }
        captured_=true; early_.clear();
    }
    void response(std::uint64_t generation, std::int64_t id, bool success) {
        if (!active_ || generation!=generation_) return;
        if (!captured_) {
            auto found=early_.find(id);
            if(found!=early_.end()) {if(!success)found->second=-1;}
            else if(early_.size()<128)early_[id]=success?1:-1;
            else ambiguous_=true;
            return;
        }
        auto found=ids_.find(id); if(found==ids_.end())return;
        // A rejection is terminal, including contradictory duplicate responses.
        if (!success) failed_=true;
        if(!success)found->second=-1;
        else if(!found->second)found->second=1;
    }
    RequestResult result(std::uint64_t generation,std::uint64_t now) const {
        if(!active_)return RequestResult::Idle;
        if(generation!=generation_)return RequestResult::Rejected;
        if(failed_)return RequestResult::Rejected;
        if(ambiguous_)return RequestResult::Untracked;
        if(!captured_)return now<started_||now-started_>=2000?RequestResult::TimedOut:RequestResult::Waiting;
        if(ids_.empty())return RequestResult::Untracked;
        bool waiting=false;
        for(auto [id,value]:ids_) { if(value<0)return RequestResult::Rejected; if(!value)waiting=true; }
        if(!waiting)return RequestResult::Accepted;
        return now<started_||now-started_>=2000?RequestResult::TimedOut:RequestResult::Waiting;
    }
    // ID tombstones survive world resets. Native packets have no world epoch;
    // forgetting these could turn a delayed old ACK into success for a new ID.
    void reset() { active_=captured_=failed_=ambiguous_=false;ids_.clear();early_.clear(); }
    // A cancelled owner must not release the next native operation while any
    // acknowledged request is still unresolved. Timeout remains fail-closed.
    bool settled(std::uint64_t generation) const noexcept {
        if(!active_||generation!=generation_)return true;
        if(!captured_||ids_.empty()||ambiguous_)return false;
        for(auto [id,value]:ids_)if(!value)return false;
        return true;
    }
    bool busy() const noexcept { return active_; }
private:
    bool active_{},captured_{},failed_{},ambiguous_{},exhausted_{};
    std::uint64_t generation_{},started_{};
    std::map<std::int64_t,int> ids_,early_;
    std::set<std::int64_t> seen_;
};
}
