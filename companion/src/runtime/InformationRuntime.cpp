#include "runtime/InformationRuntime.h"
#include "runtime/SaturationProbe.h"
#include "render/ShulkerPreview.h"
#include "core/Settings.h"
#include "gui/GuiBridge.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/render/UIRenderEvent.h"
#include "mc/client/renderer/screen/MinecraftUIRenderContext.h"
#include "mc/client/game/IClientInstance.h"
#include "mc/client/gui/screens/ScreenView.h"
#include "mc/client/gui/GuiData.h"
#include "mc/client/gui/ScreenSizeData.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/Item.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/attribute/Attribute.h"
#include "mc/world/attribute/AttributeInstance.h"
#include "mc/world/attribute/AttributeInstanceConstRef.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/biome/Biome.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/phys/HitResult.h"
#include "mc/world/level/block/Block.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/world/level/block/BrightnessPair.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/shared_types/legacy/Difficulty.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include <Windows.h>
#include <Psapi.h>
#include <atomic>
#include <ctime>
#include <iomanip>
#include <mutex>
#include <sstream>
namespace praxis::companion {
namespace {
std::atomic<std::shared_ptr<InformationOptions const>> options{std::make_shared<InformationOptions>()};
std::atomic<std::shared_ptr<InformationSnapshot const>> publication{std::make_shared<InformationSnapshot>()};
ll::event::ListenerPtr listener;
std::mutex collectionMutex;
std::atomic_bool running{};
std::atomic_bool collectionFailureLogged{};
std::uint64_t previousGeneration{},previousTime{};
std::array<double,3> previousPosition{};
ClientSaturationTracker saturationTracker;
std::uint64_t memorySampleAt{};std::string cachedMemory;
std::string numbers(std::initializer_list<double> values) {
    std::ostringstream stream;stream<<std::fixed<<std::setprecision(2);
    bool first=true;for(double v:values) {if(!std::isfinite(v))return {};if(!first)stream<<", ";first=false;stream<<v;}
    return stream.str();
}
std::optional<DurabilityValue> inspect(ItemStack const& stack) {
    if(stack.isNull()||!previewItemWithinBudget(stack)||!stack.isDamageableItem()||!stack.mItem)return std::nullopt;
    return durabilityValue(stack.getHoverName(),stack.getDamageValue(),stack.mItem->getMaxDamage());
}
void collect(ll::event::AfterUIRenderEvent& event) {
    std::lock_guard lock(collectionMutex);
    if(!running.load()||!runtimeReady.load()){saturationTracker.reset();return;}
    auto o=options.load();
    if(!o->infoHud&&!o->durabilityHud&&!o->playerHud.foodHud&&!inspectorNeeded(o->inspector)) { publication.store(std::make_shared<InformationSnapshot>());previousTime=0;saturationTracker.reset();return; }
    auto& client=event.uiRenderContext().mClient;
    auto* player=client.getLocalPlayer();
    if(!player) {publication.store(std::make_shared<InformationSnapshot>());previousTime=0;saturationTracker.reset();return;}
    auto const screen=event.screenView().getScreenName();
    auto top=client.getTopScreenName();
    auto view=informationView(screen,top);
    if(view==InformationView::Retire) {
        publication.store(std::make_shared<InformationSnapshot>());previousTime=0;saturationTracker.reset();return;
    }
    if(view==InformationView::Defer)return;
    auto generation=worldGeneration.load();auto now=GetTickCount64();
    if(previousGeneration==generation&&previousTime&&now-previousTime<100)return;
    InformationSnapshot next;next.generation=generation;next.observedMs=now;next.valid=true;
    auto const logical=client.getGuiData()->mScreenSizeData.get().clientUIScreenSize.get();
    next.logicalWidth=logical.x;next.logicalHeight=logical.y;
    if(o->playerHud.foodHud) {
        // A real client attribute, explicitly labelled as such. A syncable flag
        // alone is not proof of a received server value for hidden attributes.
        auto const& hunger=Player::HUNGER();
        auto ref=player->getAttribute(hunger);auto* instance=ref.mPtr;
        if(instance&&instance->mAttribute==&hunger&&hunger.mSyncable)
            next.food.hunger={instance->mCurrentValue,FoodSource::ClientAttribute};
        auto const& saturation=Player::SATURATION();
        auto saturationRef=player->getAttribute(saturation);
        next.food.saturation=saturationTracker.observe(readSaturationObservation(saturation,saturationRef),
            generation,static_cast<int>(player->getDimensionId()),now);
        // This is observed client state, not proof of a server packet receipt.
        // Exhaustion has no verified client contract and remains Unknown.
    } else saturationTracker.reset();
    auto feet=player->getFeetPos();auto rot=player->getRotation();int dim=player->getDimensionId();
    next.values[0]=numbers({feet.x,feet.y,feet.z});
    if(auto scale=dimensionCoordinateScale(dim))next.values[1]=numbers({feet.x*(*scale),feet.z*(*scale)});
    next.values[2]=dim==0?"Overworld":dim==1?"Nether":dim==2?"End":std::to_string(dim);
    int difficulty=static_cast<int>(player->getLevel().getDifficulty());
    constexpr std::array difficultyNames{"Peaceful","Easy","Normal","Hard"};
    if(difficulty>=0&&difficulty<4)next.values[4]=difficultyNames[difficulty];
    if(std::isfinite(rot.y)) {
        double yaw=std::fmod(static_cast<double>(rot.y)+360.0,360.0);if(yaw<0)yaw+=360;
        constexpr std::array names{"South","West","North","East"};
        next.values[5]=names[static_cast<unsigned>((yaw+45)/90)%4];
    }
    next.values[6]=numbers({rot.y});next.values[7]=numbers({rot.x});next.values[13]=numbers({rot.y,rot.x});
    next.values[8]=player->isSprinting()?"Yes":"No";
    auto x=informationBlock(feet.x),y=informationBlock(feet.y),z=informationBlock(feet.z);
    if(x&&y&&z) {
        next.values[14]=std::to_string(*x)+", "+std::to_string(*y)+", "+std::to_string(*z);
        next.values[15]=std::to_string(informationChunk(*x))+", "+std::to_string(informationChunk(*z));
        // Do not force-load or scan a region. These are single-position reads on a loaded chunk.
        auto& source=player->getDimension().getBlockSourceFromMainChunkSource();
        BlockPos pos{*x,*y,*z};
        if(source.getChunkAt(pos)) {
            next.values[3]=source.getBiome(pos).mHash.get().mStr;
            auto const light=source.getBrightnessPair(pos);
            next.values[11]="sky "+std::to_string(light.sky.get().mValue)+", block "+std::to_string(light.block.get().mValue);
        }
    }
    if(previousGeneration==generation&&previousTime&&now>previousTime&&now-previousTime<=1000) {
        double seconds=(now-previousTime)/1000.0;
        double vx=(feet.x-previousPosition[0])/seconds,vy=(feet.y-previousPosition[1])/seconds,vz=(feet.z-previousPosition[2])/seconds;
        next.values[16]=numbers({std::sqrt(vx*vx+vy*vy+vz*vz)});
        next.values[17]=numbers({std::hypot(vx,vz)});next.values[18]=numbers({vy});
    }
    next.values[19]=std::to_string(player->getLevel().getTime());
    std::time_t wall=std::time(nullptr);std::tm local{};
    if(localtime_s(&local,&wall)==0) {char text[32]{};std::strftime(text,sizeof(text),"%H:%M:%S",&local);next.values[20]=text;}
    if(o->durabilityHud) {
        next.durability[0]=inspect(player->getSelectedItem());next.durability[1]=inspect(player->getOffhandSlot());
        for(unsigned i=0;i<4;++i)next.durability[i+2]=inspect(player->getArmor(static_cast<SharedTypes::Legacy::ArmorSlot>(i)));
    }
    if(inspectorNeeded(o->inspector)) {
        auto const& hit=client.getLatestHitResult();
        auto& source=player->getDimension().getBlockSourceFromMainChunkSource();
        // Reuse Minecraft's visible current target; no new ray cast, region
        // scan, out-of-range actor lookup, container/actor NBT or force-load.
        if(!hit.mIndirectHit&&hit.mType==HitResultType::Tile&&source.getChunkAt(hit.mBlock)) {
            auto const& block=source.getBlock(hit.mBlock);
            if(o->inspector.blockState)next.inspector.looking[0]=boundedInspectorText(block.getTypeName()+" @ "+
                std::to_string(hit.mBlock.x)+", "+std::to_string(hit.mBlock.y)+", "+std::to_string(hit.mBlock.z)+"\n"+
                block.mSerializationId.get().toSnbt());
            if(o->inspector.tags) {
                auto const& tags=block.mTags.get();std::string text;
                if(tags.size()<=64) {
                    for(auto const& tag:tags) {if(!text.empty())text+=", ";text+=tag.mStr;if(text.size()>4096){text.clear();break;}}
                    next.inspector.looking[1]=tags.empty()?"(なし)":std::move(text);
                }
            }
            if(o->inspector.fluid)next.inspector.looking[2]=boundedInspectorText(source.getLiquidBlock(hit.mBlock).getTypeName());
        }
        if(!hit.mIndirectHit&&hit.mType==HitResultType::Entity&&o->inspector.entity) {
            auto* actor=hit.getEntity();
            if(actor&&actor->isAlive()&&!actor->isInvisible()) {
                auto const& position=actor->getPosition();auto coordinates=numbers({position.x,position.y,position.z});
                if(!coordinates.empty())next.inspector.looking[3]=boundedInspectorText(actor->getTypeName()+" / "+actor->getNameTag()+" @ "+coordinates);
            }
        }
        if(o->inspector.fluid&&!hit.mIndirectHit&&hit.mIsHitLiquid&&source.getChunkAt(hit.mLiquid))
            next.inspector.looking[2]=boundedInspectorText(source.getLiquidBlock(hit.mLiquid).getTypeName()+" @ "+
                std::to_string(hit.mLiquid.x)+", "+std::to_string(hit.mLiquid.y)+", "+std::to_string(hit.mLiquid.z));
        if(o->inspector.memory) {
            if(!memorySampleAt||now-memorySampleAt>=1000) {
                PROCESS_MEMORY_COUNTERS counters{};counters.cb=sizeof(counters);cachedMemory.clear();
                if(K32GetProcessMemoryInfo(GetCurrentProcess(),&counters,sizeof(counters)))
                    cachedMemory=numbers({counters.WorkingSetSize/1048576.0})+" MiB";
                memorySampleAt=now;
            }
            next.inspector.memory=cachedMemory;
        }
    }
    // All borrowed Minecraft objects stop at this callback. Publication contains strings/numbers only.
    if(generation!=worldGeneration.load()||!running.load()||!runtimeReady.load()||client.getLocalPlayer()!=player) {
        saturationTracker.reset();return;
    }
    previousGeneration=generation;previousTime=now;previousPosition={feet.x,feet.y,feet.z};
    publication.store(std::make_shared<InformationSnapshot const>(std::move(next)));
}
}
std::shared_ptr<InformationOptions const> informationOptions() noexcept {return options.load();}
std::shared_ptr<InformationSnapshot const> informationSnapshot() noexcept {return publication.load();}
void configureInformationRuntime(InformationOptions const& value) {
    if(validInformationOptions(value)) {
        std::lock_guard lock(collectionMutex);
        if(options.load()->playerHud.foodHud!=value.playerHud.foodHud) {
            saturationTracker.reset();
            auto next=*publication.load();next.food.saturation={};
            publication.store(std::make_shared<InformationSnapshot const>(std::move(next)));
        }
        options.store(std::make_shared<InformationOptions const>(value));
    }
}
void resetInformationRuntime() noexcept {
    std::lock_guard lock(collectionMutex);publication.store(std::make_shared<InformationSnapshot>());
    previousGeneration=previousTime=memorySampleAt=0;previousPosition={};cachedMemory.clear();saturationTracker.reset();
}
bool startInformationRuntime() {
    if(running.exchange(true)||listener)return true;
    try {
        listener=ll::event::EventBus::getInstance().emplaceListener<ll::event::AfterUIRenderEvent>(
            [](auto& event){try{collect(event);}catch(...){
                resetInformationRuntime();
                if(!collectionFailureLogged.exchange(true))logGuiError("InformationRuntime UI sample threw; owned snapshot invalidated");
            }});
    } catch(...) {running=false;return false;}
    if(!listener) {running=false;return false;}return true;
}
bool stopInformationRuntime() noexcept {
    running=false;
    if(listener) {
        if(!ll::event::EventBus::getInstance().removeListener(listener)) {resetInformationRuntime();return false;}
        listener.reset();
    }
    resetInformationRuntime();return true;
}
} // namespace praxis::companion
