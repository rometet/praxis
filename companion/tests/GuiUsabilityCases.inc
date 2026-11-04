// Executed by the production ImGui renderer with owned input and optional ABI
// fixtures. No operating-system key or mouse injection is performed.
std::array<HudControlHotkeyV1,16> usabilityForeignKeys{};
unsigned usabilityForeignWrites{},usabilityForeignLastId{},usabilityForeignLastModifiers{};
uint32_t __cdecl usabilityForeignCount(){return 16;}
uint32_t __cdecl usabilityForeignRead(uint32_t id,HudControlHotkeyV1* out){
    if(id>=16||!out||out->size!=sizeof(*out))return 0;*out=usabilityForeignKeys[id];return 1;
}
uint32_t __cdecl usabilityForeignWrite(uint32_t id,uint32_t key,uint32_t modifiers,uint32_t operation){
    if(id>=16||key>255||modifiers>7||operation>2)return 0;
    ++usabilityForeignWrites;usabilityForeignLastId=id;usabilityForeignLastModifiers=modifiers;
    usabilityForeignKeys[id].key=operation==0?key:0;usabilityForeignKeys[id].modifiers=operation==0?modifiers:0;return 1;
}
uint32_t __cdecl usabilityForeignReadLayout(uint32_t id,HudControlLayoutV1* out){if(id>1||!out)return 0;*out={sizeof(*out),id,0,0,0,0,1};return 1;}
uint32_t __cdecl usabilityForeignWriteLayout(HudControlLayoutV1 const*,uint32_t){return 1;}
void testGuiOpeningAndHotkeys() {
    auto* context=ImGui::CreateContext();auto& io=ImGui::GetIO();
    io.IniFilename=nullptr;io.LogFilename=nullptr;io.DisplaySize={1280,720};io.DeltaTime=1.f/60.f;
    testConfig={};testConfig.keybinds.resize(32);testConfig.keybindModifiers.resize(32,8);
    testConfig.keybinds[31]='Z';testConfig.keybindModifiers[31]=4;
    // Keep the conflict-only fixture focused on the legacy Praxis/LHolo pair;
    // the appended FlexiblePlacement feature is covered by its own rows.
    testConfig.keybinds[static_cast<unsigned>(Feature::FlexibleBlockPlacement)]=0;
    check(configureLegacyUiFont(LegacyUiFontFamily::CorporateLogo,18.f),"interaction font loaded");
    unsigned char* pixels{};int width{},height{};io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    unifiedHotkeyTestKeys={};unifiedHotkeyTestOwner=true;unifiedHotkeyTestApi={};
    auto frame=[&] {
        ImGui::NewFrame();drawPraxisContent(context);
        check(context->ErrorCountCurrentFrame==0,"interactive production ImGui has no errors");
        check(context->CurrentWindowStack.Size==1&&context->ColorStack.empty()&&context->StyleVarStack.empty(),"interaction UI scopes balanced");
        for(auto const& p:unifiedHotkeyUiProbes){
            check(!p.visible||p.withinClip,"interactive buttons do not clip");
            check(p.contextTop>=p.nameBottom-.01f&&(p.conflictTop<0||p.conflictTop>=p.contextBottom-.01f),"interactive conflict descriptions cannot overlap their name or context");
        }
        ImGui::Render();++frames;
    };
    // Both host routes call this public transition function. A duplicated true
    // callback and an ordinary redraw are focus/visibility observations, not an
    // instruction to change an internal page.
    for(auto host:{0,1}) {
        notifyPraxisGuiVisibility(false);legacyGuiState.selected=-4;
        if(host==0)onVisibility(true);else notifyPraxisGuiVisibility(true);
        frame();check(legacyGuiState.selected==-1,"shared and standalone real opens select module list");
        legacyGuiState.selected=-9;frame();check(legacyGuiState.selected==-9,"internal HUD route survives redraw");
        if(host==0)onVisibility(true);else notifyPraxisGuiVisibility(true);
        frame();check(legacyGuiState.selected==-9,"duplicate visibility/focus return preserves internal route");
        notifyPraxisGuiVisibility(false);legacyGuiState.selected=-7;
        notifyPraxisGuiVisibility(true);frame();check(legacyGuiState.selected==-1,"reopen from Map returns to modules");
        legacyGuiState.selected=-11;frame();check(legacyGuiState.selected==-11,"direct Camera route survives redraw");
        notifyPraxisGuiVisibility(true);frame();check(legacyGuiState.selected==-11,"duplicate visibility preserves direct Camera route");
        notifyPraxisGuiVisibility(false);notifyPraxisGuiVisibility(true);frame();
        check(legacyGuiState.selected==-1,"real reopen from Camera selects modules");
    }
    legacyGuiState.selected=-4;frame();frame();
    auto probeFor=[&](int group,int id) {
        for(auto const& p:unifiedHotkeyUiProbes)if(p.group==group&&p.id==id&&p.visible)return p;
        throw std::runtime_error("required interactive hotkey row not visible");
    };
    auto click=[&](UnifiedHotkeyUiProbe p,int action) {
        float x=action==0?p.assignX:action==1?p.clearX:p.resetX;
        float y=action==0?p.assignY:action==1?p.clearY:p.resetY;
        io.AddMousePosEvent(x,y);io.AddMouseButtonEvent(0,true);frame();
        io.AddMouseButtonEvent(0,false);frame();frame();
    };
    click(probeFor(0,0),0);check(unifiedHotkeyTestCapturing(),"actual Assign button opens capture");
    unifiedHotkeyTestKeys[VK_F10]=true;frame();check(unifiedHotkeyTestCapturing()&&testConfig.keybinds[0]==0,"F10 stays reserved during capture");
    unifiedHotkeyTestKeys[VK_F10]=false;unifiedHotkeyTestKeys[VK_F11]=true;frame();
    check(unifiedHotkeyTestCapturing()&&testConfig.keybinds[0]==0,"F11 stays reserved during capture");
    unifiedHotkeyTestKeys={};unifiedHotkeyTestKeys[VK_ESCAPE]=true;frame();
    check(!unifiedHotkeyTestCapturing()&&testConfig.keybinds[0]==0,"Esc cancels the actual modal without editing");
    unifiedHotkeyTestKeys={};frame();frame();
    click(probeFor(0,0),0);unifiedHotkeyTestKeys[VK_CONTROL]=true;unifiedHotkeyTestKeys['K']=true;frame();
    check(testConfig.keybinds[0]=='K'&&testConfig.keybindModifiers[0]==1,"production capture writes Ctrl+K");
    check(lastBindingEdits==featureRegistryBit(0),"capture marks only the compiled target's edit bit");
    check(testConfig.keybinds[31]=='Z'&&testConfig.keybindModifiers[31]==4,"capture retains unknown future slot and modifiers");
    unifiedHotkeyTestKeys={};frame();frame();click(probeFor(0,0),1);
    check(testConfig.keybinds[0]==0,"actual Clear button removes only the selected binding");
    testConfig.keybinds[0]='J';frame();click(probeFor(0,0),2);
    check(testConfig.keybinds[0]==RuntimeConfig{}.keybinds[0],"actual Reset restores the selected default");
    click(probeFor(0,0),0);unifiedHotkeyTestOwner=false;unifiedHotkeyTestKeys['L']=true;frame();
    check(testConfig.keybinds[0]==0&&!unifiedHotkeyTestCapturing(),"capture owner loss cannot write a binding");
    unifiedHotkeyTestKeys={};unifiedHotkeyTestOwner=true;frame();frame();
    click(probeFor(0,0),0);cancelUnifiedHotkeyCapture();unifiedHotkeyTestKeys['L']=true;frame();
    check(testConfig.keybinds[0]==0&&!unifiedHotkeyTestCapturing(),"close-generation cancellation defeats a stale key edge");
    unifiedHotkeyTestKeys={};frame();frame();
    click(probeFor(0,0),0);notifyPraxisGuiVisibility(false);unifiedHotkeyTestKeys['L']=true;frame();
    check(testConfig.keybinds[0]==0&&!unifiedHotkeyTestCapturing(),"actual host close cancels capture before a stale key edge");
    unifiedHotkeyTestKeys={};notifyPraxisGuiVisibility(true);frame();
    check(legacyGuiState.selected==-1,"reopen after capture close returns to modules");
    legacyGuiState.selected=-4;frame();frame();
    constexpr char const* labels[]{"メニューを開く","左へ移動","右へ移動","前へ移動","後ろへ移動","上へ移動","下へ移動","レイヤーを上げる","レイヤーを下げる","投影を読み込む","投影を閉じる","手動設置を切替","配置一覧を開く","ファイルを開く","検証を開く","材料を開く"};
    for(unsigned id=0;id<16;++id){auto& key=usabilityForeignKeys[id];key={};key.size=sizeof(key);key.id=id;key.contexts=id==0||id>=12?3:1;std::snprintf(key.label,sizeof(key.label),"%s",labels[id]);}
    usabilityForeignKeys[12].key='K';usabilityForeignKeys[12].modifiers=2;
    unifiedHotkeyTestApi={sizeof(HudControlApiV1),1,usabilityForeignCount,usabilityForeignRead,usabilityForeignWrite,usabilityForeignReadLayout,usabilityForeignWriteLayout};
    testConfig.keybinds[0]='K';testConfig.keybindModifiers[0]=4;frame();frame();
    auto pointClick=[&](float x,float y){io.AddMousePosEvent(x,y);io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();frame();};
    pointClick(unifiedHotkeyHeaderProbe[2],unifiedHotkeyHeaderProbe[3]);
    check(unifiedHotkeyUiProbes.size()==2&&unifiedHotkeyUiProbes[0].group==0&&unifiedHotkeyUiProbes[1].group==3,
          "actual conflicts-only checkbox finds only the overlapping Praxis and LHolo native Alt chords");
    saveRender("praxis-ui-hotkeys-conflict-desktop.ppm",ImGui::GetDrawData(),1280,720);
    click(probeFor(3,12),0);unifiedHotkeyTestKeys[VK_SHIFT]=true;unifiedHotkeyTestKeys['P']=true;frame();
    check(usabilityForeignWrites==1&&usabilityForeignLastId==12&&usabilityForeignLastModifiers==4,
          "foreign Assign uses the owner's ID and converts canonical Shift to native LHolo bit4");
    check(testConfig.keybinds[0]=='K'&&testConfig.keybindModifiers[0]==4&&testConfig.keybinds[31]=='Z',
          "foreign edit leaves local and unknown profile bindings alone");
    unifiedHotkeyTestKeys={};frame();frame();check(unifiedHotkeyUiProbes.empty(),"resolved conflict disappears from filtered UI");
    pointClick(unifiedHotkeyHeaderProbe[2],unifiedHotkeyHeaderProbe[3]);
    pointClick(unifiedHotkeyHeaderProbe[0],unifiedHotkeyHeaderProbe[1]);io.AddInputCharactersUTF8("LHolo");frame();frame();
    check(unifiedHotkeyUiProbes.size()==16,"actual Search field finds all sixteen foreign hotkey rows");
    for(auto const& p:unifiedHotkeyUiProbes)check(p.group==3,"search excludes local and fixed rows");
    saveRender("praxis-ui-hotkeys-lholo-desktop.ppm",ImGui::GetDrawData(),1280,720);
    io.DisplaySize={640,480};
    for(auto* w:context->Windows)if(std::string_view(w->Name)=="Praxis Client###praxis_client"){w->Size={616,456};w->Pos={12,12};}
    frame();frame();saveRender("praxis-ui-hotkeys-lholo-compact.ppm",ImGui::GetDrawData(),640,480);
    io.AddKeyEvent(ImGuiKey_LeftCtrl,true);io.AddKeyEvent(ImGuiKey_A,true);frame();
    io.AddKeyEvent(ImGuiKey_A,false);io.AddKeyEvent(ImGuiKey_LeftCtrl,false);io.AddKeyEvent(ImGuiKey_Backspace,true);frame();
    io.AddKeyEvent(ImGuiKey_Backspace,false);frame();frame();
    unifiedHotkeyTestApi={};notifyPraxisGuiVisibility(false);frame();
    std::printf("UI usability: shared/standalone open, internal routes, actual Assign/Clear/Reset, Ctrl, Esc, reserved keys, ownership and unknown slot PASS\n");
    ImGui::DestroyContext(context);
}
