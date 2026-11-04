#include "core/WeatherSunnyStartupVerifier.h"
#include "core/RuntimePolicy.h"
#include <fstream>
#include <iostream>
#include <functional>
#include <thread>
#include <vector>
#include <climits>

using namespace praxis::companion;
namespace {
unsigned checks{},cases{};
void check(bool value,char const* label){++checks;if(!value)throw std::runtime_error(label);}
using Stage=SunnyStartupStage;
enum class Fault {
    None,MainMissing,MainThrow,InfoMissing,InfoThrow,ModuleSize,DosMagic,DosOffset,DosNegative,
    NtSignature,Machine,Timestamp,Magic,ImageSize,Directories,DebugRemainder,DebugCount,
    NoCodeView,SmallCodeView,CodeViewMagic,CodeViewAge,CodeViewGuid,SectionCount,
    EntryPoint,SectionAlignment,FileAlignment,HeadersSize,ImportRva,ImportSize,
    SectionName,SectionRva,SectionVirtualSize,SectionRawOffset,SectionRawSize,SectionFlags,
    MarkerName,MarkerOft,MarkerIat,MarkerThunkMissing,MarkerOrdinal,MarkerExtraThunk,
    MarkerSymbol,MarkerDuplicate,MarkerNoTerminator,CodeRange,CodeHead,CodeBody,Debug64
};
struct OwnedImage {
    std::vector<unsigned char> bytes;
    IMAGE_NT_HEADERS64 nt{};
    std::vector<IMAGE_SECTION_HEADER> sections;
    IMAGE_DEBUG_DIRECTORY codeView{};
    unsigned sectionTable{};
    explicit OwnedImage(char const* path) {
        std::ifstream input(path,std::ios::binary|std::ios::ate);
        if(!input)throw std::runtime_error("read-only PE fixture unavailable");
        bytes.resize(static_cast<std::size_t>(input.tellg()));input.seekg(0);
        if(!input.read(reinterpret_cast<char*>(bytes.data()),bytes.size()))throw std::runtime_error("PE fixture short read");
        IMAGE_DOS_HEADER dos{};std::memcpy(&dos,bytes.data(),sizeof dos);
        check(dos.e_magic==IMAGE_DOS_SIGNATURE&&dos.e_lfanew>0,"fixture DOS contract");
        std::memcpy(&nt,bytes.data()+dos.e_lfanew,sizeof nt);
        sectionTable=dos.e_lfanew+sizeof(DWORD)+sizeof(IMAGE_FILE_HEADER)+nt.FileHeader.SizeOfOptionalHeader;
        sections.resize(nt.FileHeader.NumberOfSections);
        for(unsigned i=0;i<sections.size();++i)std::memcpy(&sections[i],bytes.data()+sectionTable+i*sizeof(IMAGE_SECTION_HEADER),sizeof(IMAGE_SECTION_HEADER));
        auto debug=nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
        for(unsigned i=0;i<debug.Size/sizeof(IMAGE_DEBUG_DIRECTORY);++i) {
            IMAGE_DEBUG_DIRECTORY entry{};
            check(read(debug.VirtualAddress+i*sizeof(entry),&entry,sizeof entry),"fixture debug read");
            if(entry.Type==IMAGE_DEBUG_TYPE_CODEVIEW)codeView=entry;
        }
        check(codeView.SizeOfData>=24,"fixture CodeView present");
    }
    bool read(std::uintptr_t rva,void* output,std::size_t size) const {
        if(rva<nt.OptionalHeader.SizeOfHeaders&&size<=nt.OptionalHeader.SizeOfHeaders-rva&&rva+size<=bytes.size()){
            std::memcpy(output,bytes.data()+rva,size);return true;
        }
        for(auto const& section:sections) {
            if(rva<section.VirtualAddress)continue;auto offset=rva-section.VirtualAddress;
            if(offset<section.SizeOfRawData&&size<=section.SizeOfRawData-offset&&section.PointerToRawData+offset+size<=bytes.size()){
                std::memcpy(output,bytes.data()+section.PointerToRawData+offset,size);return true;
            }
        }
        return false;
    }
};
struct Environment {
    OwnedImage const& image;SunnyStartupTrace& trace;
    Fault fault=Fault::None;unsigned reads{},readFail{},readThrow{},ownerFail=UINT_MAX,ownerWrong=UINT_MAX,ownerThrow=UINT_MAX;
    unsigned bindingFault=UINT_MAX,bindingMode{},codeIndex{},sectionIndex{};
    mutable std::array<unsigned,weatherSkySymbols.size()> resolverCalls{};
    std::uintptr_t base=0x140000000;
    std::array<bool,2> sdkOutside{};
    std::array<std::uintptr_t,2> sdkResolver{0x200001000,0x200004000},sdkRegistered=sdkResolver,sdkTyped=sdkResolver;
    std::array<std::uintptr_t,2> sdkContinuation{0x200008000,0x20000b000};
    std::array<bool,2> sdkInstalled{true,true};bool sdkAdmitted=true;
    TimeChangerSdkRegistration sdkRegistration;
    unsigned sdkQueries{},sdkEvidenceReads{},queryFail{},queryShort{},queryThrow{},evidenceThrow{},queryFaultAt=1;
    unsigned pageFault{},recordFault{},recordFaultBinding{};
    std::function<void(unsigned)> duringQuery;
    bool viewOutside{},viewCanPrepare{},viewPrepared{},viewPrepareThrow{},viewPrepareFail{};
    std::uintptr_t viewResolver=0x20000e000;
    SunnySdkViewBindingRecord viewRecord{};
    unsigned viewEvidenceReads{},viewEvidenceThrow{},viewPreparations{};
    std::function<int()> prepareViewHook;
    std::function<void()> afterViewPrepare;
    void registerView() {
        viewOutside=true;viewCanPrepare=true;
        viewRecord.typedTarget=viewResolver;viewRecord.epoch=1;
    }
    SunnySdkViewBindingRecord viewBindingEvidence() {
        if(++viewEvidenceReads==viewEvidenceThrow)throw std::runtime_error("private view binding exception");
        return viewRecord;
    }
    bool prepareViewBinding(std::uintptr_t candidate) {
        ++viewPreparations;
        if(viewPrepareThrow)throw std::runtime_error("private view registration exception");
        if(!viewCanPrepare || viewPrepareFail || candidate!=viewRecord.typedTarget || !prepareViewHook)return false;
        if(viewRecord.closed || (viewRecord.savedTarget && viewRecord.savedTarget!=candidate)
            || (viewRecord.registeredTarget && viewRecord.registeredTarget!=candidate))return false;
        if(!viewRecord.installed) {
            if(prepareViewHook()!=0)return false;
            viewPrepared=true;viewRecord.installed=true;viewRecord.savedTarget=viewRecord.registeredTarget=candidate;
            viewRecord.continuation=0x200012000;
        }
        if(afterViewPrepare)afterViewPrepare();
        return true;
    }
    void registerSdk() {
        sdkOutside={true,true};sdkRegistration.begin();
        sdkRegistration.capture(TimeChangerSdkBinding::LightData,sdkRegistered[0],true);
        sdkRegistration.capture(TimeChangerSdkBinding::PreRender,sdkRegistered[1],true);
        sdkRegistration.finish();
    }
    TimeChangerSdkBindingRecord timeChangerBindingEvidence(TimeChangerSdkBinding binding) {
        ++sdkEvidenceReads;
        if(sdkEvidenceReads==evidenceThrow)throw std::runtime_error("private SDK evidence exception");
        auto const i=static_cast<unsigned>(binding);
        auto result=sdkRegistration.snapshot(binding,sdkRegistered[i],sdkTyped[i],sdkContinuation[i],sdkInstalled[i],sdkAdmitted);
        if(i==recordFaultBinding) {
            if(recordFault==1)++result.savedTarget;if(recordFault==2)result.epoch=0;
            if(recordFault==3)result.binding=TimeChangerSdkBinding::Count;
        }
        return result;
    }
    SIZE_T queryMemory(std::uintptr_t address,MEMORY_BASIC_INFORMATION& memory) {
        ++sdkQueries;
        if(sdkQueries==queryThrow)throw std::runtime_error("private memory-query exception");
        memory.BaseAddress=reinterpret_cast<void*>(address & ~std::uintptr_t{0xfff});
        memory.RegionSize=0x1000;memory.State=MEM_COMMIT;memory.Protect=PAGE_EXECUTE_READ;
        if(sdkQueries==queryFaultAt) {
            if(pageFault==1)memory.State=MEM_RESERVE;if(pageFault==2)memory.State=MEM_FREE;
            if(pageFault==3)memory.Protect=PAGE_NOACCESS;if(pageFault==4)memory.Protect=PAGE_READWRITE;
            if(pageFault==5)memory.Protect|=PAGE_GUARD;if(pageFault==6)memory.RegionSize=0;
            if(pageFault==7)memory.BaseAddress=reinterpret_cast<void*>(address+1);
            if(pageFault>=8&&pageFault<=11)memory.Protect=std::array<DWORD,4>{PAGE_EXECUTE,PAGE_EXECUTE_READ,
                PAGE_EXECUTE_READWRITE,PAGE_EXECUTE_WRITECOPY}[pageFault-8];
        }
        if(duringQuery)duringQuery(sdkQueries);
        if(sdkQueries==queryFail)return 0;
        return sdkQueries==queryShort?sizeof(memory)-1:sizeof(memory);
    }
    Stage stage() const{return trace.entries[trace.used-1].stage;}
    unsigned index() const{return trace.entries[trace.used-1].index;}
    std::uintptr_t mainHandle() const{
        if(fault==Fault::MainThrow)throw std::runtime_error("private module-handle exception");
        return fault==Fault::MainMissing?0:base;
    }
    bool moduleInfo(std::uintptr_t,unsigned& size) const{
        if(fault==Fault::InfoThrow)throw std::runtime_error("private module-info exception");
        size=image.nt.OptionalHeader.SizeOfImage+(fault==Fault::ModuleSize?1:0);return fault!=Fault::InfoMissing;
    }
    bool read(std::uintptr_t address,void* output,std::size_t size) {
        ++reads;if(reads==readThrow)throw std::runtime_error("E:\\private-user\\secret must never be logged");
        if(reads==readFail)return false;
        if(fault==Fault::Debug64&&stage()==Stage::DebugRead){std::memcpy(output,&image.codeView,sizeof image.codeView);return true;}
        auto rva=address-base;
        if(!image.read(rva,output,size))return false;
        if(stage()==Stage::Dos) {
            auto& d=*static_cast<IMAGE_DOS_HEADER*>(output);
            if(fault==Fault::DosMagic)d.e_magic=0;if(fault==Fault::DosOffset)d.e_lfanew=0;if(fault==Fault::DosNegative)d.e_lfanew=-1;
        }
        if(stage()==Stage::Nt) {
            auto& n=*static_cast<IMAGE_NT_HEADERS64*>(output);
            if(fault==Fault::NtSignature)n.Signature=0;if(fault==Fault::Machine)n.FileHeader.Machine=0;
            if(fault==Fault::Timestamp)++n.FileHeader.TimeDateStamp;if(fault==Fault::Magic)n.OptionalHeader.Magic=0;
            if(fault==Fault::ImageSize)++n.OptionalHeader.SizeOfImage;if(fault==Fault::Directories)n.OptionalHeader.NumberOfRvaAndSizes=6;
            if(fault==Fault::DebugRemainder)++n.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG].Size;
            if(fault==Fault::DebugCount)n.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG].Size=65*sizeof(IMAGE_DEBUG_DIRECTORY);
            if(fault==Fault::Debug64)n.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG].Size=64*sizeof(IMAGE_DEBUG_DIRECTORY);
            if(fault==Fault::SectionCount)n.FileHeader.NumberOfSections=10;
            if(fault==Fault::EntryPoint)++n.OptionalHeader.AddressOfEntryPoint;
            if(fault==Fault::SectionAlignment)++n.OptionalHeader.SectionAlignment;
            if(fault==Fault::FileAlignment)++n.OptionalHeader.FileAlignment;
            if(fault==Fault::HeadersSize)++n.OptionalHeader.SizeOfHeaders;
            if(fault==Fault::ImportRva)++n.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
            if(fault==Fault::ImportSize)++n.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size;
        }
        if(stage()==Stage::DebugRead){
            auto& d=*static_cast<IMAGE_DEBUG_DIRECTORY*>(output);
            if(fault==Fault::NoCodeView)d.Type=0;if(fault==Fault::SmallCodeView&&d.Type==IMAGE_DEBUG_TYPE_CODEVIEW)d.SizeOfData=23;
        }
        if(stage()==Stage::CodeViewRead) {
            auto* b=static_cast<unsigned char*>(output);
            if(fault==Fault::CodeViewMagic)b[0]^=1;if(fault==Fault::CodeViewGuid)b[4]^=1;if(fault==Fault::CodeViewAge)b[20]^=1;
        }
        if(stage()==Stage::SectionRead&&index()==sectionIndex) {
            auto& s=*static_cast<IMAGE_SECTION_HEADER*>(output);
            if(fault==Fault::SectionName)s.Name[0]^=1;if(fault==Fault::SectionRva)++s.VirtualAddress;
            if(fault==Fault::SectionVirtualSize)++s.Misc.VirtualSize;if(fault==Fault::SectionRawOffset)++s.PointerToRawData;
            if(fault==Fault::SectionRawSize)++s.SizeOfRawData;if(fault==Fault::SectionFlags)s.Characteristics^=IMAGE_SCN_MEM_EXECUTE;
        }
        if(stage()==Stage::ImportMarker&&size==sizeof(IMAGE_IMPORT_DESCRIPTOR)) {
            auto& descriptor=*static_cast<IMAGE_IMPORT_DESCRIPTOR*>(output);
            std::array<char,sizeof("PreLoader.dll")> name{};
            bool const target=descriptor.Name&&image.read(descriptor.Name,name.data(),name.size())
                &&std::memcmp(name.data(),"PreLoader.dll",name.size())==0;
            if(target&&fault==Fault::MarkerOft)descriptor.OriginalFirstThunk=0;
            if(target&&fault==Fault::MarkerIat)descriptor.FirstThunk=0;
            if(fault==Fault::MarkerDuplicate&&!descriptor.Name&&!descriptor.OriginalFirstThunk&&!descriptor.FirstThunk) {
                auto imports=image.nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
                for(unsigned i=0;i<imports.Size/sizeof(descriptor);++i) {
                    IMAGE_IMPORT_DESCRIPTOR candidate{};image.read(imports.VirtualAddress+i*sizeof(candidate),&candidate,sizeof candidate);
                    if(candidate.Name&&image.read(candidate.Name,name.data(),name.size())
                        &&std::memcmp(name.data(),"PreLoader.dll",name.size())==0){descriptor=candidate;break;}
                }
            }
            if(fault==Fault::MarkerNoTerminator&&!descriptor.Name&&!descriptor.OriginalFirstThunk&&!descriptor.FirstThunk)
                descriptor.Name=image.nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
        }
        if(stage()==Stage::ImportMarker&&size==sizeof(std::uint64_t)*2) {
            auto* thunk=static_cast<std::uint64_t*>(output);
            if(fault==Fault::MarkerThunkMissing)thunk[0]=0;
            if(fault==Fault::MarkerOrdinal)thunk[0]|=1ULL<<63;
            if(fault==Fault::MarkerExtraThunk)thunk[1]=thunk[0];
        }
        if(fault==Fault::MarkerSymbol&&stage()==Stage::ImportMarker&&size==sizeof("pl_resolve_symbol"))
            static_cast<char*>(output)[0]='X';
        if(fault==Fault::MarkerName&&stage()==Stage::ImportMarker&&size==sizeof("PreLoader.dll")){
            if(std::memcmp(output,"PreLoader.dll",size)==0)static_cast<char*>(output)[0]='X';
        }
        if(fault==Fault::CodeRange&&stage()==Stage::FunctionSectionRead&&index()==codeIndex)
            static_cast<IMAGE_SECTION_HEADER*>(output)->Characteristics&=~IMAGE_SCN_MEM_EXECUTE;
        if(stage()==Stage::FunctionRead&&index()==codeIndex) {
            auto* b=static_cast<unsigned char*>(output);
            if(fault==Fault::CodeHead)b[0]^=1;if(fault==Fault::CodeBody)b[size/2]^=1;
        }
        return true;
    }
    std::uintptr_t resolve(unsigned symbol) const {
        ++resolverCalls[symbol];
        unsigned position{};for(auto [index,rva]:sunnyStartupBindings){
            if(index==symbol){
                if(position==bindingFault){
                    if(bindingMode==0)return 0;if(bindingMode==1)return base-1;
                    if(bindingMode==2)return base+rva+1;
                    throw std::runtime_error("resolver exception contains private path");
                }
                if(symbol==2u&&sdkOutside[0])return sdkResolver[0];
                if(symbol==5u&&sdkOutside[1])return sdkResolver[1];
                if(symbol==4u&&viewOutside)return viewResolver;
                return base+rva;
            }++position;
        }return 0;
    }
    bool owner(std::uintptr_t address,std::uintptr_t& result) const {
        unsigned i{};for(;i<sunnyNativeFunctions.size();++i)if(address==base+sunnyNativeFunctions[i].rva)break;
        if(i==ownerThrow)throw std::runtime_error("owner exception");
        result=i==ownerWrong?base-1:base;return i!=ownerFail;
    }
};
constexpr unsigned weatherBit=1u<<static_cast<unsigned>(Feature::WeatherChanger);
void ready() {
    runtimeReady=true;weatherNativeAdmission=true;worldGeneration=17;availableFeatures=0x7fffffffu;availableRenderPasses=0xffff;
    auto s=std::make_shared<Settings>();s->enabled[static_cast<unsigned>(Feature::WeatherChanger)]=true;
    s->enabled[static_cast<unsigned>(Feature::TimeChanger)]=true;s->enabled[static_cast<unsigned>(Feature::Fullbright)]=true;
    s->renderMask=passBit(RenderPass::Weather)|passBit(RenderPass::Fog);publishedSettings.store(s);
}
struct Hook {std::function<int()> install;bool active{};};
struct Result {SunnyStartupTrace trace;unsigned reads{},installs{},mask{},ambientQueries{},sdkQueries{},sdkEvidenceReads{},viewPreparations{},viewEvidenceReads{};bool admitted{};std::uintptr_t verifiedBase{};std::string message;};
template<class Configure>
Result run(OwnedImage const& image,Configure&& configure,bool expected,Stage failure=Stage::Capability,
    unsigned failureIndex=0,int hookFault=-1,bool hookThrow=false,int hookCode=-1) {
    ++cases;ready();auto const state=settings();Result result;Environment env{image,result.trace};configure(env);
    std::array<Hook,7> sunny;std::array<Hook,4> view;
    auto initialize=[&](auto& hooks,unsigned offset){
        for(unsigned i=0;i<hooks.size();++i)hooks[i].install=[&,position=i+offset]{
            ++result.installs;
            if(static_cast<int>(position)==hookFault){if(hookThrow)throw std::runtime_error("private hook exception");return hookCode;}
            return 0;};
    };initialize(sunny,0);initialize(view,7);
    env.prepareViewHook=[&] {
        auto& hook=view[0];
        if(!hook.active)hook.active=hook.install()==0;
        if(hook.active)result.mask|=1u;
        return hook.active?0:-1;
    };
    result.verifiedBase=23;
    bool const startup=diagnosedSunnyInstall(result.trace,
        [&]{return verifySunnyStartupBinding(env,result.trace,result.verifiedBase);},
        [&]{if(!installSunnyStartupHooks(sunny,result.trace,Stage::SunnyHook,[](unsigned){}))return false;
            return installSunnyStartupHooks(view,result.trace,Stage::ViewHook,[&](unsigned i){result.mask|=1u<<i;});});
    result.admitted=weatherNativeAdmission.load();result.reads=env.reads;
    result.ambientQueries=env.resolverCalls[1];
    result.sdkQueries=env.sdkQueries;result.sdkEvidenceReads=env.sdkEvidenceReads;
    result.viewPreparations=env.viewPreparations;result.viewEvidenceReads=env.viewEvidenceReads;
    check(result.ambientQueries==0,"unused ambient is never resolved, including fault and exception cases");
    check(startup,"native startup always continues");
    check(result.admitted==expected,"Weather admission matches guard/install result");
    check(availableFeatures.load()==(expected?0x7fffffffu:(0x7fffffffu&~weatherBit)),"only Weather capability changes");
    check(runtimeReady&&worldGeneration==17&&availableRenderPasses==0xffff&&settings()==state,"other features/settings/generation retained");
    RuntimeConfig config;config.weatherChanger=true;config.fullbright=true;config.timeChanger=true;
    auto effective=runtimeSettings(config,availableRenderPasses.load(),availableFeatures.load());
    check(effective.enabled[static_cast<unsigned>(Feature::WeatherChanger)]==expected
        &&effective.enabled[static_cast<unsigned>(Feature::Fullbright)]&&effective.enabled[static_cast<unsigned>(Feature::TimeChanger)],
        "production runtimeSettings masks unavailable Weather and retains TimeChanger/Fullbright");
    check(!result.trace.overflow,"bounded startup trace fits");
    for(unsigned i=0;i<result.trace.used;++i)check(result.trace.entries[i].result!=SunnyStartupResult::Pending,"every attempted operation has terminal diagnostic");
    if(!expected){
        auto first=std::find_if(result.trace.entries.begin(),result.trace.entries.begin()+result.trace.used,
            [](auto const& e){return e.result==SunnyStartupResult::Fail||e.result==SunnyStartupResult::Exception;});
        check(first!=result.trace.entries.begin()+result.trace.used,"first failure diagnostic exists");
        if(failure!=Stage::Capability)check(first->stage==failure&&first->index==failureIndex,"first failing stage/index attributed");
        check(result.installs==0||hookFault>=0||env.viewPrepared,"failed verifier installs only the exact admission-closed SDK view passthrough");
        if(result.installs==0)check(result.verifiedBase==23,"rejected verifier never publishes native base");
    } else {
        check(result.installs==11&&result.mask==15&&result.verifiedBase==env.base,"all existing hooks admitted after verifier");
        for(unsigned symbol:{0u,2u,4u,5u})check(env.resolverCalls[symbol]==1,"each remaining required binding resolved exactly once");
    }
    SunnyStartupOnce once;unsigned emissions{};std::string message;
    auto sink=[&](std::string_view text){++emissions;message.assign(text);};
    once.emit(result.trace,result.admitted,sink);once.emit(result.trace,result.admitted,sink);
    check(emissions==1,"startup diagnostic emitted once");
    check(message.find("WeatherSunnyStartup available=")==0&&message.find("trace-overflow=0")!=std::string::npos,"fixed bounded summary");
    check(message.find("0x")==std::string::npos&&message.find("\\")==std::string::npos&&message.find("private")==std::string::npos,
        "no absolute addresses, user paths, exception text");
    SunnyStartupOnce throwing;unsigned attempted{};throwing.emit(result.trace,result.admitted,[&](auto){++attempted;throw 1;});
    throwing.emit(result.trace,result.admitted,[&](auto){++attempted;});
    check(attempted==1,"throwing diagnostic sink is swallowed and attempted only once");
    check(weatherNativeAdmission.load()==expected&&runtimeReady&&settings()==state,
        "diagnostic exception never changes availability or unrelated runtime state");
    result.message=std::move(message);
    return result;
}
void allCases(OwnedImage const& image) {
    auto native=run(image,[](auto&){},true);
    std::cout<<"OWNED_PE_FIXTURE_ONLY "<<(image.sections.size()==9?"PreLoader":"original")<<": "<<native.message<<'\n';
    for(auto [fault,stage]:std::array<std::pair<Fault,Stage>,24>{{
        std::pair{Fault::MainMissing,Stage::ModuleHandle},{Fault::InfoMissing,Stage::ModuleInfo},{Fault::ModuleSize,Stage::ModuleSize},
        {Fault::DosMagic,Stage::Dos},{Fault::DosOffset,Stage::Dos},{Fault::DosNegative,Stage::Dos},
        {Fault::NtSignature,Stage::Nt},{Fault::Machine,Stage::Nt},{Fault::Timestamp,Stage::Nt},{Fault::Magic,Stage::Nt},
        {Fault::ImageSize,Stage::Nt},{Fault::Directories,Stage::Nt},{Fault::DebugRemainder,Stage::DebugLayout},{Fault::DebugCount,Stage::DebugLayout},
        {Fault::NoCodeView,Stage::CodeViewMatch},{Fault::SmallCodeView,Stage::CodeViewRead},{Fault::CodeViewMagic,Stage::CodeViewMatch},
        {Fault::CodeViewAge,Stage::CodeViewMatch},{Fault::CodeViewGuid,Stage::CodeViewMatch},{Fault::SectionCount,Stage::SectionCount},
        {Fault::EntryPoint,Stage::ImageVariant},{Fault::SectionAlignment,Stage::ImageVariant},{Fault::FileAlignment,Stage::ImageVariant},
        {Fault::HeadersSize,Stage::ImageVariant}}}) {
        auto r=run(image,[&](auto& e){e.fault=fault;},false); // CodeView ordinal depends on actual PE.
        bool found{};for(unsigned i=0;i<r.trace.used;++i)if((r.trace.entries[i].result==SunnyStartupResult::Fail)
            &&r.trace.entries[i].stage==stage)found=true;check(found,"PE predicate failure attributed");
    }
    run(image,[](auto& e){e.fault=Fault::MainThrow;},false,Stage::ModuleHandle);
    run(image,[](auto& e){e.fault=Fault::InfoThrow;},false,Stage::ModuleInfo);
    for(auto fault:{Fault::ImportRva,Fault::ImportSize})
        run(image,[&](auto& e){e.fault=fault;},false);
    for(unsigned section=0;section<image.sections.size();++section)
        for(auto fault:{Fault::SectionName,Fault::SectionRva,Fault::SectionVirtualSize,Fault::SectionRawOffset,Fault::SectionRawSize,Fault::SectionFlags})
            run(image,[&](auto& e){e.fault=fault;e.sectionIndex=section;},false,Stage::ImageVariant);
    // Fault every read reached by the production verifier, including reads
    // nested inside the unchanged import policy and repeated section inspection.
    for(unsigned read=1;read<=native.reads;++read){
        run(image,[&](auto& e){e.readFail=read;},false);
        auto thrown=run(image,[&](auto& e){e.readThrow=read;},false);
        check(std::any_of(thrown.trace.entries.begin(),thrown.trace.entries.begin()+thrown.trace.used,
            [](auto const& e){return e.result==SunnyStartupResult::Exception;}),"C++ read exception diagnosed");
    }
    for(unsigned i=0;i<sunnyStartupBindings.size();++i)for(unsigned mode=0;mode<4;++mode) {
        bool const ambient=sunnyStartupBindings[i].first==1u;
        auto r=run(image,[&](auto& e){e.bindingFault=i;e.bindingMode=mode;},ambient,Stage::Binding,i);
        auto it=std::find_if(r.trace.entries.begin(),r.trace.entries.begin()+r.trace.used,[&](auto const& e){return e.stage==Stage::Binding&&e.index==i;});
        if(ambient)check(it->result==SunnyStartupResult::Skipped&&it->code==2&&r.ambientQueries==0
            &&r.message.find("sdk.ambient=skipped(2)")!=std::string::npos,
            "missing/outside/wrong/throwing ambient is never queried and keeps its fixed skipped diagnostic");
        else check(it->result==(mode==3?SunnyStartupResult::Exception:SunnyStartupResult::Fail)
                &&it->code==(mode==3?1:static_cast<int>(mode+2)),"remaining required binding rejection and exception classification unchanged");
    }
    run(image,[](auto& e){e.base=std::numeric_limits<std::uintptr_t>::max()-4095;},false,Stage::Binding,0);
    for(unsigned i=0;i<sunnyNativeFunctions.size();++i) {
        run(image,[&](auto& e){e.ownerFail=i;},false,Stage::FunctionOwner,i);
        run(image,[&](auto& e){e.ownerWrong=i;},false,Stage::FunctionOwner,i);
        run(image,[&](auto& e){e.ownerThrow=i;},false,Stage::FunctionOwner,i);
        run(image,[&](auto& e){e.fault=Fault::CodeRange;e.codeIndex=i;},false,Stage::FunctionRange,i);
        for(auto fault:{Fault::CodeHead,Fault::CodeBody})
            run(image,[&](auto& e){e.fault=fault;e.codeIndex=i;},false,Stage::FunctionFingerprint,i);
    }
    for(unsigned hook=0;hook<11;++hook)for(int status:{-1,5,INT_MIN}) {
        auto r=run(image,[](auto&){},false,hook<7?Stage::SunnyHook:Stage::ViewHook,hook<7?hook:hook-7,hook,false,status);
        check(r.installs==hook+1,"installer stops at first failing existing hook");
        check(r.mask==(hook<7?0u:(1u<<(hook-7))-1),"partial successful diagnostic hook mask retained");
    }
    for(unsigned hook=0;hook<11;++hook)
        run(image,[](auto&){},false,hook<7?Stage::SunnyHook:Stage::ViewHook,hook<7?hook:hook-7,hook,true);
    run(image,[](auto& e){e.fault=Fault::Debug64;},true);
    if(image.nt.OptionalHeader.SizeOfImage==sunnyPreLoaderImageSize)
        for(auto fault:{Fault::MarkerName,Fault::MarkerOft,Fault::MarkerIat,Fault::MarkerThunkMissing,
                Fault::MarkerOrdinal,Fault::MarkerExtraThunk,Fault::MarkerSymbol,Fault::MarkerDuplicate,Fault::MarkerNoTerminator})
            run(image,[&](auto& e){e.fault=fault;},false,Stage::ImportMarker);
}
void registeredSdkCases(OwnedImage const& image) {
    auto accepted=run(image,[](auto& e){e.registerSdk();},true);
    check(accepted.sdkQueries==4&&accepted.sdkEvidenceReads==4,"two registered SDK targets and continuations checked and revalidated");
    check(accepted.message.find("sdk.light-data=pass(1)")!=std::string::npos
        &&accepted.message.find("sdk.pre-render=pass(1)")!=std::string::npos,"SDK-managed path reported by existing fixed startup trace");
    for(unsigned binding=0;binding<2;++binding) {
        unsigned const position=binding?4:2;
        auto select=[&](auto& e){e.registerSdk();e.sdkOutside[1-binding]=false;};
        for(unsigned fault=0;fault<12;++fault) {
            auto r=run(image,[&](auto& e){
                select(e);
                if(fault==0)++e.sdkResolver[binding];if(fault==1)++e.sdkRegistered[binding];
                if(fault==2)++e.sdkTyped[binding];if(fault==3)e.sdkContinuation[binding]=0;
                if(fault==4)e.sdkInstalled[binding]=false;if(fault==5)e.sdkAdmitted=false;
                if(fault==6)e.sdkRegistration.retire();if(fault==7)e.sdkRegistration.begin();
                if(fault>=8&&fault<=10){e.recordFault=fault-7;e.recordFaultBinding=binding;}
                if(fault==11){e.sdkRegistration.begin();e.sdkRegistered[binding]++;
                    e.sdkRegistration.capture(TimeChangerSdkBinding::LightData,e.sdkRegistered[0],true);
                    e.sdkRegistration.capture(TimeChangerSdkBinding::PreRender,e.sdkRegistered[1],true);e.sdkRegistration.finish();}
            },false,Stage::Binding,position);
            check(r.sdkQueries==0,"untrusted registration rejected before executable-page queries");
        }
        for(unsigned at=1;at<=2;++at) {
            for(unsigned fault=1;fault<=7;++fault)
                run(image,[&](auto& e){select(e);e.queryFaultAt=at;e.pageFault=fault;},false,Stage::Binding,position);
            for(unsigned protection=8;protection<=11;++protection)
                run(image,[&](auto& e){select(e);e.queryFaultAt=at;e.pageFault=protection;},true);
            for(unsigned fault=0;fault<3;++fault)
                run(image,[&](auto& e){select(e);if(fault==0)e.queryFail=at;if(fault==1)e.queryShort=at;if(fault==2)e.queryThrow=at;},false,Stage::Binding,position);
            for(unsigned transition=0;transition<5;++transition)
                run(image,[&](auto& e){select(e);e.duringQuery=[&,at,transition](unsigned query){if(query!=at)return;
                    if(transition==0)e.sdkRegistration.retire();
                    if(transition==1)e.sdkAdmitted=false;
                    if(transition==2)++e.sdkContinuation[binding];
                    if(transition==3)e.sdkRegistration.begin();
                    if(transition==4){e.sdkRegistration.begin();
                        e.sdkRegistration.capture(TimeChangerSdkBinding::LightData,e.sdkRegistered[0],true);
                        e.sdkRegistration.capture(TimeChangerSdkBinding::PreRender,e.sdkRegistered[1],true);e.sdkRegistration.finish();}
                };},false,Stage::Binding,position);
        }
        for(unsigned read=1;read<=2;++read)
            run(image,[&](auto& e){select(e);e.evidenceThrow=read;},false,Stage::Binding,position);
        run(image,[&](auto& e){select(e);e.sdkContinuation[binding]+=0x100000;},true); // SDK updates its own chain slot.
        auto wrongMain=run(image,[&](auto& e){select(e);e.bindingFault=position;e.bindingMode=2;},false,Stage::Binding,position);
        check(wrongMain.sdkQueries==0&&wrongMain.sdkEvidenceReads==0,"SDK evidence never overrides an incorrect inside-main RVA");
    }
    run(image,[](auto& e){e.registerSdk();e.registerSdk();},true); // Retained installation, identical targets.
    for(unsigned partial=0;partial<4;++partial)
        run(image,[&](auto& e){e.sdkOutside={true,true};e.sdkRegistration.begin();
            if(partial!=0)e.sdkRegistration.capture(TimeChangerSdkBinding::LightData,e.sdkRegistered[0],partial!=3);
            if(partial==2)e.sdkRegistration.capture(TimeChangerSdkBinding::PreRender,e.sdkRegistered[1],true);
            if(partial==2)e.sdkRegistration.retire();e.sdkRegistration.finish();
        },false,Stage::Binding,2);
    run(image,[](auto& e){e.registerSdk();e.sdkRegistration.retire();e.registerSdk();},false,Stage::Binding,2);
    run(image,[](auto& e){e.registerSdk();e.fault=Fault::Timestamp;},false,Stage::Nt);
    for(unsigned binding:{0u,3u})
        run(image,[&](auto& e){e.registerSdk();e.bindingFault=binding;e.bindingMode=1;},false,Stage::Binding,binding);
    for(unsigned function=0;function<sunnyNativeFunctions.size();++function) {
        run(image,[&](auto& e){e.registerSdk();e.ownerWrong=function;},false,Stage::FunctionOwner,function);
        for(auto fault:{Fault::CodeHead,Fault::CodeBody})
            run(image,[&](auto& e){e.registerSdk();e.codeIndex=function;e.fault=fault;},false,Stage::FunctionFingerprint,function);
    }
    for(unsigned hook=0;hook<11;++hook) {
        for(int status:{-1,5,INT_MIN})
            run(image,[](auto& e){e.registerSdk();},false,hook<7?Stage::SunnyHook:Stage::ViewHook,
                hook<7?hook:hook-7,hook,false,status);
        run(image,[](auto& e){e.registerSdk();},false,hook<7?Stage::SunnyHook:Stage::ViewHook,
            hook<7?hook:hook-7,hook,true);
    }
}
void registeredViewCases(OwnedImage const& image) {
    auto accepted=run(image,[](auto& e){e.registerView();},true);
    check(accepted.viewPreparations==1 && accepted.viewEvidenceReads==3 && accepted.sdkQueries==3,
        "formal exterior view registered once and target/origin pages revalidated");
    check(accepted.message.find("sdk.view=pass(1)")!=std::string::npos
        && accepted.message.find("hook.weather.view=retained(0)")!=std::string::npos,
        "pre-registered view reused by the existing Weather installer");
    run(image,[](auto& e){e.registerSdk();e.registerView();},true);
    for(unsigned fault=0;fault<9;++fault) {
        auto r=run(image,[&](auto& e){e.registerView();
            if(fault==0)++e.viewRecord.typedTarget;if(fault==1)e.viewRecord.epoch=0;
            if(fault==2)e.viewRecord.closed=true;if(fault==3)e.viewCanPrepare=false;
            if(fault==4)e.viewPrepareFail=true;if(fault==5)e.viewPrepareThrow=true;
            if(fault==6)e.viewRecord.savedTarget=e.viewResolver+1;
            if(fault==7)e.viewRecord.registeredTarget=e.viewResolver+1;
            if(fault==8)e.viewEvidenceThrow=1;
        },false,Stage::Binding,3);
        check(r.verifiedBase==23,"untrusted view never publishes native image base");
    }
    for(unsigned fault=0;fault<7;++fault)
        run(image,[&](auto& e){e.registerView();e.afterViewPrepare=[&,fault]{
            if(fault==0)++e.viewRecord.savedTarget;if(fault==1)++e.viewRecord.registeredTarget;
            if(fault==2)++e.viewRecord.typedTarget;if(fault==3)e.viewRecord.continuation=0;
            if(fault==4)e.viewRecord.installed=false;if(fault==5)e.viewRecord.closed=true;
            if(fault==6)++e.viewRecord.epoch;
        };},false,Stage::Binding,3);
    for(unsigned at=1;at<=3;++at) {
        for(unsigned fault=1;fault<=7;++fault)
            run(image,[&](auto& e){e.registerView();e.queryFaultAt=at;e.pageFault=fault;},false,Stage::Binding,3);
        for(unsigned fault=0;fault<3;++fault)
            run(image,[&](auto& e){e.registerView();if(fault==0)e.queryFail=at;
                if(fault==1)e.queryShort=at;if(fault==2)e.queryThrow=at;},false,Stage::Binding,3);
        for(unsigned protection=8;protection<=11;++protection)
            run(image,[&](auto& e){e.registerView();e.queryFaultAt=at;e.pageFault=protection;},true);
        for(unsigned fault=0;fault<3;++fault)
            run(image,[&](auto& e){e.registerView();e.duringQuery=[&,at,fault](unsigned q){if(q!=at)return;
                if(fault==0)e.viewRecord.closed=true;if(fault==1)++e.viewRecord.epoch;
                if(fault==2 && q>1)++e.viewRecord.continuation;
            };},fault==2 && at==1,Stage::Binding,3);
    }
    for(unsigned read=2;read<=3;++read)
        run(image,[&](auto& e){e.registerView();e.viewEvidenceThrow=read;},false,Stage::Binding,3);
    run(image,[](auto& e){e.registerView();e.afterViewPrepare=[&]{e.viewRecord.continuation+=0x10000;};},true);
    auto wrongMain=run(image,[](auto& e){e.registerView();e.bindingFault=3;e.bindingMode=2;},false,Stage::Binding,3);
    check(wrongMain.viewPreparations==0,"registered SDK view never overrides a wrong inside-image RVA");
    auto badPe=run(image,[](auto& e){e.registerView();e.fault=Fault::Timestamp;},false,Stage::Nt);
    check(badPe.viewPreparations==0,"PE guards precede view registration");
    for(unsigned helper=0;helper<4;++helper) {
        auto failed=run(image,[&](auto& e){e.registerView();e.ownerWrong=helper;},false,Stage::FunctionOwner,helper);
        check(failed.installs==1 && !failed.admitted && failed.verifiedBase==23,
            "later native rejection retains only admission-closed view passthrough");
        run(image,[&](auto& e){e.registerView();e.codeIndex=helper;e.fault=Fault::CodeBody;},false,Stage::FunctionFingerprint,helper);
    }
    for(unsigned hook=0;hook<11;++hook) {
        auto const stage=hook==7?Stage::Binding:hook<7?Stage::SunnyHook:Stage::ViewHook;
        unsigned const index=hook==7?3:hook<7?hook:hook-7;
        for(int status:{-1,5,INT_MIN})
            run(image,[](auto& e){e.registerView();},false,stage,index,hook,false,status);
        run(image,[](auto& e){e.registerView();},false,stage,index,hook,true);
    }
}
void retainedAndConcurrent() {
    TimeChangerSdkRegistration registration;
    auto const invalid=static_cast<TimeChangerSdkBinding>(255);
    auto record=registration.snapshot(invalid,1,1,2,true,true);
    check(!matchesTimeChangerSdkBinding(invalid,record,1),"unknown SDK binding is never eligible");
    SunnyStartupTrace trace;unsigned calls{},active{};
    std::array<Hook,2> hooks{{{[&]{++calls;return 0;},true},{[&]{++calls;return 0;},false}}};
    check(installSunnyStartupHooks(hooks,trace,Stage::ViewHook,[&](unsigned){++active;}),"retained hook path succeeds");
    check(calls==1&&active==1&&trace.entries[0].result==SunnyStartupResult::Retained,"retained hook not reinstalled or republished");
    SunnyStartupOnce once;std::atomic_uint emissions{};std::vector<std::thread> workers;
    for(unsigned i=0;i<16;++i)workers.emplace_back([&]{once.emit(trace,true,[&](auto){++emissions;});});
    for(auto& worker:workers)worker.join();check(emissions==1,"concurrent emission attempted once");
    static_assert([]{for(auto const& f:sunnyNativeFunctions)if(f.size>1024)return false;return true;}(),
        "oversize read branch is unreachable for immutable pinned function sizes");
}
}
int main(int argc,char** argv) {
    try {
        if(argc!=3)throw std::runtime_error("two read-only original/launch PE fixtures required");
        OwnedImage original(argv[1]),launch(argv[2]);allCases(original);allCases(launch);
        registeredSdkCases(original);registeredSdkCases(launch);
        registeredViewCases(original);registeredViewCases(launch);retainedAndConcurrent();
        std::cout<<"WeatherStartupTests PASS "<<checks<<" checks; "<<cases<<" product-path fault/success cases; "
            <<"PE fixture reads only; OS/SDK resolver/hook adapters simulated; Minecraft/visual behavior NOT_RUN\n";return 0;
    }catch(std::exception const& e){std::cerr<<"FAIL "<<checks<<": "<<e.what()<<'\n';return 1;}
}
