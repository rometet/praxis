#include "resourcepacks/PassiveReloadTraceRuntime.h"
#include "resourcepacks/ReloadReadinessFile.h"
#include "resourcepacks/PassiveReloadUpdate.h"
#include "ll/api/memory/Memory.h"
#include "mc/deps/core/threading/TaskGroup.h"
#include "ll/api/mod/NativeMod.h"
#include <Windows.h>
#include <Psapi.h>
#include <bcrypt.h>
#include <array>
#include <cstring>

namespace praxis::companion::packs {
namespace {ReloadReadinessFile& readinessFile=*new ReloadReadinessFile;}
PassiveReloadTrace& passiveReloadTrace=*new PassiveReloadTrace(
    &passiveReloadTerminalNotice<ReloadReadinessFile>,&readinessFile);
namespace {
void* flushTarget{};
std::uintptr_t flushReturn{};
constexpr std::array<unsigned char,32> ImageHash{0xf8,0x2d,0xc9,0x67,0xd3,0x80,0x0b,0x7b,0x89,0x2c,0x38,0xa9,0x7b,0x67,0x2e,0x5f,0x32,0x57,0xd9,0xc0,0xc0,0xcf,0xfe,0xcc,0xdc,0x5f,0x83,0xde,0x02,0x36,0x3b,0x2a};
constexpr std::array<unsigned char,48> FlushBytes{0x55,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x56,0x57,0x53,0x48,0x81,0xec,0xd8,0x00,0x00,0x00,0x48,0x8d,0xac,0x24,0x80,0x00,0x00,0x00,0x48,0xc7,0x45,0x50,0xfe,0xff,0xff,0xff,0x48,0x89,0xd3,0x48,0x89,0xce,0x48,0x8d,0x79,0x60,0x48,0x89,0x7d};
constexpr std::array<unsigned char,5> CallerBytes{0xe8,0x4f,0xde,0xe8,0xff};
constexpr std::array<unsigned char,17> FlagGateBytes{0x48,0x8b,0x86,0xd0,0x05,0x00,0x00,0x80,0x78,0x48,0x01,0x0f,0x85,0xb5,0x00,0x00,0x00};
std::atomic<unsigned> readinessGuard{}, readinessRegistration{};
std::atomic<char const*> readinessReason{"checking_image"};
std::atomic<std::uint64_t> readinessDeadline{};
std::atomic<std::uint64_t> readinessSdkRva{};
std::atomic_bool readinessOpen{};
bool publishReadiness() noexcept {
    if(!readinessOpen.load())return true;
    auto summary=passiveReloadTrace.view();
    return readinessFile.publish({readinessGuard.load(),readinessRegistration.load(),static_cast<unsigned>(summary.end),
        summary.titleSeen,passiveReloadTrace.enabled(),readinessDeadline.load(),readinessReason.load(),readinessSdkRva.load()});
}
struct FileHandle {
    HANDLE value=INVALID_HANDLE_VALUE;
    ~FileHandle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
};
struct HashHandles {
    BCRYPT_ALG_HANDLE algorithm{}; BCRYPT_HASH_HANDLE hash{};
    ~HashHandles(){if(hash)BCryptDestroyHash(hash);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);}
};
bool exactImageFile() noexcept {
    std::array<wchar_t,32768> path{};
    auto count=GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()));
    if(!count||count>=path.size())return false;
    FileHandle file{CreateFileW(path.data(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL|FILE_FLAG_SEQUENTIAL_SCAN,nullptr)};
    LARGE_INTEGER size{};
    if(file.value==INVALID_HANDLE_VALUE||!GetFileSizeEx(file.value,&size)||size.QuadPart!=311249032)return false;
    HashHandles context;
    if(BCryptOpenAlgorithmProvider(&context.algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0
        ||BCryptCreateHash(context.algorithm,&context.hash,nullptr,0,nullptr,0,0)<0)return false;
    std::array<unsigned char,65536> buffer{};
    std::uint64_t total{};
    for(;;){
        DWORD bytes{};if(!ReadFile(file.value,buffer.data(),static_cast<DWORD>(buffer.size()),&bytes,nullptr))return false;
        if(!bytes)break;
        total+=bytes;if(total>311249032||BCryptHashData(context.hash,buffer.data(),bytes,0)<0)return false;
    }
    std::array<unsigned char,32> hash{};
    return total==311249032&&BCryptFinishHash(context.hash,hash.data(),static_cast<ULONG>(hash.size()),0)>=0&&hash==ImageHash;
}
template<std::size_t N> bool memoryMatches(std::uintptr_t base,std::size_t imageSize,std::uintptr_t rva,
                                         std::array<unsigned char,N> const& expected) noexcept {
    if(rva>=imageSize||N>imageSize-rva)return false;
    std::array<unsigned char,N> actual{};SIZE_T copied{};
    return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void const*>(base+rva),actual.data(),N,&copied)
        &&copied==N&&actual==expected;
}
void emitGuard(ReloadTraceGuard reason,char const* detail) noexcept {
    readinessGuard=static_cast<unsigned>(reason);readinessReason=detail;
    publishReadiness();
    try{if(auto mod=ll::mod::NativeMod::current())mod->getLogger().info(
        "PackReloadTrace schema=1 guard={} detail={} requested=1 preGameOnly=1 mutation=0 isCompleteCalled=0 maxSeconds=600 maxRecords=4096 maxCycles=4",
        static_cast<unsigned>(reason),detail);}catch(...){}
}
}
bool preparePassiveReloadTrace() noexcept {
    // Read one exact opt-in only. Do not log environment values or paths, or
    // persist anything to the user's normal config. OFF performs no image I/O.
    std::array<wchar_t,2> option{};
    if(GetEnvironmentVariableW(L"PRAXIS_RESOURCE_RELOAD_TRACE",option.data(),static_cast<DWORD>(option.size()))!=1||option[0]!=L'1')return false;
    try{
        std::array<wchar_t,33> token{};
        auto tokenLength=GetEnvironmentVariableW(L"PRAXIS_RESOURCE_RELOAD_STATUS_TOKEN",token.data(),static_cast<DWORD>(token.size()));
        if(!tokenLength){
            FILETIME created{},exited{},kernel{},user{};
            if(!GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user))return false;
            auto creation=(static_cast<std::uint64_t>(created.dwHighDateTime)<<32)|created.dwLowDateTime;
            _snwprintf_s(token.data(),token.size(),_TRUNCATE,L"%016llx%016llx",creation,GetTickCount64());
        }
        if(tokenLength>=token.size()||!readinessFile.open(token.data())){
            emitGuard(ReloadTraceGuard::Off,"status_unavailable");return false;
        }
        readinessOpen=true;if(!publishReadiness())return false;
        MODULEINFO module{};auto main=GetModuleHandleW(nullptr);
        bool image=main&&GetModuleInformation(GetCurrentProcess(),main,&module,sizeof(module))&&exactImageFile();
        if(!image){emitGuard(ReloadTraceGuard::ImageUnknown,"image_identity_failed");return false;}
        auto base=reinterpret_cast<std::uintptr_t>(module.lpBaseOfDll);
        // Normal SDK name resolution only. These RVAs are independently proven
        // identity checks, not function lookup fallbacks or callable addresses.
        using FlushAbi=void(TaskGroup::*)(std::function<void()>);
        static_assert(std::is_same_v<decltype(&TaskGroup::flush),FlushAbi>);
        auto target=ll::memory::toFuncPtr(&TaskGroup::flush);
        auto pointer=reinterpret_cast<std::uintptr_t>(target);
        bool inImage=pointer>=base&&pointer-base<module.SizeOfImage;
        readinessSdkRva=inImage?pointer-base:0;
        bool symbol=target&&reinterpret_cast<std::uintptr_t>(target)==base+0x6b2390;
        bool flushBytes=symbol&&memoryMatches(base,module.SizeOfImage,0x6b2390,FlushBytes);
        bool callerBytes=symbol&&memoryMatches(base,module.SizeOfImage,0x82453c,CallerBytes);
        bool gateBytes=symbol&&memoryMatches(base,module.SizeOfImage,0x8244b8,FlagGateBytes);
        auto guard=reloadTraceGuard(true,image,symbol,flushBytes&&callerBytes&&gateBytes);
        emitGuard(guard,!target?"sdk_symbol_missing":!inImage?"sdk_target_outside_image":!symbol?"sdk_rva_mismatch":!flushBytes?"flush_fingerprint_failed"
            :!callerBytes?"caller_fingerprint_failed":!gateBytes?"gate_fingerprint_failed":"sdk_identity_verified");
        if(guard!=ReloadTraceGuard::Ready)return false;
        flushTarget=target;flushReturn=base+0x824541;
        auto now=GetTickCount64();passiveReloadTrace.enable(true,now);readinessDeadline=now+600000;
        if(!publishReadiness()){passiveReloadTrace.stop(ReloadTraceEnd::ObservationError);return false;}
        return true;
    }catch(...){emitGuard(ReloadTraceGuard::SymbolUnknown,"sdk_resolution_exception");return false;}
}
void reportPassiveReloadRegistration(unsigned state,char const* reason) noexcept {
    readinessRegistration=state;readinessReason=reason;
    if(!publishReadiness())passiveReloadTrace.stop(ReloadTraceEnd::ObservationError);
}
void* passiveReloadFlushTarget() noexcept{return flushTarget;}
bool passiveReloadFlushCaller(void* caller) noexcept {
    return flushReturn&&reinterpret_cast<std::uintptr_t>(caller)==flushReturn;
}
void emitPassiveReloadTrace(unsigned limit) noexcept {
    try{
        if(!publishReadiness())passiveReloadTrace.stop(ReloadTraceEnd::ObservationError);
        auto summary=passiveReloadTrace.view();if(summary.records==summary.emitted)return;
        auto mod=ll::mod::NativeMod::current();if(!mod)return;
        limit=limit<64?limit:64;
        ReloadTraceRecord record;
        for(unsigned i=0;i<limit&&passiveReloadTrace.next(record);++i)mod->getLogger().info(
            "PackReloadTrace schema=1 event={} seq={} session={} owner=pre_game manager={} managerScope=formal_game_getter nativeListenerReceiver=unverified cycle={} boundary={} update={} task={} group={} value={} titleSeen={} coverage=partial fence=unverified",
            reloadTraceLabel(record.event),record.sequence,record.session,record.session?1:0,record.cycle,record.boundary,
            record.update,record.task,record.group,record.value,record.titleSeen?1:0);
    }catch(...){passiveReloadTrace.stop(ReloadTraceEnd::ObservationError);}
}
void stopPassiveReloadTrace(ReloadTraceEnd reason) noexcept {
    if(!passiveReloadTrace.enabled()&&!passiveReloadTrace.view().records)return;
    passiveReloadTrace.stop(reason);emitPassiveReloadTrace(64);
    try{
        auto summary=passiveReloadTrace.view();
        if(auto mod=ll::mod::NativeMod::current())mod->getLogger().info(
            "PackReloadTrace schema=1 summary session={} records={} emitted={} cycles={} flushes={} bodyTerminals={} unknownBodies={} errors={} unknownCallbacks={} reason={} lost={} coverage=partial fence=unverified",
            summary.session,summary.records,summary.emitted,summary.cycles,summary.flushes,summary.terminals,
            summary.unknownBodies,summary.errors,summary.unknownCallbacks,static_cast<unsigned>(summary.end),summary.lost?1:0);
    }catch(...){}
}
}
