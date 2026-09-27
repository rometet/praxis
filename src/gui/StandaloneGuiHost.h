#pragma once
#include "gui/GuiHost.h"
#include <Windows.h>
#include <d3d11.h>
#include <d3d11on12.h>
#include <d3d12.h>
#include <dxgi1_4.h>
// Fake Headers use preprocessor guards for these Windows SDK types. Define
// their guards after the real SDK declaration so the placeholder structs are
// not redeclared when LeviLamina event headers are included below.
#define D3D12_RAYTRACING_GEOMETRY_DESC D3D12_RAYTRACING_GEOMETRY_DESC
#define D3D12_FEATURE_DATA_ARCHITECTURE D3D12_FEATURE_DATA_ARCHITECTURE
#define D3D12_FEATURE_DATA_D3D12_OPTIONS D3D12_FEATURE_DATA_D3D12_OPTIONS
#include <wrl/client.h>
#include <atomic>
#include <array>
#include <mutex>
#include <vector>
#include "ll/api/event/EventBus.h"

namespace praxis::companion {
class StandaloneGuiHost final : public GuiHost {
    using PresentFn = HRESULT (STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
    using Present1Fn = HRESULT (STDMETHODCALLTYPE*)(IDXGISwapChain1*, UINT, UINT, DXGI_PRESENT_PARAMETERS const*);
    using ResizeFn = HRESULT (STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
    using Resize1Fn = HRESULT (STDMETHODCALLTYPE*)(IDXGISwapChain3*, UINT, UINT, UINT, DXGI_FORMAT, UINT, UINT const*, IUnknown* const*);
    using ExecuteFn = void (STDMETHODCALLTYPE*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);
    struct Detour { void* target{}; void* callback{}; bool installed{}; };
    std::array<Detour, 5> detours_{};
    PresentFn originalPresent_{};
    Present1Fn originalPresent1_{};
    ResizeFn originalResize_{};
    Resize1Fn originalResize1_{};
    ExecuteFn originalExecute_{};
    std::atomic_bool started_{};
    std::atomic_bool visible_{};
    std::atomic<unsigned> callbacks_{};
    std::recursive_mutex frameMutex_;
    ll::event::ListenerPtr keyListener_;
    ll::event::ListenerPtr mouseListener_;
    std::atomic<HWND> window_{};
    std::atomic<WNDPROC> previousWndProc_{};
    IDXGISwapChain* observedSwapchain_{};
    std::atomic_bool windowErrorLogged_{};
    std::atomic_bool graphicsWaitLogged_{};
    std::atomic_bool renderErrorLogged_{};
    std::atomic_bool deviceLossLogged_{};
    bool grabMouseHookInstalled_{};
    int cursorShowCalls_{};
    bool imguiReady_{};
    bool on12_{};
    Microsoft::WRL::ComPtr<IDXGISwapChain> initializedSwapchain_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;
    std::atomic_bool queueCaptured_{};
    Microsoft::WRL::ComPtr<ID3D11Device> device11_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context11_;
    Microsoft::WRL::ComPtr<ID3D11On12Device> deviceOn12_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target11_;
    std::vector<Microsoft::WRL::ComPtr<ID3D11Resource>> wrapped_;
    std::vector<Microsoft::WRL::ComPtr<ID3D11RenderTargetView>> targets_;
    std::atomic_bool keyDown_[256]{};
    void* imguiContext_{};

    bool install(void* target, void* callback, void** original, unsigned slot) noexcept;
    bool installWndProc(IDXGISwapChain* swapchain) noexcept;
    bool initializeGraphics(IDXGISwapChain* swapchain) noexcept;
    void releaseGraphics() noexcept;
    void handlePresentResult(HRESULT result) noexcept;
    void renderFrame(IDXGISwapChain* swapchain) noexcept;
    void releaseHeldKeys() noexcept;
    void releaseCursor() noexcept;
    void restoreCursor() noexcept;
    LRESULT windowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) noexcept;
    static LRESULT CALLBACK wndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    static HRESULT STDMETHODCALLTYPE present(IDXGISwapChain*, UINT, UINT);
    static HRESULT STDMETHODCALLTYPE present1(IDXGISwapChain1*, UINT, UINT, DXGI_PRESENT_PARAMETERS const*);
    static HRESULT STDMETHODCALLTYPE resize(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
    static HRESULT STDMETHODCALLTYPE resize1(IDXGISwapChain3*, UINT, UINT, UINT, DXGI_FORMAT, UINT, UINT const*, IUnknown* const*);
    static void STDMETHODCALLTYPE execute(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);
    static std::atomic<StandaloneGuiHost*> instance_;
public:
    static bool menuVisible() noexcept;
    bool start() noexcept override;
    bool stop() noexcept override;
    bool visible() const noexcept override { return visible_.load(std::memory_order_acquire); }
    void setVisible(bool value) noexcept override;
    bool inputCaptured() const noexcept override { return visible(); }
    GuiBackendKind kind() const noexcept override { return GuiBackendKind::Standalone; }
};

void logGuiInfo(char const* message) noexcept;
void logGuiError(char const* message) noexcept;
}
