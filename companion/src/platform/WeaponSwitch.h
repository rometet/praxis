#pragma once
#include <cstdint>
class Player;
namespace praxis::companion {
// Synchronous attack scope: no player or item pointer survives this callback.
class WeaponAttackScope {
public:
    explicit WeaponAttackScope(Player& player) noexcept;
    ~WeaponAttackScope();
    WeaponAttackScope(WeaponAttackScope const&) = delete;
    WeaponAttackScope& operator=(WeaponAttackScope const&) = delete;
private:
    Player* player_{};
    std::uint64_t generation_{};
    int previous_{-1}, selected_{-1};
};
}
