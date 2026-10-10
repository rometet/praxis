#define PRAXIS_PASSIVE_RELOAD_STOP_NOTICE_TEST
#define PRAXIS_RELOAD_READINESS_FILE_TEST
#include "resourcepacks/PassiveReloadUpdate.h"
#include "resourcepacks/ReloadReadinessFile.h"
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <thread>
using namespace praxis::companion::packs;
namespace {
unsigned checks{};
void check(bool b){++checks;if(!b){std::cerr<<"FAIL close notice check "<<checks<<'\n';std::exit(1);}}
ReloadTraceOwner owner(){return {101,202,303,0,true};}
void token(std::array<wchar_t,33>& output){
    FILETIME time{};GetSystemTimeAsFileTime(&time);
    _snwprintf_s(output.data(),output.size(),_TRUNCATE,L"%08lx%08lx%016llx",time.dwHighDateTime,time.dwLowDateTime,GetTickCount64());
}
std::string read(std::array<wchar_t,32768> const& path){
    auto f=CreateFileW(path.data(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    check(f!=INVALID_HANDLE_VALUE);std::array<char,16384> data{};DWORD bytes{};
    check(ReadFile(f,data.data(),static_cast<DWORD>(data.size()),&bytes,nullptr));CloseHandle(f);return {data.data(),bytes};
}
std::string await(std::array<wchar_t,32768> const& path,std::string const& marker){
    auto limit=GetTickCount64()+5000;
    for(;;){auto data=read(path);if(data.find(marker)!=std::string::npos)return data;check(GetTickCount64()<limit);Sleep(1);}
}
std::string utf8(wchar_t const* value){
    auto size=WideCharToMultiByte(CP_UTF8,0,value,-1,nullptr,0,nullptr,nullptr);check(size>0);
    std::string data(size,'\0');check(WideCharToMultiByte(CP_UTF8,0,value,-1,data.data(),size,nullptr,nullptr)>0);
    data.pop_back();return data;
}
struct Sink {
    ReloadReadinessFile* file{};
    std::atomic<unsigned> calls{},reason{};
    void notifyStop(unsigned end) noexcept {reason=end;++calls;file->notifyStop(end);}
};
void ready(ReloadReadinessFile& file,std::array<wchar_t,32768> const& path){
    check(file.publish({4,2,0,true,true,GetTickCount64()+600000,"registered",7021456}));await(path,"\"titleSeen\":true");
}
void verifyEvent(std::array<wchar_t,33> const& runToken,bool stopped){
    std::array<wchar_t,128> name{};check(ReloadReadinessFile::eventName(runToken.data(),name));
    auto event=OpenEventW(SYNCHRONIZE,FALSE,name.data());check(event);
    check(WaitForSingleObject(event,0)==(stopped?WAIT_OBJECT_0:WAIT_TIMEOUT));CloseHandle(event);
}
void eachCloseBranch(){
    constexpr char const* labels[]{"deadline","record_limit","title_game_identity","established_owner_identity",
        "world_generation","in_game","owner_not_ready","update_counter_overflow","title_owner_changed",
        "boundary_counter_overflow","group_limit","flush_counter_overflow","flush_ticket_identity",
        "cycle_limit","duplicate_cycle","explicit_stopped","explicit_error","invalid_none","invalid_enum"};
    for(unsigned scenario=0;scenario<std::size(labels);++scenario){
        std::array<wchar_t,33> runToken{};token(runToken);std::array<wchar_t,32768> path{};
        check(ReloadReadinessFile::path(runToken.data(),path));
        {
            ReloadReadinessFile file;check(file.open(runToken.data()));ready(file,path);Sink sink{&file};
            auto trace=std::make_unique<PassiveReloadTrace>(&passiveReloadTerminalNotice<Sink>,&sink);trace->enable(true,0);
            ReloadTraceEnd expected=ReloadTraceEnd::ObservationError;
            auto cycle=[&](std::uint64_t serial){trace->title(owner().game,1);return trace->start(owner(),serial,1,true,true,2);};
            auto flush=[&](std::uintptr_t group){return trace->flushBegin(owner(),1,true,true,false,3,group);};
            switch(scenario){
            case 0:expected=ReloadTraceEnd::Deadline;trace->update(600000,0,true,false);break;
            case 1:expected=ReloadTraceEnd::RecordLimit;for(unsigned n=0;n<4100;++n)trace->unknown(ReloadTraceEvent::CallbackUnknown,1);break;
            case 2:expected=ReloadTraceEnd::OwnerChanged;trace->title(999,1);trace->start(owner(),1,1,true,true,2);break;
            case 3:{expected=ReloadTraceEnd::OwnerChanged;cycle(1);auto other=owner();++other.loads;trace->start(other,2,1,true,true,2);break;}
            case 4:expected=ReloadTraceEnd::OwnerChanged;trace->update(1,1,true,false);break;
            case 5:expected=ReloadTraceEnd::OwnerChanged;trace->update(1,0,true,true);break;
            case 6:expected=ReloadTraceEnd::OwnerChanged;cycle(1);trace->update(3,0,false,false);break;
            case 7:trace->fixtureCounters(std::numeric_limits<std::uint64_t>::max(),0,0);trace->update(1,0,true,false);break;
            case 8:expected=ReloadTraceEnd::OwnerChanged;trace->title(101,1);trace->title(999,2);break;
            case 9:trace->title(101,1);trace->fixtureCounters(0,std::numeric_limits<std::uint64_t>::max(),0);flush(1);break;
            case 10:trace->title(101,1);for(unsigned group=1;group<=64;++group){auto ticket=flush(group);check(bool(ticket));trace->flushEnd(ticket,true,4);}flush(65);break;
            case 11:trace->title(101,1);trace->fixtureCounters(0,0,std::numeric_limits<std::uint64_t>::max());flush(1);break;
            case 12:{trace->title(101,1);auto ticket=flush(1);++ticket.invocation;trace->flushEnd(ticket,true,4);break;}
            case 13:expected=ReloadTraceEnd::CycleLimit;for(unsigned n=1;n<=4;++n)check(cycle(n).valid());cycle(5);break;
            case 14:check(cycle(1).valid());cycle(1);break;
            case 15:expected=ReloadTraceEnd::Stopped;trace->stop();break;
            case 16:trace->stop(ReloadTraceEnd::ObservationError);break;
            case 17:trace->stop(ReloadTraceEnd::None);break;
            case 18:trace->stop(static_cast<ReloadTraceEnd>(999));break;
            }
            check(!trace->enabled()&&trace->view().end==expected&&sink.calls==1&&sink.reason==static_cast<unsigned>(expected));
            verifyEvent(runToken,true);
            await(path,"\"end\":"+std::to_string(static_cast<unsigned>(expected)));
            trace->stop();trace->enable(true,10);check(sink.calls==1&&!trace->enabled());
            std::cout<<"CLOSE_BRANCH "<<labels[scenario]<<" PASS\n";
        }
        check(DeleteFileW(path.data()));
    }
    // Unsupported/default OFF cannot call the sink or create a terminal event.
    ReloadReadinessFile file;Sink sink{&file};auto trace=std::make_unique<PassiveReloadTrace>(&passiveReloadTerminalNotice<Sink>,&sink);
    trace->enable(false,0);trace->update(600000,1,true,true);trace->stop();check(!trace->enabled()&&sink.calls==0);
}
int heldNativeProbe(char const* value,bool interactive){
    std::wstring wide;for(;*value;++value)wide.push_back(static_cast<unsigned char>(*value));
    std::array<wchar_t,33> runToken{};check(wide.size()==32);std::copy(wide.begin(),wide.end(),runToken.begin());
    std::array<wchar_t,32768> path{};check(ReloadReadinessFile::path(runToken.data(),path));
    {
        ReloadReadinessFile file;check(file.open(runToken.data()));ready(file,path);
        auto trace=std::make_unique<PassiveReloadTrace>(&passiveReloadTerminalNotice<ReloadReadinessFile>,&file);trace->enable(true,0);
        file.fixtureEntered=CreateEventW(nullptr,TRUE,FALSE,nullptr);file.fixtureRelease=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        check(file.fixtureEntered&&file.fixtureRelease);file.fixtureHold=true;file.fixtureWake();
        check(WaitForSingleObject(file.fixtureEntered,5000)==WAIT_OBJECT_0);
        auto originEntered=CreateEventW(nullptr,TRUE,FALSE,nullptr),originRelease=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        check(originEntered&&originRelease);std::atomic_bool after{};
        if(interactive)std::cout<<"FILE "<<utf8(path.data())<<"\nREADY\n"<<std::flush;
        std::thread updater([&]{runPassiveReloadUpdate([&]{trace->update(1,1,true,false);},[&]{
            SetEvent(originEntered);check(WaitForSingleObject(originRelease,10000)==WAIT_OBJECT_0);
        },[&]{after=true;});});
        check(WaitForSingleObject(originEntered,5000)==WAIT_OBJECT_0);
        check(trace->view().end==ReloadTraceEnd::OwnerChanged&&!after);verifyEvent(runToken,true);
        check(read(path).find("\"end\":4")==std::string::npos); // old ready while both origin and writer are held
        if(interactive){std::cout<<"NATIVE_HELD\n"<<std::flush;std::string input;check(static_cast<bool>(std::getline(std::cin,input)));}
        SetEvent(originRelease);updater.join();check(after);
        file.fixtureHold=false;SetEvent(file.fixtureRelease);
        await(path,"\"end\":4"); // no post-origin emit, publish or subsequent update
        if(interactive){std::cout<<"STOPPED\n"<<std::flush;std::string input;check(static_cast<bool>(std::getline(std::cin,input)));}
        CloseHandle(originEntered);CloseHandle(originRelease);CloseHandle(file.fixtureEntered);CloseHandle(file.fixtureRelease);
    }
    check(DeleteFileW(path.data()));return 0;
}
}
int main(int argc,char** argv){
    if(argc==3&&std::string(argv[1])=="--native-pause-probe")return heldNativeProbe(argv[2],true);
    eachCloseBranch();std::array<wchar_t,33> runToken{};token(runToken);heldNativeProbe(utf8(runToken.data()).c_str(),false);
    std::cout<<"PassiveReloadStopNotice checks="<<checks<<" PASS; 19 close causes + real trace/writer with shared UpdateHook order; native boundary doubled; game NOT_RUN\n";
}
