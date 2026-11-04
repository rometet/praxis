#pragma once
// Real pinned SDK reflection codec; only unavailable DLL error reporting is doubled.
#define LLAPI
#include <nlohmann/json.hpp>
#include "ll/api/reflection/Serialization.h"
#include "ll/api/reflection/Deserialization.h"
#include <cstdio>
#include <cstdlib>
namespace praxis_test {
[[noreturn]] inline void unexpectedSdkError(char const* name) noexcept {
 std::fprintf(stderr,"Unexpected SDK codec error path: %s\n",name);std::abort();
}
}
namespace ll {
inline Unexpected makeExceptionError(std::exception_ptr const&) noexcept {praxis_test::unexpectedSdkError("exception");}
inline std::string Error::message() const noexcept {praxis_test::unexpectedSdkError("message");}
namespace reflection {
inline Unexpected makeSerMemberError(std::string_view,Error&) noexcept {praxis_test::unexpectedSdkError("member");}
inline Unexpected makeSerIndexError(std::size_t,Error&) noexcept {praxis_test::unexpectedSdkError("index");}
inline Unexpected makeSerKeyError(std::string_view,Error&) noexcept {praxis_test::unexpectedSdkError("key");}
inline Unexpected makeDeserStringTypeError() noexcept {praxis_test::unexpectedSdkError("string");}
inline Unexpected makeDeserArrayTypeError() noexcept {praxis_test::unexpectedSdkError("array");}
inline Unexpected makeDeserObjectTypeError() noexcept {praxis_test::unexpectedSdkError("object");}
inline Unexpected makeDeserArraySizeError(std::size_t) noexcept {praxis_test::unexpectedSdkError("size");}
inline Unexpected makeDeserMissingRequiredFieldError(std::string const&) noexcept {praxis_test::unexpectedSdkError("missing");}
}
}
