#include "core/RuntimePolicy.h"
#include <iostream>
#include <stdexcept>
#include <limits>
using namespace praxis::companion;
int main() {
    int checks{}; auto check=[&](bool b){++checks;if(!b)throw std::runtime_error("WeaponSwitch contract");};
    try {
        WeaponSwitchOptions o; std::array<WeaponCandidate,9> slots{};
        check(chooseWeapon(slots,0,o)==-1);
        check(weaponFamily("minecraft:diamond_pickaxe")==WeaponFamily::None);
        check(weaponFamily("minecraft:diamond_axe")==WeaponFamily::Axe);
        slots[1]={WeaponFamily::Sword,7,100,false};slots[2]={WeaponFamily::Axe,8,200,false};
        check(chooseWeapon(slots,0,o)==2);
        o.priority=1;check(chooseWeapon(slots,0,o)==1);
        o.priority=0;o.includeAxes=false;check(chooseWeapon(slots,0,o)==1);
        o.includeAxes=true;slots[2].protectedItem=true;check(chooseWeapon(slots,0,o)==1);
        o.skipLowDurability=false;check(chooseWeapon(slots,0,o)==2);
        slots[2].damage=std::numeric_limits<float>::quiet_NaN();check(chooseWeapon(slots,0,o)==1);
        slots[2]={WeaponFamily::Sword,7,200,false};check(chooseWeapon(slots,1,o)==1);
        check(chooseWeapon(slots,0,o)==2);
        slots[2].remaining=100;check(chooseWeapon(slots,0,o)==1);
        check(chooseWeapon(slots,9,o)==-1); o.priority=4;check(chooseWeapon(slots,0,o)==-1);
        RuntimeConfig c;check(validRuntimeConfig(c));check(!c.weaponSwitch && c.keybinds[11]==0);
        c.weaponSwitch=true;check(!runtimeSettings(c,0).on(Feature::WeaponSwitch));
        check(runtimeSettings(c,0,1u<<static_cast<unsigned>(Feature::WeaponSwitch)).on(Feature::WeaponSwitch));
        std::cout<<checks<<" WeaponSwitch checks passed\n";
    }catch(std::exception const& e){std::cerr<<e.what();return 1;}
}
