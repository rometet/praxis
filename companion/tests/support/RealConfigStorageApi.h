#pragma once
// Actual pinned SDK codec templates, with only unused imported error paths
// replaced by fatal definitions. Valid paths cannot call these definitions.
#define LLAPI
#include "ll/api/reflection/Serialization.h"
#include "ll/api/reflection/Deserialization.h"
#include <cstdio>
#include <cstdlib>
namespace praxis::companion::test {
[[noreturn]] inline void unexpectedSdkCodecError(char const* name) noexcept {
    std::fprintf(stderr,"Unexpected SDK codec error: %s\n",name);std::abort();
}
}
namespace ll {
inline Unexpected makeExceptionError(std::exception_ptr const&) noexcept {praxis::companion::test::unexpectedSdkCodecError("exception");}
inline std::string Error::message() const noexcept {praxis::companion::test::unexpectedSdkCodecError("message");}
namespace reflection {
inline Unexpected makeSerMemberError(std::string_view,Error&) noexcept {praxis::companion::test::unexpectedSdkCodecError("member");}
inline Unexpected makeSerIndexError(std::size_t,Error&) noexcept {praxis::companion::test::unexpectedSdkCodecError("index");}
inline Unexpected makeSerKeyError(std::string_view,Error&) noexcept {praxis::companion::test::unexpectedSdkCodecError("key");}
inline Unexpected makeDeserStringTypeError() noexcept {praxis::companion::test::unexpectedSdkCodecError("string");}
inline Unexpected makeDeserArrayTypeError() noexcept {praxis::companion::test::unexpectedSdkCodecError("array");}
inline Unexpected makeDeserObjectTypeError() noexcept {praxis::companion::test::unexpectedSdkCodecError("object");}
inline Unexpected makeDeserArraySizeError(std::size_t) noexcept {praxis::companion::test::unexpectedSdkCodecError("size");}
inline Unexpected makeDeserMissingRequiredFieldError(std::string const&) noexcept {praxis::companion::test::unexpectedSdkCodecError("missing");}
}
}
#include <fstream>
#include <sstream>
namespace ll::file_utils {
inline std::optional<std::string> readFile(std::filesystem::path const& path) {
    std::ifstream input(path,std::ios::binary);if(!input)return {};
    std::ostringstream output;output<<input.rdbuf();return output.str();
}
}
