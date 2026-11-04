#pragma once
#include <cstdint>
namespace praxis::companion {
// Union of explicit edits since the last adopted source document. A queued save
// owns a value copy; admission or failed I/O does not clear the runtime history.
// Key/modifier/favorite bits identify owned registry slots. Field bits identify
// descriptor positions, preserving existing positions when new owners append.
struct ProfileEditHistory {
    std::uint32_t keys{},modifiers{},favorites{},fields{};
};
}
