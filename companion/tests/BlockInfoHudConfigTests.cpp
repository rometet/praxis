// The crosshair block line is gated by ONE flat scalar (informationOptions.blockInfoHud).
// This test proves the pinned SDK codec actually round-trips it, because a field the
// serializer cannot encode invalidates the whole config file and the mod refuses to load.
#include "core/RuntimePolicy.h"
#include "runtime/ConfigCodec.h"
#include "support/SdkCodecApi.h"
#include <iostream>
#include <stdexcept>
#include <string>
using namespace praxis::companion;
int main() {
    unsigned checks{};
    auto check = [&](bool v, char const* why) {
        ++checks;
        if (!v) throw std::runtime_error(why);
    };
    try {
        // 1) off by default so an upgrade never changes an existing HUD
        {
            RuntimeConfig d;
            check(!d.informationOptions.blockInfoHud, "block info hud defaults off");
            check(validRuntimeConfig(d), "default config valid");
        }
        // 2) the scalar exists in the encoded document as a real boolean
        {
            RuntimeConfig c;
            auto doc = ll::reflection::serialize<ConfigJson>(c).value();
            check(doc["informationOptions"].contains("blockInfoHud"), "encoded document carries the field");
            check(doc["informationOptions"]["blockInfoHud"].is_boolean(), "encoded as a boolean, not an object");
            check(doc["informationOptions"]["blockInfoHud"] == false, "default serializes as false");
        }
        // 3) true survives encode -> decode (the actual failure mode we must rule out)
        {
            RuntimeConfig on;
            on.informationOptions.blockInfoHud = true;
            on.informationOptions.infoHud = true;      // neighbours must not be disturbed
            on.informationOptions.scale = 1.25f;
            auto doc = ll::reflection::serialize<ConfigJson>(on).value();
            RuntimeConfig decoded;
            check(decodeRuntimeConfig(doc, decoded), "config with the flag on still decodes");
            check(decoded.informationOptions.blockInfoHud, "true round-trips");
            check(decoded.informationOptions.infoHud, "neighbour infoHud preserved");
            check(decoded.informationOptions.scale == 1.25f, "neighbour scale preserved");
            check(validRuntimeConfig(decoded), "decoded config valid");
        }
        // 4) false and true are distinct after a full trip, in both directions
        {
            for (bool value : {false, true}) {
                RuntimeConfig c;
                c.informationOptions.blockInfoHud = value;
                auto doc = ll::reflection::serialize<ConfigJson>(c).value();
                RuntimeConfig back;
                check(decodeRuntimeConfig(doc, back), "round trip decodes");
                check(back.informationOptions.blockInfoHud == value, "value preserved exactly");
            }
        }
        // 5) an older document without the field still loads (forward compatibility)
        {
            RuntimeConfig c;
            auto doc = ll::reflection::serialize<ConfigJson>(c).value();
            doc["informationOptions"].erase("blockInfoHud");
            RuntimeConfig decoded;
            check(decodeRuntimeConfig(doc, decoded), "legacy document without the field loads");
            check(!decoded.informationOptions.blockInfoHud, "missing field means off");
        }
        // 6) a wrong type is rejected rather than silently coerced
        {
            RuntimeConfig c;
            auto doc = ll::reflection::serialize<ConfigJson>(c).value();
            doc["informationOptions"]["blockInfoHud"] = std::string("yes");
            RuntimeConfig decoded;
            check(!decodeRuntimeConfig(doc, decoded), "a string is not accepted as the flag");
        }
        std::cout << checks << " BlockInfoHudConfig checks passed\n";
    } catch (std::exception const& e) {
        std::cerr << "BlockInfoHudConfig FAILED after " << checks << " checks: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
