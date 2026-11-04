#include "core/ContainerPreviewPolicy.h"
#include "core/InformationPolicy.h"
#include <iostream>
#include <stdexcept>
using namespace praxis::companion;
int main() {
    unsigned checks=0;auto require=[&](bool value,char const* label){++checks;if(!value)throw std::runtime_error(label);};
    try {
        require(previewFamily("minecraft:bundle")==PreviewFamily::Bundle,"bundle");
        for(auto color:{"red","blue","white","black","light_blue"})require(previewFamily(std::string("minecraft:")+color+"_bundle")==PreviewFamily::Bundle,"colored bundle");
        require(previewFamily("minecraft:red_shulker_box")==PreviewFamily::Shulker,"colored shulker preserved");
        require(previewFamily("minecraft:bundle_fragment")==PreviewFamily::None,"wrong item");
        require(containerGrid(PreviewFamily::Shulker,1)==PreviewGrid{9,3,27},"Shulker fixed 27 slots");
        for(unsigned count:{0u,1u,3u,8u,27u,64u,65u}) {
            auto grid=bundleGrid(count);require(grid.columns>=3&&grid.columns<=8&&grid.cells>=static_cast<int>(std::min(count,64u))&&grid.cells<=64,"bounded packed grid");
        }
        require(bundleGrid(64)==PreviewGrid{8,8,64},"full bundle");
        for(unsigned entries=0;entries<=64;++entries){
            auto grid=bundleGrid(entries);
            require(grid.columns>=3&&grid.columns<=8&&grid.rows>=1&&grid.rows<=8
                &&grid.cells>=static_cast<int>(entries)&&grid.cells<=64,"every bundle occupancy bounded");
        }
        for(auto color:{"orange","magenta","yellow","lime","pink","gray","light_gray","cyan","purple","brown","green"})
            require(previewFamily(std::string("minecraft:")+color+"_bundle")==PreviewFamily::Bundle,"remaining colored bundles");
        require(!preferContainerPreview(true,false,true,-1),"clock reversal retires container priority");
        require(preferContainerPreview(true,false,true,220)&&!preferContainerPreview(true,false,true,221),"residency exact boundary");
        InformationOptions options;
        require(!options.bundlePreview&&validInformationOptions(options),"existing bundle default remains OFF");
        options.bundleScale=0.75f;require(validInformationOptions(options),"minimum persisted bundle scale");
        options.bundleScale=2.0f;require(validInformationOptions(options),"maximum persisted bundle scale");
        options.bundleScale=std::numeric_limits<float>::quiet_NaN();require(!validInformationOptions(options),"invalid bundle scale rejected");
        require(containerPreviewScale({9,3,27},1920,1080,1)==1.08f,"Shulker proven scale");
        auto small=containerPreviewScale(bundleGrid(64),480,320,1);
        require(small<0.75f&&small>0.15f,"large Bundle adapts to small viewport");
        require(containerPreviewScale({9,3,27},480,320,1)>=0.75f,"Shulker scale remains unchanged");
        std::array<bool,64> seen{};
        require(validatePreviewEntry(seen,0,1)&&validatePreviewEntry(seen,63,64),"edge entries");
        require(!validatePreviewEntry(seen,0,1),"duplicate source slot");
        require(!validatePreviewEntry(seen,-1,1)&&!validatePreviewEntry(seen,64,1),"invalid source slot");
        require(!validatePreviewEntry(seen,1,0)&&!validatePreviewEntry(seen,1,128),"invalid count");
        require(!seen[1],"bad entries do not mutate publication state");
        PreviewNbtBudget bytes;
        require(bytes.consume(0,PreviewNbtBudget::maximumBytes),"exact byte boundary");
        require(!bytes.consume(1,1),"aggregate byte bound");
        require(bytes.bytes==PreviewNbtBudget::maximumBytes,"rejection leaves byte state unchanged");
        PreviewNbtBudget nodes;bool all=true;
        for(unsigned i=0;i<PreviewNbtBudget::maximumNodes;++i)all&=nodes.consume(1);
        require(all&&!nodes.consume(1),"aggregate node bound");
        PreviewNbtBudget depth;
        require(depth.consume(PreviewNbtBudget::maximumDepth)&&!depth.consume(PreviewNbtBudget::maximumDepth+1),"recursion depth bound");
        require(!depth.arrayBytes(std::numeric_limits<std::size_t>::max(),4),"array multiplication overflow rejected");
        require(bundleDynamicId(0)==0&&bundleDynamicId(4294967295LL)==4294967295u,"dynamic ID edges");
        require(!bundleDynamicId(-1)&&!bundleDynamicId(4294967296LL),"invalid dynamic IDs");
        require(previewIconThreadAllowed(0,1)&&previewIconThreadAllowed(7,7)&&!previewIconThreadAllowed(7,8),"icon UI thread ownership");
        require(inspectionRefreshDue(0,1)&&!inspectionRefreshDue(100,199)&&inspectionRefreshDue(100,200)&&!inspectionRefreshDue(100,99),"bounded refresh monotonicity");
        require(inspectionVisit(true,false,false,false)==InspectionVisit::Defer,"HUD sibling view retains active container inspection");
        require(inspectionVisit(true,true,true,true)==InspectionVisit::Refresh,"owner view refreshes before tooltip draw");
        require(inspectionVisit(true,true,false,true)==InspectionVisit::Retire,"changed exact slot retires");
        require(inspectionVisit(false,false,false,false)==InspectionVisit::Retire,"expired controller retires");
        require(inspectionVisit(true,true,true,false)==InspectionVisit::Retire,"owner view hover ended retires");
        for(auto family:{PreviewFamily::Shulker,PreviewFamily::Bundle}) {
            auto grid=containerGrid(family,family==PreviewFamily::Shulker?27:64);
            require(grid.cells==(family==PreviewFamily::Shulker?27:64),"inventory Shulker and Bundle retain distinct grids");
            require(preferContainerPreview(true,false,true,100),"inventory owns preview ahead of underlying hotbar for each family");
            require(!preferContainerPreview(true,false,true,221)&&!preferContainerPreview(true,false,false,100)
                &&!preferContainerPreview(true,true,true,100),"hotbar admission only after container residency or world expires");
        }
        std::cout<<"Container preview policy checks: "<<checks<<" PASS\n";return 0;
    }catch(std::exception const& error){std::cerr<<error.what()<<'\n';return 1;}
}
