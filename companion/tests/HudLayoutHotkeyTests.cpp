#include "core/HudLayoutPolicy.h"
#include "core/HotkeyPolicy.h"
#include "core/RuntimePolicy.h"
#include <cstdio>
#include <limits>
#include <stdexcept>
using namespace praxis::companion;
int main() {
    unsigned checks{};auto check=[&](bool ok,char const* message){++checks;if(!ok)throw std::runtime_error(message);};
    try {
        RuntimeConfig config;
        check(validRuntimeConfig(config),"default config valid");
        check(config.keybindModifiers[7]==8&&config.zoomModifiers==8,"legacy wildcard modifiers retained");
        HudRect original{23,45,100,60};HudTransform t;
        auto untouched=transformHud(original,t,800,600);
        check(untouched.x==23&&untouched.y==45,"legacy custom=false preserves positions");
        for(int anchor=0;anchor<9;++anchor){
            t={true,0,0,1,anchor};auto panel=transformHud(original,t,800,600);
            check(panel.x==(anchor%3)*350&&panel.y==(anchor/3)*270,"all nine anchors");
            panel.x+=17;panel.y-=9;moveHud(t,panel,800,600);auto moved=transformHud(original,t,800,600);
            check(moved.x==panel.x&&moved.y==panel.y,"drag offset round trips with selected anchor");
        }
        t.scale=std::numeric_limits<float>::quiet_NaN();check(!validHudTransform(t),"NaN scale rejected");
        t={true,0,0,1,9};check(!validHudTransform(t),"unknown anchor rejected");
        t={true,20000,0,1,0};check(!validHudTransform(t),"out-of-range offset rejected");
        check(hotkeyConflict({'A',8,1},{'A',3,1}),"legacy wildcard detects exact chord collision");
        check(!hotkeyConflict({'A',1,1},{'A',2,1}),"distinct chords do not collide");
        check(!hotkeyConflict({'A',1,1},{'A',1,2}),"disjoint contexts do not collide");
        check(hotkeyConflict({'A',1,3},{'A',1,1}),"overlapping contexts collide");
        check(!hotkeyConflict({0,8,3},{0,0,3}),"cleared bindings never collide");
        check(modifiersMatch(8,7)&&!modifiersMatch(0,1)&&modifiersMatch(3,3),"dispatch modifier semantics");
        config.zoomKey=0;check(validRuntimeConfig(config),"clear zoom is persistent and valid");
        config.informationOptions.hudLayouts.panels[3]={true,32,-12,1.4f,8};
        check(validRuntimeConfig(config),"profile owns valid custom layout");
        config.informationOptions.hudLayouts={};check(!config.informationOptions.hudLayouts.panels[3].custom,"reset restores legacy placement");
        std::printf("HUD layout and hotkey policy: %u checks PASS; game runtime NOT RUN\n",checks);return 0;
    }catch(std::exception const& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
