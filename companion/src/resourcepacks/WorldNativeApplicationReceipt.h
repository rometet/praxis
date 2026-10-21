#pragma once
#include <cstdint>

namespace praxis::companion::packs {
// Share the real adapter's ordered/short-circuit binding decision with tests.
// Attach receives only the four fixed typed SDK hook stage IDs, never an address.
template<class Check,class Attach> bool registerWorldApplicationAdapter(Check&& check,Attach&& attach) noexcept {
    if(!check(1)||!check(2)||!check(3))return false;
    for(unsigned stage=20;stage!=24;++stage)if(!attach(stage))return false;
    return true;
}
// Receipts from the normal native Applying screen, never a private task or GPU
// completion claim. All inputs are copied identities observed by typed hooks.
class WorldNativeApplicationReceipt {
public:
    struct Identity {
        std::uint64_t token{},attempt{},world{},thread{};
        std::uintptr_t game{},client{},model{};
        explicit operator bool() const noexcept {return token&&attempt&&world&&thread&&game&&client&&model;}
        bool same(Identity const& b) const noexcept {return token==b.token&&attempt==b.attempt&&world==b.world
            &&thread==b.thread&&game==b.game&&client==b.client&&model==b.model;}
    };
    struct TickPermit {
        Identity identity;
        std::uintptr_t controller{},scene{};
        std::uint64_t tick{};
        explicit operator bool() const noexcept {return bool(identity)&&controller&&scene&&tick;}
    };
    struct FramePermit {
        Identity identity;
        std::uintptr_t game{},client{},renderer{};
        std::uint64_t world{},frame{};
        explicit operator bool() const noexcept {return bool(identity)&&game&&client&&renderer&&world&&frame;}
    };
    bool prepare(Identity id) noexcept {
        if(prepared_||!id)return false;identity_=id;prepared_=true;return true;
    }
    Identity identity() const noexcept {return identity_;}
    TickPermit enterTick(Identity id,std::uintptr_t controller,std::uintptr_t scene,std::uint64_t tick) noexcept {
        if(!prepared_||sealed_||!identity_.same(id)||ready_||!controller||!scene||!tick)return {};
        if(tickActive_||(controller_&&(controller_!=controller||scene_!=scene))){seal();return {};}
        controller_=controller;scene_=scene;tickActive_=true;querySeen_=queryFinished_=false;
        tick_={id,controller,scene,tick};return tick_;
    }
    bool callbackEntered(TickPermit const& p) noexcept {
        if(!sameTick(p)||sealed_)return false;
        if(callbackEntered_){seal();return false;}callbackEntered_=true;return true;
    }
    bool callbackReturned(TickPermit const& p,bool normal) noexcept {
        if(!sameTick(p)||sealed_)return false;
        if(!callbackEntered_||callbackNormal_||!normal){seal();return false;}callbackNormal_=true;return true;
    }
    bool loadingQueried(TickPermit const& p,std::uintptr_t model,bool value) noexcept {
        if(!sameTick(p)||sealed_||model!=identity_.model)return false;
        // Native tick queries before a callback is not application completion.
        if(!callbackNormal_)return false;querySeen_=true;queryFinished_=value;return true;
    }
    bool tickReturned(TickPermit const& p,bool sameOwner,bool normal,bool sceneExited) noexcept {
        if(!sameTick(p)||sealed_)return false;tickActive_=false;
        if(!sameOwner||!normal){seal();return false;}
        if(sceneExited&&(!callbackNormal_||!querySeen_||!queryFinished_)){seal();return false;}
        if(callbackNormal_&&querySeen_&&queryFinished_&&sceneExited)ready_=true;
        return ready_;
    }
    bool applicationReady() const noexcept {return prepared_&&!sealed_&&ready_;}
    bool callbackNormal() const noexcept {return prepared_&&!sealed_&&callbackNormal_;}
    bool sealed() const noexcept {return sealed_;}
    void seal() noexcept {sealed_=true;}
    FramePermit beginFrame(std::uintptr_t game,std::uintptr_t client,std::uint64_t world,
        std::uintptr_t renderer,std::uint64_t frame) noexcept {
        if(!applicationReady()||framePending_||frameReady_||game!=identity_.game||client!=identity_.client
            ||world!=identity_.world||!renderer||!frame)return {};
        frame_={identity_,game,client,renderer,world,frame};framePending_=true;levelRendered_=false;return frame_;
    }
    bool levelRendered(FramePermit const& p,std::uintptr_t renderer,bool normal) noexcept {
        if(!sameFrame(p)||renderer!=p.renderer)return false;
        if(!normal){seal();return false;}levelRendered_=true;return true;
    }
    bool frameReturned(FramePermit const& p,std::uintptr_t game,std::uintptr_t client,
        std::uint64_t world,std::uintptr_t renderer,bool normal) noexcept {
        if(!sameFrame(p))return false;framePending_=false;
        if(!normal||game!=p.game||client!=p.client||world!=p.world||renderer!=p.renderer){seal();return false;}
        if(!levelRendered_)return false;frameReady_=true;return true;
    }
    bool frameReady() const noexcept {return applicationReady()&&frameReady_;}
    void requireCurrentFrame(std::uintptr_t renderer) noexcept {
        if(frameReady_&&renderer!=frame_.renderer){frameReady_=false;frame_={};}
    }
private:
    bool sameTick(TickPermit const& p) const noexcept {return tickActive_&&p&&identity_.same(p.identity)
        &&p.controller==controller_&&p.scene==scene_&&p.tick==tick_.tick;}
    bool sameFrame(FramePermit const& p) const noexcept {return framePending_&&p&&identity_.same(p.identity)
        &&p.frame==frame_.frame&&p.game==frame_.game&&p.client==frame_.client&&p.world==frame_.world&&p.renderer==frame_.renderer;}
    Identity identity_{};TickPermit tick_{};FramePermit frame_{};
    std::uintptr_t controller_{},scene_{};
    bool prepared_{},sealed_{},callbackEntered_{},callbackNormal_{},tickActive_{},querySeen_{},queryFinished_{},ready_{};
    bool framePending_{},frameReady_{},levelRendered_{};
};
}
