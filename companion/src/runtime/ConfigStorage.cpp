#include "runtime/ConfigStorage.h"
#include "runtime/ConfigMigration.h"
#include "runtime/ProfileDocument.h"
#if defined(PRAXIS_CONFIG_STORAGE_REAL_CODEC_TEST)
#include "support/RealConfigStorageApi.h"
#elif defined(PRAXIS_CONFIG_STORAGE_TEST)
#include "support/ConfigStorageApi.h"
#else
#include "ll/api/Config.h"
#endif
#include "runtime/ConfigCodec.h"
#include <Windows.h>
#include <fstream>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <chrono>
#include <utility>
#include <cctype>
#include <fstream>
namespace praxis::companion {
namespace {
enum class Operation { Save, Load, Delete, Refresh };
struct Job { Operation operation; std::filesystem::path path; RuntimeConfig config{}; std::uint64_t revision{}; bool shutdownRetried{}; std::filesystem::path pendingFile; bool pendingReady{}; std::string sourceDocument; ProfileEditHistory edits{}; };
std::mutex storageMutex;
std::condition_variable storageChanged;
std::deque<Job> storageJobs;
std::deque<Job> failedSaves;
bool storageInFlight{};
std::thread storageWorker;
bool storageStopping{};
std::filesystem::path profilesDirectory;
std::vector<std::string> profileNames;
std::optional<LoadedProfile> loadedProfile;
std::string storageStatus = "Storage not started";

bool allowedName(std::string const& name) {
    return !name.empty() && name.size() <= 48 && std::all_of(name.begin(), name.end(), [](unsigned char c) {
        return std::isalnum(c) || c=='-' || c=='_';
    });
}
void refreshProfiles() {
    std::vector<std::string> next;
    if (std::filesystem::exists(profilesDirectory)) for (auto const& entry : std::filesystem::directory_iterator(profilesDirectory)) {
        if (entry.is_regular_file() && entry.path().extension()==".json" && allowedName(entry.path().stem().string()))
            next.push_back(entry.path().stem().string());
    }
    std::sort(next.begin(), next.end());
    std::lock_guard lock(storageMutex);
    profileNames = std::move(next);
}
bool saveAtomically(Job& job) {
    static std::uint64_t sequence{}; // worker-only
    std::filesystem::create_directories(job.path.parent_path());
    if (job.pendingFile.empty()) {
        auto temporary=job.path;
        temporary += ".pending-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()) + "-" + std::to_string(++sequence);
        if (std::filesystem::exists(temporary)) return false;
        job.pendingFile=std::move(temporary);
    }
    if (!job.pendingReady) {
        auto readDocument=[&](std::string const& content) {
            if(content.size()>1024*1024)throw std::runtime_error("Profile exceeds size limit");
            auto data=nlohmann::ordered_json::parse(content,nullptr,true,true);
            if(!data.is_object())throw std::runtime_error("Profile must be an object");return data;
        };
        auto target=nlohmann::ordered_json::object();
        if(std::filesystem::exists(job.path)) {
            auto content=ll::file_utils::readFile(job.path);if(!content)return false;
            target=readDocument(*content);
        }
        auto current=ll::reflection::serialize<nlohmann::ordered_json>(job.config).value();
        auto source=current,baseline=current;
        if(!job.sourceDocument.empty()) {
            source=readDocument(job.sourceDocument);
            RuntimeConfig loaded;
            if(!decodeRuntimeConfig(source,loaded))return false;
            baseline=ll::reflection::serialize<nlohmann::ordered_json>(loaded).value();
        }
        // Validate an existing destination independently, before source overlay
        // can hide malformed recognized fields. Empty new destinations are valid.
        if(!target.empty()){RuntimeConfig checked;if(!decodeRuntimeConfig(target,checked))return false;}
        auto document=mergeRuntimeProfileDocument(current,baseline,std::move(source),std::move(target),featureProfileFields,job.edits);
        RuntimeConfig saved;if(!decodeRuntimeConfig(document,saved))return false;
        auto content=document.dump(2);if(content.size()>1024*1024)return false;
        std::ofstream output(job.pendingFile,std::ios::binary|std::ios::trunc);
        output.write(content.data(),static_cast<std::streamsize>(content.size()));output.close();
        if(!output)return false;
        job.pendingReady=true;
    }
    // A failed replace retains the complete owned temporary file for recovery.
    return MoveFileExW(job.pendingFile.c_str(), job.path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
}
void runStorage() {
    auto nextRetry=std::chrono::steady_clock::now();
    for (;;) {
        Job job;
        {
            std::unique_lock lock(storageMutex);
            for (;;) {
                if (!storageJobs.empty()) {
                    job=std::move(storageJobs.front()); storageJobs.pop_front(); break;
                }
                if (storageStopping) {
                    auto retry=std::find_if(failedSaves.begin(), failedSaves.end(), [](Job const& value) { return !value.shutdownRetried; });
                    if (retry==failedSaves.end()) return;
                    job=std::move(*retry); failedSaves.erase(retry); job.shutdownRetried=true; break;
                }
                if (!failedSaves.empty() && std::chrono::steady_clock::now()>=nextRetry) {
                    job=std::move(failedSaves.front()); failedSaves.pop_front();
                    nextRetry=std::chrono::steady_clock::now()+std::chrono::seconds(3); break;
                }
                if (failedSaves.empty()) storageChanged.wait(lock);
                else storageChanged.wait_until(lock,nextRetry);
            }
            storageInFlight=true;
        }
        bool success{};
        std::string detail;
        try {
            if (job.operation == Operation::Save) success=saveAtomically(job);
            else if (job.operation == Operation::Load) {
                auto content=ll::file_utils::readFile(job.path);
                if (content && content->size() <= 1024*1024) {
                    auto data=nlohmann::ordered_json::parse(*content,nullptr,true,true);
                    RuntimeConfig candidate;
                    if (decodeRuntimeConfig(data,candidate)) {
                        std::lock_guard lock(storageMutex);
                        loadedProfile=LoadedProfile{std::move(candidate),job.revision,*content}; success=true;
                    }
                }
            } else if (job.operation == Operation::Delete) success=std::filesystem::remove(job.path);
            else success=true;
            if (job.operation != Operation::Load) refreshProfiles();
        } catch (std::exception const& error) { detail=error.what(); }
        catch (...) { detail="Unknown storage error"; }
        std::lock_guard lock(storageMutex);
        storageInFlight=false;
        if (job.operation==Operation::Save && !success) {
            // A later owned save supersedes this failure; otherwise retain it.
            bool newer=std::any_of(storageJobs.begin(),storageJobs.end(),[&](Job const& value) {
                return value.operation==Operation::Save && value.path==job.path;
            });
            if (!newer) failedSaves.push_back(std::move(job));
        } else if (success && (job.operation==Operation::Save || job.operation==Operation::Delete)) {
            std::erase_if(failedSaves,[&](Job const& value) { return value.path==job.path; });
        }
        storageStatus=success ? "Background config/profile operation complete" : "Config/profile I/O failed (pending file retained): " + detail;
        if (!failedSaves.empty()) storageStatus="Unsaved settings retained; retrying in background";
    }
}
bool enqueue(Job job) {
    std::lock_guard lock(storageMutex);
    if (!storageWorker.joinable() || storageStopping) return false;
    auto const save=job.operation==Operation::Save;
    auto const path=job.path;
    auto const obsolete=save ? std::count_if(failedSaves.begin(),failedSaves.end(),[&](Job const& value) { return value.path==path; }) : 0;
    // Only coalesce the consecutive tail save. Never cross a load/delete ordering boundary.
    if (job.operation == Operation::Save && !storageJobs.empty() && storageJobs.back().operation == Operation::Save
        && storageJobs.back().path == job.path) storageJobs.back()=std::move(job);
    else {
        if (storageJobs.size()+failedSaves.size()-obsolete+(storageInFlight?1u:0u) >= 32) return false;
        storageJobs.push_back(std::move(job));
    }
    if (save) std::erase_if(failedSaves,[&](Job const& value) { return value.path==path; });
    storageStatus="Background config/profile operation queued";
    storageChanged.notify_one();
    return true;
}
}
bool startConfigStorage(std::filesystem::path const& path) {
    std::lock_guard lock(storageMutex);
    if (storageWorker.joinable()) return !storageStopping;
    if (path.empty() || !path.is_absolute()) return false;
    profilesDirectory=path.parent_path()/"profiles";
    loadedProfile.reset(); storageStopping=false;
    for (auto& failed : failedSaves) failed.shutdownRetried=false;
    storageJobs.push_back({Operation::Refresh,{}});
    try { storageWorker=std::thread(runStorage); }
    catch (...) { storageJobs.clear(); return false; }
    return true;
}
bool queueConfigSave(RuntimeConfig const& value, std::filesystem::path const& path,std::string sourceDocument,ProfileEditHistory edits) {
 if(!validRuntimeConfig(value) || sourceDocument.size()>1024*1024)return false;
 Job job{Operation::Save,path,value};job.sourceDocument=std::move(sourceDocument);job.edits=edits;return enqueue(std::move(job));
}
bool queueProfileLoad(std::filesystem::path const& path, std::uint64_t revision) { return enqueue({Operation::Load,path,{},revision}); }
bool queueProfileDelete(std::filesystem::path const& path) { return enqueue({Operation::Delete,path}); }
std::vector<std::string> cachedProfileNames() { std::lock_guard lock(storageMutex); return profileNames; }
std::optional<LoadedProfile> takeLoadedProfile() { std::lock_guard lock(storageMutex); return std::exchange(loadedProfile,std::nullopt); }
std::string configStorageStatus() { std::lock_guard lock(storageMutex); return storageStatus; }
bool stopConfigStorage() noexcept {
    { std::lock_guard lock(storageMutex); storageStopping=true; storageChanged.notify_all(); }
    if (storageWorker.joinable()) storageWorker.join();
    std::lock_guard lock(storageMutex);
    return failedSaves.empty();
}
}
