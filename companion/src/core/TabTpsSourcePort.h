#pragma once
// TabTPS source-based Bedrock port, independent C++ implementation.
// Original behaviour reference: jpenilla/TabTPS commit
// e0e55e98524643f85aeb0e80bad4411f1a5532a6 (MIT, Jason Penilla).
// All wire layout, process metrics, and Bedrock overlay choices are port-specific.
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <deque>
#include <iomanip>
#include <limits>
#include <locale>
#include <utility>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace praxis::companion::tabtps::source {
constexpr std::string_view prefix = "PTPS2";
constexpr std::string_view packetId = "praxis:tabtps_telemetry";
constexpr unsigned packetVersion = 2;
constexpr std::uint64_t maxAgeMs = 3000;
constexpr std::int64_t absent = -1;

// Values below are *measurements*. -1 means not available, never zero by default.
// The server sends per-player snapshots because ping is player-specific.
// TPS is the measured 60-second rolling mean (centi-TPS), not current 1-second TPS.
// MSPT is the measured recent 100-tick ServerLevel::_subTick busy mean in us.
// CPU centi-percent, RAM MiB (Bedrock process private commit, not JVM heap).
struct Snapshot {
    std::uint32_t epoch{};
    std::uint32_t sequence{};
    std::uint64_t serverMonotonicMs{};
    std::int64_t tpsCenti{-1}, msptUs{-1}, pingMs{-1};
    std::int64_t cpuSystemCenti{-1}, cpuProcessCenti{-1};
    std::int64_t memoryUsedMiB{-1}, memoryCommittedMiB{-1}, memoryMaxMiB{-1};
    std::int64_t onlinePlayers{-1}, maxPlayers{-1};
    bool operator==(Snapshot const&) const = default;
};

inline bool valid(Snapshot const& s) noexcept {
    if (!s.epoch || !s.sequence || !s.serverMonotonicMs) return false;
    auto within = [](std::int64_t n, std::int64_t maximum) {
        return n == absent || (n >= 0 && n <= maximum);
    };
    if (!within(s.tpsCenti, 2000) || !within(s.msptUs, 5000000)
        || !within(s.pingMs, 3600000)
        || !within(s.cpuSystemCenti, 10000)
        || !within(s.cpuProcessCenti, 10000)
        || !within(s.memoryUsedMiB, 1LL << 40)
        || !within(s.memoryCommittedMiB, 1LL << 40)
        || !within(s.memoryMaxMiB, 1LL << 40)
        || !within(s.onlinePlayers, 100000)
        || !within(s.maxPlayers, 100000)) return false;
    if (s.onlinePlayers >= 0 && s.maxPlayers >= 0 && s.onlinePlayers > s.maxPlayers)
        return false;
    return true;
}
inline std::string encode(Snapshot const& s) {
    if (!valid(s)) return {};
    std::array<std::int64_t, 13> fields{
        s.epoch, s.sequence, static_cast<std::int64_t>(s.serverMonotonicMs),
        s.tpsCenti, s.msptUs, s.pingMs, s.cpuSystemCenti, s.cpuProcessCenti,
        s.memoryUsedMiB, s.memoryCommittedMiB, s.memoryMaxMiB,
        s.onlinePlayers, s.maxPlayers};
    std::string wire(prefix);
    for (auto field : fields) { wire += '|'; wire += std::to_string(field); }
    return wire.size() <= 256 ? wire : std::string{};
}
inline std::optional<Snapshot> decode(std::string_view wire) noexcept {
    if (wire.size() > 256 || !wire.starts_with(prefix)
        || wire.size() <= prefix.size() || wire[prefix.size()] != '|') return {};
    std::array<std::int64_t, 13> values{};
    auto pos = prefix.size() + 1;
    for (std::size_t i = 0; i < values.size(); ++i) {
        const auto end = wire.find('|',pos);
        if ((i+1 == values.size() && end != wire.npos)
            || (i+1 != values.size() && end == wire.npos)) return {};
        const auto stop = (end == wire.npos) ? wire.size() : end;
        auto field = wire.substr(pos,stop-pos);
        if (field.empty() || field.size() > 20) return {};
        for (auto c : field) if (c != '-' && (c < '0' || c > '9')) return {};
        const auto [ptr,ec] = std::from_chars(field.data(),field.data()+field.size(),values[i]);
        if (ec != std::errc{} || ptr != field.data()+field.size()) return {};
        pos=stop+1;
    }
    if (values[0] < 1 || values[0] > UINT32_MAX
        || values[1] < 1 || values[1] > UINT32_MAX || values[2] < 1) return {};
    Snapshot s{
        static_cast<std::uint32_t>(values[0]),
        static_cast<std::uint32_t>(values[1]),
        static_cast<std::uint64_t>(values[2]),
        values[3],values[4],values[5],values[6],values[7],
        values[8],values[9],values[10],values[11],values[12]
    };
    return valid(s) ? std::optional{s} : std::nullopt;
}

class Receiver {
    std::uint64_t generation_{};
    std::uint64_t lastAt_{};
    std::optional<Snapshot> sample_;
public:
    void reset(std::uint64_t generation) noexcept {
        generation_=generation; lastAt_=0; sample_.reset();
    }
    bool accept(Snapshot const& s,std::uint64_t generation,std::uint64_t nowMs) noexcept {
        if (generation != generation_ || !valid(s)) return false;
        if (sample_ && (sample_->epoch != s.epoch || s.sequence <= sample_->sequence
            || s.serverMonotonicMs <= sample_->serverMonotonicMs || nowMs < lastAt_))
            return false;
        sample_=s; lastAt_=nowMs; return true;
    }
    std::optional<Snapshot> fresh(std::uint64_t generation,std::uint64_t nowMs) const noexcept {
        if (!sample_ || generation != generation_ || nowMs < lastAt_
            || nowMs-lastAt_>maxAgeMs) return {};
        return sample_;
    }
};

// The original Fabric implementation samples every 20 completed ticks and
// uses 60 time-weighted samples for the displayed "1m" average. Its 20-TPS
// constructor warm-start is deliberately *not* copied: first 60 real samples
// must be observed before Bedrock sends a numeric display value.
// With exactly 20 ticks per sample, weighted mean = 20 * slots / sum(seconds).
class RollingTpsOriginal1m {
    std::array<std::uint64_t,60> elapsed_{};
    std::uint64_t lastBoundaryMs_{},sumMs_{};
    std::uint32_t ticks_{},slots_{},index_{};
public:
    void reset(std::uint64_t) noexcept {
        elapsed_.fill(0);lastBoundaryMs_=sumMs_=0;
        ticks_=slots_=index_=0;
    }
    void complete(std::uint64_t now) noexcept {
        if (++ticks_%20!=0)return;
        if(lastBoundaryMs_ && now>lastBoundaryMs_){
            const auto span=now-lastBoundaryMs_;
            if(span>60000){reset(now);lastBoundaryMs_=now;return;}
            sumMs_-=elapsed_[index_];
            elapsed_[index_]=span;
            sumMs_+=span;
            if(slots_<60)++slots_;
            index_=(index_+1)%60;
        }
        lastBoundaryMs_=now;
    }
    std::int64_t centi(std::uint64_t) const noexcept {
        if(slots_<60||!sumMs_)return absent;
        return std::clamp<std::int64_t>(
            static_cast<std::int64_t>(std::llround(120000000.0 / sumMs_)),0,2000);
    }
};

// At full uptime: exact number of server tick completions in previous 60,000ms.
// During first 60 seconds: unavailable rather than falsely starting at TPS 20.
class RollingTps60s {
    std::deque<std::uint64_t> ticks_;
    std::uint64_t began_{};
public:
    void reset(std::uint64_t now) {ticks_.clear();began_=now;}
    void complete(std::uint64_t now) {
        if (!began_ || now < began_) reset(now);
        ticks_.push_back(now);
        expire(now);
    }
    void expire(std::uint64_t now) {
        while (!ticks_.empty() && (ticks_.front()>now || now-ticks_.front()>=60000))
            ticks_.pop_front();
        if(ticks_.size()>1800)ticks_.erase(ticks_.begin(), ticks_.end()-1800);
    }
    std::int64_t centi(std::uint64_t now) {
        if (!began_ || now<began_ || now-began_<60000) return absent;
        expire(now);
        return std::clamp<std::int64_t>(
            static_cast<std::int64_t>(std::llround(ticks_.size()*100000.0/60000.0)),
            0,2000);
    }
};
class TickTimeAverage {
    std::deque<std::uint32_t> times_;
    std::uint64_t sum_{};
public:
    void reset(){times_.clear();sum_=0;}
    void complete(std::uint32_t us) {
        if (!us || us>5000000){reset();return;}
        if(times_.size()>=100){sum_-=times_.front();times_.pop_front();}
        times_.push_back(us);sum_+=us;
    }
    std::int64_t meanUs() const {
        if(times_.size()<20) return absent;
        return static_cast<std::int64_t>((sum_+times_.size()/2)/times_.size());
    }
};

enum class Module { Tps, Mspt, Ping, Cpu, Memory, Players };
inline constexpr std::array<std::string_view,6> moduleNames={
    "tps","mspt","ping","cpu","memory","players"};
inline bool modules(std::string_view csv, std::vector<Module>& out) {
    std::vector<Module> result;
    while (!csv.empty()) {
        auto const n=csv.find(',');
        auto const word=csv.substr(0,n);
        std::string key;
        for(char c:word)if(c!=' ')key+=c;
        if(!key.empty()){
            auto it=std::find(moduleNames.begin(),moduleNames.end(),key);
            if(it==moduleNames.end())return false;
            result.push_back(static_cast<Module>(it-moduleNames.begin()));
        }
        if(n==csv.npos)break;
        csv.remove_prefix(n+1);
    }
    out=std::move(result);
    return true;
}
inline constexpr std::uint32_t gray=0xAAAAAA, white=0xFFFFFF,
    red=0xFF5555,gold=0xFFAA00,yellow=0xFFFF55,
    green=0x55FF55,darkGreen=0x00AA00;

enum class Grade { Good, Medium, Low, Missing };
inline Grade grade(Module m,double x) {
    if(!std::isfinite(x))return Grade::Missing;
    if(m==Module::Tps)return x>=18.5?Grade::Good:(x>15.0?Grade::Medium:Grade::Low);
    if(m==Module::Mspt)return x<=25.0?Grade::Good:(x<=40.0?Grade::Medium:Grade::Low);
    if(m==Module::Ping)return x<100?Grade::Good:(x<250?Grade::Medium:Grade::Low);
    return Grade::Good;
}
inline std::pair<std::uint32_t,std::uint32_t> ramp(Grade g) {
    if(g==Grade::Good)return {green,darkGreen};
    if(g==Grade::Medium)return {gold,yellow};
    if(g==Grade::Low)return {red,gold};
    return {gray,gray};
}
inline std::uint32_t gradient(std::uint32_t a,std::uint32_t b,std::size_t i,std::size_t n) {
    if(!n)return a;
    auto const t=static_cast<double>(i)/static_cast<double>(n);
    auto ch=[t](std::uint32_t a,std::uint32_t b,int shift){
        return static_cast<std::uint32_t>(std::lround(
            static_cast<double>((a>>shift)&255)*(1.0-t)
            +static_cast<double>((b>>shift)&255)*t));
    };
    return (ch(a,b,16)<<16)|(ch(a,b,8)<<8)|ch(a,b,0);
}
struct Colored {
    char c{};
    std::uint32_t rgb{};
    bool operator==(Colored const&)const=default;
};
inline void appendSolid(std::vector<Colored>& chars,std::string_view s,std::uint32_t c){
    for(auto ch:s)chars.push_back({ch,c});
}
inline void appendGradient(std::vector<Colored>& chars,std::string_view s,Grade g){
    const auto [a,b]=ramp(g);
    for(std::size_t i=0;i<s.size();++i)chars.push_back({s[i],gradient(a,b,i,s.size())});
}
inline std::string fixed2(double value){
    std::ostringstream out;out.imbue(std::locale::classic());
    out<<std::fixed<<std::setprecision(2)<<value;return out.str();
}
inline std::string javaCpu(double value){
    auto s=fixed2(value);
    if(s.size()>=2 && s.back()=='0')s.pop_back(); // e.g. 5.00 -> 5.0
    return s;
}
inline std::string label(Module m){
    switch(m){
      case Module::Tps:return "TPS";
      case Module::Mspt:return "MSPT";
      case Module::Ping:return "Ping";
      case Module::Cpu:return "CPU";
      case Module::Memory:return "RAM";
      case Module::Players:return "Online";
    }
    return "";
}
struct DisplayValue{
    std::string number;
    std::string suffix;
    Grade color{Grade::Missing};
    bool known{};
};
inline DisplayValue displayValue(Module m,Snapshot const& s){
    switch(m){
      case Module::Tps:
        if(s.tpsCenti>=0)return {fixed2(s.tpsCenti/100.0),"",grade(m,s.tpsCenti/100.0),true};
        break;
      case Module::Mspt:
        if(s.msptUs>=0)return {fixed2(s.msptUs/1000.0),"",grade(m,s.msptUs/1000.0),true};
        break;
      case Module::Ping:
        if(s.pingMs>=0)return {std::to_string(s.pingMs),"ms",grade(m,static_cast<double>(s.pingMs)),true};
        break;
      case Module::Cpu:
        if(s.cpuSystemCenti>=0&&s.cpuProcessCenti>=0)return{
          javaCpu(s.cpuSystemCenti/100.0)+"%, "+javaCpu(s.cpuProcessCenti/100.0)+"%",
          " (sys., proc.)",Grade::Good,true};
        break;
      case Module::Memory:
        if(s.memoryUsedMiB>=0&&s.memoryCommittedMiB>=0){
            std::string val=std::to_string(s.memoryUsedMiB)+"M/"+std::to_string(s.memoryCommittedMiB)+"M";
            if(s.memoryMaxMiB>=0&&s.memoryMaxMiB!=s.memoryCommittedMiB)
                val+=" (max. "+std::to_string(s.memoryMaxMiB)+"M)";
            return{val,"",Grade::Good,true};
        }
        break;
      case Module::Players:
        if(s.onlinePlayers>=0&&s.maxPlayers>=0)return{
          std::to_string(s.onlinePlayers)+"/"+std::to_string(s.maxPlayers),"",Grade::Good,true};
        break;
    }
    return{"--","",Grade::Missing,false};
}
inline std::vector<Colored> render(Snapshot const& s,std::vector<Module> const& list,
                                   std::string_view separator=" ") {
    std::vector<Colored> out;
    for(std::size_t i=0;i<list.size();++i){
        if(i)appendSolid(out,separator,gray);
        appendSolid(out,label(list[i]),gray);
        appendSolid(out,":",white);
        appendSolid(out," ",white);
        auto v=displayValue(list[i],s);
        appendGradient(out,v.number,v.color);
        appendSolid(out,v.suffix,white);
    }
    return out;
}
inline std::string plain(std::vector<Colored> const& chars){
    std::string str;str.reserve(chars.size());
    for(auto const& c:chars)str+=c.c;
    return str;
}
enum class Fill { Mspt,Tps,ReverseMspt,ReverseTps };
inline std::optional<float> bossProgress(Snapshot const& s,Fill mode) {
    const bool tps=(mode==Fill::Tps||mode==Fill::ReverseTps);
    auto const input=tps?s.tpsCenti:s.msptUs;
    if(input<0)return {};
    float result=std::clamp(tps?static_cast<float>(input)/2000.f
        :static_cast<float>(input)/50000.f,0.f,1.f);
    if(mode==Fill::ReverseTps||mode==Fill::ReverseMspt)result=1.f-result;
    return result;
}
inline Grade bossGrade(Snapshot const& s,Fill mode){
    if(mode==Fill::Mspt||mode==Fill::ReverseMspt) {
        if(s.msptUs<0)return Grade::Missing;
        return s.msptUs<25000?Grade::Good:(s.msptUs<40000?Grade::Medium:Grade::Low);
    }
    if(s.tpsCenti<0)return Grade::Missing;
    return s.tpsCenti>1850?Grade::Good:(s.tpsCenti>1500?Grade::Medium:Grade::Low);
}
} // namespace praxis::companion::tabtps::source
