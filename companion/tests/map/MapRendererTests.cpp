#include "map/MapCore.h"
#include "map/MapRenderer.h"
#include "map/MapGraphics.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <backends/imgui_impl_dx11.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <imgui.h>
#include <iostream>
#include <stdexcept>
using namespace praxis::companion::map;
int main() {
    auto *context = ImGui::CreateContext();
    try {
        auto &io = ImGui::GetIO();
        io.DisplaySize = {800, 600};
        io.DeltaTime = 1.f / 60;
        unsigned char *pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        io.IniFilename = nullptr;
        Snapshot s;
        s.session = {"render-world", 1, 0, false};
        s.playerX = 0;
        s.playerZ = 0;
        std::vector<Cell> terrain;
        for (int z = -32; z < 32; ++z)
            for (int x = -32; x < 32; ++x) {
                Sample sample;
                sample.known = true;
                sample.color = 0xff409940;
                sample.height = 64;
                terrain.push_back({x * 4, z * 4, 4, sample});
            }
        s.cells = std::make_shared<std::vector<Cell> const>(std::move(terrain));
        std::vector<double> times;
        int cases = 0;
        for (bool circle : {false, true})
            for (double angle : {0., 0.1, 0.7, 1.57, 3.14, -1.2})
                for (double zoom : {.25, .75, 1., 2., 4.}) {
                    ImGui::NewFrame();
                    auto *draw = ImGui::GetForegroundDrawList();
                    Canvas c{{{0, 0}, {400, 300}, zoom, angle}, 96, circle};
                    auto start = std::chrono::steady_clock::now();
                    auto cells = drawMapCanvas(*draw, s, c);
                    times.push_back(
                        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                            .count());
                    if (cells > 4096 || draw->VtxBuffer.empty() || ImGui::GetCurrentContext() != context)
                        throw std::runtime_error("renderer cell budget / empty output");
                    // ImGui clip rectangles clip square vertices at submission. Circle
                    // terrain is geometrically clipped; border antialiasing adds <3px.
                    if (circle)
                        for (auto const &vertex : draw->VtxBuffer) {
                            double x = vertex.pos.x - 400, z = vertex.pos.y - 300;
                            if (x * x + z * z > 99. * 99.)
                                throw std::runtime_error("circle vertex escaped bounds");
                        }
                    ImGui::Render();
                    ++cases;
                }
        int featureCases = 0;
        for (bool circle : {false,true}) for (bool names : {false,true}) for (bool cave : {false,true}) {
            Snapshot fixture;
            fixture.session = s.session;
            fixture.playerY = 64;
            fixture.layer = cave ? Layer::Cave : Layer::Surface;
            fixture.caveTop = 68; fixture.caveBottom = 36;
            std::vector<Cell> terrain(8192);
            for (std::size_t i = 0; i < terrain.size(); ++i) {
                auto &cell = terrain[i];
                cell.x = int(i%64)-32; cell.z = int(i/64)-64; cell.stride = 1;
                cell.key = tileKey(cell.x,cell.z,0,fixture.layer,68,36);
                cell.viewMask = i < 4096 ? 1 : 2;
                if (i%2) { cell.sample.known = true; cell.sample.color = 0xff408040; cell.sample.height = 60; }
            }
            fixture.cells = std::make_shared<std::vector<Cell> const>(std::move(terrain));
            for (std::size_t i = 0; i < 1024; ++i)
                fixture.waypoints.push_back({i+1,"point",double(int(i%32)-16),64,double(int(i/32)-16),0,
                                            0xffaa5566u,i%3 != 0});
            for (std::size_t i = 0; i < 160; ++i)
                fixture.radar.push_back({double(int(i%16)-8),double(56+(i%3)*8),double(int(i/16)-5),
                    std::string(100,'N'),0xffbbcc55u});
            ImGui::NewFrame();
            auto *draw = ImGui::GetForegroundDrawList();
            int clipDepth = draw->_ClipRectStack.Size;
            Canvas canvas{{{0,0},{400,300},.75,0},96,circle};
            canvas.radarNames = names;
            canvas.viewMask = 1;
            auto cells = drawMapCanvas(*draw,fixture,canvas);
            if (cells != 4096)
                throw std::runtime_error("per-view terrain cap / selection");
            bool unknown = false;
            for (auto const &vertex : draw->VtxBuffer) {
                unknown |= vertex.col == 0xff30383f || vertex.col == 0xff252c32;
                if (circle) {
                    double dx = vertex.pos.x - 400, dz = vertex.pos.y - 300;
                    if (dx*dx+dz*dz > 99.*99.) throw std::runtime_error("feature marker outside circle");
                }
            }
            if (!unknown || draw->_ClipRectStack.Size != clipDepth)
                throw std::runtime_error("unknown color / unbalanced clip stack");
            ImGui::Render();
            ++featureCases;
        }
        // Isolated WARP render target: exercise the real texture upload and
        // point-sampler callbacks without a game, window, host or injected DLL.
        using Microsoft::WRL::ComPtr;
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> gpu;
        if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,
            D3D11_SDK_VERSION,&device,nullptr,&gpu))) throw std::runtime_error("WARP creation failed");
        if(!ImGui_ImplDX11_Init(device.Get(),gpu.Get())) throw std::runtime_error("DX11 backend init failed");
        D3D11_TEXTURE2D_DESC desc{};desc.Width=800;desc.Height=600;desc.MipLevels=desc.ArraySize=1;
        desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> target,staging;ComPtr<ID3D11RenderTargetView> rtv;
        if(FAILED(device->CreateTexture2D(&desc,nullptr,&target))||
            FAILED(device->CreateRenderTargetView(target.Get(),nullptr,&rtv)))throw std::runtime_error("offscreen target failed");
        desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        if(FAILED(device->CreateTexture2D(&desc,nullptr,&staging)))throw std::runtime_error("readback target failed");
        Snapshot dense;dense.session=s.session;dense.playerX=dense.playerZ=10000;
        std::vector<Cell> pixelsDense;
        for(int z=-64;z<=64;++z)for(int x=-64;x<=64;++x) {
            Sample sample;sample.known=true;
            constexpr std::array colors{0xff0000ffu,0xff00ff00u,0xffff0000u,0xffffffffu};
            sample.color=colors[std::size_t(floorMod(x+2*z,4))];
            pixelsDense.push_back({x,z,1,sample,tileKey(x,z,0,Layer::Surface),1,{1,1}});
        }
        dense.cells=std::make_shared<std::vector<Cell> const>(std::move(pixelsDense));
        dense.terrainImages[0]=rasterTerrain(View{0,0,64},*dense.cells,1);
        int gpuCases=0,gpuChecks=0;
        for(bool circle:{false,true})for(double angle:{0.,.1,.7,1.57})for(double scale:{.25,.75,1.,2.,4.}) {
            setMapGraphics(device.Get(),gpu.Get());
            ImGui_ImplDX11_NewFrame();ImGui::NewFrame();
            Canvas canvas{{{0,0},{400,300},scale,angle},96,circle};canvas.viewMask=1;
            auto* draw=ImGui::GetForegroundDrawList();
            if(drawMapCanvas(*draw,dense,canvas)!=129*129)throw std::runtime_error("dense image did not retain every art pixel");
            ImGui::Render();auto* view=rtv.Get();gpu->OMSetRenderTargets(1,&view,nullptr);
            float clear[4]{};gpu->ClearRenderTargetView(view,clear);ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            gpu->CopyResource(staging.Get(),target.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
            if(FAILED(gpu->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)))throw std::runtime_error("map readback failed");
            for(int py=275;py<=325;py+=2)for(int px=375;px<=425;px+=2) {
                auto world=canvas.transform.unproject({px+.5,py+.5});
                int x=int(std::floor(world.x+.5)),z=int(std::floor(world.z+.5));
                if(std::abs(x)>60||std::abs(z)>60)continue;
                constexpr std::array colors{0xff0000ffu,0xff00ff00u,0xffff0000u,0xffffffffu};
                auto expected=colors[std::size_t(floorMod(x+2*z,4))];
                auto actual=*reinterpret_cast<std::uint32_t const*>(
                    static_cast<unsigned char const*>(mapped.pData)+std::size_t(py)*mapped.RowPitch+px*4);
                // D3D11 quantizes rasterized vertices to a subpixel grid; allow
                // the adjacent exact palette texel near an edge, never mixed colors.
                double fx=world.x+.5-std::floor(world.x+.5),fz=world.z+.5-std::floor(world.z+.5);
                if(std::find(colors.begin(),colors.end(),actual)==colors.end()) {
                    gpu->Unmap(staging.Get(),0);throw std::runtime_error("point sampling introduced interpolated palette colors");
                }
                if(fx<.03||fz<.03||fx>.97||fz>.97)continue;
                if(actual!=expected){std::cerr<<"GPU case circle="<<circle<<" angle="<<angle<<" scale="<<scale<<" pixel="<<px<<","<<py<<" world="<<world.x<<","<<world.z<<" rgba="<<std::hex<<actual<<" expected="<<expected<<std::dec<<"\n";gpu->Unmap(staging.Get(),0);throw std::runtime_error("point sampling mixed terrain palette colors");}
                ++gpuChecks;
            }
            gpu->Unmap(staging.Get(),0);++gpuCases;
        }
        if(!firstMapSubmission(dense.session))throw std::runtime_error("first texture submission was not recorded");
        clearMapGraphics();ImGui_ImplDX11_Shutdown();
        std::cout<<"OFFLINE WARP texture cases: "<<gpuCases<<" / exact native-color pixels "<<gpuChecks<<" PASS; no game claim\n";
        std::sort(times.begin(), times.end());
        std::cout << "PraxisMapRendererTests: " << cases << " transform/shape/zoom cases PASS\n";
        std::cout << "Feature payload cases: " << featureCases << " PASS (unknown/cave/radar/names/waypoint/view caps)\n";
        std::cout << "OFFLINE render CPU ms p50=" << times[times.size() / 2]
                  << " p95=" << times[std::size_t(times.size() * .95)] << " max=" << times.back()
                  << "; 4096 input cells; no game/runtime visual claim\n";
        ImGui::DestroyContext(context);
        return 0;
    } catch (std::exception const &e) {
        std::cerr << "FAIL " << e.what() << '\n';
        clearMapGraphics();
        if(ImGui::GetIO().BackendRendererUserData)ImGui_ImplDX11_Shutdown();
        ImGui::DestroyContext(context);
        return 1;
    }
}
