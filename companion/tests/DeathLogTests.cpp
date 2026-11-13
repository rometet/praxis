#include "runtime/DeathLogCodec.h"
#include "map/DeathHistory.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace praxis::companion;
using namespace praxis::companion::map;

int main() {
    int checks = 0;
    auto check = [&](bool value, char const* message) {
        ++checks;
        if (!value) throw std::runtime_error(message);
    };
    try {
        std::vector<DeathHistoryEntry> entries{
            {1700000000ull, 0, 10.25, 64.0, -20.5},
            {1699999999ull, 0, -30.0, 12.0, 44.0}};
        auto bytes = deathlog::encode("local-v2:10:/worlds/Test:7:level-id", entries);
        check(!bytes.empty(), "owned death log encodes a valid world log");
        check(bytes.size() <= deathlog::maxFileBytes, "bounded file size");
        auto decoded = deathlog::decode(bytes, "local-v2:10:/worlds/Test:7:level-id");
        check(decoded && decoded->size() == 2, "decode round-trip count");
        check(decoded->front().unixSeconds == 1700000000ull && decoded->front().z == -20.5,
            "decode preserves epoch and coordinates");
        check(!deathlog::decode(bytes, "another-world"), "foreign world identity rejected");
        auto corrupted = bytes;
        corrupted[corrupted.size() / 2] ^= 0xff;
        check(!deathlog::decode(corrupted, "local-v2:10:/worlds/Test:7:level-id"),
            "checksum corruption rejected");
        auto truncated = bytes;
        truncated.resize(truncated.size() - 3);
        check(!deathlog::decode(truncated, "local-v2:10:/worlds/Test:7:level-id"),
            "truncated file rejected");
        std::vector<DeathHistoryEntry> invalid{{1, 0, NAN, 1, 1}};
        check(deathlog::encode("world", invalid).empty(), "non-finite coordinate is never persisted");
        invalid[0] = {1, 0, 40000000.0, 1, 1};
        check(deathlog::encode("world", invalid).empty(), "out-of-range coordinate rejected");
        check(deathlog::encode("", entries).empty(), "empty world identity rejected");
        std::vector<DeathHistoryEntry> tooMany(DeathHistory::capacity + 1);
        for (std::size_t i = 0; i < tooMany.size(); ++i) tooMany[i] = {i + 1, 0, double(i), 64, 0};
        check(deathlog::encode("world", tooMany).empty(), "capacity boundary enforced at encode");

        Session world{"local-world", 1, 0, true};
        Session other{"other-world", 2, 0, true};
        DeathHistory history;
        check(history.preload(world, entries), "preload accepts owned session data");
        check(history.entries(world).size() == 2, "preloaded entries published");
        std::vector<DeathHistoryEntry> mixed{
            {3, 0, 1, 2, 3},
            {4, 1, 4, 5, 6},     // wrong dimension
            {5, 0, NAN, 2, 3},   // invalid coordinate
            {6, 0, 7, 8, 9}};
        DeathHistory filtered;
        check(filtered.preload(world, mixed), "preload session accepted");
        auto kept = filtered.entries(world);
        check(kept.size() == 2 && kept[0].unixSeconds == 3 && kept[1].unixSeconds == 6,
            "preload rejects invalid dimension/coordinates individually");
        check(!history.preload({}, entries), "invalid session rejected");
        check(history.entries(other).empty(), "foreign session never exposed");
        std::vector<DeathHistoryEntry> replacement{{9, 0, 1, 2, 3}};
        check(history.preload(world, replacement), "second preload accepted");
        auto replaced = history.entries(world);
        check(replaced.size() == 1 && replaced[0].unixSeconds == 9,
            "preload replaces prior session entries instead of appending duplicates");

        Session logSession{world.world, 9, 0, true};
        DeathHistory worldLog;
        check(worldLog.recordAnyDimension(logSession, {1, "nether", 4, 5, 6, 1}, 10),
            "world-scoped log accepts a different dimension");
        check(worldLog.recordAnyDimension(logSession, {1, "overworld", 7, 8, 9, 0}, 11),
            "world-scoped log accepts the main dimension");
        auto worldEntries = worldLog.entries(logSession);
        check(worldEntries.size() == 2 && worldEntries[0].dimension == 0 && worldEntries[1].dimension == 1,
            "world-scoped entries retain their own dimension");
        check(worldLog.record(logSession, {1, "wrong", 1, 2, 3, 1}, 12) == false,
            "dimension-checked map record still rejects a mismatched dimension");

        std::cout << "Death log codec/history: " << checks << " checks PASS; file I/O NOT_RUN\n";
        return 0;
    } catch (std::exception const& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
