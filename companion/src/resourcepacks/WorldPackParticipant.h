#pragma once
#include "resourcepacks/WorldPackReloadApi.h"
#include <atomic>
#include <mutex>

namespace praxis::companion::packs {
// CPU admission and copied state only. Native owners are serviced by their
// existing UI/player callbacks, never by Query or an arbitrary calling thread.
class WorldPackParticipant {
    mutable std::mutex mutex;
    WorldPackReloadRequest current;
    WorldPackReloadPhase phase{WorldPackReloadPhase::Idle};
    std::atomic<bool> closed{}, stopped{};
    std::atomic<std::uint64_t> epoch{1};
    mutable std::atomic<std::uint64_t> rejected{};
    std::atomic<unsigned> readers{}, borrowers{}, jobs{};
    std::uint64_t lastToken{};
    bool previewPrepared{}, mapPrepared{}, previewReady{}, mapReady{};
    bool same(WorldPackReloadRequest const& r) const noexcept {
        return current.token==r.token&&current.world==r.world&&current.client==r.client
            &&current.renderer==r.renderer&&current.oldEpoch==r.oldEpoch&&current.nextEpoch==r.nextEpoch;
    }
    bool sameAttemptExceptRenderer(WorldPackReloadRequest const& r) const noexcept {
        return current.token==r.token&&current.world==r.world&&current.client==r.client
            &&current.oldEpoch==r.oldEpoch&&current.nextEpoch==r.nextEpoch;
    }
    bool drained() const noexcept {return !readers.load()&&!borrowers.load()&&!jobs.load();}
public:
    class Lease {
        WorldPackParticipant* owner{};
        bool native{};
        std::uint64_t generation{};
    public:
        explicit Lease(WorldPackParticipant& p,bool nativeBorrow=false):native(nativeBorrow) {
            std::lock_guard lock(p.mutex);
            if(p.closed.load()||p.stopped.load())return;
            generation=p.epoch.load();owner=&p;p.readers.fetch_add(1);
            if(native)p.borrowers.fetch_add(1);
        }
        ~Lease(){if(owner){if(native)owner->borrowers.fetch_sub(1);owner->readers.fetch_sub(1);}}
        Lease(Lease const&)=delete;
        bool admitted() const noexcept {return owner!=nullptr;}
        std::uint64_t resourceEpoch() const noexcept {return generation;}
        bool current() const noexcept {return owner&&owner->accept(generation);}
    };
    class AsyncLease {
        WorldPackParticipant* owner{};
        std::uint64_t generation{};
    public:
        AsyncLease(WorldPackParticipant& p,std::uint64_t value):generation(value){
            std::lock_guard lock(p.mutex);
            if(p.closed.load()||p.stopped.load()||value!=p.epoch.load())return;
            owner=&p;p.jobs.fetch_add(1);
        }
        ~AsyncLease(){if(owner)owner->jobs.fetch_sub(1);}
        bool admitted() const noexcept {return owner!=nullptr;}
        bool current() const noexcept {return owner&&owner->accept(generation);}
    };
    bool accept(std::uint64_t value) const noexcept {
        if(!value||value!=epoch.load()||closed.load()||stopped.load()){rejected.fetch_add(1);return false;}return true;
    }
    std::uint64_t resourceEpoch() const noexcept {return epoch.load();}
    bool paused() const noexcept {return closed.load();}
    bool quiescent() const noexcept {return drained();}
    WorldPackReloadPhase state() const noexcept {std::lock_guard lock(mutex);return phase;}
    bool ownerMatches(std::uint64_t world,std::uintptr_t client,std::uintptr_t renderer) const noexcept {
        std::lock_guard lock(mutex);
        return closed.load()&&current.world==world&&current.client==client&&current.renderer==renderer;
    }
    // Existing owner callbacks call this after their real local cache handling.
    void ownerServiced(bool preview,std::uint64_t value,bool prepared,bool ready) noexcept {
        std::lock_guard lock(mutex);
        if(!closed.load()||value!=epoch.load()||phase==WorldPackReloadPhase::Invalidated)return;
        if(preview){previewPrepared=prepared;previewReady=ready;}else{mapPrepared=prepared;mapReady=ready;}
    }
    void stop() noexcept {std::lock_guard lock(mutex);stopped=true;closed=true;phase=WorldPackReloadPhase::Invalidated;}
    int command(WorldPackReloadCommand command,WorldPackReloadRequest const* r,WorldPackReloadReply* reply) noexcept {
        if(!r||!reply||r->bytes!=sizeof(*r)||r->abi!=WorldPackReloadAbi
            ||reply->bytes!=sizeof(*reply)||reply->abi!=WorldPackReloadAbi)return 0;
        std::lock_guard lock(mutex);
        bool valid=r->token&&r->world&&r->client&&r->renderer&&r->oldEpoch&&r->nextEpoch>r->oldEpoch;
        int accepted{};
        if(valid&&!stopped.load()){
            if(command==WorldPackReloadCommand::Pause&&phase==WorldPackReloadPhase::Idle
                &&r->token>lastToken&&r->oldEpoch==epoch.load()){
                current=*r;lastToken=r->token;closed=true;previewPrepared=mapPrepared=previewReady=mapReady=false;
                phase=WorldPackReloadPhase::Pausing;accepted=1;
            }else {
                // A native reload may replace the renderer. The coordinator
                // supplies its current nonzero renderer only at the settled
                // boundary; no earlier command may rewrite the old owner.
                if(command==WorldPackReloadCommand::NativeSettled&&phase==WorldPackReloadPhase::NativeActive
                    &&drained()&&sameAttemptExceptRenderer(*r))current.renderer=r->renderer;
                if(same(*r)){
                switch(command){
                case WorldPackReloadCommand::Query:accepted=1;break;
                case WorldPackReloadCommand::Pause:accepted=phase==WorldPackReloadPhase::Pausing;break;
                case WorldPackReloadCommand::Prepare:
                    if(phase==WorldPackReloadPhase::Prepared){accepted=1;break;}
                    if(phase==WorldPackReloadPhase::Pausing&&drained()&&previewPrepared&&mapPrepared){phase=WorldPackReloadPhase::Prepared;accepted=1;}break;
                case WorldPackReloadCommand::BeginNative:
                    if(phase==WorldPackReloadPhase::Prepared){phase=WorldPackReloadPhase::NativeActive;accepted=1;}
                    else accepted=phase==WorldPackReloadPhase::NativeActive;break;
                case WorldPackReloadCommand::NativeSettled:
                    if(phase==WorldPackReloadPhase::NativeActive&&drained()){epoch=r->nextEpoch;previewReady=mapReady=false;phase=WorldPackReloadPhase::Reacquiring;accepted=1;}
                    else accepted=phase==WorldPackReloadPhase::Reacquiring||phase==WorldPackReloadPhase::Ready;break;
                case WorldPackReloadCommand::Resume:
                    if(phase==WorldPackReloadPhase::Ready&&drained()){closed=false;phase=WorldPackReloadPhase::Idle;accepted=1;}break;
                case WorldPackReloadCommand::CancelBeforeNative:
                    if((phase==WorldPackReloadPhase::Pausing||phase==WorldPackReloadPhase::Prepared)&&drained()){phase=WorldPackReloadPhase::Cancelled;accepted=1;}
                    else accepted=phase==WorldPackReloadPhase::Cancelled;break;
                case WorldPackReloadCommand::ResumeCancelled:
                    if(phase==WorldPackReloadPhase::Cancelled&&drained()){closed=false;phase=WorldPackReloadPhase::Idle;accepted=1;}break;
                case WorldPackReloadCommand::Stop:closed=true;phase=WorldPackReloadPhase::Invalidated;accepted=1;break;
                }
                }
            }
        }
        if(phase==WorldPackReloadPhase::Reacquiring&&drained()&&previewReady&&mapReady)phase=WorldPackReloadPhase::Ready;
        *reply={};reply->token=current.token;reply->world=current.world;reply->client=current.client;reply->renderer=current.renderer;
        reply->epoch=epoch.load();reply->phase=phase;reply->cpuReaders=readers.load();reply->nativeBorrowers=borrowers.load();reply->asyncJobs=jobs.load();
        reply->admissionClosed=closed.load();reply->oldOwnersRetained=1;
        // Preview submits only through the engine ItemRenderer. Map's fixed RGB
        // textures stay retained and are independent of pack atlas/materials.
        // This participant has no pack-dependent owned GPU allocation to retire.
        reply->ownedGpu=OwnedGpuFence::NoOwnedSubmission;reply->rejectedOldPublications=rejected.load();
        reply->error=accepted?0:1;return accepted;
    }
};
inline WorldPackParticipant& praxisWorldParticipant(){static auto* value=new WorldPackParticipant;return *value;}
inline std::uint64_t currentResourceEpoch() noexcept {return praxisWorldParticipant().resourceEpoch();}
inline bool resourcePublicationCurrent(std::uint64_t epoch) noexcept {return praxisWorldParticipant().accept(epoch);}
}
