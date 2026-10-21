#define PRAXIS_RELOAD_READINESS_FILE_TEST
#include "resourcepacks/ReloadReadinessFile.h"
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
using namespace praxis::companion::packs;
namespace {
unsigned checks{};
void check(bool b){++checks;if(!b){std::cerr<<"FAIL readiness file check "<<checks<<'\n';std::exit(1);}}
std::string read(std::array<wchar_t,32768> const& path){
    auto file=CreateFileW(path.data(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    check(file!=INVALID_HANDLE_VALUE);
    std::array<char,16384> text{};DWORD bytes{};check(ReadFile(file,text.data(),static_cast<DWORD>(text.size()),&bytes,nullptr));
    CloseHandle(file);return {text.data(),bytes};
}
std::string await(std::array<wchar_t,32768> const& path,char const* marker){
    auto until=GetTickCount64()+5000;
    for(;;){auto text=read(path);if(text.find(marker)!=std::string::npos)return text;
        check(GetTickCount64()<until);Sleep(1);}
}
std::string lastLine(std::string text){
    check(text.ends_with("\n"));text.pop_back();auto start=text.rfind('\n');return text.substr(start==std::string::npos?0:start+1);
}
void token(std::array<wchar_t,33>& output){
    FILETIME time{};GetSystemTimeAsFileTime(&time);
    _snwprintf_s(output.data(),output.size(),_TRUNCATE,L"%08lx%08lx%016llx",time.dwHighDateTime,time.dwLowDateTime,GetTickCount64());
}
ReloadReadinessState ready(){return {4,2,0,true,true,GetTickCount64()+600000,"registered",7021456};}
std::string utf8(wchar_t const* text){
    auto n=WideCharToMultiByte(CP_UTF8,0,text,-1,nullptr,0,nullptr,nullptr);check(n>0);
    std::string output(n,'\0');check(WideCharToMultiByte(CP_UTF8,0,text,-1,output.data(),n,nullptr,nullptr)>0);
    output.pop_back();return output;
}
int tempProbe(char const* value){
    std::wstring wide;for(;*value;++value)wide.push_back(static_cast<unsigned char>(*value));
    std::array<wchar_t,32768> path{};check(ReloadReadinessFile::path(wide.c_str(),path));
    {
        ReloadReadinessFile file;check(file.open(wide.c_str()));auto state=ready();check(file.publish(state));
        await(path,"\"titleSeen\":true");
        std::cout<<"FILE "<<utf8(path.data())<<'\n'<<"READY\n"<<std::flush;
        std::string input;check(static_cast<bool>(std::getline(std::cin,input)));
        state.end=6;state.enabled=false;check(file.publish(state));await(path,"\"end\":6");
        std::cout<<"STOPPED\n"<<std::flush;check(static_cast<bool>(std::getline(std::cin,input)));
    }
    check(DeleteFileW(path.data()));return 0;
}
}
int main(int argc,char** argv){
    if(argc==3&&std::string(argv[1])=="--temp-probe")return tempProbe(argv[2]);
    check(!ReloadReadinessFile::validToken(nullptr));check(!ReloadReadinessFile::validToken(L"../world"));
    check(!ReloadReadinessFile::validToken(L"0123456789012345678901234567890xyz"));
    std::array<wchar_t,33> runToken{};token(runToken);
    std::array<wchar_t,32768> path{};check(ReloadReadinessFile::path(runToken.data(),path));
    {
        ReloadReadinessFile file;check(!file.open(L"/outside/temp"));check(file.open(runToken.data()));
        ReloadReadinessState state;check(file.publish(state));
        auto first=await(path,"\"guard\":0");check(first.ends_with("\n"));
        check(file.publish(state));check(read(path)==first);
        ReloadReadinessFile duplicate;check(!duplicate.open(runToken.data()));check(read(path)==first);
        state.guard=4;state.registration=1;state.enabled=true;state.deadlineTick=12345;state.reason="registering";check(file.publish(state));
        state.registration=2;state.reason="registered";check(file.publish(state));
        state.titleSeen=true;check(file.publish(state));
        auto title=await(path,"\"titleSeen\":true");
        // Hold the real file worker after taking a ready snapshot. A stop and
        // concurrent stale publishers must return without releasing this hold.
        file.fixtureEntered=CreateEventW(nullptr,TRUE,FALSE,nullptr);file.fixtureRelease=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        check(file.fixtureEntered&&file.fixtureRelease);file.fixtureHold=true;file.fixtureWake();
        check(WaitForSingleObject(file.fixtureEntered,5000)==WAIT_OBJECT_0);
        auto stale=state;state.enabled=false;state.end=1;
        auto before=GetTickCount64();check(file.publish(state));check(GetTickCount64()-before<2000);
        std::array<wchar_t,128> name{};check(ReloadReadinessFile::eventName(runToken.data(),name));
        auto stoppedEvent=OpenEventW(SYNCHRONIZE,FALSE,name.data());check(stoppedEvent);
        check(WaitForSingleObject(stoppedEvent,0)==WAIT_OBJECT_0);
        check(lastLine(read(path)).find("\"end\":0")!=std::string::npos); // stale file, event already rejects it
        std::vector<std::thread> writers;
        for(unsigned i=0;i<8;++i)writers.emplace_back([&]{for(unsigned j=0;j<1000;++j)if(!file.publish(stale))std::exit(2);});
        for(auto& writer:writers)writer.join();
        check(WaitForSingleObject(stoppedEvent,0)==WAIT_OBJECT_0);
        file.fixtureHold=false;SetEvent(file.fixtureRelease);
        auto stopped=lastLine(await(path,"\"end\":1"));
        check(stopped.find("\"enabled\":false")!=std::string::npos);
        check(file.publish(stale));check(lastLine(read(path))==stopped);
        CloseHandle(stoppedEvent);CloseHandle(file.fixtureEntered);CloseHandle(file.fixtureRelease);
    }
    // CREATE_NEW also preserves a closed stale run, including a reused PID.
    {ReloadReadinessFile stale;check(!stale.open(runToken.data()));}
    check(DeleteFileW(path.data()));
    std::cout<<"ReloadReadinessFile checks="<<checks<<" PASS; deterministic blocked worker + 8000 stale concurrent publishes; game NOT_RUN\n";
}
