#pragma once
#include "core/WeatherSunnyIsolationPolicy.h"
#include <atomic>
#include <charconv>
#include <cstring>
#include <string_view>

namespace praxis::companion {
enum class SunnyStartupStage : unsigned char {
    ModuleHandle, ModuleInfo, ModuleSize, Dos, Nt, DebugLayout, DebugRead,
    CodeViewRead, CodeViewMatch, SectionCount, SectionRead, ImportMarker, ImageVariant,
    Binding, FunctionOwner, FunctionSectionRead, FunctionRange, FunctionRead,
    FunctionFingerprint, SunnyHook, ViewHook, Capability
};
enum class SunnyStartupResult : unsigned char { Pending, Pass, Fail, Exception, Retained, Skipped };
struct SunnyStartupEntry {
    SunnyStartupStage stage{};
    unsigned char index{};
    SunnyStartupResult result = SunnyStartupResult::Pending;
    int code{};
};
// Fixed labels and integer codes only. No addresses, paths, exception text,
// native data, pointer ownership or borrowed render data enters this record.
struct SunnyStartupTrace {
    std::array<SunnyStartupEntry,256> entries{};
    unsigned used{};
    bool overflow{};
    void begin(SunnyStartupStage stage, unsigned index=0) noexcept {
        if (used==entries.size()) { overflow=true; return; }
        entries[used++]={stage,static_cast<unsigned char>(index),SunnyStartupResult::Pending,0};
    }
    void finish(bool passed, int code=0) noexcept {
        if (used && !overflow) { entries[used-1].result=passed?SunnyStartupResult::Pass:SunnyStartupResult::Fail; entries[used-1].code=code; }
    }
    bool fail(int code) noexcept { finish(false,code); return false; }
    void result(SunnyStartupResult value,int code=0) noexcept {
        if (used && !overflow) { entries[used-1].result=value; entries[used-1].code=code; }
    }
    void exception() noexcept { result(SunnyStartupResult::Exception,1); } // C++ exception; never what().
};
inline std::string_view sunnyStartupResultLabel(SunnyStartupResult result) noexcept {
    constexpr std::array labels{"pending","pass","fail","exception","retained","skipped"};
    auto i=static_cast<unsigned>(result);return i<labels.size()?labels[i]:"invalid";
}
struct SunnyStartupMessage {
    std::array<char,16384> bytes{};
    unsigned used{};
    bool overflow{};
    void append(std::string_view text) noexcept {
        if (text.size()>bytes.size()-used) { overflow=true; return; }
        std::memcpy(bytes.data()+used,text.data(),text.size());used+=static_cast<unsigned>(text.size());
    }
    void number(int code) noexcept {
        std::array<char,32> value{};auto end=std::to_chars(value.data(),value.data()+value.size(),code);
        if (end.ec==std::errc{}) append({value.data(),static_cast<std::size_t>(end.ptr-value.data())});
        else append("invalid");
    }
    std::string_view view() const noexcept { return {bytes.data(),used}; }
};
inline void sunnyStartupLabel(SunnyStartupMessage& message,SunnyStartupEntry const& entry) noexcept {
    auto indexed=[&](std::string_view label){message.append(label);message.append(".");message.number(entry.index);};
    auto helper=[&](std::string_view suffix){
        constexpr std::array names{"color","cloud","fog","sky-brightness"};
        message.append("helper.");message.append(entry.index<names.size()?names[entry.index]:"invalid");
        message.append(".");message.append(suffix);
    };
    switch(entry.stage) {
    case SunnyStartupStage::ModuleHandle:message.append("module.handle");break;
    case SunnyStartupStage::ModuleInfo:message.append("module.info");break;
    case SunnyStartupStage::ModuleSize:message.append("module.size");break;
    case SunnyStartupStage::Dos:message.append("pe.dos");break;
    case SunnyStartupStage::Nt:message.append("pe.nt");break;
    case SunnyStartupStage::DebugLayout:message.append("pe.debug-layout");break;
    case SunnyStartupStage::DebugRead:indexed("pe.debug-read");break;
    case SunnyStartupStage::CodeViewRead:indexed("pe.codeview-read");break;
    case SunnyStartupStage::CodeViewMatch:message.append("pe.codeview-match");break;
    case SunnyStartupStage::SectionCount:message.append("pe.section-count");break;
    case SunnyStartupStage::SectionRead:indexed("pe.section-read");break;
    case SunnyStartupStage::ImportMarker:message.append("pe.preloader-import");break;
    case SunnyStartupStage::ImageVariant:message.append("pe.variant");break;
    case SunnyStartupStage::Binding: {
        constexpr std::array names{"sky-darken","ambient","light-data","view","pre-render"};
        message.append("sdk.");message.append(entry.index<names.size()?names[entry.index]:"invalid");break;
    }
    case SunnyStartupStage::FunctionOwner:helper("owner");break;
    case SunnyStartupStage::FunctionSectionRead:helper("section-read");break;
    case SunnyStartupStage::FunctionRange:helper("range");break;
    case SunnyStartupStage::FunctionRead:helper("read");break;
    case SunnyStartupStage::FunctionFingerprint:helper("fingerprint");break;
    case SunnyStartupStage::SunnyHook: {
        constexpr std::array names{"color","cloud","fog","sky-brightness","pre-render","light-data","clock"};
        message.append("hook.sunny.");message.append(entry.index<names.size()?names[entry.index]:"invalid");break;
    }
    case SunnyStartupStage::ViewHook: {
        constexpr std::array names{"view","weather-move","sky-move","rain-splash"};
        message.append("hook.weather.");message.append(entry.index<names.size()?names[entry.index]:"invalid");break;
    }
    case SunnyStartupStage::Capability:message.append("capability.weather");break;
    }
}
class SunnyStartupOnce {
    std::atomic_flag attempted_=ATOMIC_FLAG_INIT;
public:
    template<class Sink> void emit(SunnyStartupTrace const& trace,bool available,Sink&& sink) noexcept {
        if (attempted_.test_and_set(std::memory_order_relaxed)) return;
        try {
            SunnyStartupMessage message;message.append("WeatherSunnyStartup available=");message.append(available?"1":"0");
            for(unsigned i=0;i<trace.used;++i) {
                auto const& entry=trace.entries[i];message.append("; ");sunnyStartupLabel(message,entry);
                message.append("=");message.append(sunnyStartupResultLabel(entry.result));
                message.append("(");message.number(entry.code);message.append(")");
            }
            message.append("; trace-overflow=");message.append(trace.overflow||message.overflow?"1":"0");
            message.append("; visual-result=unverified");
            sink(message.view()); // Logger formats/copies synchronously; no view retained here.
        } catch (...) { /* Diagnostics cannot change startup or escape into the game. */ }
    }
};
template<class Operation> bool observeSunnyStartup(SunnyStartupTrace& trace,Operation&& operation) noexcept {
    try { return std::forward<Operation>(operation)(); }
    catch (...) { trace.exception();return false; }
}
template<class Verify,class Install>
bool diagnosedSunnyInstall(SunnyStartupTrace& trace,Verify&& verify,Install&& install) noexcept {
    bool const startup=isolatedSunnyInstall(
        [&]{return observeSunnyStartup(trace,std::forward<Verify>(verify));},
        [&]{return observeSunnyStartup(trace,std::forward<Install>(install));});
    trace.begin(SunnyStartupStage::Capability);
    trace.finish(weatherNativeAdmission.load(std::memory_order_seq_cst));
    return startup;
}
template<class Hooks,class OnActive>
bool installSunnyStartupHooks(Hooks& hooks,SunnyStartupTrace& trace,SunnyStartupStage stage,OnActive&& onActive) {
    for(unsigned index=0;index<hooks.size();++index) {
        auto& hook=hooks[index];trace.begin(stage,index);
        if(hook.active) { trace.result(SunnyStartupResult::Retained);continue; }
        int const code=hook.install();hook.active=code==0;
        trace.finish(hook.active,code);
        if(hook.active) onActive(index);
        if(!hook.active) return false;
    }
    return true;
}
} // namespace praxis::companion
