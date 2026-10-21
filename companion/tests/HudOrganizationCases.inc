// Production ImGui with isolated settings and borrowed-ABI doubles only.
// These inputs never reach the operating system or Minecraft.
void testHudOrganization() {
    auto* context=ImGui::CreateContext();auto& io=ImGui::GetIO();
    io.IniFilename=nullptr;io.LogFilename=nullptr;io.DisplaySize={640,480};io.DeltaTime=1.f/60;
    io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
    testConfig={};testConfig.inventoryHud=true;testConfig.keybinds.resize(32);testConfig.keybindModifiers.resize(32,8);
    testConfig.keybinds[23]='I';testConfig.keybindModifiers[23]=4;testConfig.keybinds[31]='Z';
    auto retiredLayout=HudTransform{true,77,23,2,8};testConfig.informationOptions.hudLayouts.inventory=retiredLayout;
    check(configureLegacyUiFont(LegacyUiFontFamily::CorporateLogo,18.f),"HUD test uses the product Japanese18px font");
    unsigned char* pixels{};int width{},height{};io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    legacyGuiState={};legacyGuiState.selected=-8;
    for(int i=0;i<3;++i)navigationFrame(context);
    auto* content=activeWindow(context,"##praxis_content");check(content,"HUD page uses the production content window");
    check(hudSettingsTestSections.size()==6,"all retained HUD groups and foreign editor route are shown");
    check(content->ScrollMax.y>0,"small HUD page retains native vertical scrolling");
    check(content->DC.CursorMaxPos.x<=content->WorkRect.Max.x+1,"HUD groups fit the compact Japanese UI");
    saveRender("praxis-ui-hud-640.ppm",ImGui::GetDrawData(),640,480);
    auto key=[&](ImGuiKey k){io.AddKeyEvent(k,true);navigationFrame(context);io.AddKeyEvent(k,false);navigationFrame(context);navigationFrame(context);};
    // Reach each existing toggle by Tab; activation must edit its original field.
    std::set<ImGuiID> toggles;
    for(auto label:{"情報HUDを表示","装備の耐久値HUDを表示","オフハンドHUDを表示","満腹度HUDを表示","チャンク情報HUDを表示"})
        toggles.insert(content->GetID(label));
    std::set<ImGuiID> reached;
    ImGui::FocusWindow(content);ImGui::SetFocusID(content->GetID("##hud_search"),content);
    for(int step=0;step<65&&reached.size()<toggles.size();++step) {
        key(ImGuiKey_Tab);
        if(!toggles.contains(context->NavId))continue;
        reached.insert(context->NavId);
        auto* win=context->NavWindow;auto rect=ImGui::WindowRectRelToAbs(win,win->NavRectRel[context->NavLayer]);
        check(rect.Min.y>=win->ClipRect.Min.y-1&&rect.Max.y<=win->ClipRect.Max.y+1,"focused HUD toggle remains visible after scrolling");
        if(context->NavId==content->GetID("オフハンドHUDを表示"))saveRender("praxis-ui-hud-640-scrolled.ppm",ImGui::GetDrawData(),640,480);
        key(ImGuiKey_Space);
    }
    check(reached==toggles,"all retained HUD toggles are keyboard reachable");
    check(testConfig.informationOptions.infoHud&&testConfig.informationOptions.durabilityHud
        &&testConfig.informationOptions.playerHud.offhandHud&&testConfig.informationOptions.playerHud.foodHud,
        "HUD toggles update the existing config fields");
    check(!testConfig.chunkBorder.showHud,"chunk HUD toggle updates its existing initially-ON field");
    // Focus and type into the actual InputText widget, then inspect its rendered groups.
    ImGui::FocusWindow(content);ImGui::SetFocusID(content->GetID("##hud_search"),content);key(ImGuiKey_Enter);
    io.AddInputCharactersUTF8("オフハンド");navigationFrame(context);navigationFrame(context);
    check(hudSettingsTestSections.size()==1&&hudSettingsTestSections.front()=="オフハンドHUD","Japanese text input filters the actual HUD page");
    saveRender("praxis-ui-hud-search-640.ppm",ImGui::GetDrawData(),640,480);
    io.AddKeyEvent(ImGuiMod_Ctrl,true);io.AddKeyEvent(ImGuiKey_LeftCtrl,true);key(ImGuiKey_A);
    io.AddKeyEvent(ImGuiMod_Ctrl,false);io.AddKeyEvent(ImGuiKey_LeftCtrl,false);key(ImGuiKey_Backspace);
    io.AddInputCharactersUTF8("Materials");navigationFrame(context);navigationFrame(context);
    check(hudSettingsTestSections.size()==1&&hudSettingsTestSections.front()=="設計図・材料HUD / HUD配置","English Materials alias finds the foreign HUD route");
    key(ImGuiKey_Escape);ImGui::FocusWindow(content);ImGui::SetFocusID(content->GetID("HUD配置を開く"),content);key(ImGuiKey_Enter);
    check(legacyGuiState.selected==-9,"actual HUD page button opens the product layout editor");
    navigationFrame(context);check(hudLayoutTestTargetCount==5,"layout editor omits retired inventory and retains five local panels");
    saveRender("praxis-ui-hud-layout-640.ppm",ImGui::GetDrawData(),640,480);
    content=activeWindow(context,"##praxis_content");ImGui::FocusWindow(content);ImGui::SetFocusID(content->GetID("全体を初期化"),content);key(ImGuiKey_Enter);
    ImGui::SetFocusID(content->GetID("保存"),content);key(ImGuiKey_Enter);
    check(testConfig.informationOptions.hudLayouts.inventory==retiredLayout,"layout reset/save preserves the retired stored transform");
    unifiedHotkeyTestApi={sizeof(HudControlApiV1),1,usabilityForeignCount,usabilityForeignRead,usabilityForeignWrite,usabilityForeignReadLayout,usabilityForeignWriteLayout};
    ImGui::SetFocusID(content->GetID("読込み"),content);key(ImGuiKey_Enter);
    navigationFrame(context);check(hudLayoutTestTargetCount==7,"connected Schematic and Materials layout targets remain available");
    unifiedHotkeyTestApi={};navigationFrame(context);
    legacyGuiState.selected=-4;navigationFrame(context);
    for(auto const& row:unifiedHotkeyUiProbes)check(row.group!=0||row.id!=23,"actual key assignment page excludes retired23");
    check(testConfig.inventoryHud&&testConfig.keybinds[23]=='I'&&testConfig.keybindModifiers[23]==4&&testConfig.keybinds[31]=='Z',
        "product GUI leaves legacy enable and reserved/future bindings untouched");
    notifyPraxisGuiVisibility(false);notifyPraxisGuiVisibility(true);navigationFrame(context);
    check(legacyGuiState.selected==-1,"GUI reopen from HUD editing still opens the module list");
    std::printf("HUD organization: keyboard, small viewport, scrolling, Japanese/English search, editor, foreign panels, retired data and reopen PASS; Minecraft NOT_RUN\n");
    ImGui::DestroyContext(context);
}
