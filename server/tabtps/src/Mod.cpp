#include "core/TabTpsSourcePort.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/mod/NativeMod.h"
#include "ll/api/mod/RegisterHelper.h"
#include "ll/api/network/packet/Packet.h"
#include "mc/deps/core/utility/BinaryStream.h"
#include "mc/deps/core/utility/ReadOnlyBinaryStream.h"
#include "mc/server/ServerLevel.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/Level.h"
#include <Windows.h>
#include <Psapi.h>
#include <bcrypt.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace praxis::server::tabtps {
namespace {
namespace wire = praxis::companion::tabtps::source;
struct TelemetryPacket final : ll::network::PacketBase<TelemetryPacket> {
    static constexpr std::string_view CustomPacketId{wire::packetId};
    static constexpr uint PacketVersion{wire::packetVersion};
    static constexpr ll::network::PacketSide Side{ll::network::PacketSide::Client};
    static constexpr bool Optional{true};
    std::string payload;
    void write(BinaryStream& out) const override {
        out.writeString(payload, "tabtps_telemetry", nullptr);
    }
    Bedrock::Result<void> read(ReadOnlyBinaryStream& in) override {
        auto decoded = in.getString(256);
        if (!decoded) return nonstd::make_unexpected(decoded.error());
        payload=std::move(decoded.value());
        return {};
    }
};
std::uint32_t randomEpoch() noexcept {
    std::uint32_t id{};
    if (BCryptGenRandom(nullptr,reinterpret_cast<PUCHAR>(&id),
                        sizeof(id),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0) return 0;
    return id;
}
std::uint64_t filetime(FILETIME v) noexcept {
    return (static_cast<std::uint64_t>(v.dwHighDateTime)<<32)|v.dwLowDateTime;
}
int readMaxPlayers() {
    // Minecraft's actual server.properties, only read at enable; never a guessed 20.
    std::ifstream file(std::filesystem::current_path()/"server.properties");
    std::string line;
    while (std::getline(file,line)) {
        if (line.rfind("max-players=",0)!=0)continue;
        auto n=line.substr(12);
        try {
            std::size_t consumed{};
            auto const value=std::stoi(n,&consumed);
            if(value>=0&&value<=100000&&consumed==n.size())return value;
        } catch (...) {}
        return -1;
    }
    return -1;
}
void measuredTick(ServerLevel& level,std::uint64_t us) noexcept;
LL_TYPE_INSTANCE_HOOK(TickWorkHook,ll::memory::HookPriority::Lowest,
    ServerLevel,&ServerLevel::$_subTick,void) {
    auto start=std::chrono::steady_clock::now();
    origin();
    auto elapsed=std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now()-start).count();
    measuredTick(*this,elapsed>0?static_cast<std::uint64_t>(elapsed):0);
}
class CpuMonitor {
    std::uint64_t lastIdle_{},lastKernel_{},lastUser_{},lastProcess_{};
    std::uint64_t lastMs_{};
    std::deque<double> system_,process_;
    static void add(std::deque<double>& a,double value){
        if(std::isfinite(value)&&value>0 && value<=100){
            a.push_back(value);
            if(a.size()>20)a.pop_front();
        }
    }
    static std::int64_t average(std::deque<double> const& a){
        if(a.empty())return wire::absent;
        double total{};
        for(double x:a)total+=x;
        return static_cast<std::int64_t>(std::llround(100.0*total/a.size()));
    }
public:
    void reset(){system_.clear();process_.clear();lastMs_=0;}
    void poll(std::uint64_t nowMs){
        if(lastMs_ && nowMs-lastMs_<500)return;
        FILETIME idle{},kernel{},user{},create{},exit{},pk{},pu{};
        if(!GetSystemTimes(&idle,&kernel,&user)
            || !GetProcessTimes(GetCurrentProcess(),&create,&exit,&pk,&pu))return;
        const auto i=filetime(idle),k=filetime(kernel),u=filetime(user);
        const auto process=filetime(pk)+filetime(pu);
        if(lastMs_ && nowMs>lastMs_ && k>=lastKernel_ && u>=lastUser_
            && i>=lastIdle_ && process>=lastProcess_) {
            double total=double((k-lastKernel_)+(u-lastUser_));
            double busy=total-double(i-lastIdle_);
            if(total>0&&busy>=0)add(system_,busy*100.0/total);
            DWORD cores=GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
            if(cores>0) {
                double duration100ns=double(nowMs-lastMs_)*10000.0;
                double pct=(process-lastProcess_)*100.0/(duration100ns*cores);
                add(process_,pct);
            }
        }
        lastMs_=nowMs;lastIdle_=i;lastKernel_=k;
        lastUser_=u;lastProcess_=process;
    }
    std::int64_t system()const{return average(system_);}
    std::int64_t process()const{return average(process_);}
};
} // namespace

class TabTpsServerMod final {
    std::shared_ptr<ll::mod::NativeMod> self_;
    std::optional<ll::memory::HookRegistrar<TickWorkHook>> hook_;
    std::mutex mutex_;
    wire::RollingTpsOriginal1m tps_;
    wire::TickTimeAverage mspt_;
    CpuMonitor cpu_;
    std::uint64_t lastSent_{},lastMemoryRead_{};
    std::int64_t privateWorkingSetMiB_{wire::absent},commitMiB_{wire::absent};
    int maxPlayers_{-1};
    std::uint32_t epoch_{},seq_{};
    std::atomic_bool enabled_{};
    std::uint32_t windows_{};
    void memoryPoll(std::uint64_t now){
        if(lastMemoryRead_ && now-lastMemoryRead_<1000)return;
        lastMemoryRead_=now;
        PROCESS_MEMORY_COUNTERS_EX2 data{};
        data.cb=sizeof(data);
        if(GetProcessMemoryInfo(GetCurrentProcess(),
            reinterpret_cast<PPROCESS_MEMORY_COUNTERS>(&data),sizeof(data))) {
            // Consistent pair: Private Working Set is RESIDENT private
            // committed memory; PrivateUsage is total committed private
            // memory. Do not compare whole-process RSS to private commit
            // (RSS includes shared DLL pages and can exceed private commit).
            privateWorkingSetMiB_=static_cast<std::int64_t>(
                (static_cast<std::uint64_t>(data.PrivateWorkingSetSize)+524288)/1048576);
            commitMiB_=static_cast<std::int64_t>(
                (static_cast<std::uint64_t>(data.PrivateUsage)+524288)/1048576);
            if(privateWorkingSetMiB_>commitMiB_)privateWorkingSetMiB_=wire::absent;
        } else {privateWorkingSetMiB_=wire::absent;commitMiB_=wire::absent;}
    }
    void tick(ServerLevel& level,std::uint64_t durationUs){
        if(!enabled_.load(std::memory_order_acquire))return;
        wire::Snapshot snapshot{};
        {
            std::lock_guard guard(mutex_);
            auto now=GetTickCount64();
            tps_.complete(now);
            if(durationUs<=5000000 && durationUs>0)
                mspt_.complete(static_cast<std::uint32_t>(durationUs));
            else mspt_.complete(0);
            cpu_.poll(now);
            memoryPoll(now);
            if(lastSent_ && now-lastSent_<250)return;
            lastSent_=now;
            if(seq_==UINT32_MAX){enabled_.store(false);return;}
            snapshot.epoch=epoch_;
            snapshot.sequence=++seq_;
            snapshot.serverMonotonicMs=now;
            snapshot.tpsCenti=tps_.centi(now);
            snapshot.msptUs=mspt_.meanUs();
            snapshot.cpuSystemCenti=cpu_.system();
            snapshot.cpuProcessCenti=cpu_.process();
            snapshot.memoryUsedMiB=privateWorkingSetMiB_;
            snapshot.memoryCommittedMiB=commitMiB_;
            // There is no JVM heap cap on BDS. Unknown maximum must stay absent.
            snapshot.memoryMaxMiB=wire::absent;
            const auto online=level.getActivePlayerCount();
            snapshot.onlinePlayers=online>=0?online:wire::absent;
            snapshot.maxPlayers=maxPlayers_;
            if(snapshot.onlinePlayers>=0 && snapshot.maxPlayers>=0
                && snapshot.onlinePlayers>snapshot.maxPlayers)
                snapshot.maxPlayers=wire::absent;
        }
        unsigned eligible{};
        level.forEachPlayer([&](Player& player){
            if(!enabled_.load(std::memory_order_acquire))return false;
            if(!TelemetryPacket::canSendTo(player))return true;
            auto perPlayer=snapshot;
            try{
                auto const net=player.getNetworkStatus();
                if(net){
                    auto const ping=net->mCurrentPing.get().count();
                    if(ping>=0&&ping<=3600000)perPlayer.pingMs=ping;
                }
            }catch(...){}
            auto text=wire::encode(perPlayer);
            if(text.empty())return true;
            TelemetryPacket packet;packet.payload=std::move(text);
            packet.sendTo(player);
            ++eligible;
            return true;
        });
        if(self_&&(++windows_==1 || windows_%120==0)){
            self_->getLogger().info(
              "TabTPS v2: 1mTPS={}, simMSPT={}ms, cpuSystem={}, cpuProcess={}, "
              "privateWorkingSetMiB={}, privateCommitMiB={}, online={}, max={}, eligibleClients={}",
              snapshot.tpsCenti<0?-1.0:snapshot.tpsCenti/100.0,
              snapshot.msptUs<0?-1.0:snapshot.msptUs/1000.0,
              snapshot.cpuSystemCenti,snapshot.cpuProcessCenti,
              snapshot.memoryUsedMiB,snapshot.memoryCommittedMiB,
              snapshot.onlinePlayers,snapshot.maxPlayers,eligible);
        }
    }
public:
    static TabTpsServerMod& instance(){
        static TabTpsServerMod value;return value;
    }
    void complete(ServerLevel& level,std::uint64_t durationUs)noexcept{
        try{tick(level,durationUs);}catch(...){enabled_.store(false);}
    }
    bool load(){self_=ll::mod::NativeMod::current();return bool(self_);}
    bool enable(){
        if(enabled_.load())return true;
        epoch_=randomEpoch();if(!epoch_)return false;
        maxPlayers_=readMaxPlayers();
        seq_=0;windows_=0;lastSent_=0;lastMemoryRead_=0;
        privateWorkingSetMiB_=commitMiB_=wire::absent;
        {std::lock_guard guard(mutex_);tps_.reset(GetTickCount64());
         mspt_.reset();cpu_.reset();}
        try{hook_.emplace();}catch(...){hook_.reset();}
        if(!hook_)return false;
        enabled_.store(true,std::memory_order_release);
        if(self_)self_->getLogger().info(
           "TabTPS v2 enabled: 60 time-weighted 20-tick TPS samples (no fake warmup), 100-tick "
           "simulation MSPT, per-player connection ping, 500ms CPU "
           "and process memory, 250ms optional snapshots.");
        return true;
    }
    bool disable(){enabled_.store(false,std::memory_order_release);hook_.reset();return true;}
    bool unload(){if(!disable())return false;self_.reset();return true;}
};
namespace {
void measuredTick(ServerLevel& level,std::uint64_t us)noexcept{
    TabTpsServerMod::instance().complete(level,us);
}
}
}
LL_REGISTER_MOD(praxis::server::tabtps::TabTpsServerMod,
    praxis::server::tabtps::TabTpsServerMod::instance());
