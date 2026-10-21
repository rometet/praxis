// Generated CameraCandidate.cpp body is included unchanged. Native/service/
// focus/player lifetime boundaries are deterministic doubles. Camera component
// transactions use the real pinned SDK types and real EnTT registry.
#include "platform/CameraRigTransaction.h"
#include "core/CameraFlightDirection.h"
#include "core/CameraInputPolicy.h"
#include "core/Settings.h"
#include "core/RuntimePolicy.h"
#include "gui/GuiBackendPolicy.h"
#include "mc/deps/ecs/EntityId.h"
#include "mc/entity/components/ActorRotationComponent.h"
#include "mc/entity/components/ActorHeadRotationComponent.h"
#include "mc/deps/minecraft_camera/components/ActiveCameraComponent.h"
#include "mc/deps/minecraft_camera/components/CameraOrbitComponent.h"
#include "mc/world/actor/ActorFlags.h"
#include "mc/deps/input/MouseAction.h"
#include <entt/entt.hpp>
#include <Windows.h>
#include <imgui.h>
#include <atomic>
#include <mutex>
#include <memory>
#include <string>
#include <iostream>
#include <stdexcept>
#include <limits>
#include <vector>
namespace praxis::companion {
using FixtureRegistry=entt::basic_registry<EntityId>;
unsigned fixtureEcsReads{},fixtureRigReads{};
struct FixtureRegistryOwner {std::shared_ptr<FixtureRegistry> mRegistry=std::make_shared<FixtureRegistry>();std::weak_ptr<FixtureRegistryOwner> self;};
struct EntityContext;
struct WeakEntityRef {
    std::weak_ptr<FixtureRegistryOwner> mRegistry;EntityId mEntity{};
    std::optional<EntityContext> lock() const;
};
struct EntityContext {
    FixtureRegistryOwner& mRegistry;EntityId mEntity;
    FixtureRegistry& getRegistry() const{++fixtureEcsReads;return *mRegistry.mRegistry;}
    WeakEntityRef getWeakRef() const{return {mRegistry.self,mEntity};}
    template<class T>T* tryGetComponent() const{return getRegistry().try_get<T>(mEntity);}
};
void fixtureCloseContextAtEye(int mode);
std::optional<EntityContext> WeakEntityRef::lock() const {
    auto owner=mRegistry.lock();if(!owner||!owner->mRegistry->valid(mEntity))return {};
    return EntityContext{*owner,mEntity};
}
struct LocalPlayer {
    EntityContext context;glm::vec3 eye{};int dimension{};
    bool alive{true},sleeping{},usingItem{},mounted{},advanceGenerationAtEye{};int closeContextAtEye{};
    EntityContext& getEntityContext(){return context;}
    auto getRotation(){return context.tryGetComponent<ActorRotationComponent>()->mRot.get();}
    glm::vec3 getEyePos(){if(advanceGenerationAtEye){advanceGenerationAtEye=false;worldGeneration.fetch_add(1);}
        if(closeContextAtEye){auto mode=closeContextAtEye;closeContextAtEye=0;fixtureCloseContextAtEye(mode);}return eye;}
    int getDimensionId(){return dimension;}
    bool isAlive(){return alive;}bool isSleeping(){return sleeping;}
    void* getVehicle(){return mounted?this:nullptr;}
    bool getStatusFlag(ActorFlags){return usingItem;}
};
enum class ClientInstanceState {Playing,Stopped};
void fixtureSwitchPrimaryAtRig();
struct FixtureCameraRegistry {
    struct Field {std::vector<std::unique_ptr<EntityContext>> value;mutable bool advanceGenerationAtRead{},switchPrimaryAtRead{};
        auto const& get() const{++fixtureRigReads;if(advanceGenerationAtRead){advanceGenerationAtRead=false;worldGeneration.fetch_add(1);}
            if(switchPrimaryAtRead){switchPrimaryAtRead=false;fixtureSwitchPrimaryAtRig();}return value;}} mCameraEntities;
};
struct IClientInstance {
    LocalPlayer* player{};std::shared_ptr<FixtureCameraRegistry> cameras=std::make_shared<FixtureCameraRegistry>();
    std::string screen="hud_screen";bool playing{true},keyboard{true},grabbed{true};
    LocalPlayer* getLocalPlayer(){return player;}
    auto getCameraRegistry(){return cameras;}
    std::string getTopScreenName(){return screen;}
    ClientInstanceState getClientInstanceState(){return playing?ClientInstanceState::Playing:ClientInstanceState::Stopped;}
    bool currentInputModeIsMouseAndKeyboard(){return keyboard;}bool getMouseGrabbed(){return grabbed;}
};
using ClientInstance=IClientInstance;
struct CameraClientInstanceComponent {
    struct Pointer {IClientInstance* value{};IClientInstance* get() const{return value;}} mClientInstance;
};
struct LevelRendererPlayer {
    IClientInstance& mClientInstance;
    struct Field {glm::vec3 value{0,0,-1};bool advanceGenerationAtRead{};
        glm::vec3 const& get(){if(advanceGenerationAtRead){advanceGenerationAtRead=false;worldGeneration.fetch_add(1);}return value;}} mCameraForward;
};
IClientInstance* fixturePrimary{};
IClientInstance* fixtureNextPrimary{};
void fixtureSwitchPrimaryAtRig(){fixturePrimary=fixtureNextPrimary;}
bool fixtureFocus{true},fixtureGuiCaptured{},fixtureConfigThrows{},fixtureAsyncUnavailable{},fixtureLoseFocusAtAsyncRead{};
DWORD fixtureThread=1;
std::uint64_t fixtureNow=1000;
GuiBackendKind fixtureBackend=GuiBackendKind::Standalone;
std::array<bool,256> fixtureKeys{};
std::array<unsigned,256> fixtureAsyncReads{};
std::vector<std::string> fixtureInputOrder;
RuntimeConfig fixtureConfig{};
std::array<int,3> fixtureMapBindings{};
int GetForegroundWindow(){return fixtureFocus?1:0;}
DWORD GetCurrentProcessId(){return 1;}
DWORD GetWindowThreadProcessId(int,DWORD* process){*process=1;return 1;}
DWORD GetCurrentThreadId(){return fixtureThread;}
ULONGLONG GetTickCount64(){return fixtureNow;}
SHORT GetAsyncKeyState(int key){
    if(key>=0&&key<256)++fixtureAsyncReads[key];
    if(fixtureLoseFocusAtAsyncRead){fixtureLoseFocusAtAsyncRead=false;fixtureFocus=false;return 0;}
    return !fixtureAsyncUnavailable&&key>=0&&key<256&&fixtureKeys[key]?static_cast<SHORT>(0x8000):0;
}
GuiBackendKind guiBackendKind() noexcept{return fixtureBackend;}
bool guiInputCaptured() noexcept{return fixtureGuiCaptured;}
std::array<int,3> mapActionBindings(){if(fixtureConfigThrows)throw std::bad_alloc();return fixtureMapBindings;}
void logGuiInfo(char const*) noexcept{}void logGuiError(char const*) noexcept{}
bool fixtureInventoryClaims{},fixtureZoomClaims{};
unsigned fixtureInventoryCalls{},fixtureZoomScaleCalls{},fixtureZoomScrollCalls{};
bool handleInventoryMouse(int,int,bool cancelled){++fixtureInventoryCalls;fixtureInputOrder.push_back("Inventory");return !cancelled&&fixtureInventoryClaims;}
bool handleZoomScroll(int){++fixtureZoomScrollCalls;fixtureInputOrder.push_back("Zoom");if(fixtureZoomClaims){zoomHeld=true;return true;}return false;}
void adjustZoomMouse(short& dx,short& dy){++fixtureZoomScaleCalls;fixtureInputOrder.push_back("ZoomRelative");dx/=2;dy/=2;}
}
namespace ll::service {praxis::companion::IClientInstance* getClientInstance(){return praxis::companion::fixturePrimary;}}
#include "camera-production-body.inc"
namespace praxis::companion {
void fixtureCloseContextAtEye(int mode) {
    if(mode==1)fixtureFocus=false;
    else if(mode==2){fixtureBackend=GuiBackendKind::LholoBridge;fixtureGuiCaptured=true;}
    else if(mode==3){retiring=true;mouseAdmission=false;stopRequested=true;}
}
}
using namespace praxis::companion;
unsigned fixtureChecks{};
void check(bool value,char const* reason){++fixtureChecks;if(!value)throw std::runtime_error(std::to_string(fixtureChecks)+": "+reason);}
struct FixtureMouseEvent {
    int action,data;short x{},y{};bool cancelled{};
    int actionButtonId(){return action;}int buttonData(){return data;}
    short& dx(){return x;}short& dy(){return y;}
    bool isCancelled(){return cancelled;}void cancel(){cancelled=true;}
};
struct FixtureKeyEvent {
    int key;bool down,cancelled{};
    int keyCode(){return key;}bool isDown(){return down;}
    bool isCancelled(){return cancelled;}void cancel(){cancelled=true;}
};
#include "camera-native-glue.inc"

// The real Runtime publisher is covered by the separate codec suite. Here this
// service boundary injects formal config values into the real immutable Settings
// type; no camera callback or transaction behavior is reimplemented.
void publishFixtureConfig(){
    auto value=std::make_shared<Settings>();
    for(unsigned id=0;id<featureCount;++id){value->keys[id]=fixtureConfig.keybinds[id];value->keyModifiers[id]=fixtureConfig.keybindModifiers[id];}
    value->enabled[20]=fixtureConfig.freelook;value->enabled[21]=fixtureConfig.freeCamera;
    value->freelookOptions=fixtureConfig.freelookOptions;value->freeCameraOptions=fixtureConfig.freeCameraOptions;
    value->zoomActionKey=fixtureConfig.zoomKey;value->zoomActionModifiers=fixtureConfig.zoomModifiers;
    value->mapToggleActionKey=fixtureConfig.mapToggleKey;value->mapToggleActionModifiers=fixtureConfig.mapToggleModifiers;
    publishedSettings.store(value);
}
void fixtureNativeMouse(FixtureMouseEvent& event){publishFixtureConfig();fixtureExtractedNativeMouse(event);}
void fixtureNativeKey(FixtureKeyEvent& event){publishFixtureConfig();fixtureExtractedNativeKey(event);}
void fixtureCameraStart(){publishFixtureConfig();cameraCandidateStart();}
void fixtureCameraTick(LocalPlayer& player){publishFixtureConfig();cameraCandidateTick(player);}
void fixtureCameraBeforeTick(LocalPlayer& player){publishFixtureConfig();cameraCandidateBeforeTick(player);}
void fixtureCameraRender(LevelRendererPlayer& renderer){publishFixtureConfig();cameraCandidateRender(renderer);}
bool fixtureCameraKey(int key,bool down,bool cancelled=false){publishFixtureConfig();return cameraCandidateKey(key,down,cancelled);}
bool fixtureCameraMouse(int action,int data,short dx,short dy,bool cancelled=false){publishFixtureConfig();return cameraCandidateMouse(action,data,dx,dy,cancelled);}
void fixtureCameraExit(IClientInstance& client){publishFixtureConfig();cameraCandidateExit(client);}
bool fixtureCameraCleanup(){publishFixtureConfig();return cameraCandidateCleanup();}

struct World {
    struct ConfigBinding {
        bool& enabled;int& key;bool& hold;float& degreesPerPixel;float& wheelDistance;float& flightSpeed;unsigned& modifiers;
    };
    std::shared_ptr<FixtureRegistryOwner> owner=std::make_shared<FixtureRegistryOwner>();
    std::unique_ptr<LocalPlayer> player;IClientInstance client;EntityId rig;
    World(){
        owner->self=owner;auto actor=owner->mRegistry->create();rig=owner->mRegistry->create();
        player=std::make_unique<LocalPlayer>(LocalPlayer{EntityContext{*owner,actor}});client.player=player.get();
        auto& rotation=owner->mRegistry->emplace<ActorRotationComponent>(actor);rotation.mRot->x=0;rotation.mRot->y=0;
        auto& head=owner->mRegistry->emplace<ActorHeadRotationComponent>(actor);head.mYHeadRot=0;head.mYHeadRotO=0;
        auto& direct=owner->mRegistry->emplace<MinecraftCamera::CameraDirectLookComponent>(rig);
        direct.mYaw=0;direct.mPitch=0;direct.mPitchMin=-1.5f;direct.mPitchMax=1.5f;
        owner->mRegistry->emplace<MinecraftCamera::CameraOffsetComponent>(rig).mEntityOffset=glm::vec3{};
        owner->mRegistry->emplace<VanillaCamera::UpdatePlayerFromCameraComponent>(rig).mLookMode=VanillaCamera::UpdatePlayerFromCameraComponent::LookMode::AlongCamera;
        owner->mRegistry->emplace<MinecraftCamera::ActiveCameraComponent>(rig);
        owner->mRegistry->emplace<CameraClientInstanceComponent>(rig).mClientInstance.value=&client;
        client.cameras->mCameraEntities.value.push_back(std::make_unique<EntityContext>(EntityContext{*owner,rig}));
        fixturePrimary=&client;fixtureNextPrimary=nullptr;fixtureKeys.fill(false);fixtureAsyncReads.fill(0);fixtureConfig={};fixtureConfigThrows=false;
        fixtureGuiCaptured=false;fixtureFocus=true;fixtureBackend=GuiBackendKind::Standalone;fixtureThread=1;fixtureNow=1000;
        fixtureAsyncUnavailable=false;fixtureLoseFocusAtAsyncRead=false;
        fixtureMapBindings={};
        zoomHeld=false;fixtureInventoryClaims=fixtureZoomClaims=false;
        fixtureInventoryCalls=fixtureZoomScaleCalls=fixtureZoomScrollCalls=0;fixtureInputOrder.clear();
        availableFeatures=ownedFeatureSlotMask;
        inputAllowed=true;runtimeReady=true;worldGeneration.fetch_add(1);fixtureCameraStart();
    }
    ConfigBinding binding(bool free){
        auto index=static_cast<unsigned>(free?DetachedCameraMode::FreeCamera:DetachedCameraMode::Freelook);
        auto& options=free?fixtureConfig.freeCameraOptions:fixtureConfig.freelookOptions;
        return {free?fixtureConfig.freeCamera:fixtureConfig.freelook,fixtureConfig.keybinds[index],options.hold,
                options.degreesPerPixel,options.wheelDistance,options.flightSpeed,fixtureConfig.keybindModifiers[index]};
    }
    auto& direct(){return owner->mRegistry->get<MinecraftCamera::CameraDirectLookComponent>(rig);}
    glm::vec3 offset(){return owner->mRegistry->get<MinecraftCamera::CameraOffsetComponent>(rig).mEntityOffset.get();}
    void activate(bool free=false,int key='T'){
        auto options=binding(free);options.enabled=true;options.key=key;fixtureKeys[key]=false;
        fixtureCameraTick(*player); // Publish/synchronize while the bind is physically neutral.
        fixtureKeys[key]=true;
        check(fixtureCameraKey(key,true,false),"production activation down withheld");fixtureCameraTick(*player);
        check(saved&&mouseAdmission,"production tick begins owned camera");
        check(!owner->mRegistry->all_of<VanillaCamera::UpdatePlayerFromCameraComponent>(rig),"engine copy detached");
    }
    void finish(){fixtureFocus=true;fixtureGuiCaptured=false;fixtureThread=1;fixturePrimary=&client;
        inputAllowed=true;runtimeReady=true;client.screen="hud_screen";client.playing=true;client.keyboard=true;client.grabbed=true;
        direct().mPitchMin=-1.5f;direct().mPitchMax=1.5f;
        auto rotation=player->context.tryGetComponent<ActorRotationComponent>();rotation->mRot->x=0;rotation->mRot->y=0;
        // Actual callbacks remain present while the product drains keys/buttons
        // that it withheld. Release fixture inputs before the final detach.
        (void)fixtureCameraCleanup();
        for(int key=1;key<256;++key){fixtureKeys[key]=false;FixtureKeyEvent up{key,false};fixtureNativeKey(up);}
        for(auto button:{MouseAction::ActionLeft,MouseAction::ActionRight,MouseAction::ActionMiddle,MouseAction::ActionX1,MouseAction::ActionX2}) {
            FixtureMouseEvent up{button,MouseAction::DataUp};fixtureNativeMouse(up);
        }
        fixtureCameraBeforeTick(*player);
        check(fixtureCameraCleanup(),"production cleanup restores or retires");check(!saved,"no retained camera after cleanup");}
};
void closeFixtureInputContext(World& w,int kind){
    switch(kind){
    case 0:fixtureFocus=false;break;
    case 1:fixtureGuiCaptured=true;break;
    case 2:fixtureBackend=GuiBackendKind::LholoBridge;fixtureGuiCaptured=true;break;
    case 3:fixtureBackend=GuiBackendKind::WaitingForProviders;break;
    case 4:fixtureBackend=GuiBackendKind::ConflictBlocked;break;
    case 5:fixtureBackend=GuiBackendKind::Failed;break;
    case 6:runtimeReady=false;break;
    case 7:inputAllowed=false;break;
    case 8:w.client.screen="chat_screen";break;
    case 9:w.client.keyboard=false;break;
    case 10:w.client.grabbed=false;break;
    case 11:w.client.playing=false;break;
    case 12:fixturePrimary=nullptr;break;
    case 13:w.client.player=nullptr;break;
    case 14:retiring=true;break;
    }
}

namespace praxis::companion {
extern std::vector<std::string> fixtureLogMessages;
extern std::function<void()> fixtureOriginFault,fixtureEyeFault;
}
bool lamiumHasLog(char const* value){for(auto const& line:fixtureLogMessages)if(line.find(value)!=std::string::npos)return true;return false;}
void lamiumStep(World& w){publishFixtureConfig();w.player->fixtureTickHook();}
void lamiumPress(int key,bool down){fixtureKeys[key]=down;FixtureKeyEvent e{key,down};fixtureNativeKey(e);}
void lamiumStart(World& w,bool hold=true) {
    auto o=w.binding(false);o.enabled=true;o.key='G';o.modifiers=0;o.hold=hold;lamiumStep(w);
    fixtureLogMessages.clear();fixtureKeys['G']=true;FixtureKeyEvent down{'G',true};fixtureNativeKey(down);lamiumStep(w);
    check(down.cancelled&&saved&&mouseAdmission,"Native G/modifier0 -> normal tick starts Lamium ECS Freelook");
    check(saved->mode==DetachedCameraMode::Freelook&&!w.owner->mRegistry->all_of<Link>(w.rig),"only camera-player copy Link is detached");
    check(!motion.pose()&&lamiumHasLog("source=local_player_ecs")&&lamiumHasLog("nativeLook=1"),"native Freelook neither calibrates nor drives private camera motion");
}
void lamiumRelease(World& w,bool hold=true) {
    lamiumPress('G',false);lamiumStep(w);
    if(!hold){lamiumPress('G',true);lamiumStep(w);lamiumPress('G',false);lamiumStep(w);}
    check(!saved&&w.owner->mRegistry->all_of<Link>(w.rig),"hold release/second fresh toggle restores Link");
}
void lamiumFreelookFunctionalCases() {
    auto before=fixtureChecks;
    for(bool hold:{false,true})for(unsigned roots=0;roots<7;++roots)for(bool adapter:{false,true}) {
        World w;auto& ecs=*w.owner->mRegistry;auto start=w.offset();
        if(!adapter)ecs.remove<CameraClientInstanceComponent>(w.rig);
        if(roots==0){auto empty=ecs.create();w.client.currentCamera=w.client.rawCamera={w.owner,empty};}
        if(roots==1)w.client.currentCamera=w.client.rawCamera=WeakEntityRef{};
        if(roots==2){w.client.currentCamera={};w.client.rawTarget={};}
        if(roots==3){w.client.rawCamera={};w.client.rawTarget={w.owner,ecs.create()};}
        if(roots==4){w.client.currentCamera=w.client.rawCamera=w.player->context.getWeakRef();}
        if(roots==5){auto usage=ecs.create();ecs.emplace<MinecraftCamera::CameraUsageComponent>(usage);w.client.currentCamera=w.client.rawCamera={w.owner,usage};}
        if(roots==6)w.client.cameras.reset();
        // No forced equality to actor degrees or radians, or synthetic native
        // bounds assumptions. SDK-native values are snapshots only.
        w.direct().mYaw=11.f;w.direct().mPitch=5.f;w.direct().mPitchMin=-90.f;w.direct().mPitchMax=90.f;
        auto head=w.player->context.tryGetComponent<ActorHeadRotationComponent>();head->mYHeadRot=47.f;head->mYHeadRotO=46.f;
        w.client.currentReads=w.client.rawReads=0;fixtureRigReads=0;
        lamiumStart(w,hold);
        FixtureMouseEvent move{MouseAction::ActionMoveRelative,0,18,-8};fixtureNativeMouse(move);
        check(!move.cancelled&&move.x==9&&move.y==-4,"Freelook forwards native look after existing Zoom filter at default sensitivity");
        check(pendingDx==0&&pendingDy==0,"native relative input is never queued for private motion");
        // Only the engine origin boundary is doubled: emulate its camera/head
        // writes with real SDK components. Production before/after bodies run.
        fixtureOriginFault=[&]{w.direct().mYaw=13.37f;w.direct().mPitch=-.88f;head->mYHeadRot=80.f;head->mYHeadRotO=79.f;};
        lamiumStep(w);
        check(saved&&mouseAdmission&&w.direct().mYaw==13.37f&&w.direct().mPitch==-.88f,"normal tick preserves fresh vanilla camera orientation");
        check(head->mYHeadRot==47.f&&head->mYHeadRotO==47.f,"native tick keeps both visual head interpolation endpoints");
        check(w.player->getRotation().x==0&&w.player->getRotation().y==0&&w.player->eye==glm::vec3{}&&w.offset()==start,"actor body/position and all camera displacement remain unchanged");
        check(w.client.currentReads==0&&w.client.rawReads==0&&fixtureRigReads==0,"Freelook never treats client roots/preset registry as render-camera factories");
        lamiumRelease(w,hold);
        check(w.direct().mYaw==11.f&&w.direct().mPitch==5.f&&w.direct().mYawDelta==0.f,"release returns exact native start angles before Link reattachment");
        check(head->mYHeadRot==47.f&&head->mYHeadRotO==46.f,"release restores original head snapshot");
        check(w.offset()==start,"native Freelook has no Offset ownership/writes");w.finish();
    }
    for(bool hold:{false,true})for(bool withDirect:{false,true}) {
        World w;auto& ecs=*w.owner->mRegistry;
        auto& o=ecs.emplace<MinecraftCamera::CameraOrbitComponent>(w.rig);
        o.mCurrentSpherical->mAzimuth=4.f;o.mCurrentSpherical->mPolarAngle=1.f;
        o.mIdealSpherical->mAzimuth=3.f;o.mIdealSpherical->mPolarAngle=2.f;
        if(!withDirect)ecs.remove<Direct>(w.rig);
        ecs.get<Link>(w.rig).mLookMode=Link::LookMode::AtCamera;
        lamiumStart(w,hold);
        FixtureMouseEvent mouse{MouseAction::ActionMoveRelative,0,9,5};fixtureNativeMouse(mouse);check(!mouse.cancelled,"Orbit camera turns through native input");
        fixtureOriginFault=[&]{o.mCurrentSpherical->mAzimuth=6.f;o.mCurrentSpherical->mPolarAngle=7.f;o.mIdealSpherical->mAzimuth=8.f;o.mIdealSpherical->mPolarAngle=9.f;o.mAzimuthVelocity=1.f;o.mPolarAngleVelocity=2.f;};
        lamiumStep(w);check(saved&&o.mCurrentSpherical->mAzimuth==6.f,"native Orbit state remains owned by vanilla");
        lamiumRelease(w,hold);
        check(o.mCurrentSpherical->mAzimuth==4.f&&o.mCurrentSpherical->mPolarAngle==1.f&&o.mIdealSpherical->mAzimuth==3.f&&o.mIdealSpherical->mPolarAngle==2.f,"all current/ideal Orbit angles restore without conversion");
        check(o.mAzimuthVelocity==0.f&&o.mPolarAngleVelocity==0.f&&ecs.get<Link>(w.rig).mLookMode==Link::LookMode::AtCamera,"restore resets angular smoothing and returns original Link mode");
        if(!withDirect){auto& d=ecs.emplace<Direct>(w.rig);d.mYaw=d.mPitch=0;d.mPitchMin=-1.5f;d.mPitchMax=1.5f;}
        w.finish();
    }
    for(unsigned kind=0;kind<15;++kind) {
        World w;lamiumStart(w);w.direct().mYaw=.8f;closeFixtureInputContext(w,kind);lamiumStep(w);
        check(!mouseAdmission,"all standalone/LHolo/GUI/chat/focus/runtime closures revoke Freelook input");
        if(kind!=12&&kind!=13)check(!saved&&w.owner->mRegistry->all_of<Link>(w.rig)&&w.direct().mYaw==0.f,"owner-thread context closure restores native view and Link");
        w.client.player=w.player.get();w.finish();
    }
    for(unsigned kind=0;kind<8;++kind) {
        World w;lamiumStart(w);w.direct().mYaw=.4f;
        if(kind==0)worldGeneration.fetch_add(1);if(kind==1)++w.player->dimension;if(kind==2)w.player->alive=false;
        if(kind==3)w.player->sleeping=true;if(kind==4)w.player->mounted=true;if(kind==5)w.player->usingItem=true;
        if(kind==6)fixtureCameraExit(w.client);else if(kind==7)(void)fixtureCameraCleanup();else lamiumStep(w);
        check(!saved&&w.owner->mRegistry->all_of<Link>(w.rig)&&w.direct().mYaw==0.f,"world/dimension/death/sleep/mount/item/exit/unload restore native view");w.finish();
    }
    for(unsigned kind=0;kind<8;++kind) {
        World w;auto& ecs=*w.owner->mRegistry;IClientInstance foreign;LocalPlayer foreignPlayer=*w.player;foreign.player=&foreignPlayer;
        if(kind==0)ecs.remove<MinecraftCamera::ActiveCameraComponent>(w.rig);
        if(kind==1)ecs.remove<Link>(w.rig);
        if(kind==2)ecs.get<CameraClientInstanceComponent>(w.rig).mClientInstance.value=nullptr;
        if(kind==3)ecs.get<CameraClientInstanceComponent>(w.rig).mClientInstance.value=&foreign;
        if(kind==4)w.direct().mYaw=std::numeric_limits<float>::quiet_NaN();
        if(kind==5){auto extra=ecs.create();ecs.emplace<MinecraftCamera::ActiveCameraComponent>(extra);ecs.emplace<Direct>(extra);ecs.emplace<Link>(extra);}
        if(kind==6)for(unsigned i=0;i<32;++i)ecs.emplace<MinecraftCamera::ActiveCameraComponent>(ecs.create());
        if(kind==7)fixturePrimary=&foreign;
        auto cfg=w.binding(false);cfg.enabled=true;cfg.key='G';lamiumStep(w);lamiumPress('G',true);lamiumStep(w);
        check(!saved&&!mouseAdmission,"absent, foreign, malformed, ambiguous, cap-exceeded or non-primary rigs fail before detach");
        check(kind==1||ecs.all_of<Link>(w.rig),"rejected begin preserves engine Link");
        if(kind==0)ecs.emplace<MinecraftCamera::ActiveCameraComponent>(w.rig);
        if(kind==1)ecs.emplace<Link>(w.rig).mLookMode=Link::LookMode::AlongCamera;
        if(kind==2||kind==3)ecs.get<CameraClientInstanceComponent>(w.rig).mClientInstance.value=&w.client;
        w.direct().mYaw=0;fixturePrimary=&w.client;w.finish();
    }
    for(unsigned fault=0;fault<5;++fault) {
        World w;auto o=w.binding(false);o.enabled=true;o.key='G';lamiumStep(w);
        fixtureEyeFault=[&,fault]{if(fault==0)worldGeneration.fetch_add(1);if(fault==1)++w.player->dimension;if(fault==2)fixtureGuiCaptured=true;if(fault==3)fixturePrimary=nullptr;if(fault==4)fixtureThread=2;};
        lamiumPress('G',true);lamiumStep(w);
        check(!saved&&w.owner->mRegistry->all_of<Link>(w.rig),"fresh final owner/context query rejects lifecycle change before first mutation");
        fixtureThread=1;fixtureGuiCaptured=false;fixturePrimary=&w.client;w.finish();
    }
    {
        World w;lamiumStart(w);auto& ecs=*w.owner->mRegistry;ecs.emplace<Link>(w.rig).mLookMode=Link::LookMode::AtCamera;w.direct().mYaw=.91f;
        lamiumPress('G',false);lamiumStep(w);
        check(saved&&!mouseAdmission&&w.direct().mYaw==.91f&&ecs.get<Link>(w.rig).mLookMode==Link::LookMode::AtCamera,"foreign Link reattachment preserves foreign angles/mode and defers restore");
        ecs.remove<Link>(w.rig);w.finish();
    }
    {
        World w;lamiumStart(w);fixtureThread=2;check(!fixtureCameraCleanup()&&saved,"foreign-thread cleanup never writes native camera");fixtureThread=1;w.finish();
    }
    {
        World w;lamiumStart(w);auto& ecs=*w.owner->mRegistry;auto old=w.rig;ecs.destroy(old);
        auto replacement=ecs.create();auto& d=ecs.emplace<Direct>(replacement);d.mYaw=.6f;d.mPitch=.2f;d.mPitchMin=-1.5f;d.mPitchMax=1.5f;
        ecs.emplace<MinecraftCamera::ActiveCameraComponent>(replacement);ecs.emplace<Link>(replacement).mLookMode=Link::LookMode::AlongCamera;
        lamiumStep(w);check(!saved&&ecs.all_of<Link>(replacement)&&d.mYaw==.6f,"versioned retired rig cannot restore into a replacement camera");w.rig=replacement;w.finish();
    }
    {
        World w;lamiumStart(w);lookOptions.degreesPerPixel=.3f;
        FixtureMouseEvent e{MouseAction::ActionMoveRelative,0,18,-8};fixtureNativeMouse(e);check(!e.cancelled&&e.x==18&&e.y==-8,"existing sensitivity option scales native input relative to default .15");
        // Config republishes at callbacks; set both the formal config and the
        // current synchronized option for subsequent callbacks.
        fixtureConfig.freelookOptions.degreesPerPixel=.3f;lamiumStep(w);
        auto settingsValue=std::make_shared<Settings>(*settings());settingsValue->enabled[unsigned(Feature::Zoom)]=true;settingsValue->enabled[unsigned(Feature::NoRender)]=true;settingsValue->enabled[unsigned(Feature::TimeChanger)]=true;settingsValue->enabled[unsigned(Feature::WeatherChanger)]=true;publishedSettings.store(settingsValue);
        int token{};visualNativeAdmission=true;weatherNativeAdmission=true;
        {VisualTimeScope time(&token);VisualWeatherScope weather(true,true);w.player->fixtureTickHook();check(visualTimeContext.dimension==&token&&visualWeatherContext.eligible,"native Freelook preserves nested TimeChanger/Weather render scopes");}
        LevelRendererPlayer renderer{w.client};zoomFactor=.5f;check(renderer.fixtureFovHook(0,false)==35.f,"FOV/Zoom behavior preserved while native camera is detached");zoomFactor=1.f;
        zoomHeld=true;fixtureZoomClaims=true;FixtureMouseEvent wheel{MouseAction::ActionWheel,120};fixtureNativeMouse(wheel);check(wheel.cancelled&&saved&&mouseAdmission&&pendingWheel==0,"Zoom wheel ownership coexists with Freelook");zoomHeld=false;fixtureZoomClaims=false;
        FixtureKeyEvent movement{'W',true};fixtureKeys['W']=true;fixtureNativeKey(movement);check(!movement.cancelled&&!inputLedger.owned('W'),"Freelook preserves vanilla body movement ownership");lamiumPress('W',false);
        lamiumRelease(w);w.finish();
    }
    {
        World w;fixtureConfig.keybinds[20]='G';lamiumStep(w);lamiumPress('G',true);lamiumStep(w);check(!saved&&!mouseAdmission,"default OFF never detaches or owns native look");w.finish();
    }
    std::cout<<"Lamium native Freelook functional/input/lifecycle/coexistence PASS "<<fixtureChecks-before<<" checks; engine boundary doubled; actual Minecraft NOT_RUN\n";
}


void lamiumFreelookShapeCases() {
    auto before=fixtureChecks;
    // Exact two-bit component topology, including removal of both components.
    // Each transition runs through input, head-only beforeTick, full normalTick
    // and cleanup; no engine-origin write is substituted into these HOLD cases.
    for(unsigned initial=1;initial<=3;++initial)for(unsigned changed=0;changed<=3;++changed) {
        if(initial==changed)continue;
        for(unsigned route=0;route<5;++route) {
            World w;auto& ecs=*w.owner->mRegistry;auto offset=w.offset();
            auto head=w.player->context.tryGetComponent<ActorHeadRotationComponent>();
            head->mYHeadRot=47.f;head->mYHeadRotO=46.f;
            auto putDirect=[&] {
                if(!ecs.all_of<Direct>(w.rig))ecs.emplace<Direct>(w.rig);
                auto& d=ecs.get<Direct>(w.rig);d.mYaw=11.f;d.mPitch=5.f;d.mYawDelta=0.f;d.mPitchMin=-90.f;d.mPitchMax=90.f;
            };
            auto putOrbit=[&] {
                if(!ecs.all_of<MinecraftCamera::CameraOrbitComponent>(w.rig))ecs.emplace<MinecraftCamera::CameraOrbitComponent>(w.rig);
                auto& o=ecs.get<MinecraftCamera::CameraOrbitComponent>(w.rig);
                o.mCurrentSpherical->mAzimuth=4.f;o.mCurrentSpherical->mPolarAngle=1.f;
                o.mIdealSpherical->mAzimuth=3.f;o.mIdealSpherical->mPolarAngle=2.f;o.mAzimuthVelocity=0.f;o.mPolarAngleVelocity=0.f;
            };
            if(initial&1)putDirect();else ecs.remove<Direct>(w.rig);
            if(initial&2)putOrbit();
            fixtureConfig.freelookOptions.degreesPerPixel=.3f;lamiumStart(w);
            if(changed&1)putDirect();else ecs.remove<Direct>(w.rig);
            if(changed&2)putOrbit();else ecs.remove<MinecraftCamera::CameraOrbitComponent>(w.rig);
            if(auto d=ecs.try_get<Direct>(w.rig)){d->mYaw=13.37f;d->mPitch=-.88f;d->mYawDelta=.77f;}
            if(auto o=ecs.try_get<MinecraftCamera::CameraOrbitComponent>(w.rig)) {
                o->mCurrentSpherical->mAzimuth=6.f;o->mCurrentSpherical->mPolarAngle=7.f;
                o->mIdealSpherical->mAzimuth=8.f;o->mIdealSpherical->mPolarAngle=9.f;o->mAzimuthVelocity=1.f;o->mPolarAngleVelocity=2.f;
            }
            head->mYHeadRot=80.f;head->mYHeadRotO=79.f;
            if(route==0) {
                FixtureMouseEvent e{MouseAction::ActionMoveRelative,0,18,-8};fixtureNativeMouse(e);
                check(!e.cancelled&&e.x==9&&e.y==-4,"shape mismatch closes Freelook before its 2x sensitivity/input transformation");
            }else if(route==1)fixtureCameraBeforeTick(*w.player);
            else if(route==2)lamiumStep(w);
            else if(route==3)check(!fixtureCameraCleanup(),"shape mismatch cleanup holds restoration");
            else {
                short dx=18,dy=-8;adjustFreelookMouse(dx,dy,false);
                check(dx==18&&dy==-8,"direct input helper rejects added/removed shape before changing relative input");
            }
            auto unchanged=[&] {
                check(saved&&!mouseAdmission&&pendingDx==0&&pendingDy==0&&pendingWheel==0,"added or removed shape closes all private input and retains saved restoration HOLD");
                check(saved->hasDirect==bool(initial&1)&&saved->hasOrbit==bool(initial&2),"HOLD never refreshes or widens the original native snapshot topology");
                check(!ecs.all_of<Link>(w.rig)&&head->mYHeadRot==80.f&&head->mYHeadRotO==79.f,"shape HOLD changes neither head interpolation endpoint nor Link");
                check(w.offset()==offset&&w.player->getRotation().x==0.f&&w.player->getRotation().y==0.f&&w.player->eye==glm::vec3{},"shape HOLD changes no camera displacement/body/position");
                if(auto d=ecs.try_get<Direct>(w.rig))check(d->mYaw==13.37f&&d->mPitch==-.88f&&d->mYawDelta==.77f,"shape HOLD preserves all surviving or newly added Direct angle state");
                if(auto o=ecs.try_get<MinecraftCamera::CameraOrbitComponent>(w.rig))check(o->mCurrentSpherical->mAzimuth==6.f&&o->mCurrentSpherical->mPolarAngle==7.f&&o->mIdealSpherical->mAzimuth==8.f&&o->mIdealSpherical->mPolarAngle==9.f&&o->mAzimuthVelocity==1.f&&o->mPolarAngleVelocity==2.f,"shape HOLD preserves all surviving or newly added Orbit angle/velocity state");
            };
            unchanged();
            check(!keepFreelookHead(*w.player),"head helper independently requires exact saved shape");unchanged();
            check(!restore(),"restore independently holds on added and missing components");unchanged();
            FixtureMouseEvent retry{MouseAction::ActionMoveRelative,0,18,-8};fixtureNativeMouse(retry);
            fixtureCameraBeforeTick(*w.player);lamiumStep(w);check(!fixtureCameraCleanup(),"repeated mismatch callbacks remain restoration HOLD");unchanged();
            // Recover the exact original shape only for fixture cleanup. This
            // tests existence equality, not unprovided component-instance ABA.
            if(initial&1)putDirect();else ecs.remove<Direct>(w.rig);
            if(initial&2)putOrbit();else ecs.remove<MinecraftCamera::CameraOrbitComponent>(w.rig);
            lamiumPress('G',false);fixtureCameraBeforeTick(*w.player);lamiumStep(w);
            check(!saved&&ecs.all_of<Link>(w.rig),"only exact original shape permits final native-angle/Link restoration");
            check(head->mYHeadRot==47.f&&head->mYHeadRotO==46.f,"exact shape recovery permits original head snapshot restoration");
            if(initial&1)check(w.direct().mYaw==11.f&&w.direct().mPitch==5.f&&w.direct().mYawDelta==0.f,"exact shape recovery restores saved Direct angles");
            if(initial&2) {
                auto& o=ecs.get<MinecraftCamera::CameraOrbitComponent>(w.rig);
                check(o.mCurrentSpherical->mAzimuth==4.f&&o.mCurrentSpherical->mPolarAngle==1.f&&o.mIdealSpherical->mAzimuth==3.f&&o.mIdealSpherical->mPolarAngle==2.f&&o.mAzimuthVelocity==0.f&&o.mPolarAngleVelocity==0.f,"exact shape recovery restores saved Orbit angles/velocities");
            }
            if(!(initial&1))putDirect();w.finish();
        }
    }
    std::cout<<"Lamium exact saved Direct/Orbit shape HOLD PASS "<<fixtureChecks-before<<" checks; added/removed/swapped shapes, input/head/restore unchanged; actual Minecraft NOT_RUN\n";
}

void cameraSingleKeyModifierCases() {
    // Run against the extracted product camera and Native KeyInputEvent bodies.
    // FreeCamera input is tested before tick; its SDK backend HOLD is preserved.
    for(bool free:{false,true})for(bool hold:{false,true})for(unsigned wanted:{0u,8u})for(unsigned actual=0;actual<8;++actual) {
        World w;auto options=w.binding(free);int key=free?'F':'G';
        options.enabled=true;options.key=key;options.modifiers=wanted;options.hold=hold;
        fixtureCameraTick(*w.player);
        fixtureKeys[VK_CONTROL]=(actual&1)!=0;fixtureKeys[VK_SHIFT]=(actual&2)!=0;fixtureKeys[VK_MENU]=(actual&4)!=0;
        fixtureAsyncReads.fill(0);fixtureKeys[key]=true;
        FixtureKeyEvent down{key,true};fixtureNativeKey(down);
        auto& activation=free?freeActivation:lookActivation;
        check(down.cancelled&&activation.wanted()&&inputLedger.owned(key),"camera0/Any8 fresh single key accepts every extra modifier mask");
        check(fixtureAsyncReads[VK_CONTROL]==0&&fixtureAsyncReads[VK_SHIFT]==0&&fixtureAsyncReads[VK_MENU]==0,"camera0/Any8 admission does not sample OS modifier state");
        FixtureKeyEvent repeat{key,true};fixtureNativeKey(repeat);
        check(repeat.cancelled&&activation.wanted(),"owned repeat neither reactivates nor toggles a camera single key");
        fixtureKeys[key]=false;FixtureKeyEvent up{key,false};fixtureNativeKey(up);
        check(up.cancelled&&!inputLedger.owned(key)&&activation.wanted()==!hold,"owned Up releases hold and preserves toggle");
        if(!hold) {
            fixtureKeys[key]=true;FixtureKeyEvent second{key,true};fixtureNativeKey(second);
            check(second.cancelled&&!activation.wanted(),"second fresh single key toggles OFF exactly once");
            fixtureKeys[key]=false;FixtureKeyEvent finalUp{key,false};fixtureNativeKey(finalUp);
            check(finalUp.cancelled&&!inputLedger.owned(key),"toggle OFF still pairs its owned Up");
        }
        w.finish();
    }
    for(bool free:{false,true})for(unsigned wanted:{0u,8u})for(auto sided:{VK_LCONTROL,VK_RCONTROL,VK_LSHIFT,VK_RSHIFT,VK_LMENU,VK_RMENU}) {
        World w;auto options=w.binding(free);int key=free?'F':'G';options.enabled=true;options.key=key;options.modifiers=wanted;
        fixtureCameraTick(*w.player);fixtureKeys[sided]=true;FixtureKeyEvent modifier{sided,true};fixtureNativeKey(modifier);
        fixtureKeys[key]=true;FixtureKeyEvent down{key,true};fixtureNativeKey(down);
        check(down.cancelled&&(free?freeActivation:lookActivation).wanted(),"single camera key accepts left/right modifier events without alias assumptions");
        fixtureKeys[key]=false;FixtureKeyEvent up{key,false};fixtureNativeKey(up);fixtureKeys[sided]=false;FixtureKeyEvent released{sided,false};fixtureNativeKey(released);w.finish();
    }
    for(bool free:{false,true})for(int block=0;block<5;++block) {
        World w;auto options=w.binding(free);int key=free?'F':'G';options.enabled=true;options.key=key;options.modifiers=0;
        if(block==4)fixtureKeys[key]=true;
        fixtureCameraTick(*w.player);
        if(block==0)fixtureGuiCaptured=true;if(block==1)fixtureFocus=false;if(block==2)inputAllowed=false;
        fixtureKeys[VK_CONTROL]=true;fixtureKeys[key]=true;FixtureKeyEvent down{key,true,block==3};fixtureNativeKey(down);
        auto& activation=free?freeActivation:lookActivation;
        check(!activation.wanted()&&!inputLedger.owned(key),"single-key modifier relaxation preserves GUI/focus/input/cancel/preheld barriers");
        fixtureGuiCaptured=false;fixtureFocus=true;inputAllowed=true;FixtureKeyEvent repeat{key,true};fixtureNativeKey(repeat);
        check(!repeat.cancelled&&!activation.wanted(),"rejected single-key Down cannot upgrade through a repeat");
        fixtureKeys[key]=false;FixtureKeyEvent up{key,false};fixtureNativeKey(up);
        check(!up.cancelled,"unowned rejected key Up remains with its original owner");w.finish();
    }
    for(bool free:{false,true})for(unsigned otherMask=0;otherMask<9;++otherMask)for(int owner=0;owner<3;++owner) {
        World w;auto options=w.binding(free);int key=free?'F':'G';options.enabled=true;options.key=key;options.modifiers=0;
        if(owner==0){fixtureConfig.zoomKey=key;fixtureConfig.zoomModifiers=otherMask;}
        if(owner==1){fixtureConfig.mapToggleKey=key;fixtureConfig.mapToggleModifiers=otherMask;}
        if(owner==2){fixtureConfig.keybinds[0]=key;fixtureConfig.keybindModifiers[0]=otherMask;}
        fixtureCameraTick(*w.player);fixtureKeys[key]=true;FixtureKeyEvent down{key,true};fixtureNativeKey(down);
        check(!down.cancelled&&!(free?freeActivation:lookActivation).wanted()&&!inputLedger.owned(key),"camera0 rejects same-key normal/Zoom/map owners across every modifier mask");
        fixtureKeys[key]=false;FixtureKeyEvent up{key,false};fixtureNativeKey(up);w.finish();
    }
    // Explicit masks keep the existing exact matcher, including wrong masks.
    for(bool free:{false,true})for(unsigned wanted=1;wanted<8;++wanted)for(unsigned actual=0;actual<8;++actual) {
        World w;auto options=w.binding(free);int key=free?'F':'G';options.enabled=true;options.key=key;options.modifiers=wanted;
        fixtureCameraTick(*w.player);fixtureKeys[VK_CONTROL]=(actual&1)!=0;fixtureKeys[VK_SHIFT]=(actual&2)!=0;fixtureKeys[VK_MENU]=(actual&4)!=0;
        fixtureKeys[key]=true;FixtureKeyEvent down{key,true};fixtureNativeKey(down);
        check(down.cancelled==(wanted==actual)&&(free?freeActivation:lookActivation).wanted()==(wanted==actual),"camera masks1..7 retain exact modifier admission");
        fixtureKeys[key]=false;FixtureKeyEvent up{key,false};fixtureNativeKey(up);w.finish();
    }
}
int main(){try {
    cameraSingleKeyModifierCases();
    for(bool free:{false,true})for(bool hold:{false,true})for(unsigned mask:{1u,2u,4u,7u})for(bool modifierFirst:{false,true}) {
        World w;auto options=w.binding(free);options.enabled=true;options.key='T';options.hold=hold;options.modifiers=mask;
        fixtureCameraTick(*w.player);
        auto modifiers=[&](bool down){for(auto pair:{std::pair{int(VK_CONTROL),1u},std::pair{int(VK_SHIFT),2u},std::pair{int(VK_MENU),4u}})if(mask&pair.second){
            fixtureKeys[pair.first]=down;FixtureKeyEvent event{pair.first,down};fixtureNativeKey(event);}};
        modifiers(true);fixtureKeys['T']=true;FixtureKeyEvent down{'T',true};fixtureNativeKey(down);fixtureCameraTick(*w.player);
        check(down.cancelled,"fresh accepted modifier chord acquires camera activation");
        if(free&&(mask&2))check(!saved&&freeActivation.wanted(),"accepted Shift camera chord waits for neutral body movement");
        else check(saved.has_value(),"nonmovement chord begins requested camera mode");
        if(modifierFirst){
            modifiers(false);FixtureKeyEvent repeat{'T',true};fixtureNativeKey(repeat);fixtureCameraTick(*w.player);
            check(repeat.cancelled&&saved&&mouseAdmission,"accepted chord repeat and held base key survive modifier release");
            fixtureKeys['T']=false;FixtureKeyEvent up{'T',false};fixtureNativeKey(up);fixtureCameraBeforeTick(*w.player);fixtureCameraTick(*w.player);
            check(up.cancelled&&saved.has_value()==!hold,"base Up releases hold or preserves toggle after modifier Up");
        }else{
            fixtureKeys['T']=false;FixtureKeyEvent up{'T',false};fixtureNativeKey(up);fixtureCameraBeforeTick(*w.player);
            check(up.cancelled,"base Up paired before modifiers are released");
            modifiers(false);fixtureCameraTick(*w.player);
            check(saved.has_value()==!hold,"modifier Up after base Up preserves toggle and leaves hold OFF");
        }
        w.finish();
    }
    for(bool free:{false,true}) {
        World w;auto options=w.binding(free);options.enabled=true;options.key='T';options.modifiers=2;fixtureCameraTick(*w.player);
        fixtureKeys['T']=true;FixtureKeyEvent wrong{'T',true};fixtureNativeKey(wrong);
        fixtureKeys[VK_SHIFT]=true;FixtureKeyEvent lateModifier{VK_SHIFT,true};fixtureNativeKey(lateModifier);
        FixtureKeyEvent repeat{'T',true};fixtureNativeKey(repeat);fixtureCameraTick(*w.player);
        check(!wrong.cancelled&&!repeat.cancelled&&!saved,"late modifier cannot upgrade unaccepted held base-key repeat into fresh chord");w.finish();
    }
    for(bool mouse:{false,true}) {
        World w;w.activate(true);auto ecs=fixtureEcsReads,rig=fixtureRigReads;fixtureThread=2;
        if(mouse){FixtureMouseEvent fresh{MouseAction::ActionMoveRelative,0,20,12};fixtureNativeMouse(fresh);
            check(!fresh.cancelled&&pendingDx==0&&pendingDy==0,"foreign-thread fresh mouse does not enter owned camera transaction");}
        else{fixtureKeys['W']=true;FixtureKeyEvent fresh{'W',true};fixtureNativeKey(fresh);
            check(!fresh.cancelled&&!inputLedger.owned('W'),"foreign-thread fresh movement remains unowned");}
        check(fixtureEcsReads==ecs&&fixtureRigReads==rig,"foreign-thread fresh input returns before native camera/EnTT traversal");
        check(saved&&!mouseAdmission&&stopRequested,"foreign-thread input requests owner-thread restoration");
        FixtureKeyEvent repeat{'T',true};fixtureNativeKey(repeat);fixtureKeys['T']=false;FixtureKeyEvent up{'T',false};fixtureNativeKey(up);
        check(repeat.cancelled&&up.cancelled&&!inputLedger.owned('T'),"foreign-thread owned repeats and matching Up retain delivery ownership");
        check(fixtureEcsReads==ecs&&fixtureRigReads==rig,"foreign-thread owned bookkeeping does not inspect camera ECS");
        fixtureThread=1;fixtureCameraBeforeTick(*w.player);check(!saved,"owner thread completes foreign-thread restoration request");w.finish();
    }
    for(int specific:{int(VK_LSHIFT),int(VK_RSHIFT)})for(bool missedDown:{false,true})for(bool cancelledDown:{false,true})for(bool cancelledUp:{false,true}) {
        if(missedDown&&cancelledDown)continue;
        World w;w.binding(true).enabled=true;w.binding(true).key='T';fixtureCameraTick(*w.player);
        fixtureKeys[VK_SHIFT]=fixtureKeys[specific]=true;
        if(!missedDown){FixtureKeyEvent shift{specific,true,cancelledDown};fixtureNativeKey(shift);}
        fixtureKeys['T']=true;FixtureKeyEvent activation{'T',true};fixtureNativeKey(activation);fixtureCameraTick(*w.player);
        check(activation.cancelled&&!saved,"generic+sided Shift held state blocks FreeCamera admission");
        fixtureKeys[VK_SHIFT]=fixtureKeys[specific]=false;FixtureKeyEvent up{specific,false,cancelledUp};fixtureNativeKey(up);fixtureCameraTick(*w.player);
        bool bodyMayStillBeDown=cancelledUp&&!cancelledDown;
        check(saved.has_value()==!bodyMayStillBeDown,"specific formal Up retires only safe generic Shift alias suspicion");
        if(bodyMayStillBeDown){FixtureKeyEvent deliveredUp{specific,false};fixtureNativeKey(deliveredUp);fixtureCameraTick(*w.player);
            check(saved.has_value(),"uncancelled specific Up clears startup-missed generic alias and observed body Down");}
        w.finish();
    }
    {
        World w;w.activate(true);fixtureKeys['W']=true;FixtureKeyEvent movement{'W',true};fixtureNativeKey(movement);LevelRendererPlayer renderer{w.client};
        for(int frame=0;frame<8;++frame){auto before=w.offset();auto yaw=w.direct().mYaw;
            renderer.mCameraForward.value={std::sin(yaw),0,-std::cos(yaw)};fixtureCameraRender(renderer);
            FixtureMouseEvent mouse{MouseAction::ActionMoveRelative,0,20,4};fixtureNativeMouse(mouse);fixtureNow+=20;fixtureCameraTick(*w.player);
            check(mouse.cancelled&&saved&&mouseAdmission&&std::abs(glm::length(w.offset()-before)-.08f)<.0001f,"continuous W plus mouse look advances every measured frame using prior validated basis");
            std::array<float,3> stale;check(w.direct().mYaw!=yaw&&!flightDirection.read(directionContext(),fixtureNow,stale),"applied queued look invalidates old renderer direction after timed flight");}
        auto body=w.player->getRotation();check(glm::length(w.offset())>.6f&&w.player->eye==glm::vec3{}&&body.x==0&&body.y==0,"continuous camera flight accumulates while actor position/rotation stay untouched");w.finish();
    }
    for(bool cancelledFirstUp:{false,true})for(bool secondStillPhysicallyHeld:{false,true}) {
        World w;w.binding(true).enabled=true;w.binding(true).key='T';fixtureCameraTick(*w.player);
        fixtureKeys[VK_SHIFT]=fixtureKeys[VK_LSHIFT]=fixtureKeys[VK_RSHIFT]=true;
        FixtureKeyEvent left{VK_LSHIFT,true},right{VK_RSHIFT,true};fixtureNativeKey(left);fixtureNativeKey(right);
        fixtureKeys['T']=true;FixtureKeyEvent activation{'T',true};fixtureNativeKey(activation);fixtureCameraTick(*w.player);
        fixtureKeys[VK_LSHIFT]=false;fixtureKeys[VK_RSHIFT]=secondStillPhysicallyHeld;fixtureKeys[VK_SHIFT]=secondStillPhysicallyHeld;
        FixtureKeyEvent leftUp{VK_LSHIFT,false,cancelledFirstUp};fixtureNativeKey(leftUp);fixtureCameraTick(*w.player);
        check(!saved&&inputLedger.down(VK_RSHIFT),"one Shift Up cannot erase another explicitly observed Shift Down");
        fixtureKeys[VK_SHIFT]=fixtureKeys[VK_RSHIFT]=false;FixtureKeyEvent rightUp{VK_RSHIFT,false};fixtureNativeKey(rightUp);fixtureCameraTick(*w.player);
        check(saved.has_value()==!cancelledFirstUp,"other Shift Up cannot retire a previously externally-cancelled body release");
        if(cancelledFirstUp){FixtureKeyEvent deliveredLeftUp{VK_LSHIFT,false};fixtureNativeKey(deliveredLeftUp);fixtureCameraTick(*w.player);
            check(saved.has_value(),"both delivered Shift releases establish complete neutral admission");}
        w.finish();
    }

    for(int cancelledAlias:{int(VK_SHIFT),int(VK_LSHIFT),int(VK_RSHIFT)})for(int physicalSide:{int(VK_LSHIFT),int(VK_RSHIFT)})for(int otherAlias:{int(VK_SHIFT),int(VK_LSHIFT),int(VK_RSHIFT)}) {
        if(otherAlias==cancelledAlias)continue;
        World w;w.binding(true).enabled=true;w.binding(true).key='T';fixtureCameraTick(*w.player);
        // No formal Shift Down: admission sees only unknown startup OS state.
        fixtureKeys[VK_SHIFT]=fixtureKeys[physicalSide]=true;fixtureKeys['T']=true;
        FixtureKeyEvent activate{'T',true};fixtureNativeKey(activate);fixtureCameraTick(*w.player);
        check(activate.cancelled&&!saved,"raw startup Shift held state records possible body delivery and waits for release");
        fixtureKeys[VK_SHIFT]=fixtureKeys[VK_LSHIFT]=fixtureKeys[VK_RSHIFT]=false;
        FixtureKeyEvent cancelledUp{cancelledAlias,false,true};fixtureNativeKey(cancelledUp);fixtureCameraTick(*w.player);
        check(!saved&&!inputLedger.neutral(),"cancelled raw-missed Shift Up retains its own possible release debt despite physical zero");
        FixtureKeyEvent unrelatedUp{otherAlias,false};fixtureNativeKey(unrelatedUp);fixtureCameraTick(*w.player);
        check(!unrelatedUp.cancelled&&!saved&&!inputLedger.neutral(),"another generic/sided Shift Up cannot pay cancelled alias release debt");
        fixtureCameraBeforeTick(*w.player);fixtureCameraTick(*w.player);fixtureNativeKey(unrelatedUp);fixtureCameraTick(*w.player);
        check(!saved,"trusted release polls and repeated unrelated Shift Up cannot erase original cancelled release debt");
        FixtureKeyEvent cancelledAgain{cancelledAlias,false,true};fixtureNativeKey(cancelledAgain);fixtureCameraTick(*w.player);
        check(!saved,"another externally-cancelled matching Shift Up cannot retire release debt");
        FixtureKeyEvent matchingUp{cancelledAlias,false};fixtureNativeKey(matchingUp);fixtureCameraTick(*w.player);
        check(!matchingUp.cancelled&&saved&&mouseAdmission&&inputLedger.neutral(),"matching delivered generic/sided Shift Up finally establishes neutral FreeCamera admission");w.finish();
    }
    for(int first:{int(VK_SHIFT),int(VK_LSHIFT),int(VK_RSHIFT)})for(int second:{int(VK_SHIFT),int(VK_LSHIFT),int(VK_RSHIFT)}) {
        if(first==second)continue;
        World w;w.binding(true).enabled=true;w.binding(true).key='T';fixtureCameraTick(*w.player);
        fixtureKeys[VK_SHIFT]=fixtureKeys[VK_LSHIFT]=true;fixtureKeys['T']=true;FixtureKeyEvent activate{'T',true};fixtureNativeKey(activate);fixtureCameraTick(*w.player);
        fixtureKeys[VK_SHIFT]=fixtureKeys[VK_LSHIFT]=false;FixtureKeyEvent firstCancelled{first,false,true},secondCancelled{second,false,true};
        fixtureNativeKey(firstCancelled);fixtureNativeKey(secondCancelled);fixtureCameraTick(*w.player);
        check(!saved,"multiple cancelled raw-missed Shift aliases retain independent release debts");
        FixtureKeyEvent firstDelivered{first,false};fixtureNativeKey(firstDelivered);fixtureCameraTick(*w.player);
        check(!saved,"one matching Shift Up cannot retire a second cancelled alias debt");
        FixtureKeyEvent secondDelivered{second,false};fixtureNativeKey(secondDelivered);fixtureCameraTick(*w.player);
        check(saved&&mouseAdmission&&inputLedger.neutral(),"both matching Shift releases clear independent raw-missed debts");w.finish();
    }
    for(bool free:{false,true})for(bool hold:{false,true}) {
        World w;auto options=w.binding(free);options.enabled=true;options.key='T';options.hold=hold;
        fixtureCameraTick(*w.player);fixtureKeys['T']=true;
        fixtureCameraTick(*w.player);fixtureCameraBeforeTick(*w.player);fixtureCameraStart();fixtureCameraTick(*w.player);
        check(!saved&&!mouseAdmission,"held OS bind without official Key event cannot start through Tick/BeforeTick/Start");
        fixtureKeys['T']=false;FixtureKeyEvent up{'T',false};fixtureNativeKey(up);
        fixtureKeys['T']=true;FixtureKeyEvent down{'T',true};fixtureNativeKey(down);fixtureCameraTick(*w.player);
        check(down.cancelled&&saved,"real fresh Key event is the sole activation path");w.finish();
    }
    for(int kind=0;kind<15;++kind) {
        World w;w.binding(true).enabled=true;w.binding(true).key='T';fixtureCameraTick(*w.player);
        closeFixtureInputContext(w,kind);fixtureKeys['T']=true;FixtureKeyEvent key{'T',true};fixtureNativeKey(key);
        fixtureCameraTick(*w.player);
        check(!key.cancelled&&!inputLedger.owned('T')&&!saved,"untrusted gameplay/GUI context cannot freshly acquire activation");
        w.client.player=w.player.get();fixtureBackend=GuiBackendKind::Standalone;w.finish();
    }
    for(int kind=0;kind<15;++kind) {
        World w;w.activate(true);closeFixtureInputContext(w,kind);
        fixtureKeys['W']=true;FixtureKeyEvent movement{'W',true};fixtureNativeKey(movement);
        check(!movement.cancelled&&!inputLedger.owned('W'),"fresh movement after gameplay/GUI handoff stays with external owner");
        FixtureKeyEvent repeated{'T',true};fixtureNativeKey(repeated);
        check(repeated.cancelled&&inputLedger.owned('T'),"already-owned repeat retained across input handoff");
        fixtureKeys['T']=false;FixtureKeyEvent release{'T',false};fixtureNativeKey(release);
        check(release.cancelled&&!inputLedger.owned('T'),"already-owned release retained across input handoff");
        w.client.player=w.player.get();fixtureBackend=GuiBackendKind::Standalone;w.finish();
    }
    for(int kind=0;kind<15;++kind) {
        World w;w.activate(true);closeFixtureInputContext(w,kind);
        check(!fixtureCameraMouse(MouseAction::ActionMoveRelative,0,20,12,false),"fresh mouse in stale inputAllowed context is not camera-owned");
        check(!mouseAdmission&&stopRequested&&pendingDx==0&&pendingDy==0,"invalid mouse context requests restoration without applying mouse data");
        w.client.player=w.player.get();fixtureBackend=GuiBackendKind::Standalone;w.finish();
    }
    for(auto key:cameraMovementKeys) {
        World w;w.activate(true);fixtureKeys[key]=true;
        FixtureKeyEvent down{key,true};fixtureNativeKey(down);
        FixtureKeyEvent repeat{key,true};fixtureNativeKey(repeat);
        check(down.cancelled&&repeat.cancelled&&inputLedger.owned(key),"fresh movement Down and repeat are camera-owned");
        LevelRendererPlayer renderer{w.client};fixtureCameraRender(renderer);fixtureNow+=25;
        fixtureCameraTick(*w.player);
        check(saved&&glm::length(w.offset())>.09f&&glm::length(w.offset())<.11f,"owned movement uses measured time and configured flight speed");
        auto moved=w.offset();fixtureKeys[key]=false;FixtureKeyEvent up{key,false};fixtureNativeKey(up);
        check(up.cancelled&&!inputLedger.owned(key),"matching movement Up cannot reach vanilla body");
        fixtureNow+=25;fixtureCameraTick(*w.player);
        check(w.offset()==moved,"released movement cannot keep flying");
        auto rotation=w.player->context.tryGetComponent<ActorRotationComponent>();
        check(rotation->mRot->x==0&&rotation->mRot->y==0&&w.player->eye==glm::vec3{},"camera flight never changes actor rotation or position");w.finish();
    }
    for(auto key:cameraMovementKeys) {
        World w;w.binding(true).enabled=true;w.binding(true).key='T';fixtureCameraTick(*w.player);
        fixtureKeys[key]=true;FixtureKeyEvent bodyDown{key,true};fixtureNativeKey(bodyDown);
        check(!bodyDown.cancelled,"pre-held movement remains vanilla-owned");
        fixtureKeys['T']=true;FixtureKeyEvent activate{'T',true};fixtureNativeKey(activate);fixtureCameraTick(*w.player);
        check(activate.cancelled&&!saved&&std::string(cameraCandidateStatus()).find("Release movement")!=std::string::npos,"FreeCamera waits for all pre-held body keys to become neutral");
        fixtureKeys[key]=false;fixtureCameraTick(*w.player);
        check(!saved,"physical zero cannot erase a vanilla movement Down");
        FixtureKeyEvent cancelledUp{key,false,true};fixtureNativeKey(cancelledUp);fixtureCameraTick(*w.player);
        check(!saved,"externally-cancelled Up cannot prove vanilla received its release");
        FixtureKeyEvent bodyUp{key,false};fixtureNativeKey(bodyUp);fixtureCameraTick(*w.player);
        check(!bodyUp.cancelled&&saved&&mouseAdmission,"formal unclaimed body Up permits neutral FreeCamera admission");w.finish();
    }
    for(auto key:cameraMovementKeys) {
        World w;w.binding(true).enabled=true;w.binding(true).key='T';fixtureCameraTick(*w.player);
        fixtureKeys[key]=true;FixtureKeyEvent externalDown{key,true,true};fixtureNativeKey(externalDown);
        fixtureKeys['T']=true;FixtureKeyEvent activate{'T',true};fixtureNativeKey(activate);fixtureCameraTick(*w.player);
        check(!saved&&inputLedger.inhibited(key),"cancelled held movement blocks admission while physical Down remains");
        fixtureKeys[key]=false;fixtureCameraTick(*w.player);
        check(saved&&mouseAdmission,"known externally-cancelled pre-held movement retires on trusted zero without inventing body Down");w.finish();
    }
    {
        World w;w.activate(true);fixtureKeys['W']=true;FixtureKeyEvent down{'W',true};fixtureNativeKey(down);
        fixtureNow+=50;fixtureCameraTick(*w.player);
        check(saved&&w.offset()==glm::vec3{},"held movement waits for an actual current renderer forward basis");
        LevelRendererPlayer renderer{w.client};fixtureCameraRender(renderer);fixtureNow+=50;fixtureCameraTick(*w.player);
        check(std::abs(w.offset().z+.2f)<.0001f,"movement begins from measured current time after native basis arrives");
        auto previous=w.offset();fixtureNow+=101;fixtureCameraTick(*w.player);
        check(saved&&w.offset()==previous,"stale native forward TTL prevents held-key flight");
        fixtureCameraRender(renderer);fixtureNow+=10;fixtureCameraTick(*w.player);
        check(std::abs(w.offset().z-previous.z+.04f)<.0001f,"new renderer sample resumes without stale accumulated flight");
        previous=w.offset();fixtureNow+=2000;fixtureCameraRender(renderer);fixtureCameraTick(*w.player);
        check(std::abs(w.offset().z-previous.z+.4f)<.0001f,"long pause movement time is bounded to 100ms");w.finish();
    }
    {
        World w;w.activate(true);fixtureKeys['W']=true;FixtureKeyEvent down{'W',true};fixtureNativeKey(down);
        LevelRendererPlayer renderer{w.client};fixtureCameraRender(renderer);fixtureNow-=100;fixtureCameraTick(*w.player);
        check(saved&&w.offset()==glm::vec3{},"backwards clock cannot generate flight displacement");
        fixtureCameraRender(renderer);fixtureNow+=20;fixtureCameraTick(*w.player);
        check(std::abs(w.offset().z+.08f)<.0001f,"clock recovery uses only fresh measured time");w.finish();
    }
    {
        World w;w.activate(true);for(auto key:{int('W'),int('D'),int(VK_SPACE)}){fixtureKeys[key]=true;FixtureKeyEvent down{key,true};fixtureNativeKey(down);check(down.cancelled,"diagonal camera movement Down consumed");}
        LevelRendererPlayer renderer{w.client};fixtureCameraRender(renderer);fixtureNow+=25;fixtureCameraTick(*w.player);
        check(std::abs(glm::length(w.offset())-.1f)<.0001f&&w.offset().x>0&&w.offset().y>0&&w.offset().z<0,"diagonal three-axis camera flight has normalized configured speed");w.finish();
    }
    {
        World w;w.binding(true).flightSpeed=8;w.binding(true).wheelDistance=2;w.binding(true).degreesPerPixel=.5f;w.activate(true);
        LevelRendererPlayer renderer{w.client};fixtureCameraRender(renderer);fixtureCameraMouse(MouseAction::ActionWheel,1,0,0,false);fixtureCameraTick(*w.player);
        check(std::abs(w.offset().z+2)<.0001f,"formal wheelDistance setting controls actual camera callback");
        fixtureKeys['W']=true;FixtureKeyEvent down{'W',true};fixtureNativeKey(down);fixtureNow+=25;fixtureCameraTick(*w.player);
        check(std::abs(w.offset().z+2.2f)<.0001f,"formal flightSpeed setting controls timed callback movement");
        fixtureCameraMouse(MouseAction::ActionMoveRelative,0,10,0,false);fixtureCameraTick(*w.player);
        check(std::abs(w.direct().mYaw-5*std::numbers::pi_v<float>/180)<.0001f,"formal degreesPerPixel setting controls actual detached look");w.finish();
    }
    {
        World w;w.activate();fixtureKeys['W']=true;FixtureKeyEvent bodyMovement{'W',true};fixtureNativeKey(bodyMovement);
        check(!bodyMovement.cancelled&&!inputLedger.owned('W'),"Freelook leaves new movement with body owner");
        fixtureKeys[VK_F5]=true;FixtureKeyEvent perspective{VK_F5,true};fixtureNativeKey(perspective);
        check(perspective.cancelled&&stopRequested&&!mouseAdmission,"active camera consumes perspective change and requests restoration");
        fixtureCameraBeforeTick(*w.player);fixtureKeys[VK_F5]=false;FixtureKeyEvent perspectiveUp{VK_F5,false};fixtureNativeKey(perspectiveUp);
        check(!saved&&perspectiveUp.cancelled,"perspective release remains paired after restoration");w.finish();
    }
    for(int conflict=0;conflict<5;++conflict) {
        World w;w.binding(false).enabled=true;w.binding(false).key='T';w.binding(false).modifiers=0;
        if(conflict==0){fixtureConfig.zoomKey='T';fixtureConfig.zoomModifiers=0;}
        if(conflict==1){fixtureConfig.mapToggleKey='T';fixtureConfig.mapToggleModifiers=0;}
        if(conflict==2){fixtureConfig.keybinds[0]='T';fixtureConfig.keybindModifiers[0]=0;}
        if(conflict>=3)fixtureMapBindings[conflict-3]='T';
        fixtureCameraTick(*w.player);fixtureKeys['T']=true;FixtureKeyEvent down{'T',true};fixtureNativeKey(down);fixtureCameraTick(*w.player);
        check(!down.cancelled&&!saved,"conflicting Zoom/Map/module action keeps its original input owner");w.finish();
    }
    {
        World w;w.activate(true);fixtureKeys['W']=true;FixtureKeyEvent down{'W',true};fixtureNativeKey(down);
        requestCameraCandidateReset();check(!mouseAdmission&&stopRequested,"formal reset closes input immediately");
        fixtureCameraBeforeTick(*w.player);check(!saved,"formal reset restores on owner BeforeTick");
        check(!fixtureCameraCleanup()&&std::string(cameraCandidateStatus()).find("owned input releases")!=std::string::npos,"cleanup remains pinned for owned movement and activation releases");
        inputAllowed=false;runtimeReady=false;FixtureKeyEvent repeat{'W',true};fixtureNativeKey(repeat);
        check(repeat.cancelled,"pinned callbacks retain owned repeat after runtime disables input");
        fixtureKeys['W']=false;FixtureKeyEvent up{'W',false};fixtureNativeKey(up);
        fixtureKeys['T']=false;FixtureKeyEvent activateUp{'T',false};fixtureNativeKey(activateUp);
        check(up.cancelled&&activateUp.cancelled&&fixtureCameraCleanup(),"cleanup completes only after final owned input releases");w.finish();
    }
    for(unsigned mask=0;mask<9;++mask) {
        World w;auto options=w.binding(false);options.enabled=true;options.key='T';options.modifiers=mask;
        fixtureCameraTick(*w.player);fixtureKeys[VK_CONTROL]=(mask&1)!=0;fixtureKeys[VK_SHIFT]=(mask&2)!=0;fixtureKeys[VK_MENU]=(mask&4)!=0;
        fixtureKeys['T']=true;FixtureKeyEvent down{'T',true};fixtureNativeKey(down);fixtureCameraTick(*w.player);
        check(down.cancelled&&saved,"formal modifier bits Ctrl1 Shift2 Alt4 and Any8 activate exact camera chord");w.finish();
    }
    for(auto key:{int('W'),int('A'),int('S'),int('D'),int(VK_SPACE),int(VK_SHIFT),int(VK_F5),int(VK_F10),int(VK_ESCAPE)}) {
        World w;w.binding(false).enabled=true;w.binding(false).key=key;fixtureCameraTick(*w.player);
        fixtureKeys[key]=true;FixtureKeyEvent down{key,true};fixtureNativeKey(down);fixtureCameraTick(*w.player);
        check(!down.cancelled&&!saved,"reserved movement/control bind never starts camera");w.finish();
    }
    {
        World w;w.activate(true);LevelRendererPlayer renderer{w.client};fixtureCameraRender(renderer);fixtureInventoryClaims=true;fixtureZoomClaims=true;
        FixtureMouseEvent wheel{MouseAction::ActionWheel,1};fixtureNativeMouse(wheel);
        check(wheel.cancelled&&fixtureInputOrder==std::vector<std::string>{"Inventory"}&&fixtureZoomScrollCalls==0,"actual Native Inventory ownership precedes Zoom and Camera");
        check(pendingWheel==0&&stopRequested&&!mouseAdmission,"Inventory-owned wheel is observed cancelled by Camera without flight");w.finish();
    }
    {
        World w;w.binding(false).enabled=true;w.binding(false).key='U';w.activate(true);
        fixtureKeys['W']=true;FixtureKeyEvent movement{'W',true};fixtureNativeKey(movement);
        fixtureKeys['U']=true;FixtureKeyEvent handoff{'U',true};fixtureNativeKey(handoff);fixtureCameraTick(*w.player);
        check(handoff.cancelled&&!saved&&inputLedger.owned('W'),"mode handoff restores camera while retaining movement release ownership");
        fixtureKeys['W']=false;FixtureKeyEvent movementUp{'W',false};fixtureNativeKey(movementUp);
        fixtureKeys['U']=false;FixtureKeyEvent release{'U',false};fixtureNativeKey(release);
        fixtureKeys['U']=true;FixtureKeyEvent fresh{'U',true};fixtureNativeKey(fresh);fixtureCameraTick(*w.player);
        check(movementUp.cancelled&&release.cancelled&&fresh.cancelled&&saved&&saved->mode==DetachedCameraMode::Freelook,"movement release pairs across mode handoff and next fresh mode activates");w.finish();
    }
    {
        World w;w.activate(true);fixtureKeys['W']=true;FixtureKeyEvent movement{'W',true};fixtureNativeKey(movement);
        w.binding(true).enabled=false;fixtureCameraBeforeTick(*w.player);check(!saved,"module disable restores active FreeCamera");
        w.binding(true).enabled=true;fixtureCameraTick(*w.player);
        fixtureKeys['T']=false;FixtureKeyEvent release{'T',false};fixtureNativeKey(release);
        fixtureKeys['T']=true;FixtureKeyEvent fresh{'T',true};fixtureNativeKey(fresh);fixtureCameraTick(*w.player);
        check(!saved&&inputLedger.owned('W'),"still-held former camera-owned movement blocks new mode admission");
        fixtureKeys['W']=false;fixtureCameraTick(*w.player);
        check(saved&&mouseAdmission,"trusted release of former camera-owned movement never manufactures vanilla body Down");w.finish();
    }
    for(auto button:{MouseAction::ActionLeft,MouseAction::ActionRight,MouseAction::ActionMiddle,MouseAction::ActionX1,MouseAction::ActionX2}) {
        World w;w.activate();FixtureMouseEvent down{button,MouseAction::DataDown};fixtureNativeMouse(down);
        check(down.cancelled,"camera-owned click Down is consumed before cleanup");
        fixtureCameraBeforeTick(*w.player);check(!fixtureCameraCleanup(),"cleanup pins callbacks for withheld mouse and key releases");
        inputAllowed=false;runtimeReady=false;FixtureMouseEvent up{button,MouseAction::DataUp};fixtureNativeMouse(up);
        check(up.cancelled&&!(suppressedButtons.load()&(1u<<button)),"owned mouse Up stays paired while cleanup is pinned and gameplay input disabled");
        fixtureKeys['T']=false;FixtureKeyEvent activateUp{'T',false};fixtureNativeKey(activateUp);
        check(activateUp.cancelled&&fixtureCameraCleanup(),"cleanup retires after the final key and mouse release");w.finish();
    }
    for(bool down:{false,true})for(bool cancelled:{false,true}) {
        World w;w.activate();fixtureConfigThrows=true;fixtureKeys['T']=down;
        FixtureKeyEvent event{'T',down,cancelled};fixtureNativeKey(event);
        check(event.cancelled,"owned repeat/release remains consumed when bind query fails or external owner cancels");
        if(down||cancelled)check(stopRequested&&!mouseAdmission,"owned input boundary failure requests restoration");
        else check(!lookActivation.wanted(),"owned hold Up bypasses bind query and releases hold activation");
        check(inputLedger.owned('T')==down,"failed repeat retains and failed Up retires prior key ownership");
        fixtureConfigThrows=false;fixtureCameraBeforeTick(*w.player);check(!saved,"bind exception or external cancellation restores before native body tick");w.finish();
    }
    for(bool cancelled:{false,true}) {
        World w;w.binding(false).enabled=true;w.binding(false).key='T';fixtureCameraTick(*w.player);fixtureConfigThrows=true;
        fixtureKeys['T']=true;FixtureKeyEvent down{'T',true,cancelled};fixtureNativeKey(down);
        FixtureKeyEvent repeat{'T',true,cancelled};fixtureNativeKey(repeat);
        fixtureKeys['T']=false;FixtureKeyEvent up{'T',false,cancelled};fixtureNativeKey(up);
        check(down.cancelled==cancelled&&repeat.cancelled==cancelled&&up.cancelled==cancelled&&!inputLedger.anyOwned(),"bind-query exceptions cannot create ownership for fresh/unowned repeat/Up");
        fixtureConfigThrows=false;w.finish();
    }
    for(bool free:{false,true})for(bool eventRelease:{false,true}) {
        World w;w.binding(free).hold=false;w.activate(free);
        fixtureKeys['T']=false;
        if(eventRelease){FixtureKeyEvent up{'T',false};fixtureNativeKey(up);check(up.cancelled,"toggle bind Up remains paired with owned Down");}
        fixtureCameraTick(*w.player);
        check(saved&&mouseAdmission&&!inputLedger.owned('T'),"toggle mode survives actual or trusted physical activation-key release");
        fixtureKeys['T']=true;FixtureKeyEvent next{'T',true};fixtureNativeKey(next);fixtureCameraTick(*w.player);
        check(next.cancelled&&!saved&&!mouseAdmission,"next fresh toggle press exits actual detached session");w.finish();
    }
    for(bool mouse:{false,true})for(int boundary=0;boundary<10;++boundary) {
        World w;w.activate(true);IClientInstance alternate;alternate.player=w.player.get();fixtureNextPrimary=&alternate;
        auto differentActor=w.owner->mRegistry->create();LocalPlayer differentPlayer{EntityContext{*w.owner,differentActor}};
        if(boundary==0)worldGeneration.fetch_add(1);
        if(boundary==1)++w.player->dimension;
        if(boundary==2)fixturePrimary=&alternate;
        if(boundary==3)w.client.player=&differentPlayer;
        if(boundary==4)w.owner->mRegistry->remove<MinecraftCamera::ActiveCameraComponent>(w.rig);
        if(boundary==5)w.owner->mRegistry->emplace<VanillaCamera::UpdatePlayerFromCameraComponent>(w.rig);
        if(boundary==6)w.client.cameras->mCameraEntities.value.clear();
        if(boundary==7){auto extra=w.owner->mRegistry->create();
            w.owner->mRegistry->emplace<MinecraftCamera::CameraDirectLookComponent>(extra);
            w.owner->mRegistry->emplace<MinecraftCamera::ActiveCameraComponent>(extra);
            w.owner->mRegistry->emplace<CameraClientInstanceComponent>(extra).mClientInstance.value=&w.client;
            w.client.cameras->mCameraEntities.value.push_back(std::make_unique<EntityContext>(EntityContext{*w.owner,extra}));}
        if(boundary==8)w.client.cameras->mCameraEntities.advanceGenerationAtRead=true;
        if(boundary==9)w.client.cameras->mCameraEntities.switchPrimaryAtRead=true;
        if(mouse){FixtureMouseEvent event{MouseAction::ActionMoveRelative,0,20,12};fixtureNativeMouse(event);
            check(!event.cancelled&&pendingDx==0&&pendingDy==0,"actual Native fresh mouse refuses stale generation/dimension/primary/rig ownership");}
        else {fixtureKeys['W']=true;FixtureKeyEvent event{'W',true};fixtureNativeKey(event);
            check(!event.cancelled&&!inputLedger.owned('W'),"actual Native fresh movement refuses stale generation/dimension/primary/rig ownership");}
        check(runtimeReady&&inputAllowed&&!mouseAdmission&&stopRequested,"fresh input stale-owner rejection closes camera with otherwise stale gameplay flags still true");
        if(boundary==5)w.owner->mRegistry->remove<VanillaCamera::UpdatePlayerFromCameraComponent>(w.rig);
        w.client.player=w.player.get();w.finish();
    }
    {
        World w;w.activate();auto before=w.direct().mPitch;w.direct().mPitchMin=1;w.direct().mPitchMax=-1;
        stopRequested=true;fixtureCameraBeforeTick(*w.player);
        check(saved&&!mouseAdmission&&w.direct().mPitch==before,"invalid bounds fail closed before clamp/write");
        check(!w.owner->mRegistry->all_of<VanillaCamera::UpdatePlayerFromCameraComponent>(w.rig),"invalid restore leaves copy detached");
        w.direct().mPitchMin=NAN;fixtureCameraBeforeTick(*w.player);check(saved.has_value(),"nonfinite restore remains pinned");w.finish();
    }
    {
        World w;w.binding(false).enabled=true;w.binding(false).key='T';fixtureConfigThrows=true;
        check(!fixtureCameraKey('T',true,false),"noexcept boundary contains bind-query failure before ownership");
        check(stopRequested&&!mouseAdmission,"bind-query failure closes admission");fixtureConfigThrows=false;
        check(!fixtureCameraKey('T',false,false),"unclaimed failed activation release forwarded");w.finish();
    }
    for(auto button:{MouseAction::ActionLeft,MouseAction::ActionRight}) {
        World w;w.activate();check(fixtureCameraMouse(button,MouseAction::DataDown,0,0,false),"active attack/use withheld");
        check(stopRequested&&!mouseAdmission,"active click cancels camera immediately");fixtureCameraBeforeTick(*w.player);
        check(!saved,"camera restored before simulated native player tick");
        check(fixtureCameraMouse(button,MouseAction::DataUp,0,0,false),"original suppressed release paired");w.finish();
    }
    {
        World w;w.activate();fixtureCameraCleanup();w.binding(false).enabled=false;
        check(fixtureCameraKey('T',true,false),"owned repeated activation remains withheld during cleanup");
        fixtureKeys['T']=false;check(fixtureCameraKey('T',false,false),"owned release remains paired during cleanup");
        check(fixtureCameraCleanup(),"cleanup completes after final owned key release");
        fixtureCameraStart();
        check(!fixtureCameraKey('T',true,false),"disabled bind fresh down forwards after retired ownership");
        check(!fixtureCameraKey('T',false,false),"disabled bind ordinary release forwards");w.finish();
    }
    {
        World w;w.activate();fixtureCameraMouse(MouseAction::ActionLeft,MouseAction::DataDown,0,0,false);
        fixtureCameraBeforeTick(*w.player);
        check(!fixtureCameraMouse(MouseAction::ActionLeft,MouseAction::DataDown,0,0,false),"normal fresh mouse down retires old marker");
        check(!fixtureCameraMouse(MouseAction::ActionLeft,MouseAction::DataUp,0,0,false),"normal mouse up forwarded");w.finish();
    }
    {
        World w;w.activate();fixtureBackend=GuiBackendKind::LholoBridge;fixtureGuiCaptured=true;
        check(fixtureCameraKey('T',false,false),"LHolo handoff retains matching owned release");
        check(!fixtureCameraMouse(MouseAction::ActionLeft,MouseAction::DataDown,0,0,true),"externally cancelled event remains owned by provider");
        fixtureCameraBeforeTick(*w.player);check(!saved&&!mouseAdmission,"LHolo GUI restoration precedes native player tick");w.finish();
    }
    {
        World w;w.activate();fixtureFocus=false;fixtureCameraBeforeTick(*w.player);
        check(!saved&&!mouseAdmission,"focus loss restores before origin");w.finish();
    }
    {
        World w;w.activate();w.player->context.tryGetComponent<ActorRotationComponent>()->mRot->x=NAN;
        fixtureCameraTick(*w.player);check(!mouseAdmission&&saved,"NaN actor orientation blocks admission and unsafe restoration");w.finish();
    }
    {
        World w;w.binding(false).enabled=true;w.binding(false).key='T';fixtureCameraTick(*w.player);
        fixtureKeys['T']=true;check(fixtureCameraKey('T',true,false),"generation admission uses actual fresh Key event");w.player->advanceGenerationAtEye=true;
        fixtureCameraTick(*w.player);check(!saved&&!mouseAdmission,"generation change during admission rejects old rig");
        check(w.owner->mRegistry->all_of<VanillaCamera::UpdatePlayerFromCameraComponent>(w.rig),"generation rejection never detaches engine copy");w.finish();
    }
    {
        World w;w.activate(true);LevelRendererPlayer renderer{w.client};fixtureCameraRender(renderer);
        check(fixtureCameraMouse(MouseAction::ActionWheel,1,0,0,false),"fresh wheel accepted");fixtureNow+=101;
        fixtureCameraTick(*w.player);check(!saved&&w.offset()==glm::vec3{},"stale queued flight rejected with exact offset restoration");w.finish();
    }
    {
        World w;w.activate(true);LevelRendererPlayer renderer{w.client};fixtureCameraRender(renderer);
        fixtureCameraMouse(MouseAction::ActionWheel,1,0,0,false);fixtureCameraMouse(MouseAction::ActionMoveRelative,0,5,0,false);
        fixtureCameraTick(*w.player);check(saved&&w.offset()==glm::vec3{},"look revision discards old-basis queued flight");
        fixtureCameraRender(renderer);fixtureCameraMouse(MouseAction::ActionWheel,1,0,0,false);fixtureCameraTick(*w.player);
        check(saved&&std::abs(w.offset().z+1)<.0001f,"fresh current view sample moves only camera");w.finish();
    }
    {
        World w;w.activate(true);LevelRendererPlayer renderer{w.client};fixtureCameraRender(renderer);
        renderer.mCameraForward.value={NAN,0,0};fixtureCameraRender(renderer);
        std::array<float,3> value;check(!flightDirection.read(directionContext(),fixtureNow,value),"invalid sample retires previous valid direction");
        check(stopRequested&&!mouseAdmission,"invalid render basis fails closed");w.finish();
    }
    {
        World w;w.activate(true);LevelRendererPlayer renderer{w.client};fixtureCameraRender(renderer);worldGeneration.fetch_add(1);
        fixtureCameraRender(renderer);check(stopRequested&&!mouseAdmission,"generation mismatch closes sampled flight");w.finish();
    }
    {
        World w;w.activate(true);LevelRendererPlayer renderer{w.client};fixtureCameraRender(renderer);
        fixtureCameraMouse(MouseAction::ActionWheel,1,0,0,false);w.player->advanceGenerationAtEye=true;
        fixtureCameraTick(*w.player);
        check(saved&&!mouseAdmission&&stopRequested&&w.offset()==glm::vec3{},"generation crossing during active eye sampling prevents old-rig flight write");
        fixtureCameraBeforeTick(*w.player);check(!saved,"owner callback restores after final write gate rejects boundary");w.finish();
    }
    {
        World w;w.activate(true);LevelRendererPlayer renderer{w.client};renderer.mCameraForward.advanceGenerationAtRead=true;
        fixtureCameraRender(renderer);std::array<float,3> value;
        check(stopRequested&&!mouseAdmission&&!flightDirection.read(directionContext(),fixtureNow,value),"generation crossing during native forward sampling cannot relabel old sample");w.finish();
    }
    for(int mode:{1,2,3}) {
        World w;w.activate(true);LevelRendererPlayer renderer{w.client};fixtureCameraRender(renderer);
        fixtureCameraMouse(MouseAction::ActionWheel,1,0,0,false);w.player->closeContextAtEye=mode;
        fixtureCameraTick(*w.player);
        check(saved&&!mouseAdmission&&stopRequested&&w.offset()==glm::vec3{},"focus/LHolo/retirement crossing during native eye read rejects final flight write");w.finish();
    }
    for(bool free:{false,true})for(bool hold:{false,true}) {
        World w;auto options=w.binding(free);
        options.enabled=true;options.key='T';options.hold=hold;fixtureKeys['T']=true;
        FixtureKeyEvent external{'T',true,true};fixtureNativeKey(external);
        check(external.cancelled&&inputLedger.inhibited('T'),"OFF external canceled activation records per-key inhibition");
        fixtureCameraTick(*w.player);check(!saved&&!mouseAdmission,"held canceled activation cannot start hold or toggle camera");
        FixtureKeyEvent repeated{'T',true,false};fixtureNativeKey(repeated);
        check(!repeated.cancelled&&inputLedger.inhibited('T'),"uncancelled repeat cannot take externally-owned held key");
        fixtureCameraTick(*w.player);check(!saved,"uncancelled repeat still cannot start camera");
        options.key='U';fixtureCameraTick(*w.player);options.key='T';fixtureCameraTick(*w.player);
        check(!saved&&inputLedger.inhibited('T'),"key reassignment away and back cannot retire inhibition of held original key");
        options.enabled=false;auto other=w.binding(!free);other.enabled=true;other.key='T';other.hold=hold;
        fixtureCameraTick(*w.player);check(!saved,"mode change cannot start same externally-owned physical key");
        other.enabled=false;options.enabled=true;
        fixtureFocus=false;fixtureCameraTick(*w.player);fixtureFocus=true;
        fixtureBackend=GuiBackendKind::LholoBridge;fixtureGuiCaptured=true;fixtureCameraTick(*w.player);
        fixtureGuiCaptured=false;worldGeneration.fetch_add(1);fixtureCameraTick(*w.player);
        check(!saved&&inputLedger.inhibited('T'),"focus GUI and generation transitions preserve key inhibition");
        fixtureCameraStart();options.enabled=true;options.key='T';options.hold=hold;
        fixtureCameraTick(*w.player);check(!saved&&inputLedger.inhibited('T'),"start preserves inhibition when physical key is still held");
        fixtureKeys['T']=false;FixtureKeyEvent release{'T',false,true};fixtureNativeKey(release);
        fixtureCameraTick(*w.player);check(!inputLedger.inhibited('T')&&!saved,"actual release retires inhibition without starting camera");
        fixtureKeys['T']=true;FixtureKeyEvent fresh{'T',true,false};fixtureNativeKey(fresh);
        check(fresh.cancelled,"fresh uncancelled activation after release becomes camera-owned");
        fixtureCameraTick(*w.player);
        check(saved&&saved->mode==(free?DetachedCameraMode::FreeCamera:DetachedCameraMode::Freelook),"fresh press after release starts correct mode");w.finish();
    }
    {
        World w;w.binding(false).enabled=true;w.binding(false).key='T';fixtureKeys['T']=true;
        FixtureKeyEvent external{'T',true,true};fixtureNativeKey(external);fixtureCameraTick(*w.player);
        fixtureKeys['T']=false;fixtureCameraTick(*w.player);
        check(!inputLedger.inhibited('T')&&!saved,"physical release poll retires inhibition when release event was missed");
        w.activate();w.finish();
    }
    for(int boundary:{1,2,3})for(bool hold:{false,true})for(bool free:{false,true}) {
        World w;auto options=w.binding(free);options.enabled=true;options.key='T';options.hold=hold;fixtureKeys['T']=true;
        FixtureKeyEvent external{'T',true,true};fixtureNativeKey(external);
        fixtureAsyncUnavailable=true;
        if(boundary==1)fixtureFocus=false;
        if(boundary==2)fixtureGuiCaptured=true;
        if(boundary==3)runtimeReady=false;
        fixtureCameraTick(*w.player);fixtureCameraStart();options.enabled=true;options.key='T';options.hold=hold;
        check(inputLedger.inhibited('T')&&!saved,"untrusted zero at focus GUI runtime and Start cannot retire external-owner inhibition");
        fixtureFocus=true;fixtureGuiCaptured=false;runtimeReady=true;fixtureAsyncUnavailable=false;
        fixtureCameraTick(*w.player);
        check(inputLedger.inhibited('T')&&!saved,"return to trusted context while still held cannot start camera");
        fixtureKeys['T']=false;FixtureKeyEvent release{'T',false,false};fixtureNativeKey(release);fixtureCameraTick(*w.player);
        fixtureKeys['T']=true;FixtureKeyEvent fresh{'T',true,false};fixtureNativeKey(fresh);fixtureCameraTick(*w.player);
        check(fresh.cancelled&&saved,"trusted real release and fresh press restore admission after unavailable polls");w.finish();
    }
    {
        World w;w.binding(false).enabled=true;w.binding(false).key='T';fixtureKeys['T']=true;
        FixtureKeyEvent external{'T',true,true};fixtureNativeKey(external);
        fixtureLoseFocusAtAsyncRead=true;(void)physicalCameraKeyDown('T');
        check(inputLedger.inhibited('T'),"context lost during a zero read cannot clear external ownership");
        fixtureFocus=true;fixtureCameraTick(*w.player);check(!saved,"still held after read-boundary focus loss cannot activate");w.finish();
    }
    {
        World w;w.activate(true);fixturePrimary=nullptr;fixtureCameraBeforeTick(*w.player);
        check(stopRequested&&!mouseAdmission,"missing primary cannot retain admission");w.finish();
    }
    {
        World w;w.activate();FixtureMouseEvent down{MouseAction::ActionLeft,MouseAction::DataDown};fixtureNativeMouse(down);
        check(down.cancelled&&stopRequested&&!mouseAdmission,"actual Native listener cancels active attack and stops camera");
        fixtureCameraBeforeTick(*w.player);check(!saved,"Native-click request restores before origin");
        FixtureMouseEvent up{MouseAction::ActionLeft,MouseAction::DataUp,0,0,true};fixtureNativeMouse(up);
        check(!(suppressedButtons.load()&(1u<<MouseAction::ActionLeft)),"actual Native listener retires provider-cancelled release ledger");
        FixtureMouseEvent ordinaryDown{MouseAction::ActionLeft,MouseAction::DataDown};fixtureNativeMouse(ordinaryDown);
        FixtureMouseEvent ordinaryUp{MouseAction::ActionLeft,MouseAction::DataUp};fixtureNativeMouse(ordinaryUp);
        check(!ordinaryDown.cancelled&&!ordinaryUp.cancelled,"following ordinary click pair reaches player owner");w.finish();
    }
    {
        World w;w.activate();FixtureMouseEvent external{MouseAction::ActionMoveRelative,0,20,12,true};fixtureNativeMouse(external);
        check(external.dx()==20&&external.dy()==12&&fixtureZoomScaleCalls==0,"actual Native listener preserves externally-owned mouse fields");
        check(stopRequested&&!mouseAdmission,"externally owned mouse closes camera admission");w.finish();
    }
    {
        World w;w.activate(true);LevelRendererPlayer renderer{w.client};fixtureCameraRender(renderer);fixtureZoomClaims=true;
        FixtureMouseEvent wheel{MouseAction::ActionWheel,1};fixtureNativeMouse(wheel);
        check(wheel.cancelled&&fixtureZoomScrollCalls==1&&zoomHeld,"actual Native listener admits Zoom wheel before camera");
        check(saved&&mouseAdmission&&!stopRequested&&pendingWheel==0,"Zoom owned wheel does not become flight or cancel camera");w.finish();
    }
    {
        World w;w.activate();FixtureKeyEvent external{'T',true,true};fixtureNativeKey(external);
        check(external.cancelled&&inputLedger.owned('T')&&inputLedger.inhibited('T')&&stopRequested,"actual Native key listener keeps prior owned key and external inhibition until release");w.finish();
    }
    {
        World w;w.activate();fixtureConfigThrows=true;FixtureKeyEvent repeated{'T',true};fixtureNativeKey(repeated);
        check(repeated.cancelled&&stopRequested&&!mouseAdmission,"actual Native key listener contains bind-query exception in active session");
        fixtureConfigThrows=false;fixtureCameraBeforeTick(*w.player);check(!saved,"exception stop restores before origin");w.finish();
    }
    {
        World w;w.activate();fixtureThread=2;check(!fixtureCameraCleanup(),"wrong-thread cleanup pins native callbacks for owner restoration");
        check(saved&&!mouseAdmission,"wrong-thread cleanup does not mutate camera ownership");fixtureThread=1;
        fixtureCameraBeforeTick(*w.player);check(!saved,"established owner callback completes pinned restoration");w.finish();
    }
    {
        World w;w.activate();w.owner->mRegistry->emplace<VanillaCamera::UpdatePlayerFromCameraComponent>(w.rig);
        auto yaw=w.direct().mYaw;check(!fixtureCameraCleanup(),"competing Link owner prevents unsafe restoration");
        check(saved&&w.direct().mYaw==yaw,"competing owner fields never overwritten");
        w.owner->mRegistry->remove<VanillaCamera::UpdatePlayerFromCameraComponent>(w.rig);w.finish();
    }
    {
        World w;w.activate(true);LevelRendererPlayer renderer{w.client};fixtureCameraRender(renderer);auto old=sessionEpoch;
        w.finish();fixtureCameraStart();w.activate(true);
        std::array<float,3> value;check(sessionEpoch!=old&&!flightDirection.read(directionContext(),fixtureNow,value),"new session never reuses previous direction epoch");w.finish();
    }
    {
        World w;w.activate(true);LevelRendererPlayer renderer{w.client};fixtureCameraRender(renderer);w.player->dimension++;
        fixtureCameraRender(renderer);check(stopRequested&&!mouseAdmission,"dimension discontinuity invalidates renderer sample");w.finish();
    }
    std::cout<<"Actual camera callback/control-flow fixture: "<<fixtureChecks<<" checks PASS; native/service boundaries doubled, Minecraft NOT_RUN\n";
    return 0;
}catch(std::exception const& e){std::cerr<<e.what()<<'\n';return 1;}}
