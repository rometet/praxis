#include "core/InformationPolicy.h"
#include "core/UiLayoutPolicy.h"
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace praxis::companion;
int main() {
    unsigned checks=0;auto require=[&](bool value,char const* label){++checks;if(!value)throw std::runtime_error(label);};
    try {
        InformationOptions options;require(validInformationOptions(options),"default options");
        require(!options.infoHud&&!options.durabilityHud&&!options.bundlePreview&&!options.durabilityInformation,"all features default OFF");
        for(float bad:{0.1f,3.1f,std::numeric_limits<float>::quiet_NaN()}) {auto o=options;o.scale=bad;require(!validInformationOptions(o),"bad scale");}
        for(int bad:{-1,3}) {auto o=options;o.durabilityMode=bad;require(!validInformationOptions(o),"bad display mode");}
        auto d=durabilityValue("Tool",3,10);require(d&&d->remaining==7&&d->ratio==0.7f,"remaining fraction");
        require(durabilityText(*d)=="7 / 10 (70%)","durability text");
        require(!durabilityValue("Unknown",0,0)&&!durabilityValue("Bad",-1,10),"non damageable and bad damage");
        auto broken=durabilityValue("Broken",100,10);require(broken&&broken->remaining==0,"damage clamp");
        InformationSnapshot sample;sample.valid=true;sample.generation=9;sample.observedMs=100;
        require(informationCurrent(sample,9,1100),"fresh boundary");
        require(!informationCurrent(sample,9,1101)&&!informationCurrent(sample,8,100)&&!informationCurrent(sample,9,99),"stale generation and clock");
        require(formatInformationLines(sample,options).empty(),"unknown values not fake zero");
        options.lines.fill(false);options.lines[0]=options.lines[13]=options.lines[15]=true;
        options.showUnavailable=true;auto unavailable=formatInformationLines(sample,options);
        require(unavailable==std::vector<std::string>{"Coordinates: Unavailable","Rotation: Unavailable","Chunk: Unavailable"},"unavailable selected rows in configured order");
        sample.durability[2]=*d;sample.durability[4]=*durabilityValue("Full",0,10);
        require(selectDurabilityRows(sample,options).size()==2,"equipment order");
        options.durabilityShowFull=false;auto rows=selectDurabilityRows(sample,options);
        require(rows.size()==1&&rows[0].first==2,"hide full equipment");
        for(auto [coordinate,expected]:std::array<std::pair<double,int>,5>{{{-0.1,-1},{-16.1,-17},{0,0},{15.9,15},{16,16}}})
            require(informationBlock(coordinate)==expected,"floor coordinates");
        require(informationChunk(-1)==-1&&informationChunk(-16)==-1&&informationChunk(-17)==-2,"negative chunks");
        require(!informationBlock(std::numeric_limits<double>::infinity())&&!informationBlock(2147483648.0),"integer bounds");
        require(dimensionCoordinateScale(0)==0.125&&dimensionCoordinateScale(1)==8.0&&!dimensionCoordinateScale(2),"cross dimension scale");
        require(informationLineNames.size()==23,"all audited information fields");
        auto layout=durabilityHudLayout(6,1920,1080,options);
        require(layout.x>=0&&layout.y>=0&&layout.x+layout.width<=1920&&layout.y+layout.height<=1080,"equipment panel viewport");
        auto tiny=durabilityHudLayout(99,320,180,options);
        require(tiny.x>=0&&tiny.y>=0&&tiny.x+tiny.width<=320&&tiny.y+tiny.height<=180,"bounded equipment rows and small viewport");
        auto smaller=durabilityHudLayout(6,10,10,options);
        require(smaller.x+smaller.width<=10&&smaller.y+smaller.height<=10,"tiny viewport does not overflow");
        require(durabilityHudLayout(6,0,10,options).scale==0&&durabilityHudLayout(6,std::numeric_limits<float>::infinity(),10,options).scale==0,"invalid viewport rejects draw");
        auto bad=options;bad.durabilityX=std::numeric_limits<float>::infinity();
        require(!validInformationOptions(bad),"invalid equipment anchor");
        for(float ly:{0.125f,0.25f,0.5f,1.0f})for(float user:{0.5f,1.f,2.f,3.f}) {
            float fontScale=nativeTextScale(14*user,ly,9);
            require(std::abs(9*fontScale/ly-14*user)<0.001f,"native text physical size invariant under GUI scale");
            require(9*fontScale/ly<15*user,"native text fits before next equipment line");
        }
        require(nativeTextScale(14,0,9)==0&&nativeTextScale(14,0.25f,0)==0,"reject invalid native font metrics");
        std::array<HudRect,2> reserved{paperDollBounds(.25f,.25f),HudRect{12,300,316,268}};
        auto info=placeHud({12,80,430,450},2048,1152,reserved);
        require(!hudIntersects(info,reserved[0])&&!hudIntersects(info,reserved[1]),"Info HUD avoids paper doll and equipment");
        require(info.x+info.width<=2048&&info.y+info.height<=1152,"measured Info HUD fits viewport");
        auto doll=paperDollBounds(.25f,.25f);
        auto native=placeHud({12,300,316,268},2048,1152,{&doll,1});
        std::array occupied{doll,native};
        auto measuredInfo=placeHud({12,80,430,450},2048,1152,occupied);
        std::array allOccupied{doll,native,measuredInfo};
        auto chunks=placeHud({10,300,270,126},2048,1152,allOccupied);
        require(!hudIntersects(native,measuredInfo)&&!hudIntersects(native,chunks)&&!hudIntersects(measuredInfo,chunks),"placed Info equipment and Chunk panels do not intersect");
        require(!hudIntersects(native,doll)&&!hudIntersects(measuredInfo,doll)&&!hudIntersects(chunks,doll),"all HUDs avoid paper doll");
        auto requested=placeHud({500,100,300,200},2048,1152,reserved);
        require(requested.x==500&&requested.y==100,"free configured anchor preserved");
        require(currentPreviewDraw(9,3,9,3,100,320),"native preview foreground receipt boundary");
        require(!currentPreviewDraw(8,3,9,3,100,110)&&!currentPreviewDraw(9,4,9,3,100,110)
            &&!currentPreviewDraw(9,3,9,3,100,321)&&!currentPreviewDraw(9,3,9,3,100,99),"stale world revision and clock receipts rejected");
        std::cout<<"Information policy checks: "<<checks<<" PASS\n";return 0;
    }catch(std::exception const& error){std::cerr<<error.what()<<'\n';return 1;}
}
