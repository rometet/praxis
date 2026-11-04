#pragma once
#include "core/WeatherSunnyStartupDiagnostics.h"
#include "core/WeatherSunnyImagePolicy.h"
#include "core/WeatherSkySymbolEvidence.h"
#include "platform/TimeChanger.h"
#include <Windows.h>

namespace praxis::companion {
inline constexpr std::array sunnyStartupBindings{
    std::pair{0u,std::uintptr_t{0x230e620}},std::pair{1u,std::uintptr_t{0x46852c0}},
    std::pair{2u,std::uintptr_t{0x6618030}},std::pair{4u,std::uintptr_t{0x46ac2d0}},
    std::pair{5u,std::uintptr_t{0x46aa3d0}}};
inline bool sunnyExecutableMemory(std::uintptr_t address,MEMORY_BASIC_INFORMATION const& memory) noexcept {
    auto const base=reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
    auto const protection=memory.Protect & 0xffu;
    return memory.State==MEM_COMMIT && !(memory.Protect & (PAGE_GUARD|PAGE_NOACCESS))
        && address>=base && address-base<memory.RegionSize
        && (protection==PAGE_EXECUTE || protection==PAGE_EXECUTE_READ
            || protection==PAGE_EXECUTE_READWRITE || protection==PAGE_EXECUTE_WRITECOPY);
}
template<class Environment>
bool sunnyRegisteredVirtualBinding(Environment& environment,unsigned index,std::uintptr_t address) {
    auto const binding=index==2u?TimeChangerSdkBinding::LightData:
        index==5u?TimeChangerSdkBinding::PreRender:TimeChangerSdkBinding::Count;
    if(binding==TimeChangerSdkBinding::Count) return false;
    auto const before=environment.timeChangerBindingEvidence(binding);
    if(!matchesTimeChangerSdkBinding(binding,before,address)) return false;
    MEMORY_BASIC_INFORMATION target{},continuation{};
    if(environment.queryMemory(address,target)!=sizeof(target) || !sunnyExecutableMemory(address,target)) return false;
    if(environment.queryMemory(before.continuation,continuation)!=sizeof(continuation)
        || !sunnyExecutableMemory(before.continuation,continuation)) return false;
    // An SDK chain continuation may change when another hook joins. Accept its
    // current SDK-owned slot only when both snapshots and executable checks agree.
    auto const after=environment.timeChangerBindingEvidence(binding);
    return matchesTimeChangerSdkBinding(binding,after,address)
        && before.epoch==after.epoch && before.continuation==after.continuation;
}
struct SunnySdkViewBindingRecord {
    std::uintptr_t savedTarget{},registeredTarget{},typedTarget{},continuation{};
    std::uint64_t epoch{};
    bool installed{},closed{};
};
inline bool matchesSunnySdkViewBinding(SunnySdkViewBindingRecord const& record,std::uintptr_t candidate) noexcept {
    return candidate && record.epoch && record.installed && !record.closed && record.continuation
        && record.savedTarget==candidate && record.registeredTarget==candidate && record.typedTarget==candidate;
}
template<class Environment>
bool sunnyRegisteredViewBinding(Environment& environment,std::uintptr_t address) {
    // Only the exact formal SDK member target can register this existing hook.
    // It stays an admission-closed passthrough until every Weather guard passes.
    auto const initial=environment.viewBindingEvidence();
    if(!address || initial.closed || !initial.epoch || initial.typedTarget!=address)return false;
    MEMORY_BASIC_INFORMATION target{},continuation{};
    if(environment.queryMemory(address,target)!=sizeof(target) || !sunnyExecutableMemory(address,target))return false;
    if(!environment.prepareViewBinding(address))return false;
    auto const before=environment.viewBindingEvidence();
    if(!matchesSunnySdkViewBinding(before,address) || before.epoch!=initial.epoch)return false;
    if(environment.queryMemory(address,target)!=sizeof(target) || !sunnyExecutableMemory(address,target))return false;
    if(environment.queryMemory(before.continuation,continuation)!=sizeof(continuation)
        || !sunnyExecutableMemory(before.continuation,continuation))return false;
    auto const after=environment.viewBindingEvidence();
    return matchesSunnySdkViewBinding(after,address) && after.epoch==before.epoch
        && after.continuation==before.continuation;
}
// Exact main-image anchors and all native body checks remain unchanged.
// SDK virtual aliases require their registered typed target; the formal SDK
// view target uses the same existing hook with admission closed until success.
// verifiedBase changes only after every retained guard succeeds.
template<class Environment>
bool verifySunnyStartupBinding(Environment& environment,SunnyStartupTrace& trace,std::uintptr_t& verifiedBase) {
    using Stage=SunnyStartupStage;
    trace.begin(Stage::ModuleHandle);
    auto const main=environment.mainHandle();
    if(!main)return trace.fail(1);trace.finish(true);
    trace.begin(Stage::ModuleInfo);unsigned moduleSize{};
    if(!environment.moduleInfo(main,moduleSize))return trace.fail(1);trace.finish(true);
    trace.begin(Stage::ModuleSize);
    if(!sunnyKnownImageSize(moduleSize))return trace.fail(1);trace.finish(true);
    auto const base=main;
    auto read=[&](std::uintptr_t rva,void* output,SIZE_T size){
        if(rva>=moduleSize||size>moduleSize-rva)return false;
        return environment.read(base+rva,output,size);
    };
    IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS64 nt{};
    trace.begin(Stage::Dos);
    if(!read(0,&dos,sizeof dos))return trace.fail(1);
    if(dos.e_magic!=IMAGE_DOS_SIGNATURE)return trace.fail(2);
    if(dos.e_lfanew<=0)return trace.fail(3);trace.finish(true);
    trace.begin(Stage::Nt);
    if(!read(static_cast<unsigned>(dos.e_lfanew),&nt,sizeof nt))return trace.fail(1);
    if(nt.Signature!=IMAGE_NT_SIGNATURE)return trace.fail(2);
    if(nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64)return trace.fail(3);
    if(nt.FileHeader.TimeDateStamp!=sunnyImageTimestamp)return trace.fail(4);
    if(nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC)return trace.fail(5);
    if(nt.OptionalHeader.SizeOfImage!=moduleSize)return trace.fail(6);
    if(nt.OptionalHeader.NumberOfRvaAndSizes<=IMAGE_DIRECTORY_ENTRY_DEBUG)return trace.fail(7);
    trace.finish(true);
    SunnyImageIdentity identity{moduleSize,nt.OptionalHeader.SizeOfImage,nt.FileHeader.Machine,
        nt.OptionalHeader.Magic,nt.FileHeader.TimeDateStamp,nt.OptionalHeader.AddressOfEntryPoint,
        nt.OptionalHeader.SectionAlignment,nt.OptionalHeader.FileAlignment,nt.OptionalHeader.SizeOfHeaders,
        nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress,
        nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size,nt.FileHeader.NumberOfSections};
    bool codeView{};auto const debug=nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
    trace.begin(Stage::DebugLayout);
    if(debug.Size%sizeof(IMAGE_DEBUG_DIRECTORY))return trace.fail(1);
    if(debug.Size/sizeof(IMAGE_DEBUG_DIRECTORY)>64)return trace.fail(2);trace.finish(true);
    for(unsigned i=0;i<debug.Size/sizeof(IMAGE_DEBUG_DIRECTORY);++i) {
        IMAGE_DEBUG_DIRECTORY entry{};std::array<unsigned char,24> cv{};
        trace.begin(Stage::DebugRead,i);
        if(!read(debug.VirtualAddress+i*sizeof(entry),&entry,sizeof entry))return trace.fail(1);trace.finish(true);
        if(entry.Type!=IMAGE_DEBUG_TYPE_CODEVIEW)continue;
        trace.begin(Stage::CodeViewRead,i);
        if(entry.SizeOfData<cv.size())return trace.fail(1);
        if(!read(entry.AddressOfRawData,cv.data(),cv.size()))return trace.fail(2);trace.finish(true);
        unsigned age{};std::memcpy(&age,cv.data()+20,sizeof age);
        codeView=std::memcmp(cv.data(),"RSDS",4)==0&&age==1&&std::memcmp(cv.data()+4,sunnyCodeViewGuid.data(),16)==0;
        identity.codeViewAge=age;std::memcpy(identity.codeViewGuid.data(),cv.data()+4,identity.codeViewGuid.size());
    }
    trace.begin(Stage::CodeViewMatch);
    if(!codeView)return trace.fail(1);trace.finish(true);
    trace.begin(Stage::SectionCount);
    if(nt.FileHeader.NumberOfSections>9)return trace.fail(1);trace.finish(true);
    auto const sectionTable=static_cast<unsigned>(dos.e_lfanew)+sizeof(DWORD)+sizeof(IMAGE_FILE_HEADER)+nt.FileHeader.SizeOfOptionalHeader;
    std::array<SunnyImageSection,9> sectionIdentity{};
    for(unsigned i=0;i<nt.FileHeader.NumberOfSections;++i) {
        IMAGE_SECTION_HEADER section{};trace.begin(Stage::SectionRead,i);
        if(!read(sectionTable+i*sizeof(section),&section,sizeof section))return trace.fail(1);trace.finish(true);
        auto& item=sectionIdentity[i];std::memcpy(item.name.data(),section.Name,item.name.size());
        item.rva=section.VirtualAddress;item.virtualSize=section.Misc.VirtualSize;
        item.rawOffset=section.PointerToRawData;item.rawSize=section.SizeOfRawData;item.characteristics=section.Characteristics;
    }
    trace.begin(Stage::ImportMarker);
    if(identity.imageSize==sunnyPreLoaderImageSize) {
        identity.preLoaderResolveImport=sunnyPreLoaderImportMarker<IMAGE_IMPORT_DESCRIPTOR>(identity.importRva,identity.importSize,read);
        trace.finish(identity.preLoaderResolveImport);
    } else trace.result(SunnyStartupResult::Skipped,1);
    trace.begin(Stage::ImageVariant);
    auto const variant=sunnyImageVariant(identity,sectionIdentity);
    if(variant==SunnyImageVariant::Rejected)return trace.fail(0);
    trace.finish(true,static_cast<int>(variant));
    for(unsigned i=0;i<sunnyStartupBindings.size();++i) {
        auto const [index,rva]=sunnyStartupBindings[i];trace.begin(Stage::Binding,i);
        // Ambient is an unused observer alias, not a product callee/hook.
        // Keep its stable diagnostic slot without resolving an unused target.
        if(index==1u) { trace.result(SunnyStartupResult::Skipped,2);continue; }
        if(rva>std::numeric_limits<std::uintptr_t>::max()-base)return trace.fail(1);
        auto const address=environment.resolve(index);
        if(!address)return trace.fail(2);
        if(classifyWeatherSkyAddress(address,base,moduleSize).status!=WeatherSkyAddressStatus::MainImage) {
            if(!(index==4u ? sunnyRegisteredViewBinding(environment,address)
                : sunnyRegisteredVirtualBinding(environment,index,address))) return trace.fail(3);
            trace.finish(true,1);continue;
        }
        if(address!=base+rva)return trace.fail(4);trace.finish(true);
    }
    for(unsigned i=0;i<sunnyNativeFunctions.size();++i) {
        auto const& function=sunnyNativeFunctions[i];std::array<unsigned char,1024> bytes{};
        std::uintptr_t owner{};trace.begin(Stage::FunctionOwner,i);
        if(!environment.owner(base+function.rva,owner))return trace.fail(1);
        if(owner!=main)return trace.fail(2);trace.finish(true);
        bool executableSection{};
        for(unsigned sectionIndex=0;sectionIndex<nt.FileHeader.NumberOfSections;++sectionIndex) {
            IMAGE_SECTION_HEADER section{};trace.begin(Stage::FunctionSectionRead,i);
            if(!read(sectionTable+sectionIndex*sizeof(section),&section,sizeof section))return trace.fail(static_cast<int>(sectionIndex+1));
            trace.finish(true,static_cast<int>(sectionIndex+1));
            if(function.rva<section.VirtualAddress)continue;
            auto const offset=function.rva-section.VirtualAddress;
            if(offset<section.Misc.VirtualSize&&function.size<=section.Misc.VirtualSize-offset
                &&(section.Characteristics&IMAGE_SCN_MEM_EXECUTE)&&(section.Characteristics&IMAGE_SCN_CNT_CODE))executableSection=true;
        }
        trace.begin(Stage::FunctionRange,i);
        if(!executableSection)return trace.fail(1);trace.finish(true);
        trace.begin(Stage::FunctionRead,i);
        if(function.size>bytes.size())return trace.fail(1);
        if(!read(function.rva,bytes.data(),function.size))return trace.fail(2);trace.finish(true);
        std::uint64_t hash=0xcbf29ce484222325ULL;
        for(unsigned b=0;b<function.size;++b)hash=(hash^bytes[b])*0x100000001b3ULL;
        trace.begin(Stage::FunctionFingerprint,i);
        if(hash!=function.fingerprint)return trace.fail(1);trace.finish(true);
    }
    verifiedBase=base;return true;
}
} // namespace praxis::companion
