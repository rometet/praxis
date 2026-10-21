#pragma once
#include "resourcepacks/PackDiagnostics.h"
#include <cstdint>
#include <string_view>

namespace praxis::companion::packs {
// Pure copied metadata. No lookup, dereference, logging sink or admission here.
enum class WorldDiagnosticModule : unsigned {None,Game,Praxis,LHolo,Unknown};
inline char const* worldDiagnosticModuleName(WorldDiagnosticModule value) noexcept {
    switch(value){case WorldDiagnosticModule::None:return "none";case WorldDiagnosticModule::Game:return "Game";
        case WorldDiagnosticModule::Praxis:return "Praxis";case WorldDiagnosticModule::LHolo:return "LHolo";
        default:return "unknown";}
}
struct WorldDiagnosticAddress {
    WorldDiagnosticModule module{WorldDiagnosticModule::None};
    std::uint64_t offset{};
    // 0=null, 1=validated Game image, 2=foreign image metadata,
    // 3=foreign non-image metadata, 4=metadata unavailable.
    unsigned resolution{};
};
inline WorldDiagnosticAddress worldDiagnosticAddress(std::uintptr_t value,std::uintptr_t game,
    std::uint64_t imageSize,bool metadataValid,std::uintptr_t allocation,bool image,
    std::uintptr_t praxis,std::uintptr_t lholo) noexcept {
    if(!value)return {};
    if(game&&value>=game&&value-game<imageSize)return {WorldDiagnosticModule::Game,value-game,1};
    if(!metadataValid||!allocation||value<allocation)return {WorldDiagnosticModule::Unknown,0,4};
    auto module=allocation==game?WorldDiagnosticModule::Game:allocation==praxis?WorldDiagnosticModule::Praxis:
        allocation==lholo?WorldDiagnosticModule::LHolo:WorldDiagnosticModule::Unknown;
    return {module,value-allocation,image?2u:3u};
}
// Startup ID map is recorded in the candidate's diagnostic report. Returning
// the exact predicate value preserves lazy call sites and fail-closed behavior.
class WorldPackPreflightDiagnostics {
    PackDiagnosticQueue& queue;
    unsigned records{},firstFailure{};
    bool finished{};
    template<class... Args> void record(char const* format,Args... args) noexcept {
        if(records>=Limit-1)return;++records;queue.record(format,args...);
    }
    bool result(unsigned id,bool pass) noexcept {if(!pass&&!firstFailure)firstFailure=id;return pass;}
public:
    static constexpr unsigned Limit=96;
    explicit WorldPackPreflightDiagnostics(PackDiagnosticQueue& value):queue(value){}
    unsigned failure() const noexcept {return firstFailure;}
    unsigned count() const noexcept {return records;}
    void begin(unsigned thread,unsigned owner,bool attempted,bool ready,bool provenanceLost) noexcept {
        record("RESOURCEPACK_WORLD event=completion_preflight attempt=1 stage=0 thread=%u owner_thread=%u attempted=%u ready=%u lost=%u",
            thread,owner,unsigned(attempted),unsigned(ready),unsigned(provenanceLost));
    }
    bool check(unsigned id,bool pass,std::uint64_t actual,std::uint64_t expected) noexcept {
        record("RESOURCEPACK_WORLD event=completion_preflight attempt=1 stage=%u pass=%u actual=%llu expected=%llu",
            id,unsigned(pass),diagNumber(actual),diagNumber(expected));return result(id,pass);
    }
    bool image(unsigned id,bool verified) noexcept {
        record("RESOURCEPACK_WORLD event=completion_preflight attempt=1 stage=%u pass=%u sdk=26.51.5 approved_sha256=f82dc967d3800b7b892c38a97b672e5f3257d9c0c0cffeccdc5f83de02363b2a actual=%s",
            id,unsigned(verified),verified?"match":"unverified");return result(id,verified);
    }
    bool protection(unsigned id,unsigned actual,unsigned read,unsigned readWrite,unsigned writeCopy) noexcept {
        bool pass=actual==read||actual==readWrite||actual==writeCopy;
        record("RESOURCEPACK_WORLD event=completion_preflight attempt=1 stage=%u pass=%u actual_protection=%u expected_allowed=%u,%u,%u",
            id,unsigned(pass),actual,read,readWrite,writeCopy);return result(id,pass);
    }
    bool region(unsigned id,bool pass,std::uint64_t offset,std::uint64_t size,unsigned need) noexcept {
        record("RESOURCEPACK_WORLD event=completion_preflight attempt=1 stage=%u pass=%u need_bytes=%u actual_offset_bytes=%llu region_size_bytes=%llu",
            id,unsigned(pass),need,diagNumber(offset),diagNumber(size));return result(id,pass);
    }
    bool equal(unsigned id,std::uint64_t actual,std::uint64_t expected) noexcept {return check(id,actual==expected,actual,expected);}
    bool less(unsigned id,std::uint64_t actual,std::uint64_t expected) noexcept {return check(id,actual<expected,actual,expected);}
    bool atMost(unsigned id,std::uint64_t actual,std::uint64_t expected) noexcept {return check(id,actual<=expected,actual,expected);}
    bool atLeast(unsigned id,std::uint64_t actual,std::uint64_t expected) noexcept {return check(id,actual>=expected,actual,expected);}
    bool signedCheck(unsigned id,bool pass,std::int64_t actual,std::int64_t expected) noexcept {
        record("RESOURCEPACK_WORLD event=completion_preflight attempt=1 stage=%u pass=%u actual_signed=%lld expected_signed=%lld",
            id,unsigned(pass),static_cast<long long>(actual),static_cast<long long>(expected));return result(id,pass);
    }
    bool positive(unsigned id,std::int64_t actual) noexcept {return signedCheck(id,actual>0,actual,0);}
    bool atMostSigned(unsigned id,std::int64_t actual,std::int64_t expected) noexcept {return signedCheck(id,actual<=expected,actual,expected);}
    bool clearBits(unsigned id,std::uint64_t actual,std::uint64_t mask) noexcept {return check(id,!(actual&mask),actual,mask);}
    bool formal(unsigned id,bool pass,std::uint64_t expectedRva,WorldDiagnosticAddress actual) noexcept {
        record("RESOURCEPACK_WORLD event=completion_preflight attempt=1 stage=%u pass=%u expected_module=Game expected_rva=%llu actual_module=%s actual_offset=%llu resolution=%u",
            id,unsigned(pass),diagNumber(expectedRva),worldDiagnosticModuleName(actual.module),diagNumber(actual.offset),actual.resolution);
        return result(id,pass);
    }
    bool allocation(unsigned id,bool pass,std::uint64_t rva,WorldDiagnosticAddress actual) noexcept {
        record("RESOURCEPACK_WORLD event=completion_preflight attempt=1 stage=%u pass=%u expected_module=Game target_rva=%llu actual_module=%s actual_offset=%llu resolution=%u",
            id,unsigned(pass),diagNumber(rva),worldDiagnosticModuleName(actual.module),diagNumber(actual.offset),actual.resolution);
        return result(id,pass);
    }
    bool bytes(unsigned id,std::string_view expected,std::array<unsigned char,32> const& actual,
        bool pass,unsigned mismatch) noexcept {
        std::array<char,65> hex{};constexpr char digits[]="0123456789abcdef";
        for(unsigned i=0;i<actual.size();++i){hex[i*2]=digits[actual[i]>>4];hex[i*2+1]=digits[actual[i]&15];}
        record("RESOURCEPACK_WORLD event=completion_preflight attempt=1 stage=%u pass=%u mismatch=%u expected32=%.*s actual32=%s",
            id,unsigned(pass),mismatch,int(expected.size()),expected.data(),hex.data());return result(id,pass);
    }
    bool hook(unsigned id,int code) noexcept {
        record("RESOURCEPACK_WORLD event=completion_preflight attempt=1 stage=%u pass=%u hook_code=%d",id,unsigned(code==0),code);
        return result(id,code==0);
    }
    void finish(bool ready,bool provenanceLost) noexcept {
        if(finished)return;finished=true;++records;
        queue.record("RESOURCEPACK_WORLD event=completion_preflight attempt=1 stage=199 first_fail=%u ready=%u lost=%u records=%u",
            firstFailure,unsigned(ready),unsigned(provenanceLost),records);
    }
};
class WorldSettingsBootstrapDiagnostics {
    std::array<std::uint64_t,8> values{};
    unsigned evaluated{},passed{},firstFailure{},thread{},owner{};
public:
    bool check(unsigned id,bool pass,std::uint64_t actual) noexcept {
        auto bit=1u<<(id-1);evaluated|=bit;if(pass)passed|=bit;else if(!firstFailure)firstFailure=id;
        values[id-1]=actual;return pass;
    }
    bool flag(unsigned id,bool actual,bool expected=true) noexcept {return check(id,actual==expected,actual);}
    bool zero(unsigned id,unsigned actual) noexcept {return check(id,!actual,actual);}
    bool ownerThread(unsigned actual,unsigned expected) noexcept {
        thread=actual;owner=expected;return check(1,actual==expected,actual==expected);
    }
    unsigned failure() const noexcept {return firstFailure;}
    unsigned evaluatedMask() const noexcept {return evaluated;}
    unsigned passedMask() const noexcept {return passed;}
    void publishFirst(PackDiagnosticQueue& queue,std::atomic_bool& attempted) const noexcept {
        if(!firstFailure||attempted.exchange(true))return;
        queue.record("RESOURCEPACK_WORLD event=settings_bootstrap attempt=1 first_fail=%u evaluated=%u passed=%u thread=%u owner_thread=%u reason=1",
            firstFailure,evaluated,passed,thread,owner);
        for(unsigned i=0;i<values.size();++i)queue.record(
            "RESOURCEPACK_WORLD event=settings_bootstrap attempt=1 stage=%u evaluated=%u pass=%u actual=%llu",
            i+1,unsigned(bool(evaluated&(1u<<i))),unsigned(bool(passed&(1u<<i))),diagNumber(values[i]));
    }
};
}
