#pragma once
#include <Windows.h>

namespace kiero {
    enum class Status {
        Success = 0,
        UnknownError = -1,
    };

    enum class RenderType {
        None,
        D3D11,
        D3D12,
        Auto
    };

    Status init(RenderType renderType);
    void shutdown();
    RenderType getRenderType();
    void** getMethodsTable();
}
