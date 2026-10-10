#include "gui/StandaloneGuiHost.h"
#include "gui/GuiBridge.h"
#include "gui/HudTools.h"
#include "map/MapGraphics.h"
#include "gui/LegacyUiFont.h"
#include "render/Renderer.h"
#include "render/ShulkerPreview.h"
#include "runtime/Runtime.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/event/input/KeyInputEvent.h"
#include "ll/api/event/input/MouseInputEvent.h"
#include "ll/api/service/TargetedBedrock.h"
#include "mc/client/game/IClientInstance.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/deps/input/MouseAction.h"
#include "kiero.h"
#include <imgui.h>
#include <backends/imgui_impl_win32.h>
#include <backends/imgui_impl_dx11.h>
#include <algorithm>
#include <chrono>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace praxis::companion {
using Microsoft::WRL::ComPtr;
std::atomic<StandaloneGuiHost*> StandaloneGuiHost::instance_{};

namespace {
constexpr UINT kVisibilityMessage = WM_APP + 0x510;
thread_local unsigned presentDepth{};
thread_local unsigned hostCallbackDepth{};
struct PresentDepthGuard {
    PresentDepthGuard() { ++presentDepth; }
    ~PresentDepthGuard() { --presentDepth; }
};
struct CallbackGuard {
    std::atomic<unsigned>& count;
    explicit CallbackGuard(std::atomic<unsigned>& value) : count(value) { count.fetch_add(1, std::memory_order_acq_rel); ++hostCallbackDepth; }
    ~CallbackGuard() { --hostCallbackDepth; count.fetch_sub(1, std::memory_order_acq_rel); }
};
bool keyboardMessage(UINT message) noexcept {
    return message == WM_KEYDOWN || message == WM_KEYUP || message == WM_SYSKEYDOWN
        || message == WM_SYSKEYUP || message == WM_CHAR || message == WM_SYSCHAR;
}
bool mouseMessage(UINT message) noexcept {
    return message >= WM_MOUSEFIRST && message <= WM_MOUSELAST;
}
}

bool StandaloneGuiHost::menuVisible() noexcept {
    auto* host = instance_.load(std::memory_order_acquire);
    return host && host->visible();
}

LL_TYPE_INSTANCE_HOOK(
    StandaloneGrabMouseHook,
    ll::memory::HookPriority::Highest,
    ClientInstance,
    &ClientInstance::$grabMouse,
    void
) {
    if (StandaloneGuiHost::menuVisible()) return;
    origin();
}

bool StandaloneGuiHost::install(void* target, void* callback, void** original, unsigned slot) noexcept {
    if (!target || !callback || !original || slot >= detours_.size()) return false;
    if (ll::memory::hook(target, callback, original, ll::memory::HookPriority::Normal) != 0) return false;
    detours_[slot] = {target, callback, true};
    return true;
}

bool StandaloneGuiHost::start() noexcept {
    if (started_) return true;
    instance_ = this;
    // Original Praxis D3DHook uses the local kiero method discovery. Its
    // dummy DXGI swapchain supplies COM methods; no Minecraft RVA is used.
    auto mode = GetModuleHandleW(L"d3d12.dll") ? kiero::RenderType::D3D12 : kiero::RenderType::D3D11;
    if (kiero::init(mode) != kiero::Status::Success) {
        if (mode != kiero::RenderType::D3D12 || kiero::init(kiero::RenderType::D3D11) != kiero::Status::Success) {
            logGuiError("Standalone GUI: DXGI method discovery failed");
            instance_ = nullptr;
            return false;
        }
        mode = kiero::RenderType::D3D11;
    }
    void** methods = kiero::getMethodsTable();
    bool const d3d12 = mode == kiero::RenderType::D3D12;
    bool ok = methods && install(methods[8], reinterpret_cast<void*>(&present),
                                 reinterpret_cast<void**>(&originalPresent_), 0)
             && install(methods[13], reinterpret_cast<void*>(&resize),
                        reinterpret_cast<void**>(&originalResize_), 1);
    if (ok) {
        // IDXGISwapChain1::Present1 and IDXGISwapChain3::ResizeBuffers1.
        if (!install(methods[22], reinterpret_cast<void*>(&present1),
                     reinterpret_cast<void**>(&originalPresent1_), 2))
            logGuiError("Standalone GUI: Present1 hook unavailable; Present remains active");
        if (!install(methods[39], reinterpret_cast<void*>(&resize1),
                     reinterpret_cast<void**>(&originalResize1_), 3))
            logGuiError("Standalone GUI: ResizeBuffers1 hook unavailable; ResizeBuffers remains active");
    }
    if (ok && d3d12) {
        ComPtr<ID3D12Device> dummy;
        if (SUCCEEDED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&dummy)))) {
            D3D12_COMMAND_QUEUE_DESC desc{};
            desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
            ComPtr<ID3D12CommandQueue> queue;
            if (SUCCEEDED(dummy->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue)))) {
                void** queueMethods = *reinterpret_cast<void***>(queue.Get());
                ok = install(queueMethods[10], reinterpret_cast<void*>(&execute),
                             reinterpret_cast<void**>(&originalExecute_), 4);
            } else ok = false;
        } else ok = false;
    }
    kiero::shutdown();
    if (!ok) { stop(); logGuiError("Standalone GUI: DXGI hooks failed"); return false; }
    grabMouseHookInstalled_ = StandaloneGrabMouseHook::hook() == 0;
    if (!grabMouseHookInstalled_) {
        stop();
        logGuiError("Standalone GUI: ClientInstance::grabMouse gate unavailable");
        return false;
    }
    keyListener_ = ll::event::EventBus::getInstance().emplaceListener<ll::event::KeyInputEvent>(
        [](ll::event::KeyInputEvent& event) {
            auto* host = instance_.load(std::memory_order_acquire);
            if (!host) return;
            int const key = event.keyCode();
            if (key >= 0 && key < 256) host->keyDown_[key].store(event.isDown(), std::memory_order_release);
            if (host->visible() && key != VK_F11) event.cancel();
        });
    mouseListener_ = ll::event::EventBus::getInstance().emplaceListener<ll::event::MouseInputEvent>(
        [](ll::event::MouseInputEvent& event) {
            auto* host = instance_.load(std::memory_order_acquire);
            if (!host) return;
            if (event.actionButtonId() == MouseAction::ActionLeft)
                host->keyDown_[VK_LBUTTON].store(event.buttonData() == MouseAction::DataDown, std::memory_order_release);
            if (event.actionButtonId() == MouseAction::ActionRight)
                host->keyDown_[VK_RBUTTON].store(event.buttonData() == MouseAction::DataDown, std::memory_order_release);
            if (host->visible()) event.cancel();
        });
    if (!keyListener_ || !mouseListener_) { stop(); logGuiError("Standalone GUI: input guard unavailable"); return false; }
    started_ = true;
    logGuiInfo("Standalone GUI: DXGI hooks installed");
    return true;
}

bool StandaloneGuiHost::stop() noexcept {
    setVisible(false);
    started_ = false;
    // LL may disable synchronously inside the original WM_DESTROY handler.
    // Keep the module/resources retained instead of waiting for our own wrapper.
    if (hostCallbackDepth) {
        logGuiError("Standalone retirement requested inside its callback; module retention required");
        return false;
    }
    bool ok = !visible();
    auto& bus = ll::event::EventBus::getInstance();
    if (keyListener_) {
        if (bus.removeListener<ll::event::KeyInputEvent>(keyListener_)) keyListener_.reset();
        else ok = false;
    }
    if (mouseListener_) {
        if (bus.removeListener<ll::event::MouseInputEvent>(mouseListener_)) mouseListener_.reset();
        else ok = false;
    }
    if (grabMouseHookInstalled_) {
        if (StandaloneGrabMouseHook::unhook()) grabMouseHookInstalled_ = false;
        else ok = false;
    }
    bool wndProcRestored{};
    {
        // Serialize restoration with the render-side admission check and installation.
        // Do not hold this lock while removing hooks or draining their callbacks.
        std::lock_guard lock(frameMutex_);
        wndProcRestored = !previousWndProc_.load(std::memory_order_acquire);
        if (auto previous = previousWndProc_.load(std::memory_order_acquire)) {
            HWND const hwnd = window_.load(std::memory_order_acquire);
            if (!IsWindow(hwnd)) wndProcRestored = true;
            else {
                auto const current = GetWindowLongPtrW(hwnd, GWLP_WNDPROC);
                if (current == reinterpret_cast<LONG_PTR>(previous)) wndProcRestored = true;
                else if (current != reinterpret_cast<LONG_PTR>(&wndProc)) {
                    logGuiError("Standalone lost WndProc ownership; retaining hook relays until process exit");
                    return false;
                }
                else {
                    SetLastError(ERROR_SUCCESS);
                    SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(previous));
                    if (GetLastError() == ERROR_SUCCESS) wndProcRestored = true;
                    else { ok = false; logGuiError("Standalone GUI: WndProc restore failed"); }
                }
            }
        }
    }
    for (auto it = detours_.rbegin(); it != detours_.rend(); ++it) {
        if (it->installed) {
            if (ll::memory::unhook(it->target, it->callback)) it->installed = false;
            else ok = false;
        }
    }
    if (ok) {
        while (callbacks_.load(std::memory_order_acquire)) Sleep(0);
        std::lock_guard lock(frameMutex_);
        if (!releaseGraphics()) return false;
        queue_.Reset();
        queueCaptured_ = false;
        observedSwapchain_ = nullptr;
        if (wndProcRestored) { previousWndProc_ = nullptr; window_ = nullptr; }
        instance_ = nullptr;
    }
    return ok;
}

void StandaloneGuiHost::releaseHeldKeys() noexcept {
    // Release only movement/action keys already observed by LL. Synthesize
    // the matching UP before the input guard starts cancelling new events.
    constexpr std::array<unsigned, 13> keys{ 'W', 'A', 'S', 'D', VK_SPACE, VK_SHIFT,
        VK_CONTROL, VK_LSHIFT, VK_RSHIFT, VK_LCONTROL, VK_RCONTROL, VK_LBUTTON, VK_RBUTTON };
    for (unsigned key : keys) {
        if (!keyDown_[key].exchange(false, std::memory_order_acq_rel)) continue;
        INPUT input{};
        if (key == VK_LBUTTON || key == VK_RBUTTON) {
            input.type = INPUT_MOUSE;
            input.mi.dwFlags = key == VK_LBUTTON ? MOUSEEVENTF_LEFTUP : MOUSEEVENTF_RIGHTUP;
        } else {
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = static_cast<WORD>(key);
            input.ki.dwFlags = KEYEVENTF_KEYUP;
        }
        SendInput(1, &input, sizeof(input));
    }
}

void StandaloneGuiHost::releaseCursor() noexcept {
    if (auto client = ll::service::getClientInstance()) client->releaseMouse();
    ClipCursor(nullptr);
    cursorShowCalls_ = 0;
    int count = -1;
    do { count = ShowCursor(TRUE); ++cursorShowCalls_; }
    while (count < 0 && cursorShowCalls_ < 16);
    if (count < 0) logGuiError("Standalone GUI: cursor visibility counter did not recover");
}

void StandaloneGuiHost::restoreCursor() noexcept {
    while (cursorShowCalls_ > 0) { ShowCursor(FALSE); --cursorShowCalls_; }
    DWORD foregroundProcess{};
    auto foreground = GetForegroundWindow();
    if (!foregroundActive_.load(std::memory_order_acquire)
        || foreground != window_.load(std::memory_order_acquire)
        || !GetWindowThreadProcessId(foreground, &foregroundProcess)
        || foregroundProcess != GetCurrentProcessId()) return;
    if (auto client = ll::service::getClientInstance()) {
        auto screen = client->getTopScreenName();
        if (screen == "hud_screen" || screen == "in_game_play_screen") {
            // releaseMouse() leaves the native Windows cursor at the last GUI
            // position.  If that position is on the bottom screen edge, the
            // hidden cursor can still activate an auto-hidden taskbar after
            // the menu closes.  Move it back inside Minecraft before handing
            // mouse ownership to the game.
            HWND const hwnd = window_.load(std::memory_order_acquire);
            RECT rect{};
            if (hwnd && IsWindow(hwnd) && !IsIconic(hwnd)
                && GetClientRect(hwnd, &rect) && rect.right > rect.left && rect.bottom > rect.top) {
                POINT center{
                    rect.left + (rect.right - rect.left) / 2,
                    rect.top + (rect.bottom - rect.top) / 2
                };
                if (ClientToScreen(hwnd, &center)) SetCursorPos(center.x, center.y);
            }
            client->grabMouse();
        }
    }
}

void StandaloneGuiHost::setVisible(bool value) noexcept {
    HWND const hwnd = window_.load(std::memory_order_acquire);
    if (hwnd && IsWindow(hwnd)
        && GetWindowThreadProcessId(hwnd, nullptr) != GetCurrentThreadId()) {
        DWORD_PTR result{};
        if (SendMessageTimeoutW(hwnd, kVisibilityMessage, value ? 1 : 0, 0,
                                SMTO_ABORTIFHUNG, 500, &result)) return;
        logGuiError("Standalone GUI: window thread did not accept visibility handoff");
        return;
    }
    if (value && (!hwnd || hwnd != GetForegroundWindow() || IsIconic(hwnd))) return;
    bool const old = visible_.load(std::memory_order_acquire);
    if (old == value) return;
    if (value) {
        releaseHeldKeys();
        releaseCursor();
    }
    visible_.store(value, std::memory_order_release);
    notifyPraxisGuiVisibility(value);
    Renderer::get().setGuiOpen(value);
    if (!value) { restoreCursor(); flushRuntimeConfig(); }
    logGuiInfo(value ? "Standalone GUI: F10 OPEN" : "Standalone GUI: F10 CLOSE");
}

LRESULT StandaloneGuiHost::windowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
    if (message == kVisibilityMessage) { setVisible(wparam != 0); return 0; }
    if ((message == WM_KEYDOWN || message == WM_SYSKEYDOWN) && wparam == VK_F10) {
        if (!(static_cast<ULONG_PTR>(lparam) & (1ULL << 30))) setVisible(!visible());
        return 1;
    }
    if (message == WM_KILLFOCUS || (message == WM_ACTIVATEAPP && !wparam)) {
        foregroundActive_.store(false, std::memory_order_release);
        if (visible()) setVisible(false);
    } else if (message == WM_SETFOCUS || (message == WM_ACTIVATEAPP && wparam)) {
        foregroundActive_.store(true, std::memory_order_release);
    }
    if (visible()) {
        if ((message == WM_KEYDOWN || message == WM_KEYUP
             || message == WM_SYSKEYDOWN || message == WM_SYSKEYUP)
            && wparam == VK_F11) {
            auto previous = previousWndProc_.load(std::memory_order_acquire);
            return previous ? CallWindowProcW(previous, hwnd, message, wparam, lparam)
                            : DefWindowProcW(hwnd, message, wparam, lparam);
        }
        std::lock_guard lock(frameMutex_);
        if (imguiContext_) {
            auto* previous = ImGui::GetCurrentContext();
            ImGui::SetCurrentContext(static_cast<ImGuiContext*>(imguiContext_));
            ImGui_ImplWin32_WndProcHandler(hwnd, message, wparam, lparam);
            ImGui::SetCurrentContext(previous);
        }
        if (message == WM_SETCURSOR && LOWORD(lparam) == HTCLIENT) {
            SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            return TRUE;
        }
        if (keyboardMessage(message) || mouseMessage(message) || message == WM_INPUT) return 1;
    }
    auto previous = previousWndProc_.load(std::memory_order_acquire);
    return previous ? CallWindowProcW(previous, hwnd, message, wparam, lparam)
                    : DefWindowProcW(hwnd, message, wparam, lparam);
}

LRESULT CALLBACK StandaloneGuiHost::wndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* host = instance_.load(std::memory_order_acquire);
    if (!host || !host->previousWndProc_.load()) return DefWindowProcW(hwnd, message, wparam, lparam);
    CallbackGuard guard(host->callbacks_);
    return host->windowProc(hwnd, message, wparam, lparam);
}

bool StandaloneGuiHost::installWndProc(IDXGISwapChain* swapchain) noexcept {
    auto previousProc = previousWndProc_.load(std::memory_order_acquire);
    if (previousProc && observedSwapchain_ == swapchain) return true;
    DXGI_SWAP_CHAIN_DESC desc{};
    if (!swapchain || FAILED(swapchain->GetDesc(&desc)) || !IsWindow(desc.OutputWindow)) {
        if (!windowErrorLogged_.exchange(true)) logGuiError("Standalone GUI: game swapchain window unavailable");
        return false;
    }
    if (previousProc && window_.load(std::memory_order_acquire) == desc.OutputWindow) {
        observedSwapchain_ = swapchain;
        return true;
    }
    if (previousProc) {
        HWND const oldWindow = window_.load(std::memory_order_acquire);
        if (IsWindow(oldWindow)) {
            if (GetWindowLongPtrW(oldWindow, GWLP_WNDPROC) != reinterpret_cast<LONG_PTR>(&wndProc)) {
                if (!windowErrorLogged_.exchange(true)) logGuiError("Standalone GUI: WndProc ownership changed");
                return false;
            }
            SetLastError(ERROR_SUCCESS);
            SetWindowLongPtrW(oldWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(previousProc));
            if (GetLastError() != ERROR_SUCCESS) {
                if (!windowErrorLogged_.exchange(true)) logGuiError("Standalone GUI: old WndProc restore failed");
                return false;
            }
        }
        previousWndProc_ = nullptr;
        window_ = nullptr;
        if (!releaseGraphics()) return false;
    }
    SetLastError(ERROR_SUCCESS);
    auto previous = SetWindowLongPtrW(desc.OutputWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&wndProc));
    if (!previous || GetLastError() != ERROR_SUCCESS) {
        if (!windowErrorLogged_.exchange(true)) logGuiError("Standalone GUI: WndProc install failed");
        return false;
    }
    window_ = desc.OutputWindow;
    previousWndProc_ = reinterpret_cast<WNDPROC>(previous);
    observedSwapchain_ = swapchain;
    logGuiInfo("Standalone GUI: WndProc installed");
    return true;
}

bool StandaloneGuiHost::initializeGraphics(IDXGISwapChain* swapchain) noexcept {
    if (graphicsRetirementPending_ && !releaseGraphics()) return false;
    if (imguiReady_ && initializedSwapchain_.Get() == swapchain) return true;
    if (!releaseGraphics()) return false;
    if (!swapchain || !window_.load(std::memory_order_acquire)) return false;
    ComPtr<ID3D12Device> device12;
    on12_ = SUCCEEDED(swapchain->GetDevice(IID_PPV_ARGS(&device12)));
    if (on12_) {
        if (!queue_) return false;
        ComPtr<ID3D12Device> queueDevice;
        if (FAILED(queue_->GetDevice(IID_PPV_ARGS(&queueDevice))) || queueDevice.Get() != device12.Get()) return false;
        IUnknown* queues[]{queue_.Get()};
        if (FAILED(D3D11On12CreateDevice(device12.Get(), D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                nullptr, 0, queues, 1, 0, &device11_, &context11_, nullptr))) return false;
        if (FAILED(device11_.As(&deviceOn12_))) { releaseGraphics(); return false; }
        ComPtr<IDXGISwapChain3> chain3;
        DXGI_SWAP_CHAIN_DESC desc{};
        if (FAILED(swapchain->QueryInterface(IID_PPV_ARGS(&chain3)))
            || FAILED(swapchain->GetDesc(&desc)) || !desc.BufferCount) { releaseGraphics(); return false; }
        wrapped_.resize(desc.BufferCount);
        targets_.resize(desc.BufferCount);
        for (UINT i = 0; i < desc.BufferCount; ++i) {
            ComPtr<ID3D12Resource> buffer;
            D3D11_RESOURCE_FLAGS flags{};
            flags.BindFlags = D3D11_BIND_RENDER_TARGET;
            if (FAILED(swapchain->GetBuffer(i, IID_PPV_ARGS(&buffer)))
                || FAILED(deviceOn12_->CreateWrappedResource(buffer.Get(), &flags,
                    D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_PRESENT,
                    IID_PPV_ARGS(&wrapped_[i])))
                || FAILED(device11_->CreateRenderTargetView(wrapped_[i].Get(), nullptr, &targets_[i]))) {
                releaseGraphics(); return false;
            }
        }
    } else {
        if (FAILED(swapchain->GetDevice(IID_PPV_ARGS(&device11_)))) return false;
        device11_->GetImmediateContext(&context11_);
        ComPtr<ID3D11Texture2D> buffer;
        if (FAILED(swapchain->GetBuffer(0, IID_PPV_ARGS(&buffer)))
            || FAILED(device11_->CreateRenderTargetView(buffer.Get(), nullptr, &target11_))) {
            releaseGraphics(); return false;
        }
    }
    auto* previous = ImGui::GetCurrentContext();
    imguiContext_ = ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NoMouseCursorChange;
    auto const runtimeConfig = runtimeConfigSnapshot();
    auto const fontFamily = static_cast<LegacyUiFontFamily>(std::clamp(runtimeConfig.guiStyle.fontFamily, 0, 2));
    if (!configureLegacyUiFont(fontFamily, 18.0f))
        logGuiError("Standalone GUI: embedded legacy Praxis font initialization failed");
    bool const win32 = ImGui_ImplWin32_Init(window_.load(std::memory_order_acquire));
    bool const dx11 = win32 && ImGui_ImplDX11_Init(device11_.Get(), context11_.Get());
    if (!dx11) {
        if (win32) ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext(static_cast<ImGuiContext*>(imguiContext_));
        imguiContext_ = nullptr;
        ImGui::SetCurrentContext(previous);
        releaseGraphics();
        return false;
    }
    imguiReady_ = true;
    deviceLossLogged_ = false;
    initializedSwapchain_ = swapchain;
    ImGui::SetCurrentContext(previous);
    logGuiInfo("Standalone GUI: ImGui initialized");
    return true;
}

bool StandaloneGuiHost::releaseGraphics() noexcept {
    if (context11_ && !graphicsRetirementPending_) {
        ID3D11RenderTargetView* empty{};
        context11_->OMSetRenderTargets(1, &empty, nullptr);
        context11_->Flush();
    }
    graphicsRetirementPending_ = true;
    auto failed = [] {
        logGuiError("Standalone GUI: GPU retirement incomplete; graphics resources retained");
        return false;
    };
    if (on12_ && context11_) {
        if (!queue_) return failed();
        ComPtr<ID3D12Device> device;
        if (FAILED(queue_->GetDevice(IID_PPV_ARGS(&device)))) return failed();
        // A removed device has aborted its work and signals every fence to
        // UINT64_MAX. Keep the existing device-loss reinitialization path.
        if (SUCCEEDED(device->GetDeviceRemovedReason())) {
            if (!retirementFence_) {
                ComPtr<ID3D12Fence> fence;
                if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))
                    || FAILED(queue_->Signal(fence.Get(), 1))) return failed();
                retirementFence_ = fence;
            }
            if (retirementFence_->GetCompletedValue() < 1) {
                if (!retirementEvent_) {
                    retirementEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
                    if (!retirementEvent_) return failed();
                }
                if (!retirementEventRegistered_) {
                    if (FAILED(retirementFence_->SetEventOnCompletion(1, retirementEvent_))) return failed();
                    retirementEventRegistered_ = true;
                }
                if (WaitForSingleObject(retirementEvent_, 3000) != WAIT_OBJECT_0
                    || retirementFence_->GetCompletedValue() < 1) return failed();
            }
        }
    }
    // A timed-out event/fence stays owned with the resources until completion
    // or confirmed device removal; later cleanup can safely retry the wait.
    if (retirementEvent_) { CloseHandle(retirementEvent_); retirementEvent_ = nullptr; }
    retirementEventRegistered_ = false;
    retirementFence_.Reset();
    if (imguiContext_) {
        auto* previous = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(static_cast<ImGuiContext*>(imguiContext_));
        if (imguiReady_) { map::clearMapGraphics(); ImGui_ImplDX11_Shutdown(); ImGui_ImplWin32_Shutdown(); }
        ImGui::DestroyContext(static_cast<ImGuiContext*>(imguiContext_));
        ImGui::SetCurrentContext(previous == imguiContext_ ? nullptr : previous);
    }
    imguiContext_ = nullptr;
    imguiReady_ = false;
    initializedSwapchain_.Reset();
    target11_.Reset();
    targets_.clear();
    wrapped_.clear();
    deviceOn12_.Reset();
    context11_.Reset();
    device11_.Reset();
    on12_ = false;
    graphicsRetirementPending_ = false;
    return true;
}

void StandaloneGuiHost::handlePresentResult(HRESULT result) noexcept {
    if (result != DXGI_ERROR_DEVICE_REMOVED && result != DXGI_ERROR_DEVICE_RESET) return;
    std::lock_guard lock(frameMutex_);
    if (!releaseGraphics()) return;
    queue_.Reset();
    queueCaptured_ = false;
    graphicsWaitLogged_ = false;
    if (!deviceLossLogged_.exchange(true))
        logGuiError("Standalone GUI: graphics device lost; resources released for reinitialization");
}

void StandaloneGuiHost::renderFrame(IDXGISwapChain* swapchain) noexcept {
    static thread_local bool rendering{};
    if (rendering || !started_) return;
    rendering = true;
    struct RenderGuard { bool& value; ~RenderGuard() { value = false; } } guard{rendering};
    std::lock_guard lock(frameMutex_);
    if (!started_) return;
    if (!installWndProc(swapchain)) return;
    if (!visible() && !chunkHudNeeded() && !shulkerPreviewNeeded()) return;
    if (!initializeGraphics(swapchain)) {
        if (!graphicsWaitLogged_.exchange(true))
            logGuiError("Standalone GUI: graphics initialization pending or failed; waiting for a compatible device and D3D12 queue");
        return;
    }
    auto* previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(imguiContext_));
    ImGui_ImplDX11_NewFrame();
    map::setMapGraphics(device11_.Get(),context11_.Get());
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    if (visible() && window_.load() == GetForegroundWindow() && !IsIconic(window_.load())) {
        ImGui::GetIO().MouseDrawCursor = false;
        ClipCursor(nullptr);
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
    }
    if (chunkHudNeeded()) drawChunkHudContent(imguiContext_);
    if (visible()) drawPraxisContent(imguiContext_);
    ImGui::Render();
    if (on12_) {
        ComPtr<IDXGISwapChain3> chain3;
        if (SUCCEEDED(swapchain->QueryInterface(IID_PPV_ARGS(&chain3)))) {
            UINT const index = chain3->GetCurrentBackBufferIndex();
            if (index < wrapped_.size() && index < targets_.size()) {
                ID3D11Resource* resource = wrapped_[index].Get();
                deviceOn12_->AcquireWrappedResources(&resource, 1);
                ID3D11RenderTargetView* target = targets_[index].Get();
                context11_->OMSetRenderTargets(1, &target, nullptr);
                ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
                ID3D11RenderTargetView* empty{};
                context11_->OMSetRenderTargets(1, &empty, nullptr);
                deviceOn12_->ReleaseWrappedResources(&resource, 1);
                context11_->Flush();
            }
        }
    } else {
        ComPtr<ID3D11RenderTargetView> previousTarget;
        context11_->OMGetRenderTargets(1, &previousTarget, nullptr);
        ID3D11RenderTargetView* target = target11_.Get();
        context11_->OMSetRenderTargets(1, &target, nullptr);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        target = previousTarget.Get();
        context11_->OMSetRenderTargets(1, &target, nullptr);
    }
    ImGui::SetCurrentContext(previous);
}

HRESULT STDMETHODCALLTYPE StandaloneGuiHost::present(IDXGISwapChain* chain, UINT sync, UINT flags) {
    auto* host = instance_.load(std::memory_order_acquire);
    if (!host || !host->originalPresent_) return DXGI_ERROR_INVALID_CALL;
    CallbackGuard guard(host->callbacks_);
    bool const outer = presentDepth == 0;
    PresentDepthGuard depthGuard;
    if (outer && host->started_) { try { host->renderFrame(chain); } catch (...) {
        if (!host->renderErrorLogged_.exchange(true)) logGuiError("Standalone GUI: Present render failed");
    } }
    HRESULT const result = host->originalPresent_(chain, sync, flags);
    host->handlePresentResult(result);
    return result;
}
HRESULT STDMETHODCALLTYPE StandaloneGuiHost::present1(IDXGISwapChain1* chain, UINT sync, UINT flags, DXGI_PRESENT_PARAMETERS const* params) {
    auto* host = instance_.load(std::memory_order_acquire);
    if (!host || !host->originalPresent1_) return DXGI_ERROR_INVALID_CALL;
    CallbackGuard guard(host->callbacks_);
    bool const outer = presentDepth == 0;
    PresentDepthGuard depthGuard;
    if (outer && host->started_) { try { host->renderFrame(chain); } catch (...) {
        if (!host->renderErrorLogged_.exchange(true)) logGuiError("Standalone GUI: Present1 render failed");
    } }
    HRESULT const result = host->originalPresent1_(chain, sync, flags, params);
    host->handlePresentResult(result);
    return result;
}
HRESULT STDMETHODCALLTYPE StandaloneGuiHost::resize(IDXGISwapChain* chain, UINT count, UINT width, UINT height, DXGI_FORMAT format, UINT flags) {
    auto* host = instance_.load(std::memory_order_acquire);
    if (!host || !host->originalResize_) return DXGI_ERROR_INVALID_CALL;
    CallbackGuard guard(host->callbacks_);
    {
        std::lock_guard lock(host->frameMutex_);
        if (!host->releaseGraphics()) return DXGI_ERROR_INVALID_CALL;
        host->observedSwapchain_ = nullptr;
    }
    return host->originalResize_(chain, count, width, height, format, flags);
}
HRESULT STDMETHODCALLTYPE StandaloneGuiHost::resize1(IDXGISwapChain3* chain, UINT count, UINT width, UINT height, DXGI_FORMAT format, UINT flags, UINT const* masks, IUnknown* const* queues) {
    auto* host = instance_.load(std::memory_order_acquire);
    if (!host || !host->originalResize1_) return DXGI_ERROR_INVALID_CALL;
    CallbackGuard guard(host->callbacks_);
    {
        std::lock_guard lock(host->frameMutex_);
        if (!host->releaseGraphics()) return DXGI_ERROR_INVALID_CALL;
        host->observedSwapchain_ = nullptr;
        if (queues && queues[0]) {
            ComPtr<ID3D12CommandQueue> next;
            if (SUCCEEDED(queues[0]->QueryInterface(IID_PPV_ARGS(&next)))) {
                host->queue_ = next;
                host->queueCaptured_ = true;
            }
        }
    }
    return host->originalResize1_(chain, count, width, height, format, flags, masks, queues);
}
void STDMETHODCALLTYPE StandaloneGuiHost::execute(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists) {
    auto* host = instance_.load(std::memory_order_acquire);
    if (!host || !host->originalExecute_) return;
    CallbackGuard guard(host->callbacks_);
    if (host->started_ && !host->queueCaptured_.load(std::memory_order_acquire)
        && queue && queue->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT) {
        std::lock_guard lock(host->frameMutex_);
        if (!host->queue_) host->queue_ = queue;
        host->queueCaptured_.store(true, std::memory_order_release);
    }
    host->originalExecute_(queue, count, lists);
}
}
