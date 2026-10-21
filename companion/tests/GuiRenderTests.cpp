#define PRAXIS_GUI_HEADLESS_TEST
#include "../src/gui/GuiBridge.cpp"
#include "resourcepacks/PackBrowser.h"
#include <imgui_internal.h>
#include <backends/imgui_impl_dx11.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <fstream>
#include <chrono>
#include <thread>
#include <stdexcept>
#include <set>
#include "JapaneseUiText.inc"

namespace praxis::companion {
RuntimeConfig testConfig;
char const* handRestockStatus() noexcept {return "未実行";}
unsigned mapCalls{}, infoCalls{}, previewCalls{}, cameraStatusCalls{}, checks{}, frames{};
std::uint32_t lastBindingEdits{};
char const* cameraStatusFixture="OFF";
char const* cameraCandidateStatus() noexcept {
    ++cameraStatusCalls;
    return cameraStatusFixture;
}
RuntimeConfig runtimeConfigSnapshot() { return testConfig; }
bool updateRuntimeConfig(RuntimeConfig const& next) { testConfig = next; return true; }
RuntimeConfigView runtimeConfigView() {return {testConfig,0};}
bool updateRuntimeConfigIfRevision(RuntimeConfig const& next,std::uint64_t revision,std::uint32_t edits) {lastBindingEdits=edits;return revision==0&&updateRuntimeConfig(next);}
bool flushRuntimeConfig() noexcept { return true; }
std::vector<std::string> listRuntimeProfiles() { return {"Example"}; }
bool saveRuntimeProfile(std::string const&) noexcept { return true; }
bool loadRuntimeProfile(std::string const&) noexcept { return true; }
bool deleteRuntimeProfile(std::string const&) noexcept { return true; }
std::string configStorageStatus() { return "Configuration saved"; }
bool informationHudNeeded() noexcept { return true; }
bool foodHudNeeded() noexcept { return false; }
std::shared_ptr<InformationOptions const> informationOptions() noexcept {return std::make_shared<InformationOptions>(testConfig.informationOptions);}
void drawFoodHud(void*) noexcept {}
bool shulkerPreviewNeeded() noexcept { return true; }
bool mapHudNeeded() noexcept { return true; }
void drawInformationHud(void*) noexcept { ++infoCalls; }
void drawShulkerPreviewContent(void*) noexcept { ++previewCalls; }
void drawMapHud(void*) noexcept { ++mapCalls; }
// Map's terrain/settings runtime has its own integration tests. Here the stub
// verifies that the existing Map page still reaches its actual consumer API.
void drawMapSettings() noexcept { ++mapCalls; ImGui::TextUnformatted("Map settings consumer"); }
std::array<int,map::mapActionCount> mapActionBindings() noexcept {return {};}
bool setMapActionBinding(map::MapAction,int) noexcept {return true;}
HudRect durabilityHudBounds() noexcept { return {}; }
HudRect informationHudBounds() noexcept { return {}; }
std::shared_ptr<InformationSnapshot const> informationSnapshot() noexcept {
    static auto const snapshot = std::make_shared<InformationSnapshot>(); return snapshot;
}
void logGuiInfo(char const*) noexcept {}
void logGuiError(char const* message) noexcept { std::fprintf(stderr, "%s\n", message); }
namespace map {
unsigned graphicsCalls{}, graphicsResets{};
ID3D11Device* suppliedDevice{};
ID3D11DeviceContext* suppliedContext{};
void setMapGraphics(ID3D11Device* device,ID3D11DeviceContext* context) noexcept {
    ++graphicsCalls; suppliedDevice=device; suppliedContext=context;
}
void clearMapGraphics() noexcept { ++graphicsResets; suppliedDevice=nullptr; suppliedContext=nullptr; }
}
}

namespace {
using namespace praxis::companion;
using Microsoft::WRL::ComPtr;
void check(bool success, char const* message) {
    ++checks;
    if (!success) throw std::runtime_error(message);
}
std::filesystem::path preparePackUiFixture(){
    auto root=std::filesystem::temp_directory_path()/("PraxisPackUi-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
    check(std::filesystem::create_directory(root),"unique owned pack UI fixture");
    for(unsigned i=1;i<=2;++i){
        auto folder=root/std::to_string(i);std::filesystem::create_directories(folder/"textures");
        std::ofstream file(folder/"manifest.json",std::ios::binary);
        file<<"{\"format_version\":2,\"header\":{\"name\":\""<<std::string(200,'A')<<i
            <<"\",\"uuid\":\"00000000-0000-0000-0000-00000000000"<<i
            <<"\",\"version\":[1,0,0]},\"modules\":[{\"type\":\"resources\",\"uuid\":\"00000000-0000-0000-0000-00000000010"
            <<i<<"\",\"version\":[1,0,0]}]}";
        file.close();check(bool(file),"owned UI manifest written");
    }
    check(packs::startBrowser()&&packs::requestScan(root),"actual background browser admits UI fixture");
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(packs::browserView().busy&&std::chrono::steady_clock::now()<deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    auto view=packs::browserView();
    check(!view.busy&&view.catalog->complete&&view.catalog->packs.size()==2,"actual local catalog available to resource page");
    return root;
}
void checkJapaneseGlyphs(ImFont* font) {
    std::vector<unsigned int> missing;
    for(auto* text:japaneseUiCorpus)while(*text) {
        unsigned int character{};int const bytes=ImTextCharFromUtf8(&character,text,nullptr);
        check(bytes>0,"valid UTF-8 in actual UI corpus");text+=bytes;
        if(character<32)continue;
        if(!font->FindGlyphNoFallback(static_cast<ImWchar>(character))) {
            if(std::find(missing.begin(),missing.end(),character)==missing.end())missing.push_back(character);
        }
    }
    for(auto character:missing)std::fprintf(stderr,"Missing UI glyph U+%04X\n",character);
    check(missing.empty(),"rendered Japanese characters are available in existing font atlas");
}
void saveRender(char const* path, ImDrawData* data, int width, int height) {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    check(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
          nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context)), "WARP device");
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width; desc.Height = height; desc.MipLevels = desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> image;
    ComPtr<ID3D11RenderTargetView> target;
    check(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &image)), "offscreen target");
    check(SUCCEEDED(device->CreateRenderTargetView(image.Get(), nullptr, &target)), "offscreen view");
    auto* view = target.Get();
    context->OMSetRenderTargets(1, &view, nullptr);
    float const background[]{0.09f, 0.1f, 0.12f, 1.0f};
    context->ClearRenderTargetView(view, background);
    auto const fontTextureBeforeUpload = ImGui::GetIO().Fonts->TexID;
    check(ImGui_ImplDX11_Init(device.Get(), context.Get()), "offscreen backend");
    check(ImGui_ImplDX11_CreateDeviceObjects(), "offscreen font texture");
    std::vector<std::pair<ImDrawCmd*,ImTextureID>> temporaryTextures;
    for (auto* list:data->CmdLists)
        for (auto& command:list->CmdBuffer)
            if (command.TextureId==fontTextureBeforeUpload) {
                temporaryTextures.emplace_back(&command,command.TextureId);
                command.TextureId=ImGui::GetIO().Fonts->TexID;
            }
    ImGui_ImplDX11_RenderDrawData(data);
    desc.BindFlags = 0; desc.Usage = D3D11_USAGE_STAGING; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> readback;
    check(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &readback)), "readback texture");
    context->CopyResource(readback.Get(), image.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    check(SUCCEEDED(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped)), "readback");
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
    check(out.good(), "render artifact write");
    check(drawnPixels>10000, "offscreen artifact contains rendered Praxis pixels");
}
ImGuiWindow* activeWindow(ImGuiContext* context, char const* fragment) {
    for (auto* window : context->Windows)
        if (window->Active && std::string_view(window->Name).find(fragment)!=std::string_view::npos)
            return window;
    return nullptr;
}
ImGuiID scopedId(ImGuiWindow* window, int scope, char const* label) {
    auto seed=ImHashData(&scope,sizeof(scope),window->ID);
    return ImHashStr(label,0,seed);
}
ImGuiID bindingId(ImGuiWindow* window, int slot, char const* label) {
    int const group=0;
    int const section=slot==20||slot==21||slot==26?2:1;
    auto seed=ImHashData(&section,sizeof(section),window->ID);
    seed=ImHashData(&group,sizeof(group),seed);
    seed=ImHashData(&slot,sizeof(slot),seed);
    return ImHashStr(label,0,seed);
}
void navigationFrame(ImGuiContext* context) {
    ImGui::NewFrame();
    drawPraxisContent(context);
    check(context->ErrorCountCurrentFrame==0,"navigation has no ImGui errors");
    check(context->FontStack.empty()&&context->ColorStack.empty()&&context->StyleVarStack.empty(),
          "navigation restores borrowed host stacks");
    ImGui::Render(); ++frames;
}
void checkCameraBounds(ImGuiContext* context) {
    auto* content=activeWindow(context,"##praxis_content");
    check(content,"camera settings content exists");
    if(content->DC.CursorMaxPos.x>content->WorkRect.Max.x+1.f) {
        auto const display=ImGui::GetIO().DisplaySize;
        std::fprintf(stderr,"Camera overflow: viewport=%.0fx%.0f font=%d page=%d cursorX=%.2f workMaxX=%.2f contentWidth=%.2f\n",
            display.x,display.y,testConfig.guiStyle.fontFamily,legacyGuiState.selected,
            content->DC.CursorMaxPos.x,content->WorkRect.Max.x,content->WorkRect.GetWidth());
        saveRender("praxis-ui-camera-bounds-failure.ppm",ImGui::GetDrawData(),int(display.x),int(display.y));
    }
    check(content->DC.CursorMaxPos.x<=content->WorkRect.Max.x+1.f,
          "camera labels and controls fit horizontally without clipping");
    check(content->ScrollMax.y>=std::max(0.f,content->ContentSize.y-content->InnerRect.GetHeight())-1.f,
          "camera page vertical overflow remains reachable by scrolling");
}
void testCameraNavigation(ImGuiContext* context,bool captureCompact) {
    auto& io=ImGui::GetIO();
    auto const flags=io.ConfigFlags;
    io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
    legacyGuiState={};legacyGuiState.selected=-11;
    for(int i=0;i<3;++i)navigationFrame(context);
    checkCameraBounds(context);
    if(captureCompact)
        saveRender("praxis-ui-camera-640.ppm",ImGui::GetDrawData(),int(io.DisplaySize.x),int(io.DisplaySize.y));
    auto* content=activeWindow(context,"##praxis_content");
    std::set<ImGuiID> expected{
        scopedId(content,20,"有効"),scopedId(content,20,"押す間だけ"),scopedId(content,20,"##cameraSensitivity"),
        scopedId(content,21,"有効"),scopedId(content,21,"##cameraSensitivity"),
        scopedId(content,21,"##cameraFlightSpeed"),scopedId(content,21,"##cameraWheelDistance")};
    std::set<ImGuiID> reached;
    ImGui::FocusWindow(content);
    ImGui::SetFocusID(scopedId(content,20,"有効"),content);
    for(int step=0;step<30&&reached.size()<expected.size();++step) {
        io.AddKeyEvent(ImGuiKey_Tab,true);navigationFrame(context);
        io.AddKeyEvent(ImGuiKey_Tab,false);navigationFrame(context);
        navigationFrame(context);
        check(context->NavId!=scopedId(content,21,"押す間だけ"),"FreeCamera toggle has no keyboard-reachable hold control");
        if(expected.contains(context->NavId)) {
            reached.insert(context->NavId);
            auto* window=context->NavWindow;
            auto rect=ImGui::WindowRectRelToAbs(window,window->NavRectRel[context->NavLayer]);
            if(rect.Min.y<window->ClipRect.Min.y-1.f||rect.Max.y>window->ClipRect.Max.y+1.f) {
                std::fprintf(stderr,"Camera focus: id=%08X reached=%zu navY=%.2f..%.2f clipY=%.2f..%.2f scrollY=%.2f maxScrollY=%.2f window=%s\n",
                    context->NavId,reached.size(),rect.Min.y,rect.Max.y,window->ClipRect.Min.y,window->ClipRect.Max.y,
                    window->Scroll.y,window->ScrollMax.y,window->Name);
                saveRender("praxis-ui-camera-nav-failure.ppm",ImGui::GetDrawData(),int(io.DisplaySize.x),int(io.DisplaySize.y));
            }
            check(rect.Min.y>=window->ClipRect.Min.y-1.f&&rect.Max.y<=window->ClipRect.Max.y+1.f,
                  "focused camera control is visible after native ImGui scrolling");
            if(captureCompact&&context->NavId==scopedId(content,21,"##cameraWheelDistance"))
                saveRender("praxis-ui-camera-640-scrolled.ppm",ImGui::GetDrawData(),int(io.DisplaySize.x),int(io.DisplaySize.y));
        }
    }
    check(reached==expected,"all seven camera controls are keyboard reachable, including Freelook hold and the lower FreeCamera section");
    checkCameraBounds(context);
    io.ConfigFlags=flags;
}
void testHotkeyGapNavigation(ImGuiContext* context) {
    auto& io=ImGui::GetIO();auto const flags=io.ConfigFlags;
    io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
    legacyGuiState={};legacyGuiState.selected=-4;
    for(int i=0;i<3;++i)navigationFrame(context);
    auto* child=activeWindow(context,"##unified_hotkeys");
    check(child,"unified bindings child exists");
    std::set<ImGuiID> expected,reserved,reached;
    for(int slot=0;slot<int(featureCount);++slot)
        if(ownedFeatureSlotMask&featureRegistryBit(slot))expected.insert(bindingId(child,slot,"割当"));
        else for(auto label:{"割当","解除","初期化"})reserved.insert(bindingId(child,slot,label));
    ImGui::FocusWindow(child);ImGui::SetFocusID(bindingId(child,0,"割当"),child);
    for(int step=0;step<200&&reached.size()<expected.size();++step) {
        io.AddKeyEvent(ImGuiKey_Tab,true);navigationFrame(context);
        io.AddKeyEvent(ImGuiKey_Tab,false);navigationFrame(context);
        check(!reserved.contains(context->NavId),"reserved storage gaps have no visible binding controls");
        if(expected.contains(context->NavId))reached.insert(context->NavId);
        if(context->NavId==bindingId(child,21,"割当")) {
            navigationFrame(context);
            saveRender("praxis-ui-hotkeys-camera-640.ppm",ImGui::GetDrawData(),int(io.DisplaySize.x),int(io.DisplaySize.y));
        }
    }
    check(reached==expected,"bindings for all 20 modules, including camera20/21 and action26, are reachable");
    io.ConfigFlags=flags;
}
void testInventoryActionNavigation(ImGuiContext* context) {
    auto& io=ImGui::GetIO();auto const flags=io.ConfigFlags;io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
    legacyGuiState={};legacyGuiState.selected=-4;
    for(int i=0;i<3;++i)navigationFrame(context);
    auto* child=activeWindow(context,"##unified_hotkeys");check(child,"inventory action binding child exists");
    auto actionId=[&](int id){int section=2,group=5;auto seed=ImHashData(&section,sizeof(section),child->ID);seed=ImHashData(&group,sizeof(group),seed);seed=ImHashData(&id,sizeof(id),seed);return ImHashStr("割当",0,seed);};
    std::set<ImGuiID> expected,reached;for(int i=0;i<4;++i)expected.insert(actionId(i));
    ImGui::FocusWindow(child);ImGui::SetFocusID(bindingId(child,0,"割当"),child);
    for(int step=0;step<220&&reached.size()<expected.size();++step){
        io.AddKeyEvent(ImGuiKey_Tab,true);navigationFrame(context);io.AddKeyEvent(ImGuiKey_Tab,false);navigationFrame(context);
        if(expected.contains(context->NavId))reached.insert(context->NavId);
    }
    check(reached==expected,"four execution/cancel bindings are reachable through real ImGui navigation");
    saveRender("praxis-ui-inventory-actions-640.ppm",ImGui::GetDrawData(),int(io.DisplaySize.x),int(io.DisplaySize.y));
    for(int page:{13,14}){
        legacyGuiState={};legacyGuiState.selected=page;for(int i=0;i<3;++i)navigationFrame(context);
        auto* content=activeWindow(context,"##praxis_content");check(content,"inventory settings content exists");
        ImGui::FocusWindow(content);ImGui::SetFocusID(content->GetID("< 機能一覧へ戻る"),content);
        std::set<ImGuiID> controls;
        for(auto name:page==13?std::vector<char const*>{"##sortMethod","##sortMaxSteps","収納画面を除外"}:std::vector<char const*>{"まとめ移動で1個残す","##transferMaxSteps","収納画面を除外"})controls.insert(content->GetID(name));
        reached.clear();
        for(int step=0;step<45&&reached.size()<controls.size();++step){
            io.AddKeyEvent(ImGuiKey_Tab,true);navigationFrame(context);io.AddKeyEvent(ImGuiKey_Tab,false);navigationFrame(context);navigationFrame(context);
            if(controls.contains(context->NavId)){reached.insert(context->NavId);auto* win=context->NavWindow;auto rect=ImGui::WindowRectRelToAbs(win,win->NavRectRel[context->NavLayer]);
                check(rect.Min.y>=win->ClipRect.Min.y-1.f&&rect.Max.y<=win->ClipRect.Max.y+1.f,"inventory focused control visible after scrolling");}
        }
        check(reached==controls,"new sort/transfer controls are keyboard reachable");
    }
    io.ConfigFlags=flags;
}
void testCameraStatusPages(ImGuiContext* context,bool capture) {
    char const* const nativeStatuses[]{"Freelook active","FreeCamera active; WASD/Space/Shift flies",
                                      "Restore pending: camera owner conflict"};
    for(unsigned state=0;state<std::size(nativeStatuses);++state) {
        cameraStatusFixture=nativeStatuses[state];
        legacyGuiState={};legacyGuiState.selected=-11;
        for(int repeat=0;repeat<3;++repeat) {
            navigationFrame(context);
            checkCameraBounds(context);
        }
        if(capture) {
            auto file=std::string("praxis-ui-camera-status-")+std::to_string(state)+".ppm";
            saveRender(file.c_str(),ImGui::GetDrawData(),int(ImGui::GetIO().DisplaySize.x),int(ImGui::GetIO().DisplaySize.y));
        }
    }
    cameraStatusFixture="OFF";
}
void renderCase(ImVec2 viewport, int family, float hostScale, bool capture) {
    auto* context = ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr; io.LogFilename = nullptr;
    io.DisplaySize = viewport; io.DeltaTime = 1.f/60.f;
    io.ConfigErrorRecoveryEnableAssert = false;
    ImFontConfig hostConfig{}; hostConfig.SizePixels = 36;
    auto* const hostFont = io.Fonts->AddFontDefault(&hostConfig);
    io.FontDefault = hostFont;
    testConfig = {}; testConfig.guiStyle.fontFamily = family;
    // Owned display fixture; no physical key injection or input capture.
    testConfig.inventorySortOptions.actionKey = 82;
    check(keyName(82)=="R", "sort action displays the actual Windows key name for 82");
    auto hostStyle = ImGuiStyle{};
    hostStyle.ScaleAllSizes(hostScale*1.18f);
    hostStyle.WindowPadding = {16*hostScale*1.18f,16*hostScale*1.18f};
    hostStyle.FramePadding = {12*hostScale*1.18f*.82f,12*hostScale*1.18f*.42f};
    ImGui::GetStyle() = hostStyle;
    io.FontGlobalScale = hostScale*.5f*1.18f;
    auto const fontScale = io.FontGlobalScale;
    initializeSharedLegacyUiFont(context);
    check(sharedLegacyUiFontReady(context), "embedded Praxis font appended");
    check(io.FontDefault == hostFont && io.Fonts->Fonts.Size == 2, "host default and font retained");
    check(io.Fonts->Sources.back().FontDataOwnedByAtlas, "appended font owns resource copy");
    for (int i=1; i<io.Fonts->Sources.Size; ++i) {
        auto const& fontData = io.Fonts->Sources[i];
        check(fontData.FontDataOwnedByAtlas && fontData.GlyphRanges
              && reinterpret_cast<std::uintptr_t>(fontData.GlyphRanges)
                  >= reinterpret_cast<std::uintptr_t>(fontData.FontData)+fontData.FontDataSize,
              "shared font ranges live with owned font data, not in companion DLL");
    }
    unsigned char* pixels{}; int atlasWidth{}, atlasHeight{};
    io.Fonts->GetTexDataAsRGBA32(&pixels, &atlasWidth, &atlasHeight);
    check(pixels && atlasWidth>0 && atlasHeight>0, "shared atlas builds");
    check(io.Fonts->Fonts.back()->FontSize == 18, "exact 18px Praxis font");
    int const sourcesBefore=io.Fonts->Sources.Size;
    if(viewport.x==2048 && hostScale==2)std::printf("Japanese shared atlas family=%d width=%d height=%d sources=%d\n",family,atlasWidth,atlasHeight,sourcesBefore);
    check(io.Fonts->Fonts.back()->FindGlyphNoFallback(0x65E5), "Japanese glyph preserved");
    sharedFontRequired.store(true);
    std::vector<int> pages{-12,-11};
    for(int page=-9;page<static_cast<int>(cards.size());++page)pages.push_back(page);
    for (int page:pages) {
        legacyGuiState = {}; legacyGuiState.selected = page;
        for (int repeat=0; repeat<3; ++repeat) {
            if(page==-12&&repeat>0)
                for(auto* window:context->Windows)
                    if(std::string_view(window->Name).find("##praxis_content")!=std::string_view::npos)
                        ImGui::SetScrollY(window,repeat==1?window->ScrollMax.y:0.f);
            if(page>=0 && cards[page].feature==Feature::InventorySort && repeat>0)
                for(auto* window:context->Windows)
                    if(std::string_view(window->Name).find("##praxis_content")!=std::string_view::npos)
                        for(auto label:{"手持ちの除外","収納の除外"})
                            window->StateStorage.SetInt(window->GetID(label),1);

            ImGui::NewFrame();
            auto* const previousFont = ImGui::GetFont();
            if (page == static_cast<int>(Feature::WeatherChanger) && repeat > 0) {
                for (auto* window : context->Windows)
                    if (std::string_view(window->Name).find("##praxis_content") != std::string_view::npos)
                        window->StateStorage.SetInt(window->GetID("実機確認用の診断"), 1);
            }
            if(page==-8) {
                testConfig.informationOptions.inspector={true,true,true,true,true};
                for(auto* window:context->Windows)
                    if(std::string_view(window->Name).find("##praxis_content")!=std::string_view::npos)
                        window->StateStorage.SetBool(window->GetID("見ている対象・端末の情報"),true);
            }
            drawPraxisContent(context);
            if(page==-4)for(auto const& probe:unifiedHotkeyUiProbes){
                check(!probe.visible||probe.withinClip,"visible hotkey action buttons stay inside their content column at every viewport/font/scale");
                check(probe.contextTop>=probe.nameBottom-.01f&&
                      (probe.conflictTop<0||probe.conflictTop>=probe.contextBottom-.01f),"hotkey name, context and conflict descriptions do not overlap");
                if(probe.group==0){
                    check((ownedFeatureSlotMask&featureRegistryBit(probe.id))!=0,"reserved slots never appear as assignment rows");
                    check(probe.category==(probe.id==20||probe.id==21||probe.id==26?2:1),"camera activation stays in action category without changing persisted group zero");
                }
            }
            if(page==static_cast<int>(Feature::ToolProtection)&&repeat==2) {
                auto const& probe=toolPercentLabelProbe;
                check(probe.window&&probe.widgetId==probe.window->GetID("耐久率の下限 (%)"),
                      "short percentage label retains the previous exact slider ImGui ID");
                if(viewport.x==640) {
                    check(probe.labelRight<=probe.clipRight+.01f,
                          "percentage threshold label fits inside compact content clip");
                    if(hostScale==1)std::printf("ToolProtection compact family=%d old_label=%.1f short_label=%.1f available=%.1f slider_id=%08X PASS\n",
                        family,probe.legacyWidth,probe.visibleWidth,probe.availableWidth,probe.widgetId);
                }
            }
            if (page == static_cast<int>(Feature::WeatherChanger) && repeat > 0) {
                bool diagnosticsOpen{};
                for (auto* window : context->Windows)
                    if (window->Active && std::string_view(window->Name).find("##praxis_content") != std::string_view::npos)
                        diagnosticsOpen = window->StateStorage.GetInt(window->GetID("実機確認用の診断")) == 1;
                check(diagnosticsOpen, "production diagnostic panel expanded for UI verification");
            }
            check(ImGui::GetCurrentContext()==context, "borrowed context restored");
            check(io.FontDefault==hostFont && io.FontGlobalScale==fontScale
                  && ImGui::GetFont()==previousFont, "host font and scale restored");
            auto const& restored = ImGui::GetStyle();
            check(restored.WindowPadding.x==hostStyle.WindowPadding.x
                  && restored.FramePadding.y==hostStyle.FramePadding.y
                  && restored.ItemSpacing.x==hostStyle.ItemSpacing.x, "host spacing restored");
            check(context->CurrentWindowStack.Size==1 && context->FontStack.empty()
                  && context->ColorStack.empty() && context->StyleVarStack.empty(), "Praxis scopes balanced");
            check(context->ErrorCountCurrentFrame==0, "no ImGui errors");
            if(page==-12){
                auto* content=activeWindow(context,"##praxis_content");
                check(content&&content->ScrollMax.y>=std::max(0.f,content->ContentSize.y-content->InnerRect.GetHeight())-1.f,
                      "pack proposal page preserves vertical scroll access");
                if(content->DC.CursorMaxPos.x>content->WorkRect.Max.x+1.f)
                    std::fprintf(stderr,"Pack page width: viewport=%.0fx%.0f font=%d workWidth=%.2f cursorX=%.2f workMax=%.2f\n",
                        viewport.x,viewport.y,family,content->WorkRect.GetWidth(),content->DC.CursorMaxPos.x,content->WorkRect.Max.x);
                check(content->DC.CursorMaxPos.x<=content->WorkRect.Max.x+1.f,
                      "pack proposal text and controls fit compact content width");
            }
            check(io.Fonts->Sources.Size==sourcesBefore && io.Fonts->TexWidth==atlasWidth && io.Fonts->TexHeight==atlasHeight,
                  "Japanese UI does not enlarge or reload the existing atlas");
            auto* sidebar = ImGui::FindWindowByName("Praxis Client###praxis_client/##praxis_sidebar_50A26670");
            if (!sidebar) {
                for (auto* candidate:context->Windows)
                    if (std::string_view(candidate->Name).find("##praxis_sidebar")!=std::string_view::npos)
                        sidebar=candidate;
            }
            check(sidebar && sidebar->FontWindowScale==1.f, "Praxis sidebar has original font scale");
            {
                LegacyUiScope scope;
                check(ImGui::GetFontSize()==18.f, "logical font unaffected by host scale");
                check(ImGui::GetFontSize()+2*ImGui::GetStyle().FramePadding.y<=31.f,
                      "category and page button heights contain font and padding");
                for (char const* label : {"設計図", "表示設定", "保存設定", "HUD", "HUD配置", "キー割当", "カメラ", "リソースパック", "このModについて"})
                    check(ImGui::CalcTextSize(label).x + 2*ImGui::GetStyle().FramePadding.x
                          <= sidebar->WorkRect.GetWidth(), "sidebar label fits button");
            }
            if (page==-1 && repeat==2) {
                unsigned cardsDrawn{};
                for (auto* window:context->Windows) {
                    if (!window->Active) continue;
                    auto const name=std::string_view(window->Name);
                    if (name.find("##module_card_")!=std::string_view::npos
                        && name.find("##module_title")==std::string_view::npos
                        && name.find("##description")==std::string_view::npos) {
                        ++cardsDrawn;
                        check(window->DC.CursorMaxPos.y<=window->InnerRect.Max.y,
                              "card controls remain inside their original bounds");
                    }
                    if (name.find("##module_title")!=std::string_view::npos
                        ||name.find("##description")!=std::string_view::npos)
                        check(window->DC.CursorMaxPos.y<=window->InnerRect.Max.y,
                              "card title and description are not clipped vertically");
                }
                check(cardsDrawn==cards.size(),"all registered utility cards rendered");
                if (viewport.y<=480)
                    check(sidebar->ScrollMax.y>0,"compact viewport retains scroll access to categories and favorites");
            }
            ImGui::Render(); ++frames;
            if(page==-11||(page>=0&&(cards[page].feature==Feature::Freelook||cards[page].feature==Feature::FreeCamera)))
                checkCameraBounds(context);
            auto* data = ImGui::GetDrawData();
            check(data && data->Valid && data->TotalVtxCount>0, "real ImGui draw data emitted");
            if(capture&&page==-12&&repeat==2)saveRender("praxis-ui-resourcepacks.ppm",data,int(viewport.x),int(viewport.y));
            for (auto* list:data->CmdLists) {
                for (auto const& v:list->VtxBuffer)
                    check(std::isfinite(v.pos.x)&&std::isfinite(v.pos.y), "finite rendered geometry");
            }
            if (capture && page==-1 && repeat==2)
                saveRender("praxis-ui-modules.ppm",data,static_cast<int>(viewport.x),static_cast<int>(viewport.y));
            if (capture && page==-8 && repeat==2)
                saveRender("praxis-ui-hud-bundle.ppm",data,static_cast<int>(viewport.x),static_cast<int>(viewport.y));
            if (capture && page==0 && repeat==2)
                saveRender("praxis-ui-norender.ppm",data,static_cast<int>(viewport.x),static_cast<int>(viewport.y));
            if (capture && page==7 && repeat==2)
                saveRender("praxis-ui-timechanger.ppm",data,static_cast<int>(viewport.x),static_cast<int>(viewport.y));
            if (capture && page==static_cast<int>(Feature::WeatherChanger) && repeat==2)
                saveRender("praxis-ui-weather-diagnostics.ppm",data,static_cast<int>(viewport.x),static_cast<int>(viewport.y));
            if(capture && page>=8 && repeat==2){
                auto file=std::string("praxis-ui-")+featureNames[static_cast<unsigned>(cards[page].feature)]+".ppm";
                saveRender(file.c_str(),data,static_cast<int>(viewport.x),static_cast<int>(viewport.y));
            }
            if(capture&&repeat==2&&(page==-9||page==-4))
                saveRender(page==-9?"praxis-ui-layout.ppm":"praxis-ui-hotkeys.ppm",data,static_cast<int>(viewport.x),static_cast<int>(viewport.y));
            if(capture&&repeat==2&&page==-11)
                saveRender("praxis-ui-camera.ppm",data,static_cast<int>(viewport.x),static_cast<int>(viewport.y));
            if(capture&&repeat==2) {
                auto file=std::string("praxis-ui-ja-page-")+std::to_string(page)+".ppm";
                saveRender(file.c_str(),data,static_cast<int>(viewport.x),static_cast<int>(viewport.y));
            }
            if(family==0&&hostScale==1&&viewport.x==640&&repeat==2&&
               (page==-9||page==-4||page>=8)) {
                auto file=std::string("praxis-ui-compact-page-")+std::to_string(page)+".ppm";
                saveRender(file.c_str(),data,640,480);
            }
        }
    }
    if(family==0&&viewport.x==1280&&hostScale==1){
        ImGui::NewFrame();ImGui::Begin("Pack apply safety fixture");
        packs::PackPlan plan;plan.validation=packs::ApplyResult::Valid;plan.currentKnown=true;plan.proposed.push_back({"a",packs::ProposalChange::Enable});
        drawResourcePackApplyControls(plan);
        check((context->LastItemData.ItemFlags&ImGuiItemFlags_Disabled)!=0,
              "actual rebuild control disabled even with a valid proposal and known fixture state");
        auto* window=ImGui::GetCurrentWindow();
        ImGui::ActivateItemByID(window->GetID("選択を適用"));
        ImGui::End();ImGui::Render();++frames;
        ImGui::NewFrame();ImGui::Begin("Pack apply safety fixture");check(drawResourcePackApplyControls(plan)==ResourcePackAction::None,"default/world backend rejects synthetic Apply");
        check((context->LastItemData.ItemFlags&ImGuiItemFlags_Disabled)!=0,
              "synthetic activation cannot enable a native handler");
        ImGui::End();ImGui::Render();++frames;
        packs::TitlePackView title;title.available=true;title.current.known=true;
        ImGui::NewFrame();ImGui::Begin("Pack title apply fixture");drawResourcePackApplyControls(plan,title);
        window=ImGui::GetCurrentWindow();ImGui::FocusWindow(window);ImGui::ActivateItemByID(window->GetID("選択を適用"));
        ImGui::End();ImGui::Render();++frames;
        ImGui::NewFrame();ImGui::Begin("Pack title apply fixture");
        check(drawResourcePackApplyControls(plan,title)==ResourcePackAction::Apply,"eligible title control dispatches Apply request");
        check((context->LastItemData.ItemFlags&ImGuiItemFlags_Disabled)!=0,"same-stack reconstruction stays disabled");
        ImGui::End();ImGui::Render();++frames;
        title.canReload=true;
        ImGui::NewFrame();ImGui::Begin("Pack world reload fixture");drawResourcePackApplyControls(plan,title);
        window=ImGui::GetCurrentWindow();ImGui::FocusWindow(window);ImGui::ActivateItemByID(window->GetID("選択した構成を再読込"));
        ImGui::End();ImGui::Render();++frames;
        ImGui::NewFrame();ImGui::Begin("Pack world reload fixture");
        auto action=drawResourcePackApplyControls(plan,title);
        check(action==ResourcePackAction::Reload,"actual world reload click preserves distinct Reload intent");
        packs::TitlePackControl control;title.until=100;control.publish(title);
        auto catalog=std::make_shared<packs::Catalog>();catalog->complete=true;catalog->generation=1;
        catalog->packs.push_back({"a","a","uuid","1.0.0","","texture",true});
        packs::Selection selection;selection.generation=1;selection.order={"a"};
        check(control.requestResult(catalog,selection,10,action==ResourcePackAction::Reload).accepted,"real clicked Reload reaches copied request queue");
        auto request=control.take();check(request&&request->reload,"GUI Reload never collapses to Apply on dequeue");
        ImGui::End();ImGui::Render();++frames;
        plan.proposed.clear();
        ImGui::NewFrame();ImGui::Begin("Pack empty proposal fixture");drawResourcePackApplyControls(plan,title);
        window=ImGui::GetCurrentWindow();ImGui::FocusWindow(window);ImGui::ActivateItemByID(window->GetID("選択を適用"));
        ImGui::End();ImGui::Render();++frames;
        ImGui::NewFrame();ImGui::Begin("Pack empty proposal fixture");
        check(drawResourcePackApplyControls(plan,title)==ResourcePackAction::None,"empty unchanged proposal cannot trigger a native cycle through Apply");
        ImGui::End();ImGui::Render();++frames;
        plan.proposed.push_back({"a",packs::ProposalChange::Enable});
        title.busy=true;
        ImGui::NewFrame();ImGui::Begin("Pack busy reload fixture");drawResourcePackApplyControls(plan,title);
        window=ImGui::GetCurrentWindow();ImGui::FocusWindow(window);ImGui::ActivateItemByID(window->GetID("選択した構成を再読込"));
        ImGui::End();ImGui::Render();++frames;
        ImGui::NewFrame();ImGui::Begin("Pack busy reload fixture");
        check(drawResourcePackApplyControls(plan,title)==ResourcePackAction::None,"synthetic Reload cannot bypass busy native admission");
        ImGui::End();ImGui::Render();++frames;
    }
    {
        packs::PackPlan plan;plan.validation=packs::ApplyResult::Valid;plan.proposed.push_back({"a",packs::ProposalChange::Enable});
        packs::TitlePackView world;world.inWorld=true;world.available=true;world.current.known=true;world.canReload=true;
        ImGui::NewFrame();ImGui::Begin("Native Settings world route fixture");
        check(drawResourcePackApplyControls(plan,world)==ResourcePackAction::None,"world controls wait for explicit input");
        auto* window=ImGui::GetCurrentWindow();ImGui::FocusWindow(window);ImGui::ActivateItemByID(window->GetID("現在の有効構成を再読込"));
        ImGui::End();ImGui::Render();++frames;
        plan.proposed.clear();plan.validation=packs::ApplyResult::StaleCatalog;
        ImGui::NewFrame();ImGui::Begin("Native Settings world route fixture");
        check(drawResourcePackApplyControls(plan,world)==ResourcePackAction::Reload,"current-active world reload remains independent of empty or stale edited candidate");
        ImGui::End();ImGui::Render();++frames;
    }
    testCameraNavigation(context,family==0&&viewport.x==640&&hostScale==2);
    if(family==0&&viewport.x==640&&hostScale==2)testHotkeyGapNavigation(context);
    if(family==0&&viewport.x==640&&hostScale==2)testInventoryActionNavigation(context);
    testCameraStatusPages(context,capture);
    auto const mapBefore=mapCalls, infoBefore=infoCalls, previewBefore=previewCalls;
    ImGui::NewFrame(); drawChunkHudContent(context); ImGui::Render(); ++frames;
    check(mapCalls==mapBefore+1 && infoCalls==infoBefore+1 && previewCalls==previewBefore+1,
          "shared HUD reaches Map, Information and Shulker consumers");
    check(context->FontStack.empty() && io.FontGlobalScale==fontScale, "HUD font scope restored");
    // A later host atlas rebuild must continue to work with atlas-owned copies.
    io.Fonts->ClearTexData(); check(io.Fonts->Build(), "host atlas rebuild after companion registration");
    sharedFontRequired.store(false);
    ImGui::DestroyContext(context);
}
void testStandaloneFont(int family) {
    auto* context=ImGui::CreateContext();
    auto& io=ImGui::GetIO();
    io.IniFilename=nullptr; io.LogFilename=nullptr;
    io.DisplaySize={2048,1152}; io.DeltaTime=1.f/60.f;
    testConfig={}; testConfig.guiStyle.fontFamily=family;
    check(configureLegacyUiFont(static_cast<LegacyUiFontFamily>(family),18.f),"standalone embedded font loads");
    unsigned char* pixels{}; int width{},height{};
    io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    check(pixels&&io.FontDefault->FontSize==18,"standalone exact 18px atlas");
    checkJapaneseGlyphs(io.FontDefault);
    std::printf("Japanese standalone atlas family=%d width=%d height=%d fonts=%d sources=%d\n",family,width,height,io.Fonts->Fonts.Size,io.Fonts->Sources.Size);
    check(!sharedLegacyUiFontReady(context),"standalone does not reuse retired shared font cache");
    auto* font=io.FontDefault;
    legacyGuiState={};
    for (int repeat=0; repeat<3; ++repeat) {
        ImGui::NewFrame(); drawPraxisContent(context);
        check(ImGui::GetFont()==font&&context->FontStack.empty(),"standalone font scope preserved");
        ImGui::Render(); ++frames;
        if (family==0&&repeat==2)
            saveRender("praxis-ui-standalone.ppm",ImGui::GetDrawData(),2048,1152);
    }
    ImGui::DestroyContext(context);
}
std::atomic_bool closeEntered{}, closeAllowed{}, closeFinished{}, unregisterObservedFinished{}, stopFinished{};
void holdClose() noexcept {
    closeEntered.store(true);
    while (!closeAllowed.load()) std::this_thread::yield();
    closeFinished.store(true);
}
bool completeUnregister(void*) noexcept {
    unregisterObservedFinished.store(closeFinished.load()); return true;
}
bool rejectUnregister(void*) noexcept { return false; }
bool fakeRegistered(void*) noexcept { return true; }
bool fakeCaptured() noexcept { return true; }
void testBridgeCloseDrain() {
    registered=true;
    unregisterFn=completeUnregister;
    closeFn.store(holdClose);
    registeredFn.store(fakeRegistered);
    inputCapturedFn.store(fakeCaptured);
    bool stopResult{};
    std::thread closer([] { closeLholoBridge(); });
    auto const deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while (!closeEntered.load()&&std::chrono::steady_clock::now()<deadline) std::this_thread::yield();
    if (!closeEntered.load()) { closeAllowed.store(true); closer.join(); check(false,"close callback enters lease"); }
    std::thread stopper([&] { stopResult=stopLholoBridge(); stopFinished.store(true); });
    while (closeFn.load()!=nullptr&&std::chrono::steady_clock::now()<deadline) std::this_thread::yield();
    bool const waiting=!stopFinished.load()&&closeFn.load()==nullptr&&bridgeReaders.load()!=0;
    closeAllowed.store(true); closer.join(); stopper.join();
    check(waiting,"provider shutdown drains in-flight close callback");
    check(stopResult&&unregisterObservedFinished.load()&&bridgeReaders.load()==0,
          "provider unregistration occurs after close reader retires");
    registered=true; unregisterFn=rejectUnregister;
    closeFn.store(holdClose); registeredFn.store(fakeRegistered); inputCapturedFn.store(fakeCaptured);
    check(!stopLholoBridge()&&closeFn.load()==holdClose&&registeredFn.load()==fakeRegistered
          &&inputCapturedFn.load()==fakeCaptured,"failed provider unregister retains callback leases");
    unregisterFn=completeUnregister; check(stopLholoBridge(),"retained provider unregister retries safely");
}
void testMapGraphicsAdapters() {
    auto* device=reinterpret_cast<ID3D11Device*>(std::uintptr_t{0x1234});
    auto* context=reinterpret_cast<ID3D11DeviceContext*>(std::uintptr_t{0x5678});
    prepareSharedMapGraphics(device,context);
    check(map::graphicsCalls==1&&map::suppliedDevice==device&&map::suppliedContext==context,
          "shared frame forwards borrowed graphics to Map consumer");
    resetSharedMapGraphics();
    check(map::graphicsResets==1&&!map::suppliedDevice&&!map::suppliedContext,
          "provider graphics reset clears Map resource consumer");
}
std::atomic_bool providerPresent{}, queryEntered{}, queryAllowed{}, queryFinished{}, queryUnregisteredAfterDrain{};
void* expectedOwner{};
bool queryProvider(void* owner) noexcept { return owner==expectedOwner&&providerPresent.load(); }
bool holdQuery(void* owner) noexcept {
    queryEntered.store(true);
    while (!queryAllowed.load()) std::this_thread::yield();
    queryFinished.store(true);
    return owner==expectedOwner;
}
bool unregisterAfterQuery(void*) noexcept {
    queryUnregisteredAfterDrain.store(queryFinished.load()); return true;
}
void testBridgeRegistrationQuery() {
    check(!lholoBridgeRegistered(),"unregistered provider query is false");
    unsigned ownerToken{};
    expectedOwner=&ownerToken;
    bridgeOwner.store(reinterpret_cast<HMODULE>(expectedOwner));
    registeredFn.store(queryProvider);
    providerPresent.store(true);
    check(lholoBridgeRegistered(),"current owner registration is queried");
    providerPresent.store(false);
    check(!lholoBridgeRegistered(),"provider callback registration disappearance is detected");
    registeredFn.store(holdQuery); registered=true; unregisterFn=unregisterAfterQuery;
    bool queryResult{}, stopResult{};
    stopFinished.store(false);
    std::thread reader([&] { queryResult=lholoBridgeRegistered(); });
    auto const deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while (!queryEntered.load()&&std::chrono::steady_clock::now()<deadline) std::this_thread::yield();
    if (!queryEntered.load()) { queryAllowed.store(true); reader.join(); check(false,"registration query enters lease"); }
    std::thread stopper([&] { stopResult=stopLholoBridge(); stopFinished.store(true); });
    while (registeredFn.load()!=nullptr&&std::chrono::steady_clock::now()<deadline) std::this_thread::yield();
    bool const waiting=!stopFinished.load()&&registeredFn.load()==nullptr&&bridgeReaders.load()!=0;
    bool const retired=registeredFn.load()==nullptr&&!lholoBridgeRegistered();
    queryAllowed.store(true); reader.join(); stopper.join();
    check(waiting&&retired,"shutdown stops new registration queries and drains the in-flight query");
    check(queryResult&&stopResult&&queryUnregisteredAfterDrain.load()&&bridgeReaders.load()==0,
          "provider unregister happens after registration query finishes");
    check(!lholoBridgeRegistered()&&!bridgeOwner.load(),"retired provider query has no stale owner or callback");
}
#include "GuiUsabilityCases.inc"
#include "HudOrganizationCases.inc"
void testCameraSingleKeyHotkeyUi() {
    auto* context=ImGui::CreateContext();auto& io=ImGui::GetIO();
    io.IniFilename=nullptr;io.LogFilename=nullptr;io.DisplaySize={1280,720};io.DeltaTime=1.f/60;
    check(configureLegacyUiFont(LegacyUiFontFamily::CorporateLogo,18.f),"camera hotkey test uses the product Japanese font");
    unsigned char* pixels{};int width{},height{};io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    unifiedHotkeyTestKeys={};unifiedHotkeyTestOwner=true;
    for(unsigned id=0;id<16;++id){auto& row=usabilityForeignKeys[id];row={};row.size=sizeof(row);row.id=id;row.contexts=1;std::snprintf(row.label,sizeof(row.label),"LHolo input %u",id);}
    usabilityForeignKeys[12].key='G';usabilityForeignKeys[12].modifiers=2; // native LHolo Alt -> canonical4
    unifiedHotkeyTestApi={sizeof(HudControlApiV1),1,usabilityForeignCount,usabilityForeignRead,usabilityForeignWrite,usabilityForeignReadLayout,usabilityForeignWriteLayout};
    auto draw=[&] {
        bool changed=false;std::uint32_t edits{};ImGui::NewFrame();
        ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({1280,720});ImGui::Begin("Camera input comparison");
        ImGui::LogToBuffer();drawUnifiedHotkeys(testConfig,changed,edits);
        std::string text=context->LogBuffer.c_str();ImGui::LogFinish();ImGui::End();ImGui::Render();
        check(!changed&&!edits,"hotkey display and conflict projection never rewrite saved bindings");return text;
    };
    for(unsigned modifiers:{0u,1u,8u}) {
        testConfig={};testConfig.keybinds[20]='G';testConfig.keybindModifiers[20]=modifiers;
        testConfig.keybinds[21]='F';testConfig.keybindModifiers[21]=modifiers;
        testConfig.keybinds[0]='G';testConfig.keybindModifiers[0]=4;testConfig.zoomKey='F';testConfig.zoomModifiers=4;
        std::string text;for(int i=0;i<3;++i)text=draw();
        for(auto pair:{std::pair{0,20},std::pair{0,21},std::pair{0,0},std::pair{3,12},std::pair{1,0}}) {
            auto found=std::find_if(unifiedHotkeyUiProbes.begin(),unifiedHotkeyUiProbes.end(),[&](auto const& p){return p.group==pair.first&&p.id==pair.second;});
            check(found!=unifiedHotkeyUiProbes.end(),"camera/normal/Zoom/LHolo bindings remain represented in the actual UI");
            check((found->conflictTop>=0)==(modifiers!=1||pair==std::pair{0,0}||pair==std::pair{3,12}),"UI conflict projection matches camera0/Any8 and preserves exact camera chords");
        }
        check(testConfig.keybindModifiers[20]==modifiers&&testConfig.keybindModifiers[21]==modifiers,"camera UI leaves persisted0/1/8 unchanged");
        if(modifiers==0)check(text.find("単キー（修飾キー不問）")!=std::string::npos,"camera0 UI explains its single-key modifier requirement");
    }
    unifiedHotkeyTestApi={};unifiedHotkeyTestKeys={};cancelUnifiedHotkeyCapture();ImGui::DestroyContext(context);
}

}
int main() {
    try {
        check(cameraStatusJa("OFF")!=cameraStatusJa("OFF; camera restored"),
              "camera display retains the distinction between inactive and successfully restored");
        check(cameraStatusJa("Future camera diagnostic: detail=123")=="Future camera diagnostic: detail=123",
              "unknown native camera diagnostics remain intact at presentation");
        check(std::string_view(featureDisplayName("FutureCameraModule"))=="FutureCameraModule",
              "new aliases do not change unknown stable module names");
        check(cards[7].feature==Feature::TimeChanger&&cards[8].feature==Feature::WeatherChanger&&cards[9].feature==Feature::Hitbox,
              "TimeChanger/Weather favorite cards retained; Hitbox appends at nine");
        check(cards.size()==19&&cards[15].feature==Feature::Freelook&&cards[16].feature==Feature::FreeCamera&&cards[17].feature==Feature::AntiEffect&&cards[18].feature==Feature::OffhandSwap,
              "OffhandSwap appends without reordering camera cards");
        for(auto name:{"NoRender","TimeChanger","WeatherChanger"})
            check(std::string_view(featureDisplayName(name))==name,"requested English feature names retained");
        for(unsigned i=0;i<kRenderPassCount;++i)
            check(std::string_view(renderPassDisplayNames[i])==kRenderPassLabels[i],"NoRender choices use the canonical English names");
        check(enabledField(testConfig,Feature::InventoryHUD)==nullptr,"retired InventoryHUD has no UI toggle");
        for(auto const& card:cards)check((ownedFeatureSlotMask&featureRegistryBit(static_cast<unsigned>(card.feature)))!=0,
              "reserved slots never form a module card");
        testCameraSingleKeyHotkeyUi();
        for (int family=0; family<3; ++family) testStandaloneFont(family);
        auto const packUiFixture=preparePackUiFixture();
        for (int family=0; family<3; ++family)
            for (auto viewport : {ImVec2{480,800},ImVec2{640,480},ImVec2{1280,720},
                                  ImVec2{2048,1152},ImVec2{3840,2160}})
                for (float scale : {1.f,2.f,5.f})
                    renderCase(viewport,family,scale,family==0&&viewport.x==2048&&scale==2);
        testBridgeCloseDrain();
        testMapGraphicsAdapters();
        testBridgeRegistrationQuery();
        testGuiOpeningAndHotkeys();
        testHudOrganization();
        check(packs::stopBrowser(),"offline UI fixture worker retires");
        check(packUiFixture.is_absolute()&&packUiFixture.filename().string().starts_with("PraxisPackUi-")
              &&std::filesystem::equivalent(packUiFixture.parent_path(),std::filesystem::temp_directory_path()),
              "owned fixture cleanup stays inside test TEMP");
        std::filesystem::remove_all(packUiFixture);
        std::ifstream shared("praxis-ui-modules.ppm",std::ios::binary), standalone("praxis-ui-standalone.ppm",std::ios::binary);
        std::string const sharedPixels{std::istreambuf_iterator<char>{shared},{}};
        std::string const standalonePixels{std::istreambuf_iterator<char>{standalone},{}};
        check(!sharedPixels.empty()&&sharedPixels==standalonePixels,
              "shared and standalone Corporate Logo UI pixels match at 2048x1152");
        check(mapCalls>0,"Map settings page connected");
        check(cameraStatusCalls>0,"camera page reaches the native status API double");
        std::printf("Praxis real ImGui checks=%u frames=%u font families=3 viewports=5 host scales=3\n",checks,frames);
        std::puts("Camera/inventory controls and 19-module bindings: synthetic ImGui navigation; Minecraft NOT_RUN");
        return 0;
    } catch (std::exception const& e) {
        std::fprintf(stderr,"Praxis UI failed after %u frames: %s\n",frames,e.what()); return 1;
    }
}
