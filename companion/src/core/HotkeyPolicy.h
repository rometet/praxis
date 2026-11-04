#pragma once
#include <array>
#include <cstdint>
#include <string_view>
namespace praxis::companion {
// Praxis / inventory owner's persisted contract. Never migrate stored 2/4.
enum class HotkeyModifier : unsigned { None=0, Control=1, Shift=2, Alt=4, Any=8 };
inline constexpr unsigned modifierBit(HotkeyModifier value) noexcept {return static_cast<unsigned>(value);}
inline constexpr unsigned anyModifiers=modifierBit(HotkeyModifier::Any);
inline constexpr unsigned modifierBits(bool control,bool shift,bool alt) noexcept {
    return (control?modifierBit(HotkeyModifier::Control):0u)
        |(shift?modifierBit(HotkeyModifier::Shift):0u)
        |(alt?modifierBit(HotkeyModifier::Alt):0u);
}
inline constexpr unsigned encodeModifiers(bool control,bool shift,bool alt) noexcept {
    return modifierBits(control,shift,alt);
}
inline constexpr std::string_view hotkeyModifierPrefix(unsigned modifiers) noexcept {
    constexpr std::array<std::string_view,9> names{"","Ctrl+","Shift+","Ctrl+Shift+","Alt+","Ctrl+Alt+","Shift+Alt+","Ctrl+Shift+Alt+","Any+"};
    return modifiers<names.size()?names[modifiers]:"Invalid+";
}
// Optional LHolo ABI v1 transports its owner's native bits: Ctrl1/Alt2/Shift4.
// This is a boundary interpretation, never a profile/schema conversion. Keeping
// the owner's native mask at read/write preserves existing LHolo files and keys.
inline constexpr unsigned lholoModifiersToPraxis(unsigned native) noexcept {
    return (native&~6u)|((native&4u)?2u:0u)|((native&2u)?4u:0u);
}
inline constexpr unsigned praxisModifiersToLholo(unsigned canonical) noexcept {
    return lholoModifiersToPraxis(canonical);
}
inline constexpr bool validModifiers(unsigned m)noexcept{return m<=8;}
inline constexpr bool modifiersMatch(unsigned wanted,unsigned actual)noexcept{return wanted==anyModifiers||wanted==actual;}
struct HotkeyChord {unsigned key{},modifiers{},contexts{1};};
inline constexpr bool hotkeyConflict(HotkeyChord a,HotkeyChord b)noexcept {
    return a.key&&a.key==b.key&&(a.contexts&b.contexts)
        &&(a.modifiers==anyModifiers||b.modifiers==anyModifiers||a.modifiers==b.modifiers);
}
static_assert(modifierBit(HotkeyModifier::Control)==1&&modifierBit(HotkeyModifier::Shift)==2&&modifierBit(HotkeyModifier::Alt)==4);
}
