#pragma once
#include <Windows.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cwchar>

namespace praxis::companion::packs {
struct ReloadReadinessState {
    unsigned guard{}, registration{}, end{};
    bool titleSeen{}, enabled{};
    std::uint64_t deadlineTick{};
    char const* reason="checking_image";
    std::uint64_t sdkRva{};
    bool same(ReloadReadinessState const& b) const noexcept {
        return guard==b.guard&&registration==b.registration&&end==b.end&&titleSeen==b.titleSeen
            &&enabled==b.enabled&&deadlineTick==b.deadlineTick&&reason==b.reason&&sdkRva==b.sdkRva;
    }
    bool terminal() const noexcept {return end||registration==3||(guard>=1&&guard<=3);}
};
// Producers publish monotonic facts only. One process-pinned worker owns all
// disk I/O. A sticky kernel event invalidates stale ready even during blocked
// or failed I/O. Production retains this owner for the entire process lifetime.
class ReloadReadinessFile {
    HANDLE file=INVALID_HANDLE_VALUE, wake{}, terminalEvent{}, thread{};
    std::atomic<unsigned> facts{};
    std::atomic<std::uint64_t> deadline{}, sdkRva{};
    std::atomic<char const*> guardReason{"checking_image"}, failureReason{};
    std::atomic_bool published{}, failed{}, closing{};
    static unsigned join(unsigned old,ReloadReadinessState const& state) noexcept {
        auto guard=old&7u;if(!guard)guard=state.guard;
        auto registration=(old>>3)&3u;
        if(state.registration>registration)registration=state.registration;
        auto end=(old>>7)&7u;if(!end)end=state.end;
        return guard|(registration<<3)|((old|(state.titleSeen?32u:0u)|(state.enabled?64u:0u))&96u)|(end<<7);
    }
    ReloadReadinessState snapshot() const noexcept {
        auto value=facts.load(std::memory_order_acquire);
        ReloadReadinessState result{value&7u,(value>>3)&3u,(value>>7)&7u,!!(value&32u),!!(value&64u),
            deadline.load(),guardReason.load(),sdkRva.load()};
        if(result.terminal())result.enabled=false;
        if(result.registration==3){auto reason=failureReason.load();result.reason=reason?reason:"registration_failed";}
        else if(result.end)result.reason="stopped";
        else if(result.registration==2)result.reason="registered";
        else if(result.registration==1)result.reason="registering";
        return result;
    }
    bool write(ReloadReadinessState const& state,unsigned sequence) noexcept {
        std::array<char,768> line{};
        auto n=std::snprintf(line.data(),line.size(),
            "{\"schema\":2,\"pid\":%lu,\"seq\":%u,\"guard\":%u,\"reason\":\"%s\",\"registration\":%u,\"titleSeen\":%s,\"enabled\":%s,\"end\":%u,\"deadlineTick\":%llu,\"sdkRva\":%llu,\"expectedRva\":7021456,\"resolution\":\"mcapi_import\",\"maxSeconds\":600,\"maxCycles\":4,\"mutation\":false,\"coverage\":\"partial\",\"fence\":\"unverified\"}\n",
            GetCurrentProcessId(),sequence,state.guard,state.reason,state.registration,state.titleSeen?"true":"false",
            state.enabled?"true":"false",state.end,static_cast<unsigned long long>(state.deadlineTick),
            static_cast<unsigned long long>(state.sdkRva));
        DWORD wrote{};
        return n>0&&static_cast<std::size_t>(n)<line.size()&&WriteFile(file,line.data(),static_cast<DWORD>(n),&wrote,nullptr)
            &&wrote==static_cast<DWORD>(n);
    }
    static DWORD WINAPI workerEntry(void* owner) noexcept {
        static_cast<ReloadReadinessFile*>(owner)->run();return 0;
    }
    void run() noexcept {
        ReloadReadinessState last{};unsigned sequence{};
        for(;;){
            if(WaitForSingleObject(wake,INFINITE)!=WAIT_OBJECT_0){failed=true;SetEvent(terminalEvent);return;}
            if(published.load()){
                auto state=snapshot();
#ifdef PRAXIS_RELOAD_READINESS_FILE_TEST
                if(fixtureHold.load()){SetEvent(fixtureEntered);WaitForSingleObject(fixtureRelease,INFINITE);}
#endif
                if(!sequence||!state.same(last)){
                    if(sequence==16||!write(state,sequence+1)){failed=true;SetEvent(terminalEvent);return;}
                    ++sequence;last=state;
                }
            }
            if(closing.load()){
                if(!published.load()||snapshot().same(last))return;
                SetEvent(wake); // Test-owner destruction drains its final mailbox.
            }
        }
    }
public:
#ifdef PRAXIS_RELOAD_READINESS_FILE_TEST
    std::atomic_bool fixtureHold{};
    HANDLE fixtureEntered{},fixtureRelease{};
    void fixtureWake() noexcept {SetEvent(wake);}
#endif
    ReloadReadinessFile()=default;
    ReloadReadinessFile(ReloadReadinessFile const&)=delete;
    // Tests own a stack instance. Production deliberately never destroys its
    // retained owner; no join/wait is performed on a game's hook or stop path.
    ~ReloadReadinessFile(){
        closing=true;if(wake)SetEvent(wake);
        if(thread){WaitForSingleObject(thread,INFINITE);CloseHandle(thread);}
        if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);
        if(wake)CloseHandle(wake);if(terminalEvent)CloseHandle(terminalEvent);
    }
    static bool validToken(wchar_t const* token) noexcept {
        if(!token||std::wcslen(token)!=32)return false;
        for(unsigned i=0;i<32;++i)if(!((token[i]>=L'0'&&token[i]<=L'9')
            ||(token[i]>=L'a'&&token[i]<=L'f')||(token[i]>=L'A'&&token[i]<=L'F')))return false;
        return true;
    }
    static bool eventName(wchar_t const* token,std::array<wchar_t,128>& output) noexcept {
        return validToken(token)&&_snwprintf_s(output.data(),output.size(),_TRUNCATE,
            L"Local\\PraxisResourceReloadStopped-%lu-%s",GetCurrentProcessId(),token)>0;
    }
    static bool path(wchar_t const* token,std::array<wchar_t,32768>& output) noexcept {
        if(!validToken(token))return false;
        std::array<wchar_t,32768> temp{};auto n=GetTempPathW(static_cast<DWORD>(temp.size()),temp.data());
        if(n<3||n>=temp.size()||temp[1]!=L':'||temp[2]!=L'\\'
            ||!((temp[0]>=L'A'&&temp[0]<=L'Z')||(temp[0]>=L'a'&&temp[0]<=L'z')))return false;
        return _snwprintf_s(output.data(),output.size(),_TRUNCATE,L"%spraxis-resource-reload-%lu-%s.jsonl",
            temp.data(),GetCurrentProcessId(),token)>0;
    }
    bool open(wchar_t const* token) noexcept {
        if(file!=INVALID_HANDLE_VALUE)return false;
        std::array<wchar_t,32768> output{};std::array<wchar_t,128> name{};
        if(!path(token,output)||!eventName(token,name))return false;
        file=CreateFileW(output.data(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE)return false;
        terminalEvent=CreateEventW(nullptr,TRUE,FALSE,name.data());
        if(!terminalEvent||GetLastError()==ERROR_ALREADY_EXISTS){failed=true;return false;}
        wake=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!wake){failed=true;SetEvent(terminalEvent);return false;}
        HMODULE module{};
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&workerEntry),&module)){failed=true;SetEvent(terminalEvent);return false;}
        thread=CreateThread(nullptr,0,&workerEntry,this,0,nullptr);
        if(!thread){failed=true;SetEvent(terminalEvent);return false;}
        return true;
    }
    void notifyStop(unsigned end) noexcept {
        if(!thread||!end||end>6)return;
        // Reader invalidation is independent of disk I/O and later snapshots.
        if(!SetEvent(terminalEvent)){failed=true;return;}
        ReloadReadinessState stopped;stopped.end=end;stopped.reason="stopped";
        publish(stopped); // Monotonic pending state; worker retains its wake.
    }
    bool publish(ReloadReadinessState const& state) noexcept {
        if(failed.load()||!thread||state.guard>4||state.registration>3||state.end>6)return false;
        bool changed=!published.exchange(true);
        std::uint64_t zero{};
        if(state.deadlineTick&&deadline.compare_exchange_strong(zero,state.deadlineTick))changed=true;
        zero=0;if(state.sdkRva&&sdkRva.compare_exchange_strong(zero,state.sdkRva))changed=true;
        if(state.guard&&state.registration==0){auto old=guardReason.exchange(state.reason);changed=changed||old!=state.reason;}
        if(state.registration==3){char const* empty{};if(failureReason.compare_exchange_strong(empty,state.reason))changed=true;}
        auto old=facts.load();unsigned next{};
        do{next=join(old,state);}while(!facts.compare_exchange_weak(old,next,std::memory_order_release,std::memory_order_relaxed));
        changed=changed||next!=old;
        if(((next>>7)&7u)||((next>>3)&3u)==3||((next&7u)>=1&&(next&7u)<=3)){
            if(!SetEvent(terminalEvent)){failed=true;return false;}
        }
        if(changed&&!SetEvent(wake)){failed=true;SetEvent(terminalEvent);return false;}
        return !failed.load();
    }
};
}
