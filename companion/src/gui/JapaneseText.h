#pragma once
#include "core/InformationPolicy.h"
#include <string_view>

namespace praxis::companion {
// Display-only aliases. Persisted feature names, IDs, order and policies stay
// with their existing owners. New modules can append a stable-name alias here.
inline constexpr char const* featureDisplayName(char const* stable) noexcept {
    constexpr std::pair<char const*,char const*> names[]{
        {"NoRender","NoRender"},{"ChunkBorders","チャンク境界"},
        {"Fullbright","明るさ固定"},{"ShulkerPreview","シュルカーの中身"},
        {"RedstoneOverlay","信号強度"},{"LightOverlay","光レベル"},
        {"Zoom","ズーム"},{"TimeChanger","TimeChanger"},
        {"WeatherChanger","WeatherChanger"},{"Hitbox","当たり判定の枠"},
        {"ToolProtection","道具の破損防止"},{"WeaponSwitch","武器の自動選択"},
        {"HandRestock","手持ちの自動補充"},{"InventorySort","所持品の整理"},
        {"InventoryTransfer","ホイール・ドラッグ移動"},
        {"AntiEffect","画面効果の抑制"},{"Freelook","フリールック"},{"FreeCamera","フリーカメラ"},{"OffhandSwap","オフハンド持ち替え"}};
    for(auto const& [key,label]:names)if(std::string_view(stable)==key)return label;
    return stable;
}
inline constexpr std::array<char const*,6> hudDisplayNames{
    "情報","装備の耐久値","オフハンド","満腹度","チャンク情報","所持品"};
inline constexpr std::array<char const*,11> renderPassDisplayNames{
    "Entities","Dropped items","Block entities","HUD","Particles","Sky",
    "Weather","Portal overlay","Fire overlay","Lightning","Fog"};
inline constexpr std::array<char const*,23> informationDisplayNames{
    "座標","換算座標","次元","バイオーム","難易度","方角",
    "左右の角度","上下の角度","疾走","FPS (HUD側)","フレーム時間 (HUD側)","光","通信遅延",
    "視点の角度","ブロック座標","チャンク座標","速度","水平速度","垂直速度",
    "ゲーム内時刻","現在時刻","天気","月"};
inline std::string displayValueJa(std::string_view value) {
    constexpr std::pair<char const*,char const*> names[]{
        {"Unknown","不明"},{"Unavailable","取得不可"},
        {"Overworld","地上"},{"Nether","ネザー"},{"End","ジ・エンド"},
        {"Peaceful","ピースフル"},{"Easy","イージー"},{"Normal","ノーマル"},{"Hard","ハード"},
        {"South","南"},{"West","西"},{"North","北"},{"East","東"},
        {"Yes","はい"},{"No","いいえ"}};
    for(auto const& [key,label]:names)if(value==key)return label;
    return std::string(value);
}
inline std::array<std::string,3> foodHudLinesJa(FoodSnapshot const& food) {
    return {"満腹度 (端末): "+displayValueJa(foodText(food.hunger,20)),
        "隠し満腹度 (端末): "+displayValueJa(foodText(food.saturation,20)),
        "消耗度: "+displayValueJa(foodText(food.exhaustion,5))};
}
inline std::vector<std::string> informationLinesJa(InformationSnapshot const& sample,InformationOptions const& options) {
    auto lines=formatInformationLines(sample,options);
    for(auto& line:lines)for(unsigned i=0;i<informationLineNames.size();++i) {
        std::string const prefix=std::string(informationLineNames[i])+": ";
        if(line.starts_with(prefix)) {
            auto value=displayValueJa(std::string_view(line).substr(prefix.size()));
            if(i==11&&value.starts_with("sky ")) {
                value.replace(0,4,"空 ");
                if(auto at=value.find(", block ");at!=std::string::npos)value.replace(at,8,", ブロック ");
            }
            line=std::string(informationDisplayNames[i])+": "+value;break;
        }
    }
    // Inspector observations are consumed by the F10 block-compare page.
    // Never append serialized block/NBT text to the in-game information HUD:
    // the full state is often thousands of characters and obscures gameplay.
    return lines;
}
// Translate owned UI status text at presentation, never the worker's state,
// exceptions, recovery rules, filenames or logs. Unknown details remain intact.
inline std::string storageStatusJa(std::string_view status) {
    constexpr std::pair<char const*,char const*> messages[]{
        {"Storage not started","保存処理は未開始です"},
        {"Background config/profile operation complete","設定の保存・読込みが完了しました"},
        {"Config/profile I/O failed (pending file retained): ","設定の処理に失敗しました (保存データは保持): "},
        {"Unsaved settings retained; retrying in background","未保存の設定を保持し、保存を再試行しています"},
        {"Background config/profile operation queued","設定の保存・読込みを受け付けました"},
        {"Not initialized","地図は未準備です"},
        {"Waypoint save and recovery failed: owned edits retained; Map paused","地点の保存に失敗。編集内容を保持し、地図を一時停止しています"},
        {"Persistence saturated: previous-world waypoints retained; Map paused","前のワールドの地点を保持し、地図を一時停止しています"},
        {"Previous-world save completed","前のワールドの保存が完了しました"},
        {"Previous world: ","前のワールド: "},{"Saved","保存済み"},
        {"New world / no saved data","新しいワールド / 保存データなし"},{"Loaded","読込み済み"},
        {"Loading settings","地図設定を読み込んでいます"},
        {"Unsaved previous-world waypoints retained: retrying; Map paused","前のワールドの未保存地点を保持し、保存を再試行しています"},
        {"Loading world","ワールドを読み込んでいます"},
        {"Session only: stable world ID unavailable","ワールドを識別できないため、今回の接続中だけ有効です"}};
    for(auto const& [key,label]:messages)if(status.starts_with(key))return std::string(label)+std::string(status.substr(std::char_traits<char>::length(key)));
    return std::string(status);
}
// The camera worker keeps its original status/log vocabulary. Only its known
// display values are translated here; unknown details are preserved verbatim.
inline std::string cameraStatusJa(std::string_view status) {
    constexpr std::pair<char const*,char const*> messages[]{
        {"OFF","無効"},
        {"Camera retired; OFF","カメラは終了しました（無効）"},
        {"Restore pending: camera shape changed","復帰待ち: カメラの構成が変わりました"},
        {"Restore pending: camera owner conflict","復帰待ち: 別のカメラ操作と競合しています"},
        {"Restore pending: original camera owner unavailable","復帰待ち: 元のカメラを確認できません"},
        {"Restore pending: original actor retired","復帰待ち: 元のプレイヤーを確認できません"},
        {"Restore pending: actor orientation unavailable","復帰待ち: プレイヤーの向きを取得できません"},
        {"Restore pending: invalid camera pitch bounds","復帰待ち: カメラの上下角の範囲が不正です"},
        {"OFF; camera restored","無効（元の視点へ復帰済み）"},
        {"Needs one owned first-person camera rig","使用できる一人称カメラを確認できません"},
        {"Unsupported camera component contract","このカメラの構成には対応していません"},
        {"Camera position is invalid","カメラの位置が不正です"},
        {"Camera/actor angular convention not confirmed","カメラとプレイヤーの角度の対応を確認できません"},
        {"Camera angle bounds unsupported","カメラの角度の範囲に対応していません"},
        {"Freelook active","フリールック使用中"},
        {"FreeCamera active; WASD/Space/Shift flies","フリーカメラ使用中（WASD・Space・Shiftで移動）"},
        {"Camera thread changed","カメラの処理スレッドが変わりました"},
        {"Release movement keys before FreeCamera starts","フリーカメラを始める前に移動キーを離してください"},
        {"Unload waiting for camera restoration","カメラの復帰を待っています"},
        {"Unload waiting for owned input releases","操作キー・ボタンが離されるのを待っています"}};
    for(auto const& [key,label]:messages)if(status==key)return label;
    return std::string(status);
}
}
