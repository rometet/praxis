#include "core/TabTpsServerSampler.h"
#include <iostream>
#include <stdexcept>
using namespace praxis::companion::tabtps;

int main() {
    unsigned checks = 0;
    auto require = [&](bool condition, char const* message) {
        ++checks;
        if (!condition) throw std::runtime_error(message);
    };
    try {
        ServerSampler sampler;
        require(!sampler.poll(1000), "inactive sampler emits nothing");
        require(!sampler.completedTick(1000, 20000), "inactive sampler rejects tick");
        sampler.start(11, 1000);
        require(!sampler.poll(1999), "no early packet");
        for (int i=1; i<=20; ++i)
            require(sampler.completedTick(1000 + i * 50, 43000), "full-frame tick accepted");
        auto first = sampler.poll(2000);
        require(first.has_value(), "first measurement");
        require(first->epoch==11 && first->sequence==1 && first->ticks==20
            && first->windowMs==1000 && first->avgFrameUs==43000, "exact measurement");
        require(tps(*first)==20.0 && mspt(*first)==43.0, "independent metrics");
        require(!sampler.poll(2999), "no spurious packet after reset");
        for (int i=1; i<=10; ++i)
            require(sampler.completedTick(2000 + i*100), "ticks without MSPT");
        auto second = sampler.poll(3000);
        require(second && second->sequence==2 && second->ticks==10
            && second->avgFrameUs==0 && tps(*second)==10.0
            && !mspt(*second), "TPS valid while full frame unavailable");
        require(!sampler.poll(4000), "no zero-TPS invented during halt");
        require(sampler.completedTick(4001, 10000), "fresh tick after halt");
        auto third=sampler.poll(5000);
        require(third && third->sequence==3 && third->ticks==1, "sample resumes");
        sampler.start(12, 2000);
        require(!sampler.completedTick(1999), "reject clock rollback tick");
        require(!sampler.poll(1999), "clock rollback discards window");
        require(sampler.completedTick(2000, 10000), "new window after rollback");
        auto rolled=sampler.poll(3000);
        require(rolled && rolled->epoch==12 && rolled->sequence==1, "new epoch resets sequence");
        sampler.start(13, 0);
        require(sampler.completedTick(1, 12000), "first frame");
        require(sampler.completedTick(2), "missing frame");
        auto mixed=sampler.poll(1000);
        require(mixed && mixed->ticks==2 && mixed->avgFrameUs==0, "partial frame data hidden");
        sampler.start(14,0);
        for(int i=0;i<1200;++i) require(sampler.completedTick(1,1000), "bounded tick");
        require(!sampler.completedTick(1,1000), "excess tick rejected");
        require(!sampler.poll(1000), "overflow rejects entire sample");
        require(sampler.completedTick(1001,2000), "next window usable");
        require(sampler.poll(2000).has_value(), "recovery after overflow");
        // Simulation processing time differs from 50 ms between tick starts.
        sampler.start(16,0);
        for (int i=1;i<=20;++i)
            require(sampler.completedTick(i*50, i%2 ? 1000 : 2000), "real work durations accepted");
        auto measured=sampler.poll(1000);
        require(measured && measured->ticks==20 && measured->windowMs==1000,
            "simulation count and elapsed time are independent");
        require(measured->avgFrameUs==1500 && mspt(*measured)==1.5,
            "mean MSPT is measured processing duration, never tick interval");
        require(tps(*measured)==20.0, "TPS independent from 1.5ms busy MSPT");
        sampler.start(0,0);
        require(!sampler.completedTick(1,10000) && !sampler.poll(1000), "zero epoch prohibited");
        sampler.start(15,0);
        require(sampler.completedTick(1,10000), "stale window tick");
        require(!sampler.poll(61000), "oversize window discarded");
        require(sampler.completedTick(61001,10000), "new window after long pause");
        require(sampler.poll(62000).has_value(), "recovery from long pause");
        std::cout << "TabTPS server sampler checks: " << checks << " PASS\n";
        return 0;
    } catch (std::exception const& e) {
        std::cerr << "Sampler error: " << e.what() << '\n';
        return 1;
    }
}
