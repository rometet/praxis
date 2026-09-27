#include "platform/EffectPatches.h"
#include "ll/api/memory/Memory.h"
#include "ll/api/memory/Signature.h"
#include "ll/api/thread/GlobalThreadPauser.h"
#include <Windows.h>
#include <array>
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <span>
#include <mutex>

namespace praxis::companion {
namespace {
struct Patch {
 RenderPass pass;
 char const* pattern;
 std::byte* address{};
 std::array<std::byte,5> original{};
 bool active{};
};
std::array<Patch,2> patches{{
 {RenderPass::PortalOverlay,"E8 ? ? ? ? 4C 8B 75 ? 49 8B 06 48 8B 80 ? ? ? ? 4C 89 F1 FF 15"},
 {RenderPass::FireOverlay,"E8 ? ? ? ? 48 8B 45 ? 48 85 C0 4C 8B 65 ? 4C 8B 6D"}
}};
constexpr std::array<std::byte,5> nops{std::byte{0x90},std::byte{0x90},std::byte{0x90},std::byte{0x90},std::byte{0x90}};
std::mutex patchMutex;
std::span<std::byte> executableText(){
 auto* base=reinterpret_cast<std::byte*>(GetModuleHandleW(nullptr));
 if(!base)return {};
 auto const& dos=*reinterpret_cast<IMAGE_DOS_HEADER const*>(base);
 if(dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<=0||dos.e_lfanew>0x100000)return {};
 auto const* nt=reinterpret_cast<IMAGE_NT_HEADERS64 const*>(base+dos.e_lfanew);
 if(nt->Signature!=IMAGE_NT_SIGNATURE||nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64)return {};
 auto const* sections=IMAGE_FIRST_SECTION(nt);
 for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i){
  auto const& section=sections[i];
  if(std::memcmp(section.Name,".text",5)==0&&(section.Characteristics&IMAGE_SCN_MEM_EXECUTE)!=0){
   const auto offset=section.VirtualAddress;const auto size=section.Misc.VirtualSize;
   if(offset>nt->OptionalHeader.SizeOfImage||size>nt->OptionalHeader.SizeOfImage-offset)return {};
   return {base+offset,size};
  }
 }
 return {};
}
std::byte* uniqueMatch(std::span<std::byte> text,ll::memory::Signature const& pattern){
 std::byte* match{};
 if(pattern.size()>text.size())return nullptr;
 for(size_t i=0;i<=text.size()-pattern.size();++i){
  if(pattern[0]!=text[i])continue;
  bool equal=true;
  for(size_t j=1;j<pattern.size();++j)if(pattern[j]!=text[i+j]){equal=false;break;}
  if(equal){if(match)return nullptr;match=text.data()+i;}
 }
 return match;
}
}
RenderMask inspectEffectPatches(){
 std::lock_guard lock(patchMutex);auto text=executableText();RenderMask found{};
 if(text.empty())return found;
 for(auto& patch:patches){
  if(patch.active){found|=passBit(patch.pass);continue;}
  patch.address=uniqueMatch(text,ll::memory::Signature::parse(patch.pattern));
  if(!patch.address||patch.address[0]!=std::byte{0xe8}){patch.address=nullptr;continue;}
  std::int32_t displacement{};std::memcpy(&displacement,patch.address+1,sizeof(displacement));
  auto target=reinterpret_cast<std::intptr_t>(patch.address)+5+displacement;
  auto begin=reinterpret_cast<std::intptr_t>(text.data());
  if(target<begin||target>=begin+static_cast<std::intptr_t>(text.size())){patch.address=nullptr;continue;}
  std::memcpy(patch.original.data(),patch.address,5);found|=passBit(patch.pass);
 }
 return found;
}
bool updateEffectPatches(RenderMask requested)noexcept{
 try{
  std::lock_guard lock(patchMutex);bool changing=false;
  for(auto const& patch:patches){
   bool wanted=(requested&passBit(patch.pass))!=0;
   if(wanted&&!patch.address)return false;
   if(patch.address&&wanted!=patch.active)changing=true;
  }
  if(!changing)return true;
  ll::thread::GlobalThreadPauser pause;
  for(auto const& patch:patches){
   bool wanted=(requested&passBit(patch.pass))!=0;
   if(!patch.address||wanted==patch.active)continue;
   auto const& expected=patch.active?nops:patch.original;
   if(std::memcmp(patch.address,expected.data(),5)!=0)return false;
  }
  for(auto& patch:patches){
   bool wanted=(requested&passBit(patch.pass))!=0;
   if(!patch.address||wanted==patch.active)continue;
   auto const& replacement=wanted?nops:patch.original;
   ll::memory::modify(patch.address,5,[&]{std::memcpy(patch.address,replacement.data(),5);});
   FlushInstructionCache(GetCurrentProcess(),patch.address,5);patch.active=wanted;
  }
  return true;
 }catch(...){return false;}
}
bool restoreEffectPatches()noexcept{return updateEffectPatches(0);}
}
