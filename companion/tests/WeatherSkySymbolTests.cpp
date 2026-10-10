#include "core/WeatherSkySymbolEvidence.h"
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string_view>
using namespace praxis::companion;
int main() {
    unsigned checks{};
    auto check = [&](bool ok) { ++checks; if (!ok) throw std::runtime_error("WeatherSkySymbol contract"); };
    try {
        std::set<std::string_view> names, identifiers;
        for (auto const& symbol : weatherSkySymbols) {
            check(names.emplace(symbol.name).second);
            check(identifiers.emplace(symbol.decorated).second);
            check(std::string_view(symbol.decorated).starts_with('?'));
        }
        check(weatherSkySymbols.size() == 9);
        auto verify = [&](std::uintptr_t address, std::uintptr_t base, std::uintptr_t size,
            WeatherSkyAddressStatus expected, std::uintptr_t rva = 0) {
            auto const result = classifyWeatherSkyAddress(address, base, size);
            check(result.status == expected); check(result.rva == rva);
        };
        verify(0, 100, 10, WeatherSkyAddressStatus::Missing);
        verify(99, 100, 10, WeatherSkyAddressStatus::OutsideMainImage);
        verify(100, 100, 10, WeatherSkyAddressStatus::MainImage, 0);
        verify(109, 100, 10, WeatherSkyAddressStatus::MainImage, 9);
        verify(110, 100, 10, WeatherSkyAddressStatus::OutsideMainImage);
        verify(1, 0, 10, WeatherSkyAddressStatus::OutsideMainImage);
        verify(100, 100, 0, WeatherSkyAddressStatus::OutsideMainImage);
        auto const max = std::numeric_limits<std::uintptr_t>::max();
        verify(max, max - 3, 4, WeatherSkyAddressStatus::MainImage, 3);
        verify(max, max - 3, 3, WeatherSkyAddressStatus::OutsideMainImage);
        verify(2, max - 3, 20, WeatherSkyAddressStatus::OutsideMainImage);
        std::cout << "WeatherSkySymbolTests PASS " << checks << " checks; native resolver/PE observer NOT_RUN\n";
        return 0;
    } catch (std::exception const& e) { std::cerr << e.what() << '\n'; return 1; }
}
