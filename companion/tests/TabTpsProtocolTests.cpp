#include "core/TabTpsProtocol.h"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
using namespace praxis::companion::tabtps;
int main() {
    int checks = 0;
    auto require = [&](bool cond, char const* why) {
        ++checks;
        if (!cond) throw std::runtime_error(why);
    };
    try {
        Sample a{42,1,20,1000,43000};
        require(valid(a), "valid baseline");
        require(encode(a) == "PTPS1|42|1|20|1000|43000", "stable wire");
        require(decode(encode(a)) == a, "roundtrip");
        require(tps(a) == 20.0 && mspt(a) == 43.0, "TPS/MSPT independently measured");
        a.ticks = 10;
        require(tps(a) == 10.0 && mspt(a) == 43.0, "low TPS not inferred from MSPT");
        a.ticks = 21;
        require(tps(a) == 20.0, "display ceiling while server catches up");
        Sample withoutMspt{42,3,20,1000,0};
        require(valid(withoutMspt) && decode(encode(withoutMspt)) == withoutMspt
            && !mspt(withoutMspt), "TPS remains available when MSPT measurement unsupported");
        for (std::string bad : std::vector<std::string>{
            "", "PTPS2|42|1|20|1000|43000", "PTPS1", "PTPS1|",
            "PTPS1|42|1|20|1000", "PTPS1|42|1|20|1000|43000|1",
            "PTPS1|42|1|20|1000|43000x",
            "PTPS1|-1|1|20|1000|43000", "PTPS1|42|+1|20|1000|43000",
            "PTPS1|42|1||1000|43000", "PTPS1|42|1|20|0|43000",
            "PTPS1|0|1|20|1000|43000", "PTPS1|42|0|20|1000|43000",
            "PTPS1|42|1|20|249|43000", "PTPS1|42|1|20|60001|43000",
            "PTPS1|42|1|20|1000|5000001",
            "PTPS1|4294967296|1|20|1000|43000",
            std::string(129,'x')
        }) {
            require(!decode(bad), bad.c_str());
        }
        require(!valid({1,2,1201,60000,2000}), "tick count bound");
        require(!valid({1,2,1200,250,2000}), "implausible tick speed bound");

        Receiver r;
        r.reset(33);
        require(r.status(33,100) == Status::Unavailable, "no data means unavailable");
        require(!r.accept({42,1,20,1000,43000},34,100), "wrong generation cannot publish");
        require(r.accept({42,1,20,1000,43000},33,100), "accept first packet");
        require(r.status(33,2600) == Status::Fresh, "fresh at 2500ms");
        require(r.status(33,2601) == Status::Stale, "stale after 2500ms");
        require(r.status(33,99) == Status::Stale, "clock rollback marks stale");
        require(r.status(34,100) == Status::Unavailable, "connection mismatch unavailable");
        require(!r.freshSample(33,2601), "must not present stale TPS");
        require(!r.accept({42,1,19,1000,42000},33,200), "duplicate sequence ignored");
        require(!r.accept({42,0,19,1000,42000},33,200), "bad sequence");
        require(!r.accept({43,2,19,1000,42000},33,200), "unexpected epoch rejected");
        require(!r.accept({42,2,19,1000,42000},33,99), "reordered clock rejected");
        require(r.accept({42,2,19,1000,42000},33,200), "next sample accepted");
        require(r.freshSample(33,200)->sequence == 2, "newest sample visible");
        require(r.status(33,2701) == Status::Stale, "new sample ages out");
        r.reset(34);
        require(r.status(34,1000) == Status::Unavailable, "reconnect clears old sample");
        require(r.accept({43,1,20,1000,12000},34,1000), "new server epoch after reconnect");
        require(r.freshSample(34,1000)->epoch == 43, "new epoch sample");
        std::cout << "TabTPS protocol checks: " << checks << " PASS\n";
        return 0;
    } catch (std::exception const& e) {
        std::cerr << "TabTPS protocol failed: " << e.what() << '\n';
        return 1;
    }
}
