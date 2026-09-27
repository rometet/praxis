#include "kiero.h"
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <iostream>
#include <vector>

namespace kiero {
    static RenderType g_renderType = RenderType::None;
    static void** g_methodsTable = nullptr;

    Status init(RenderType renderType) {
        if (g_renderType != RenderType::None) return Status::Success;

        std::cout << "[Kiero] Initializing..." << std::endl;

        if (renderType == RenderType::Auto) {
            if (GetModuleHandleA("d3d12.dll")) {
                renderType = RenderType::D3D12;
                std::cout << "[Kiero] Auto-detected D3D12" << std::endl;
            } else {
                renderType = RenderType::D3D11;
                std::cout << "[Kiero] Auto-detected D3D11" << std::endl;
            }
        }

        WNDCLASSEXA wc = { sizeof(WNDCLASSEXA), CS_HREDRAW | CS_VREDRAW, DefWindowProcA, 0, 0, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, "Kiero", NULL };
        RegisterClassExA(&wc);
        HWND hwnd = CreateWindowA(wc.lpszClassName, "Kiero", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, NULL, NULL, wc.hInstance, NULL);

        if (renderType == RenderType::D3D11) {
            D3D_FEATURE_LEVEL featureLevel;
            ID3D11Device* device = nullptr;
            ID3D11DeviceContext* context = nullptr;
            IDXGISwapChain* swapChain = nullptr;
            DXGI_SWAP_CHAIN_DESC scd = {};
            scd.BufferCount = 1;
            scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            scd.OutputWindow = hwnd;
            scd.SampleDesc.Count = 1;
            scd.Windowed = TRUE;

            if (FAILED(D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &scd, &swapChain, &device, &featureLevel, &context))) {
                std::cerr << "[Kiero] D3D11 dummy device creation failed!" << std::endl;
                DestroyWindow(hwnd);
                UnregisterClassA(wc.lpszClassName, wc.hInstance);
                return Status::UnknownError;
            }

            g_methodsTable = (void**)*(uintptr_t**)swapChain;

            swapChain->Release();
            device->Release();
            context->Release();
        }
        else if (renderType == RenderType::D3D12) {
            HMODULE d3d12 = GetModuleHandleA("d3d12.dll");
            HMODULE dxgi = GetModuleHandleA("dxgi.dll");
            if (!d3d12 || !dxgi) {
                std::cerr << "[Kiero] D3D12/DXGI modules not found!" << std::endl;
                return Status::UnknownError;
            }

            auto pCreateDevice = (PFN_D3D12_CREATE_DEVICE)GetProcAddress(d3d12, "D3D12CreateDevice");
            auto pCreateFactory = (decltype(&CreateDXGIFactory))GetProcAddress(dxgi, "CreateDXGIFactory");

            if (!pCreateDevice || !pCreateFactory) {
                std::cerr << "[Kiero] D3D12/DXGI exports not found!" << std::endl;
                return Status::UnknownError;
            }

            IDXGIFactory* factory = nullptr;
            if (FAILED(pCreateFactory(IID_PPV_ARGS(&factory)))) return Status::UnknownError;

            IDXGIAdapter* adapter = nullptr;
            if (FAILED(factory->EnumAdapters(0, &adapter))) return Status::UnknownError;

            ID3D12Device* device = nullptr;
            if (FAILED(pCreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) return Status::UnknownError;

            ID3D12CommandQueue* commandQueue = nullptr;
            D3D12_COMMAND_QUEUE_DESC queueDesc = {};
            queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
            queueDesc.Priority = 0;
            queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
            queueDesc.NodeMask = 0;
            if (FAILED(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue)))) return Status::UnknownError;

            IDXGISwapChain* swapChain = nullptr;
            DXGI_SWAP_CHAIN_DESC scd = {};
            scd.BufferCount = 2;
            scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            scd.OutputWindow = hwnd;
            scd.SampleDesc.Count = 1;
            scd.Windowed = TRUE;
            scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

            if (FAILED(factory->CreateSwapChain(commandQueue, &scd, &swapChain))) {
                std::cerr << "[Kiero] D3D12 dummy swapchain creation failed!" << std::endl;
                return Status::UnknownError;
            }

            g_methodsTable = (void**)*(uintptr_t**)swapChain;

            swapChain->Release();
            commandQueue->Release();
            device->Release();
            adapter->Release();
            factory->Release();
        }

        DestroyWindow(hwnd);
        UnregisterClassA(wc.lpszClassName, wc.hInstance);

        if (g_methodsTable) {
            g_renderType = renderType;
            std::cout << "[Kiero] VTable acquired at: " << g_methodsTable << std::endl;
            return Status::Success;
        }
        return Status::UnknownError;
    }

    void shutdown() {
        g_renderType = RenderType::None;
        g_methodsTable = nullptr;
    }

    RenderType getRenderType() { return g_renderType; }
    void** getMethodsTable() { return g_methodsTable; }
}
