#include "core/WeatherSunnyIsolationPolicy.h"
#include "core/WeatherSunnyImagePolicy.h"
#include <iostream>
#include <stdexcept>
#include <thread>
#include <cstring>
using namespace praxis::companion;
namespace {
unsigned checks{};
void check(bool ok,char const* message){++checks;if(!ok)throw std::runtime_error(message);}
constexpr unsigned weatherBit=1u<<static_cast<unsigned>(Feature::WeatherChanger);
void ready(){runtimeReady=true;weatherNativeAdmission=true;worldGeneration=17;
    auto s=std::make_shared<Settings>();s->enabled[static_cast<unsigned>(Feature::WeatherChanger)]=true;
    s->enabled[static_cast<unsigned>(Feature::TimeChanger)]=true;s->enabled[static_cast<unsigned>(Feature::Fullbright)]=true;
    s->renderMask=passBit(RenderPass::Weather)|passBit(RenderPass::Fog);publishedSettings.store(s);
    availableFeatures=0x7fffffffu;availableRenderPasses=0xffff;}
void imageVariants(){
    for (bool patched : {false,true}) {
        SunnyImageIdentity native{patched?sunnyPreLoaderImageSize:sunnyImageSize,
            patched?sunnyPreLoaderImageSize:sunnyImageSize,0x8664,0x20b,sunnyImageTimestamp,
            240669872,4096,512,1024,patched?314609751u:252706408u,patched?10289u:1660u,
            patched?9u:8u,1,sunnyCodeViewGuid};
        native.preLoaderResolveImport=patched;
        std::array<SunnyImageSection,9> sections{};
        std::copy(sunnyOriginalSections.begin(),sunnyOriginalSections.end(),sections.begin());
        if(patched)sections[8]=sunnyPreLoaderSection;
        auto expected=patched?SunnyImageVariant::PreLoaderImport:SunnyImageVariant::Original;
        check(sunnyImageVariant(native,sections)==expected,"exact original and PreLoader import variants accepted");
        for(unsigned fault=0;fault<19;++fault) {
            auto bad=native;
            if(fault==0)++bad.moduleSize;if(fault==1)++bad.imageSize;if(fault==2)bad.machine=0x14c;
            if(fault==3)bad.optionalMagic=0x10b;if(fault==4)++bad.timestamp;if(fault==5)++bad.entryPoint;
            if(fault==6)++bad.sectionAlignment;if(fault==7)++bad.fileAlignment;if(fault==8)++bad.headersSize;
            if(fault==9)++bad.importRva;if(fault==10)++bad.importSize;if(fault==11)++bad.sectionCount;
            if(fault==12)++bad.codeViewAge;if(fault==13)bad.codeViewGuid[0]^=1;
            if(fault==14)bad.moduleSize=bad.imageSize=314580992; // no intermediate size accepted
            if(fault==15)bad.sectionCount=7;if(fault==16)bad.sectionCount=10;
            if(fault==17)bad.imageSize=patched?sunnyImageSize:sunnyPreLoaderImageSize;
            if(fault==18)bad.preLoaderResolveImport=!patched;
            check(sunnyImageVariant(bad,sections)==SunnyImageVariant::Rejected,"altered header/CodeView/size/count rejects");
        }
        for(unsigned section=0;section<native.sectionCount;++section)for(unsigned field=0;field<6;++field){
            auto bad=sections;auto& item=bad[section];
            if(field==0)item.name[0]^=1;if(field==1)++item.rva;if(field==2)++item.virtualSize;
            if(field==3)++item.rawOffset;if(field==4)++item.rawSize;if(field==5)item.characteristics^=0x20000000;
            check(sunnyImageVariant(native,bad)==SunnyImageVariant::Rejected,"any section name/layout/access change rejects");
        }
        check(sunnyImageVariant(native,std::span<SunnyImageSection const>(sections.data(),native.sectionCount-1))
            ==SunnyImageVariant::Rejected,"truncated section read rejects");
        auto crossed=native;crossed.moduleSize=crossed.imageSize=patched?sunnyImageSize:sunnyPreLoaderImageSize;
        check(sunnyImageVariant(crossed,sections)==SunnyImageVariant::Rejected,"original size with patched layout or reverse rejects");
        ready();unsigned installs{};auto bad=native;++bad.imageSize;
        check(isolatedSunnyInstall([&]{return sunnyImageVariant(bad,sections)!=SunnyImageVariant::Rejected;},
            [&]{++installs;return true;}) && installs==0 && !weatherNativeAdmission && runtimeReady,
            "invalid image installs no Weather hook and preserves native startup");
    }
}
void importMarker(){
    struct Descriptor {unsigned OriginalFirstThunk{},TimeDateStamp{},ForwarderChain{},Name{},FirstThunk{};};
    for(unsigned fault=0;fault<13;++fault) {
        std::array<unsigned char,256> image{};Descriptor descriptor{128,0,0,64,184};
        std::array<std::uint64_t,2> thunk{92,0};
        if(fault==5)descriptor.OriginalFirstThunk=0;if(fault==6)descriptor.FirstThunk=0;
        if(fault==7)thunk[0]|=1ULL<<63;if(fault==8)thunk[1]=92;if(fault==9)thunk[0]=0;
        std::memcpy(image.data()+16,&descriptor,sizeof(descriptor));
        std::memcpy(image.data()+64,"PreLoader.dll",sizeof("PreLoader.dll"));
        std::memcpy(image.data()+94,"pl_resolve_symbol",sizeof("pl_resolve_symbol"));
        std::memcpy(image.data()+128,thunk.data(),sizeof(thunk));
        if(fault==3)image[64]='X';if(fault==4)image[94]='X';
        if(fault==10)std::memcpy(image.data()+36,&descriptor,sizeof(descriptor));
        auto const before=image;
        auto read=[&](std::uint64_t rva,void* target,std::size_t size) {
            if((fault==1 && rva==16)||(fault==2 && rva==64)||(fault==11 && rva==128)
                ||(fault==12 && rva==94)||rva>=image.size()||size>image.size()-rva)return false;
            std::memcpy(target,image.data()+rva,size);return true;
        };
        check(sunnyPreLoaderImportMarker<Descriptor>(16,60,read)==(fault==0),
            "only one named PreLoader resolver import accepted; bad/missing/ordinal/extra/read failures reject");
        check(image==before,"import identity inspection never writes input image");
        if(fault==0)check(!sunnyPreLoaderImportMarker<Descriptor>(16,20,read),"missing import terminator rejects");
    }
}
void failureIsolation(){
    constexpr std::array bindings{std::pair{0u,std::uintptr_t{0x230e620}},std::pair{1u,std::uintptr_t{0x46852c0}},
        std::pair{2u,std::uintptr_t{0x6618030}},std::pair{4u,std::uintptr_t{0x46ac2d0}},std::pair{5u,std::uintptr_t{0x46aa3d0}}};
    for(unsigned missing=0;missing<bindings.size();++missing)for(unsigned mode=0;mode<3;++mode){
        ready();auto const state=settings();unsigned installs{},queries{};
        bool const startup=isolatedSunnyInstall([&]{return sunnyBindingsMatch(0x140000000,bindings,[&](unsigned index)->std::uintptr_t{
            ++queries;for(auto [id,rva]:bindings)if(id==index){
                if(index==bindings[missing].first){if(mode==2)throw std::runtime_error("resolver failure");return mode==0?0:0x140000000+rva+1;}
                return 0x140000000+rva;}return 0;});},[&]{++installs;return true;});
        check(startup&&installs==0&&queries==missing+1,"missing/mismatched/throwing runtime resolution preserves startup and installs nothing");
        check(!weatherNativeAdmission&&(availableFeatures.load()&weatherBit)==0
            &&availableFeatures.load()==(0x7fffffffu&~weatherBit),"only Weather capability closes");
        check(runtimeReady&&availableRenderPasses==0xffff&&settings()==state&&worldGeneration==17,
            "other features, NoRender, ready state, settings and generation survive");
    }
    for(unsigned mode=0;mode<2;++mode){ready();unsigned retained{};
        check(isolatedSunnyInstall([]{return true;},[&]{++retained;if(mode)throw 1;return false;}),"installer failure retains overall startup");
        check(retained==1&&!weatherNativeAdmission&&(availableFeatures.load()&weatherBit)==0,"partial hooks remain disabled passthroughs");}
    ready();unsigned installs{};
    check(isolatedSunnyInstall([]{return true;},[&]{++installs;return true;})&&installs==1&&weatherNativeAdmission
        &&(availableFeatures.load()&weatherBit),"verified install admits Weather");
}
void alternativeException(){
    ready();float const native=.47f;unsigned first{},second{};auto const state=settings();
    auto original=[&]{++first;return native;};float const result=original();
    auto alternative=trySunnyAlternative([&]()->float{++second;throw std::runtime_error("extra native color call");});
    check(!alternative&&result==native&&first==1&&second==1,"second-call exception returns already-owned native value");
    check(!weatherNativeAdmission&&runtimeReady&&settings()==state&&availableFeatures.load()==(0x7fffffffu&~weatherBit)
        &&availableRenderPasses==0xffff,"exception disables only Weather; settings and other native modules unchanged");
    ready();auto success=trySunnyAlternative([]{return .8f;});check(success&&*success==.8f&&weatherNativeAdmission,"success keeps admission");
}
struct Color {float r{},g{},b{},a{1};};
void nativeArithmetic(){
    WeatherSunnyParameters<Color> p{};p.rainCloudWeight=.95f;p.rainCloudColor={.5f,.5f,.5f,1};
    p.thunderCloudWeight=.95f;p.thunderCloudColor={.3f,.59f,.11f,1};
    // Fixtures independently taken from the pinned SSE consumer constants and
    // branch arithmetic, including full fog where inversion would lose time.
    for(auto [sine,expected]:std::array{std::pair{-1.f,std::array<float,3>{.1f,.1f,.15f}},
            std::pair{0.f,std::array<float,3>{.55f,.55f,.575f}},std::pair{1.f,std::array<float,3>{1,1,1}}}){
        auto clear=sunnyCloudProducerValue(sine,sunnyParameters(p,false),0,0,false,true);
        check(sunnyValueMatches(clear[0],expected[0])&&sunnyValueMatches(clear[1],expected[1])
            &&sunnyValueMatches(clear[2],expected[2])&&clear[3]==1,"native daylight/night palette preserved");}
    auto storm=sunnyCloudProducerValue(1,p,1,1,false,true);
    check(sunnyValueMatches(storm[0],.31125f)&&sunnyValueMatches(storm[1],.58675f)
        &&sunnyValueMatches(storm[2],.13075f),"native configured rain/thunder blend correspondence");
    auto wither=sunnyCloudProducerValue(1,sunnyParameters(p,true),0,0,true,false);
    check(sunnyValueMatches(wither[0],.24f)&&sunnyValueMatches(wither[1],.24f)
        &&sunnyValueMatches(wither[2],.24f),"native Wither grey reused without weather");
    for(float fog:{0.f,.5f,1.f}){
        auto clear=sunnyFogProducerValue(0,{0,1,0},.9f);
        check(clear&&*clear==1&&sunnyValueMatches(*clear+fog*(-*clear),1-fog),"full fog derives clear value from observed angle, including fog=1");}
    auto night=sunnyFogProducerValue(.5f,{0,1,0},.9f);
    check(night&&*night==0,"native night HDR remains night");
    auto sunset=sunnyFogProducerValue(.125f,{0,1,0},0);
    check(sunset&&sunnyValueMatches(*sunset,.8535534f),"native direction/clock trigonometry retained");
    check(!sunnyFogProducerValue(0,{0,1,0},1)&&!sunnyCloudLookupIndex(std::numeric_limits<float>::infinity()),"invalid arithmetic rejects");
    check(sunnyCloudLookupIndex(0)==16384&&sunnyCloudLookupIndex(.25f)==32768,"native LUT index follows source operation order");
}
void nativeColorArithmetic() {
    WeatherSunnyParameters<Color> p{};p.rainSkyWeight=.5f;p.rainSkyColor={.2f,.3f,.4f,1};
    p.thunderSkyWeight=.8f;p.thunderSkyColor={.1f,.2f,.3f,1};p.flashSkyWeight=.25f;p.flashSkyColor={1,1,1,1};
    std::array<float,4> const base{.6f,.7f,.8f,1};
    auto clear=weatherColorProducerValue(base,.5f,p,0,0,0,false,true,true,0,.5f);
    check(clear==std::array<float,4>{.3f,.35f,.4f,.5f},"native celestial intensity retained in Clear");
    auto rain=weatherColorProducerValue(base,.5f,p,1,1,0,false,true,true,0,.5f);
    for(unsigned i=0;i<4;++i)check(sunnyValueMatches(rain[i],std::array<float,4>{.2f,.25f,.3f,.5f}[i]),"native rain palette/sky coefficient produces full overcast");
    for(float level:{0.f,.2f})check(weatherColorProducerValue(base,.5f,p,1,level,0,false,true,true,0,.5f)==clear,
        "pinned rain branch threshold remains native at <= .2");
    auto storm=weatherColorProducerValue(base,.5f,p,1,1,.5f,false,true,true,0,.5f);
    for(unsigned i=0;i<4;++i)check(sunnyValueMatches(storm[i],std::array<float,4>{.16f,.23f,.3f,.7f}[i]),"configured native thunder blend");
    auto wither=weatherColorProducerValue(base,.5f,p,0,0,0,true,true,true,0,.5f);
    for(unsigned i=0;i<4;++i)check(sunnyValueMatches(wither[i],std::array<float,4>{.14f,.23f,.32f,.9f}[i]),"Wither retained even when weather envelope is Clear");
    auto grey=weatherColorProducerValue(base,.5f,p,0,0,1.f,false,false,true,0,.5f);
    for(unsigned i=0;i<4;++i)check(sunnyValueMatches(grey[i],std::array<float,4>{.11448f,.12448f,.13448f,.9f}[i]),
        "native unconfigured grayscale thunder/Wither palette follows verified .3/.59/.11/.2 constants");
    auto flash=weatherColorProducerValue(base,.5f,p,0,0,0,false,true,false,2,.5f);
    for(unsigned i=0;i<4;++i)check(sunnyValueMatches(flash[i],std::array<float,4>{.475f,.5125f,.55f,.625f}[i]),"native flash weight before selected weather removes lightning only");
}
void oneNativeQuery(){
    ready();SunnyClockCapture observed,nested;unsigned nativeCalls{},getters{};
    {
        SunnyClockScope scope(observed,true);
        auto original=[&]{++nativeCalls;++getters;sunnyClockCapture->angle=0;++sunnyClockCapture->samples;return 0.f;};
        float const native=original();auto clear=sunnyFogProducerValue(observed.angle,{0,1,0},.9f);
        check(native==0&&clear&&*clear==1&&nativeCalls==1&&getters==1,"Cloud/Fog alternate math does not re-enter stateful native getters");
        {SunnyClockScope rejected(nested,false);check(!sunnyClockCapture,"rejected nested scope masks outer clock observation");}
        check(sunnyClockCapture==&observed,"clock scope restores outer");
        try{SunnyClockScope inner(nested,true);throw 1;}catch(int){}
        check(sunnyClockCapture==&observed,"exception restores clock pointer");
        bool separate{};std::thread thread([&]{separate=!sunnyClockCapture;});thread.join();check(separate,"clock observation thread isolation");
    }
    check(!sunnyClockCapture,"no borrowed receiver or owned stack capture survives producer");
}
}
int main(){try{imageVariants();importMarker();failureIsolation();alternativeException();nativeArithmetic();nativeColorArithmetic();oneNativeQuery();
    std::cout<<"WeatherSunnyIsolationTests PASS "<<checks<<" checks; native/game startup NOT_RUN\n";return 0;
}catch(std::exception const& e){std::cerr<<"FAIL "<<checks<<": "<<e.what()<<'\n';return 1;}}
