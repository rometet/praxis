#include "platform/FreeCameraOffsetTransaction.h"
#include "platform/FreeCameraInputPolicy.h"
#include "core/CameraCandidatePolicy.h"
#include "mc/deps/ecs/EntityId.h"
#include <entt/entt.hpp>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace praxis::companion;
using Direct=MinecraftCamera::CameraDirectLookComponent;
using Offset=MinecraftCamera::CameraOffsetComponent;
using Active=MinecraftCamera::ActiveCameraComponent;
using Orbit=MinecraftCamera::CameraOrbitComponent;
using Link=VanillaCamera::UpdatePlayerFromCameraComponent;
using Registry=entt::basic_registry<EntityId>;
unsigned checks{};
void check(bool result,char const* text){++checks;if(!result)throw std::runtime_error(text);}
bool eq(glm::vec3 const& a,glm::vec3 const& b){return a.x==b.x&&a.y==b.y&&a.z==b.z;}
EntityId rig(Registry& ecs){auto id=ecs.create();ecs.emplace<Active>(id);ecs.emplace<Direct>(id);ecs.emplace<Link>(id);return id;}
int main(){try{
    for(bool preexisting:{false,true}) {
        Registry ecs;auto id=rig(ecs);
        if(preexisting){auto& o=ecs.emplace<Offset>(id);o.mEntityOffset=glm::vec3{1,2,3};o.mPivot=glm::vec3{4,5,6};o.mCalculatedOffset=glm::vec3{7,8,9};}
        auto s=takeFreeCameraOffset(ecs,id);check(bool(s),"normal SDK rig admission");
        check(s->added!=preexisting,"original component presence captured");
        check(ecs.all_of<Link>(id),"taking Offset does not detach or modify Link");
        check(!writeFreeCameraOffset(ecs,id,*s,{8,9,10}),"attached body owner blocks write");
        ecs.remove<Link>(id);
        check(writeFreeCameraOffset(ecs,id,*s,{8,9,10}),"detached camera Offset write");
        check(eq(ecs.get<Offset>(id).mEntityOffset.get(),{8,9,10}),"exact typed value written");
        check(!writeFreeCameraOffset(ecs,id,*s,{std::numeric_limits<float>::quiet_NaN(),0,0}),"nonfinite write rejected");
        check(eq(ecs.get<Offset>(id).mEntityOffset.get(),{8,9,10}),"nonfinite failure leaves values intact");
        if(preexisting){check(eq(ecs.get<Offset>(id).mPivot.get(),{4,5,6}),"native pivot preserved");check(eq(ecs.get<Offset>(id).mCalculatedOffset.get(),{7,8,9}),"native derived value preserved");}
        check(restoreFreeCameraOffset(ecs,id,*s),"normal restore succeeds");
        check(ecs.all_of<Offset>(id)==preexisting,"original component presence restored");
        if(preexisting)check(eq(ecs.get<Offset>(id).mEntityOffset.get(),{1,2,3}),"original Offset value restored");
    }
    for(int change=0;change<5;++change) {
        Registry ecs;auto id=rig(ecs);auto s=takeFreeCameraOffset(ecs,id);ecs.remove<Link>(id);
        check(writeFreeCameraOffset(ecs,id,*s,{2,3,4}),"fault setup write");
        if(change==0)ecs.emplace<Link>(id);
        if(change==1)ecs.remove<Active>(id);
        if(change==2)ecs.emplace<Orbit>(id);
        if(change==3)ecs.remove<Direct>(id);
        if(change==4)ecs.get<Offset>(id).mEntityOffset=glm::vec3{9,8,7};
        auto prior=ecs.get<Offset>(id).mEntityOffset.get();
        check(!writeFreeCameraOffset(ecs,id,*s,{30,31,32}),"owner/shape/conflicting write rejected");
        check(!restoreFreeCameraOffset(ecs,id,*s),"changed owner/shape never restored over");
        check(eq(prior,ecs.get<Offset>(id).mEntityOffset.get()),"conflicting owner's value preserved");
    }
    {
        Registry ecs;auto old=rig(ecs);auto s=takeFreeCameraOffset(ecs,old);ecs.remove<Link>(old);ecs.destroy(old);
        auto replacement=rig(ecs);ecs.emplace<Offset>(replacement).mEntityOffset=glm::vec3{11,12,13};
        check(old!=replacement,"entity version distinguishes reused index");
        check(!writeFreeCameraOffset(ecs,old,*s,{4,5,6}),"destroyed version cannot write");
        check(!restoreFreeCameraOffset(ecs,old,*s),"destroyed version cannot restore");
        check(eq(ecs.get<Offset>(replacement).mEntityOffset.get(),{11,12,13}),"replacement camera untouched");
    }
    {
        Registry ecs;auto id=rig(ecs);auto s=takeFreeCameraOffset(ecs,id);ecs.remove<Link>(id);ecs.remove<Offset>(id);
        check(!writeFreeCameraOffset(ecs,id,*s,{1,1,1}),"missing Offset is not recreated mid-session");
        check(!restoreFreeCameraOffset(ecs,id,*s),"missing Offset prevents stale restore");
        ecs.emplace<Offset>(id).mEntityOffset=glm::vec3{9,9,9};
        check(!writeFreeCameraOffset(ecs,id,*s,{1,1,1}),"changed-value replacement rejected");
        // Deliberately expose the remaining SDK limitation, without calling it
        // lifetime-safe: an equal-value replacement is not distinguishable.
        ecs.get<Offset>(id).mEntityOffset=s->lastWritten;
        check(freeCameraOffsetMatches(ecs,id,*s),"same-value ABA is a documented unresolved distinction");
    }
    {
        Registry ecs;auto id=rig(ecs);ecs.emplace<Offset>(id).mEntityOffset=glm::vec3{0,std::numeric_limits<float>::infinity(),0};
        check(!takeFreeCameraOffset(ecs,id),"nonfinite existing Offset rejected before detaching");
        check(ecs.all_of<Link>(id),"rejected admission preserves Link");
        ecs.emplace<Orbit>(id);check(!takeFreeCameraOffset(ecs,id),"third-person candidate rejects at admission");
    }
    for(int bit=0;bit<static_cast<int>(MoveInputState::Flag::Count);++bit) {
        RawMoveInputComponent raw{};
        raw.mRawMove->x=.4f;raw.mRawMove->y=-.7f;
        raw.mRawInput->mAnalogMoveVector->x=.8f;raw.mRawInput->mAnalogMoveVector->y=.3f;
        raw.mRawInput->mFlagValues->set(bit,true);
        raw.mRawInput->mLookSlightDirField=3;raw.mRawInput->mLookNormalDirField=5;raw.mRawInput->mLookSmoothDirField=7;
        withholdFreeCameraMovement(raw);
        check(raw.mRawMove->x==0&&raw.mRawMove->y==0,"raw movement neutralized");
        check(raw.mRawInput->mAnalogMoveVector->x==0&&raw.mRawInput->mAnalogMoveVector->y==0,"analog movement neutralized");
        bool retained=bit==static_cast<int>(MoveInputState::Flag::BlockSelectDown)||bit==static_cast<int>(MoveInputState::Flag::LookCenter);
        check(raw.mRawInput->mFlagValues->test(bit)==retained,"movement flags withheld; selection/centering retained");
        check(raw.mRawInput->mLookSlightDirField==3&&raw.mRawInput->mLookNormalDirField==5&&raw.mRawInput->mLookSmoothDirField==7,"look fields unchanged");
    }
    {
        CameraCandidateMotion m;check(m.begin(0,0,-1,1),"displacement-only motion begins");
        check(m.move(1,1,{0,0,-1}),"camera displacement stages");
        check(m.moveTimed(1,0,0,4,.1f,{1,0,0}),"fresh render basis controls flight");
        check(m.pose()->yaw==0&&m.pose()->pitch==0,"native angles never enter private pose");
        check(m.move(32,4,{1,0,0})&&m.pose()->displacement[0]>128,"wheel flight has no 16m cap");
        check(!CameraCandidateOptions{}.enabled,"FreeCamera candidate defaults OFF");
    }
    {
        CameraCandidateMotion m;check(m.begin(0,0,-1,1),"far flight motion begins");
        bool wheel=true,timed=true;
        for(int i=0;i<1000;++i)wheel=wheel&&m.move(32,4,{0,0,-1});
        for(int i=0;i<1000;++i)timed=timed&&m.moveTimed(0,-1,0,16,.1f,{0,0,-1});
        check(wheel&&m.pose()->displacement[2]<-120000,"repeated wheel moves far beyond former cap");
        check(timed&&m.pose()->displacement[0]<-1500,"timed lateral flight has no accumulated distance cap");
        auto before=m.pose()->displacement;
        for(auto bad:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity()}) {
            check(!m.move(1,bad,{0,0,-1}),"nonfinite distance remains invalid");
            check(!m.move(1,1,{bad,0,-1}),"nonfinite wheel direction remains invalid");
            check(!m.moveTimed(1,0,0,bad,.1f,{0,0,-1}),"nonfinite speed remains invalid");
            check(!m.moveTimed(1,0,0,4,bad,{0,0,-1}),"nonfinite duration remains invalid");
            check(!m.moveTimed(1,0,0,4,.1f,{bad,0,-1}),"nonfinite timed direction remains invalid");
            check(m.pose()->displacement==before,"numeric rejection is atomic");
        }
    }
    for(bool preexisting:{false,true})for(float sign:{-1.f,1.f}) {
        Registry ecs;auto id=rig(ecs);glm::vec3 original{2,3,4};
        if(preexisting)ecs.emplace<Offset>(id).mEntityOffset=original;
        auto snapshot=takeFreeCameraOffset(ecs,id);ecs.remove<Link>(id);
        glm::vec3 far{sign*1e20f,-sign*1e20f,sign*1e20f};
        check(finiteFreeCameraOffset(far)&&!std::isfinite(glm::dot(far,far)),"large components can be finite while squared norm overflows");
        check(writeFreeCameraOffset(ecs,id,*snapshot,far),"large positive/negative finite Offset has no distance cap");
        check(eq(ecs.get<Offset>(id).mEntityOffset.get(),far),"large Offset is written without clamping");
        for(auto bad:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity()}) {
            check(!writeFreeCameraOffset(ecs,id,*snapshot,{bad,0,0}),"NaN/inf Offset rejected");
            check(eq(ecs.get<Offset>(id).mEntityOffset.get(),far),"invalid Offset cannot corrupt current lease");
        }
        auto largest=std::numeric_limits<float>::max();glm::vec3 overflowing=glm::vec3{largest}+glm::vec3{largest};
        check(!writeFreeCameraOffset(ecs,id,*snapshot,overflowing),"arithmetic overflow rejected before component write");
        check(restoreFreeCameraOffset(ecs,id,*snapshot),"far lease restores normally");
        check(ecs.all_of<Offset>(id)==preexisting,"far restore returns original Offset presence");
        if(preexisting)check(eq(ecs.get<Offset>(id).mEntityOffset.get(),original),"far restore returns exact original Offset value");
    }
    std::cout<<"FreeCamera SDK Offset/input transactions: "<<checks<<" checks PASS; Minecraft NOT_RUN\n";return 0;
}catch(std::exception const& e){std::cerr<<e.what()<<'\n';return 1;}}
