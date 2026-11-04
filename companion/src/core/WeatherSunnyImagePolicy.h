#pragma once
#include "core/WeatherSunnyNativeContract.h"
#include <span>
#include <algorithm>

namespace praxis::companion {
enum class SunnyImageVariant { Rejected, Original, PreLoaderImport };
struct SunnyImageSection {
    std::array<unsigned char,8> name{};
    unsigned rva{}, virtualSize{}, rawOffset{}, rawSize{}, characteristics{};
    friend bool operator==(SunnyImageSection const&,SunnyImageSection const&)=default;
};
struct SunnyImageIdentity {
    unsigned moduleSize{}, imageSize{}, machine{}, optionalMagic{}, timestamp{};
    unsigned entryPoint{}, sectionAlignment{}, fileAlignment{}, headersSize{};
    unsigned importRva{}, importSize{}, sectionCount{}, codeViewAge{};
    std::array<unsigned char,16> codeViewGuid{};
    bool preLoaderResolveImport{};
};
// The actual launch EXE preserves all eight original raw sections byte-for-byte.
// Its sole added import is PreLoader.dll!pl_resolve_symbol in .nimp. These are
// exact observed layouts, not a permitted size range or arbitrary appended PE.
inline constexpr std::array<SunnyImageSection,8> sunnyOriginalSections{{
    {{{'.','t','e','x','t'}},4096,240802486,1024,240802816,0x60000020},
    {{{'.','r','d','a','t','a'}},240807936,52636516,240803840,52636672,0x40000040},
    {{{'.','d','a','t','a'}},293445632,8772568,293440512,5414912,0xc0000040},
    {{{'.','p','d','a','t','a'}},302219264,11333496,298855424,11333632,0x40000040},
    {{{'.','t','l','s'}},313552896,11505,310189056,11776,0xc0000040},
    {{{'.','x','b','l','d'}},313565184,260,310200832,512,0xc0000040},
    {{{'.','r','s','r','c'}},313569280,18600,310201344,18944,0x40000040},
    {{{'.','r','e','l','o','c'}},313589760,985580,310220288,985600,0x42000040},
}};
inline constexpr SunnyImageSection sunnyPreLoaderSection{
    {{'.','n','i','m','p'}},314576896,43520,311205888,43144,0xc0000000};
inline constexpr unsigned sunnyPreLoaderImageSize=314621952;
inline bool sunnyKnownImageSize(unsigned size) noexcept {
    return size==sunnyImageSize || size==sunnyPreLoaderImageSize;
}
inline SunnyImageVariant sunnyImageVariant(SunnyImageIdentity const& id,
    std::span<SunnyImageSection const> sections) noexcept {
    if (!sunnyKnownImageSize(id.moduleSize) || id.moduleSize!=id.imageSize
        || id.machine!=0x8664 || id.optionalMagic!=0x20b || id.timestamp!=sunnyImageTimestamp
        || id.entryPoint!=240669872 || id.sectionAlignment!=4096 || id.fileAlignment!=512
        || id.headersSize!=1024 || id.codeViewAge!=1 || id.codeViewGuid!=sunnyCodeViewGuid
        || (id.sectionCount!=8 && id.sectionCount!=9) || sections.size()<id.sectionCount)
        return SunnyImageVariant::Rejected;
    for (unsigned i=0;i<sunnyOriginalSections.size();++i)
        if (sections[i]!=sunnyOriginalSections[i]) return SunnyImageVariant::Rejected;
    if (id.imageSize==sunnyImageSize && id.sectionCount==8
        && id.importRva==252706408 && id.importSize==1660 && !id.preLoaderResolveImport) return SunnyImageVariant::Original;
    if (id.imageSize==sunnyPreLoaderImageSize && id.sectionCount==9
        && sections[8]==sunnyPreLoaderSection && id.importRva==314609751 && id.importSize==10289
        && id.preLoaderResolveImport)
        return SunnyImageVariant::PreLoaderImport;
    return SunnyImageVariant::Rejected;
}
// Names and OriginalFirstThunk remain RVAs after loader binding; the IAT's
// resolved addresses are deliberately not compared. Descriptor is the Win32
// IMAGE_IMPORT_DESCRIPTOR in product, a same-layout owned fixture in tests.
template<class Descriptor,class Read>
bool sunnyPreLoaderImportMarker(unsigned importRva,unsigned importSize,Read&& read) {
    static_assert(sizeof(Descriptor)==20);
    bool found{};
    for(unsigned i=0;i<importSize/sizeof(Descriptor);++i) {
        Descriptor descriptor{};
        if(!read(importRva+i*sizeof(Descriptor),&descriptor,sizeof(descriptor))) return false;
        if(!descriptor.Name && !descriptor.OriginalFirstThunk && !descriptor.FirstThunk) return found;
        std::array<char,sizeof("PreLoader.dll")> moduleName{};
        if(!read(descriptor.Name,moduleName.data(),moduleName.size())) return false;
        constexpr char expectedModule[]="PreLoader.dll";
        if(!std::equal(moduleName.begin(),moduleName.end(),expectedModule)) continue;
        if(found || !descriptor.OriginalFirstThunk || !descriptor.FirstThunk) return false;
        std::array<std::uint64_t,2> thunk{};
        if(!read(descriptor.OriginalFirstThunk,thunk.data(),sizeof(thunk)) || !thunk[0]
            || (thunk[0]&(1ULL<<63)) || thunk[1]) return false;
        std::array<char,sizeof("pl_resolve_symbol")> symbol{};
        if(!read(thunk[0]+sizeof(std::uint16_t),symbol.data(),symbol.size())) return false;
        constexpr char expectedSymbol[]="pl_resolve_symbol";
        if(!std::equal(symbol.begin(),symbol.end(),expectedSymbol)) return false;
        found=true;
    }
    return false; // Missing terminal descriptor is not a verified import table.
}
} // namespace praxis::companion
