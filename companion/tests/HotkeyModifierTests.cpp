#include "core/HotkeyPolicy.h"
#include "core/RuntimePolicy.h"
#include "core/CameraCandidatePolicy.h"
#include <array>
#include <cstdio>
#include <stdexcept>
using namespace praxis::companion;
int main() {
    unsigned checks{};auto check=[&](bool value,char const* message){++checks;if(!value)throw std::runtime_error(message);};
    try {
        constexpr std::array<std::string_view,8> labels{"","Ctrl+","Shift+","Ctrl+Shift+","Alt+","Ctrl+Alt+","Shift+Alt+","Ctrl+Shift+Alt+"};
        constexpr std::array<unsigned,8> lholoNative{0,1,4,5,2,3,6,7};
        for(unsigned mask=0;mask<8;++mask) {
            // Physical control/shift/alt states pass through the same encoder
            // used by UI capture and runtime dispatch, not a second bit formula.
            auto physical=modifierBits((mask&1)!=0,(mask&2)!=0,(mask&4)!=0);
            check(encodeModifiers((mask&1)!=0,(mask&2)!=0,(mask&4)!=0)==physical,"compatibility encoder shares the canonical physical formula");
            check(physical==mask,"physical chord uses inventory owner's persisted bits");
            check(hotkeyModifierPrefix(mask)==labels[mask],"existing mask displays the correct Shift/Alt label");
            for(unsigned pressed=0;pressed<8;++pressed)
                check(modifiersMatch(mask,pressed)==(mask==pressed),"persisted exact mask matches only its physical chord");
            check(praxisModifiersToLholo(mask)==lholoNative[mask],"fresh capture writes LHolo's native mask");
            check(lholoModifiersToPraxis(lholoNative[mask])==mask,"existing LHolo mask reads with correct physical meaning");
            check(praxisModifiersToLholo(lholoModifiersToPraxis(lholoNative[mask]))==lholoNative[mask],"LHolo existing saved mask round trips unchanged");
        }
        check(hotkeyConflict({'K',2,1},{'K',lholoModifiersToPraxis(4),1}),"Praxis Shift and LHolo Shift conflict");
        check(!hotkeyConflict({'K',2,1},{'K',lholoModifiersToPraxis(2),1}),"Praxis Shift and LHolo Alt remain distinct");
        check(hotkeyConflict({'K',4,1},{'K',lholoModifiersToPraxis(2),1}),"Praxis Alt and LHolo Alt conflict");
        check(modifiersMatch(anyModifiers,2)&&modifiersMatch(anyModifiers,4)&&hotkeyModifierPrefix(8)=="Any+","legacy Any remains unchanged");
        for(unsigned wanted=0;wanted<9;++wanted)for(unsigned actual=0;actual<8;++actual) {
            check(cameraModifiersMatch(wanted,actual)==(wanted==0||wanted==8||wanted==actual),"camera-only single-key requirement; explicit chords preserved");
            check(modifiersMatch(wanted,actual)==(wanted==8||wanted==actual),"normal owner None and exact masks remain unchanged");
        }
        for(unsigned mask=0;mask<9;++mask) {
            RuntimeConfig config;config.keybinds[20]='G';config.keybinds[21]='F';
            config.keybindModifiers[20]=config.keybindModifiers[21]=mask;
            auto published=runtimeSettings(config,0,ownedFeatureSlotMask);
            check(published.keyModifiers[20]==mask&&published.keyModifiers[21]==mask
                &&config.keybindModifiers[20]==mask&&config.keybindModifiers[21]==mask,"camera modifier publication and storage retain the original mask");
        }
        RuntimeConfig current;current.keybindModifiers[7]=2;current.keybindModifiers[8]=4;current.keybindModifiers[9]=6;
        current.keybinds.resize(featureCount);current.keybindModifiers.resize(featureCount,8);current.keybindModifiers[10]=2;current.keybindModifiers[14]=4;
        auto before=current.keybindModifiers;check(validRuntimeConfig(current),"old owner masks remain valid after current registry padding");
        auto snapshot=runtimeSettings(current,0);(void)snapshot;
        check(current.keybindModifiers==before&&current.keybindModifiers[8]==4&&current.keybindModifiers[10]==2,"publication never flips existing profile modifiers");
        std::printf("Shared modifier contract: %u checks PASS; physical native input NOT_RUN\n",checks);return 0;
    }catch(std::exception const& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
}
