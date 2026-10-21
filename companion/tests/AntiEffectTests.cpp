#include "core/RuntimePolicy.h"
#include "runtime/ConfigMigration.h"
#include "runtime/ProfileDocument.h"
#include "support/SdkCodecApi.h"
#include "runtime/ConfigCodec.h"
#include <iostream>
#include <stdexcept>
using namespace praxis::companion;
int main() {
    unsigned checks{};auto check=[&](bool v){++checks;if(!v)throw std::runtime_error("AntiEffect boundary");};
    check(unsigned(Feature::AntiEffect)==24);check(featureCount==27);RuntimeConfig config;check(!config.antiEffect);
    for(unsigned flags=0;flags<128;++flags) {
        config.antiEffect=flags&1;config.antiEffectOptions.fire=flags&2;config.antiEffectOptions.portal=flags&4;
        config.noRender.enabled=flags&8;config.noRender.passes[unsigned(RenderPass::FireOverlay)]=flags&16;
        config.noRender.passes[unsigned(RenderPass::PortalOverlay)]=flags&32;config.noRender.passes[unsigned(RenderPass::Sky)]=flags&64;
        config.fullbright=true;config.weatherChanger=true;
        for(auto available:std::array<RenderMask,4>{0,passBit(RenderPass::FireOverlay),passBit(RenderPass::PortalOverlay),kAllRenderPasses}) {
            auto s=runtimeSettings(config,available,featureRegistryBit(8));
            auto expected=(config.noRender.requestedMask()|antiEffectMask(config.antiEffect,config.antiEffectOptions))&available;
            check(s.renderMask==expected);check(s.on(Feature::Fullbright)&&s.on(Feature::WeatherChanger));
            check(frameEffectMask(s.renderMask,true,true)==(expected&antiEffectPasses));
            check(frameEffectMask(s.renderMask,false,true)==0);check(frameEffectMask(s.renderMask,true,false)==0);
        }
    }
    config={};config.antiEffect=true;auto full=ll::reflection::serialize<ConfigJson>(config).value();
    full["antiEffectOptions"]["futureDarkness"]=nullptr;full["keybinds"]=std::vector<int>(32,85);full["keybindModifiers"]=std::vector<unsigned>(32,7);
    full["favoriteModules"]=UINT32_MAX;RuntimeConfig decoded;check(decodeRuntimeConfig(full,decoded));check(decoded.antiEffect);
    auto typed=ll::reflection::serialize<ConfigJson>(decoded).value();overlayOwnedConfig(full,typed);check(full["antiEffectOptions"]["futureDarkness"].is_null());
    for(unsigned i:{15u,16u,17u,18u,19u,22u,25u,27u,28u,29u,30u,31u}) {check(!(ownedFeatureSlotMask&featureRegistryBit(i)));check(full["keybinds"][i]==85&&full["keybindModifiers"][i]==7);}
    auto before=full;auto raw=typed;raw["antiEffect"]=false;raw["inventoryHud"]=false;
    overlayOwnedConfig(full,raw,false,ownedFeatureSlotMask&~(featureRegistryBit(23)|featureRegistryBit(24)));
    check(full["antiEffect"]==before["antiEffect"]);check(full["antiEffectOptions"]==before["antiEffectOptions"]);check(full["inventoryHudOptions"]==before["inventoryHudOptions"]);
    RuntimeConfig defaults;auto baseline=ll::reflection::serialize<ConfigJson>(defaults).value();auto old=baseline;old.erase("antiEffect");old.erase("antiEffectOptions");
    old["keybinds"]=std::vector<int>(22,0);old["keybindModifiers"]=std::vector<unsigned>(22,8);
    check(decodeRuntimeConfig(old,decoded));check(!decoded.antiEffect);
    auto saved=mergeRuntimeProfileDocument(baseline,baseline,old,before);check(saved["antiEffect"]==true);check(saved["antiEffectOptions"]["futureDarkness"].is_null());
    for(unsigned modifier:{0u,1u,2u,4u,7u,8u})check(validModifiers(modifier));
    std::cout<<"AntiEffect "<<checks<<" checks PASS; actual renderer/restoration Minecraft NOT_RUN\n";
}
