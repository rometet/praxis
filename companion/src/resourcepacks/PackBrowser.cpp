#include "resourcepacks/PackBrowser.h"
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <Windows.h>

namespace praxis::companion::packs {
namespace {
// The module is process-pinned. On a bounded stop timeout the worker and its
// synchronization objects must remain valid; no joinable static destructor.
auto& mutex=*new std::mutex;
auto& wake=*new std::condition_variable;
auto& retired=*new std::condition_variable;
auto& worker=*new std::thread;
auto& cancelled=*new std::atomic_bool{};
auto& running=*new bool{};auto& stopping=*new bool{};auto& finished=*new bool{};auto& pending=*new bool{};auto& busy=*new bool{};
auto& requested=*new std::filesystem::path;
auto& nextGeneration=*new std::uint64_t{};
auto& published=*new std::shared_ptr<Catalog const>(std::make_shared<Catalog>());
auto& status=*new std::string("Choose a local resource_packs folder and reload the list");
void run() noexcept {
    std::unique_lock lock(mutex);
    while(true) {
        wake.wait(lock,[]{return stopping||pending;});
        if(stopping)break;
        auto path=requested; auto generation=nextGeneration;
        pending=false;
        lock.unlock();
        std::shared_ptr<Catalog const> candidate;
        try {
            auto catalog=scanCatalog(path,cancelled);
            catalog.generation=generation;
            candidate=std::make_shared<Catalog>(std::move(catalog));
        } catch(...) {}
        lock.lock();
        // Only complete, uncancelled catalog generations replace the last good view.
        if(!stopping&&candidate&&candidate->complete&&!cancelled.load()) {
            published=std::move(candidate);status="Local list reloaded";
        } else if(!stopping) {
            status=candidate&&!candidate->error.empty()?candidate->error:"Scan cancelled or failed; previous list retained";
        }
        busy=false;
    }
    busy=pending=false; running=false; finished=true;retired.notify_all();
}
}
bool startBrowser() {
    std::lock_guard lock(mutex);
    if(running)return !stopping;
    if(worker.joinable())return false;
    HMODULE module{};
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&startBrowser),&module))return false;
    stopping=finished=pending=busy=false;cancelled=false;
    try{worker=std::thread(run);running=true;return true;}
    catch(...){status="Scan cancelled or failed; previous list retained";return false;}
}
bool stopBrowser() noexcept {
    try {
        std::unique_lock lock(mutex);
        if(!worker.joinable())return true;
        stopping=true;cancelled=true;wake.notify_all();
        if(!retired.wait_for(lock,std::chrono::milliseconds(500),[]{return finished;}))return false;
        worker.join();return true;
    } catch(...) {return false;}
}
bool requestScan(std::filesystem::path const& path) {
    std::lock_guard lock(mutex);
    if(!running||stopping||busy)return false;
    requested=path;cancelled=false;++nextGeneration;pending=busy=true;
    status="Reading local manifests in background...";wake.notify_one();return true;
}
void cancelScan() noexcept {cancelled=true;}
BrowserView browserView() {
    std::lock_guard lock(mutex);return {published,running&&!stopping,busy,status};
}
}
