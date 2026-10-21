#pragma once
#include "resourcepacks/WorldNativeApplicationReceipt.h"

namespace praxis::companion::packs {
// Selection callback return is not a native reload. The real engine's stack
// listener, finish, UI reload, post-UI loading query and current draw form a
// separate receipt. A foreign native reload invalidates the entire receipt.
// A public engine notification can schedule its actual native entry for a
// later game update or wrapped engine task. Request once, then await that
// hook; lack of a synchronous hook is not a native failure receipt.
enum class WorldEngineDispatchAction {Request,AwaitNativeEntry,Started};
inline constexpr WorldEngineDispatchAction worldEngineDispatchAction(bool requested,bool started) noexcept {
    return started?WorldEngineDispatchAction::Started:
        requested?WorldEngineDispatchAction::AwaitNativeEntry:WorldEngineDispatchAction::Request;
}
class WorldEngineReloadReceipt {
public:
    using Identity=WorldNativeApplicationReceipt::Identity;
    using FramePermit=WorldNativeApplicationReceipt::FramePermit;
    bool prepare(Identity id) noexcept {
        if(id_||!id)return false;id_=id;return true;
    }
    Identity identity() const noexcept{return id_;}
    bool selectionEntered(Identity id) noexcept {
        if(sealed_||!id_.same(id)||selectionEntered_)return false;selectionEntered_=true;return true;
    }
    bool selectionReturned(Identity id,bool normal,bool globalMatches) noexcept {
        if(sealed_||!id_.same(id)||!selectionEntered_||selectionNormal_||!normal||!globalMatches){seal();return false;}
        selectionNormal_=true;return true;
    }
    bool selectionNormal() const noexcept{return selectionNormal_&&!sealed_;}
    bool engineEntered(Identity id,std::uint64_t serial,bool fullMatches,bool preparedPeers) noexcept {
        if(sealed_||!id_.same(id)||!selectionEntered_||serial_||!serial||!fullMatches||!preparedPeers){seal();return false;}
        serial_=serial;return true;
    }
    bool engineReturned(std::uint64_t serial,bool normal) noexcept {
        if(sealed_||!serial_||serial!=serial_||engineNormal_||!normal){seal();return false;}
        engineNormal_=true;return true;
    }
    bool finishReturned(std::uint64_t serial,bool normal) noexcept {
        if(sealed_||!serial_||serial!=serial_||finishNormal_||!normal){seal();return false;}
        finishNormal_=true;return true;
    }
    bool needsUI(std::uint64_t serial,bool pending,bool fullMatches,bool loadingFinished) const noexcept {
        return !sealed_&&selectionNormal_&&serial_&&serial==serial_&&engineNormal_&&finishNormal_
            &&!uiRequested_&&!pending&&fullMatches&&loadingFinished;
    }
    bool requestUI(std::uint64_t serial) noexcept {
        if(sealed_||!selectionNormal_||!serial_||serial!=serial_||!engineNormal_||!finishNormal_||uiRequested_){seal();return false;}
        uiRequested_=true;return true;
    }
    bool uiReturned(std::uint64_t serial,bool normal) noexcept {
        if(sealed_||serial!=serial_||!uiRequested_||uiNormal_||!normal){seal();return false;}
        uiNormal_=true;return true;
    }
    bool loadingObserved(std::uint64_t serial,bool pending,bool fullMatches,bool loadingFinished) noexcept {
        if(sealed_||!serial_||serial!=serial_||!uiNormal_)return false;
        loaded_=!pending&&fullMatches&&loadingFinished;
        if(!loaded_){framePending_=frameReady_=levelRendered_=false;frame_={};}
        return applicationReady();
    }
    bool applicationReady() const noexcept {
        return id_&&!sealed_&&selectionNormal_&&serial_&&engineNormal_&&finishNormal_&&uiNormal_&&loaded_;
    }
    std::uint64_t serial() const noexcept{return serial_;}
    bool engineNormal() const noexcept{return engineNormal_&&!sealed_;}
    bool finishNormal() const noexcept{return finishNormal_&&!sealed_;}
    bool sealed() const noexcept{return sealed_;}
    void seal() noexcept {sealed_=true;}
    FramePermit beginFrame(std::uintptr_t game,std::uintptr_t client,std::uint64_t world,
        std::uintptr_t renderer,std::uint64_t frame) noexcept {
        if(!applicationReady()||framePending_||frameReady_||game!=id_.game||client!=id_.client||world!=id_.world||!renderer||!frame)return {};
        frame_={id_,game,client,renderer,world,frame};framePending_=true;levelRendered_=false;return frame_;
    }
    bool levelRendered(FramePermit const& p,std::uintptr_t renderer,bool normal) noexcept {
        if(!sameFrame(p)||renderer!=p.renderer)return false;if(!normal){seal();return false;}levelRendered_=true;return true;
    }
    bool frameReturned(FramePermit const& p,std::uintptr_t game,std::uintptr_t client,
        std::uint64_t world,std::uintptr_t renderer,bool normal) noexcept {
        if(!sameFrame(p))return false;framePending_=false;
        if(!normal||game!=p.game||client!=p.client||world!=p.world||renderer!=p.renderer){seal();return false;}
        if(!levelRendered_)return false;frameReady_=true;return true;
    }
    bool frameReady() const noexcept{return applicationReady()&&frameReady_;}
    void requireCurrentFrame(std::uintptr_t renderer) noexcept {if(frameReady_&&renderer!=frame_.renderer){frameReady_=false;frame_={};}}
private:
    bool sameFrame(FramePermit const& p) const noexcept {return framePending_&&p&&id_.same(p.identity)
        &&p.frame==frame_.frame&&p.game==frame_.game&&p.client==frame_.client&&p.world==frame_.world&&p.renderer==frame_.renderer;}
    Identity id_{};FramePermit frame_{};std::uint64_t serial_{};
    bool selectionEntered_{},selectionNormal_{},engineNormal_{},finishNormal_{},uiRequested_{},uiNormal_{},loaded_{},sealed_{};
    bool framePending_{},frameReady_{},levelRendered_{};
};
}
