#pragma once
#include <imgui.h>
#include <backends/imgui_impl_dx11.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <fstream>
#include <stdexcept>
#include <vector>
namespace offline_ui {
inline void requireRaster(bool ok,char const* message){if(!ok)throw std::runtime_error(message);}
inline void saveRender(char const* path, ImDrawData* data, int width, int height) {
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    requireRaster(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
          nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context)), "WARP device");
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width; desc.Height = height; desc.MipLevels = desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> image;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target;
    requireRaster(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &image)), "offscreen target");
    requireRaster(SUCCEEDED(device->CreateRenderTargetView(image.Get(), nullptr, &target)), "offscreen view");
    auto* view = target.Get();
    context->OMSetRenderTargets(1, &view, nullptr);
    float const background[]{0.09f, 0.1f, 0.12f, 1.0f};
    context->ClearRenderTargetView(view, background);
    auto const fontTextureBeforeUpload = ImGui::GetIO().Fonts->TexID;
    requireRaster(ImGui_ImplDX11_Init(device.Get(), context.Get()), "offscreen backend");
    requireRaster(ImGui_ImplDX11_CreateDeviceObjects(), "offscreen font texture");
    std::vector<std::pair<ImDrawCmd*,ImTextureID>> temporaryTextures;
    for (auto* list:data->CmdLists)
        for (auto& command:list->CmdBuffer)
            if (command.TextureId==fontTextureBeforeUpload) {
                temporaryTextures.emplace_back(&command,command.TextureId);
                command.TextureId=ImGui::GetIO().Fonts->TexID;
            }
    ImGui_ImplDX11_RenderDrawData(data);
    desc.BindFlags = 0; desc.Usage = D3D11_USAGE_STAGING; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> readback;
    requireRaster(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &readback)), "readback texture");
    context->CopyResource(readback.Get(), image.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    requireRaster(SUCCEEDED(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped)), "readback");
    std::ofstream out(path, std::ios::binary);
    out << "P6\n" << width << ' ' << height << "\n255\n";
    unsigned drawnPixels{};
    for (int y=0; y<height; ++y) {
        auto const* row = static_cast<unsigned char*>(mapped.pData)+y*mapped.RowPitch;
        for (int x=0; x<width; ++x) {
            out.write(reinterpret_cast<char const*>(row+x*4), 3);
            if (row[x*4]!=23 || row[x*4+1]!=26 || row[x*4+2]!=31) ++drawnPixels;
        }
    }
    context->Unmap(readback.Get(), 0);
    // Multiple captures can use the same frame. Never leave an SRV from the
    // temporary WARP device in the borrowed draw commands after shutdown.
    for(auto const& [command,texture]:temporaryTextures)command->TextureId=texture;
    ImGui_ImplDX11_Shutdown();
    requireRaster(out.good(), "render artifact write");
    requireRaster(drawnPixels>10000, "offscreen artifact contains rendered Praxis pixels");
}
}
