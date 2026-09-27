#include "core/ShulkerPreviewPolicy.h"
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace praxis::companion;
int main() {
    int checks{};
    auto require=[&](bool condition,char const* message){
        ++checks;if(!condition)throw std::runtime_error(message);
    };
    try {
        require(shulkerIdentity("minecraft:shulker_box"),"default shulker");
        constexpr std::array colors{"white","orange","magenta","light_blue","yellow","lime",
            "pink","gray","light_gray","cyan","purple","blue","brown","green","red","black"};
        for(auto const* color:colors) {
            std::string name="minecraft:";name+=color;name+="_shulker_box";
            require(shulkerIdentity(name),"colored shulker");
        }
        require(!shulkerIdentity("minecraft:chest"),"non-shulker rejected");
        require(!shulkerIdentity("minecraft:shulker_shell"),"shulker shell rejected");
        ShulkerSlotViews slots;
        require(slots.size()==27,"exact slot count");
        require(acceptShulkerSlot(0,1),"first slot");
        require(acceptShulkerSlot(26,64),"last slot");
        require(!acceptShulkerSlot(-1,1)&&!acceptShulkerSlot(27,1),"malformed slot rejected");
        require(!acceptShulkerSlot(0,0)&&!acceptShulkerSlot(0,128),"malformed count rejected");
        slots[0]={true,1};slots[26]={true,64};
        require(!slots[13].present&&slots[0].count==1&&slots[26].count==64,
                "empty slots and source slot indices preserved");
        ShulkerSlotViews empty{};
        require(!empty[0].present&&!empty[26].present,"empty shulker has 27 empty slots");
        require(storeShulkerSlot(empty,13,1)&&empty[13].present&&empty[13].count==1,
                "one item remains in its NBT slot");
        require(!storeShulkerSlot(empty,13,2),"duplicate slot rejected");
        ShulkerSlotViews full{};
        bool populated=true;
        for(int slot=0;slot<27;++slot)populated&=storeShulkerSlot(full,slot,slot+1);
        require(populated,"all 27 slots accepted");
        require(full[0].count==1&&full[26].count==27,"full shulker preserves counts");
        require(!storeShulkerSlot(full,27,1),"out-of-range NBT rejected");
        require(hotbarPreviewActive(0)&&hotbarPreviewActive(2199)&&!hotbarPreviewActive(2200),
                "hotbar preview expires with the item tooltip");
        std::cout<<"Shulker preview policy checks: "<<checks<<" PASS\n";
        return 0;
    } catch(std::exception const& error){std::cerr<<error.what()<<'\n';return 1;}
}
