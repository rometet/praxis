#pragma once
#include "resourcepacks/WorldNativeTaskReceipt.h"
#include <cstdint>

namespace praxis::companion::packs {
// CPU receipt join for the three audited native callable bodies. A task
// wrapper's return/disposal never advances this protocol: only each original
// native body detour, renderer reinit, and a same-owner loading observation do.
class WorldNativeExecutedCompletion {
public:
    enum class Part { Finalize, Level26, Tess11 };
    struct ObservedOwner {std::uintptr_t game{},client{};std::uint64_t world{};};
    struct FramePermit {
        WorldNativeProducer source;
        std::uintptr_t game{},client{},renderer{};
        std::uint64_t world{},frame{};
        constexpr explicit operator bool() const noexcept {return bool(source)&&game&&client&&renderer&&world&&frame;}
    };
    bool begin(WorldNativeProducer source,std::uintptr_t game,std::uintptr_t client,std::uint64_t world) noexcept {
        if(started_||!source||!game||!client||!world)return false;
        source_=source;game_=game;client_=client;world_=world;started_=true;return true;
    }
    bool enter(Part part,WorldNativeInvocation const& entry,std::uintptr_t callableGame,ObservedOwner current) noexcept {
        if(!started_||sealed_||!entry||!source_.same(entry.source))return false;
        if(callableGame!=game_||!sameOwner(current)){seal();return false;}
        auto& p=phase(part);
        if(part==Part::Tess11&&(!finalize_.entered||entry.parentInvocation!=finalize_.entry.invocation))return false;
        if(part==Part::Finalize&&p.entered){seal();return false;}
        if(p.entered&&(p.entry.task!=entry.task||p.inFlight||p.done)){seal();return false;}
        p.entry=entry;p.entered=true;p.inFlight=true;return true;
    }
    bool returned(Part part,WorldNativeInvocation const& entry,ObservedOwner current,bool normal,bool done) noexcept {
        if(!started_||sealed_||!entry||!source_.same(entry.source))return false;
        if(!sameOwner(current)){seal();return false;}
        auto& p=phase(part);
        if(!p.inFlight||p.entry.task!=entry.task||p.entry.invocation!=entry.invocation){seal();return false;}
        p.inFlight=false;
        if(!normal){seal();return false;}
        p.done=done;
        return true;
    }
    bool rendererReturned(WorldNativeInvocation const& level,std::uintptr_t renderer,ObservedOwner current,bool normal) noexcept {
        if(!started_||sealed_||!source_.same(level.source))return false;
        if(!sameOwner(current)||!level_.inFlight||level_.entry.task!=level.task||level_.entry.invocation!=level.invocation||!renderer){seal();return false;}
        if(!normal){seal();return false;}
        renderer_=renderer;rendererExecuted_=true;return true;
    }
    bool loadingFinished(std::uintptr_t game,std::uintptr_t client,std::uint64_t world,bool finished) noexcept {
        if(!started_||sealed_||game!=game_||client!=client_||world!=world_)return false;
        if(!finalize_.done||!level_.done||!tess_.done||!rendererExecuted_||!finished)return false;
        loadingFinished_=true;return true;
    }
    bool readyForLoading() const noexcept {
        return started_&&!sealed_&&finalize_.done&&level_.done&&tess_.done&&rendererExecuted_&&!loadingFinished_;
    }
    // This is an input to the later owner/frame join, not a resume permit.
    bool nativeReadyForFrame() const noexcept {return started_&&!sealed_&&finalize_.done&&level_.done&&tess_.done&&rendererExecuted_&&loadingFinished_;}
    FramePermit beginFrame(std::uintptr_t game,std::uintptr_t client,std::uint64_t world,
        std::uintptr_t currentRenderer,std::uint64_t frame) noexcept {
        if(!nativeReadyForFrame()||framePending_||frameReady_||game!=game_||client!=client_||world!=world_
            ||!currentRenderer||!frame)return {};
        framePermit_={source_,game,client,currentRenderer,world,frame};
        framePending_=true;levelRendered_=false;return framePermit_;
    }
    bool levelRendered(FramePermit const& permit,std::uintptr_t renderer,bool normal) noexcept {
        if(!framePending_||!sameFrame(permit)||renderer!=permit.renderer)return false;
        if(!normal){seal();return false;}
        levelRendered_=true;return true;
    }
    bool frameReturned(FramePermit const& permit,std::uintptr_t game,std::uintptr_t client,
        std::uint64_t world,std::uintptr_t renderer,bool normal) noexcept {
        if(!framePending_||!sameFrame(permit))return false;
        framePending_=false;
        if(!normal||game!=permit.game||client!=permit.client||world!=permit.world
            ||renderer!=permit.renderer){seal();return false;}
        if(!levelRendered_)return false; // a frame may legitimately skip world draw
        frameReady_=true;return true;
    }
    bool frameReady() const noexcept {return nativeReadyForFrame()&&frameReady_;}
    std::uintptr_t frameRenderer() const noexcept {return frameReady_?framePermit_.renderer:0;}
    void requireCurrentFrame(std::uintptr_t renderer) noexcept {
        if(frameReady_&&renderer!=framePermit_.renderer){frameReady_=false;framePermit_={};}
    }
    std::uintptr_t renderer() const noexcept {return renderer_;}
    WorldNativeProducer const& source() const noexcept {return source_;}
    bool sealed() const noexcept {return sealed_;}
    void seal() noexcept {sealed_=true;}
private:
    bool sameOwner(ObservedOwner current) const noexcept {
        return current.game==game_&&current.client==client_&&current.world==world_;
    }
    bool sameFrame(FramePermit const& permit) const noexcept {
        return permit&&framePermit_.source.same(permit.source)&&framePermit_.frame==permit.frame
            &&framePermit_.game==permit.game&&framePermit_.client==permit.client
            &&framePermit_.world==permit.world&&framePermit_.renderer==permit.renderer;
    }
    struct PhaseReceipt {WorldNativeInvocation entry;bool entered{},inFlight{},done{};};
    PhaseReceipt& phase(Part part) noexcept {return part==Part::Finalize?finalize_:part==Part::Level26?level_:tess_;}
    WorldNativeProducer source_{};
    std::uintptr_t game_{},client_{},renderer_{};
    std::uint64_t world_{};
    PhaseReceipt finalize_,level_,tess_;
    FramePermit framePermit_{};
    bool started_{},sealed_{},rendererExecuted_{},loadingFinished_{},framePending_{},levelRendered_{},frameReady_{};
};
}
