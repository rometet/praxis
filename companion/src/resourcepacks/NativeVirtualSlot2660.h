#pragma once
// Client 26.60 virtual interception: LeviLamina's template
// getVtableIndex(member_pointer) accepts only pointer-size PMFs. Many
// Minecraft MSVC classes have 16-byte multiple-inheritance PMFs.
// Its FuncPtr overload, however, can decode the compiler thunk.
//
// Do not apply the thunk's slot to a different subobject vtable. A nonzero
// this-adjustor requires an independently verified layout, so fail closed.
#include "ll/api/memory/Memory.h"
#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>

namespace praxis::companion::packs::native2660 {
// Exact Clang-CL x64 optimized vcall thunk verified in our current 26.60
// COFF object files, NOT a guessed Minecraft function offset:
//   mov rax, [rcx]
//   mov rax, [rax + disp8/disp32]
//   jmp rax
// The SDK's stock LL getter does not recognize all such thunks (returns
// nullopt for TaskGroup::queue_DEPRECATED and onFullPackStackInvalid).
// Decode ONLY these two proven encodings; reject every other byte pattern.
inline std::optional<unsigned int> clangClVirtualThunkSlot(void const* entry) noexcept {
    if(!entry)return std::nullopt;
    HMODULE own{},target{};
    if(!GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&clangClVirtualThunkSlot),&own)
       ||!GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(entry),&target)
       ||own!=target)return std::nullopt;
    MEMORY_BASIC_INFORMATION info{};
    if(VirtualQuery(entry,&info,sizeof(info))!=sizeof(info)
       ||info.State!=MEM_COMMIT || info.Type!=MEM_IMAGE
       ||(info.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return std::nullopt;
    auto page=info.Protect&0xffu;
    if(page!=PAGE_EXECUTE_READ&&page!=PAGE_EXECUTE_READWRITE
       &&page!=PAGE_EXECUTE_WRITECOPY)return std::nullopt;
    auto const begin=reinterpret_cast<std::uintptr_t>(entry);
    auto const pageEnd=reinterpret_cast<std::uintptr_t>(info.BaseAddress)+info.RegionSize;
    if(begin>pageEnd||pageEnd-begin<13)return std::nullopt;
    auto* byte=static_cast<unsigned char const*>(entry);
    if(byte[0]!=0x48||byte[1]!=0x8b||byte[2]!=0x01
       ||byte[3]!=0x48||byte[4]!=0x8b)return std::nullopt;
    unsigned int displacement{};
    unsigned int continuation{};
    if(byte[5]==0x40){
        displacement=byte[6];
        continuation=7;
    }else if(byte[5]==0x80){
        std::memcpy(&displacement,byte+6,sizeof(displacement));
        continuation=10;
    }else return std::nullopt;
    if(byte[continuation]!=0x48||byte[continuation+1]!=0xff
       ||byte[continuation+2]!=0xe0)return std::nullopt;
    if(displacement%sizeof(void*)!=0 || displacement>1024*sizeof(void*))
        return std::nullopt;
    return displacement/sizeof(void*);
}
inline std::optional<unsigned int> validatedCompilerSlot(ll::memory::FuncPtr entry) noexcept {
    auto fromLibrary=ll::memory::getVtableIndex(entry);
    auto fromThunk=clangClVirtualThunkSlot(entry);
    // If two decoders disagree, decline the hook, never guess a slot.
    if(fromLibrary&&fromThunk&&*fromLibrary!=*fromThunk)return std::nullopt;
    return fromLibrary?fromLibrary:fromThunk;
}
template <class Member>
std::optional<unsigned int> zeroAdjustVirtualSlot(Member member) noexcept {
    using ll::memory::FuncPtr;
    if constexpr (sizeof(Member)==sizeof(FuncPtr)) {
        return validatedCompilerSlot(ll::memory::toFuncPtr(member));
    } else if constexpr (sizeof(Member)==sizeof(FuncPtr)+sizeof(std::ptrdiff_t)) {
        struct Encoded {FuncPtr thunk;std::ptrdiff_t thisAdjust;};
        Encoded encoded{};
        static_assert(sizeof(Encoded)==sizeof(Member));
        std::memcpy(&encoded,&member,sizeof(encoded));
        if(encoded.thisAdjust!=0 || !encoded.thunk)return std::nullopt;
        return validatedCompilerSlot(encoded.thunk);
    } else {
        return std::nullopt;
    }
}
}
