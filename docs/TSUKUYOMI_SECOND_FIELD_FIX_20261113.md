# Praxis / Tsukuyomi 移植：実機フィードバック第2回

対象: メインPC LeviLauncher 1.26.60.05 / LeviLamina Client 26.60.0

## 修正
1. ToggleSneakSprint: KeyInputEventのデバウンスだけでなく、毎TickのLocalPlayer::setSneaking / setSprinting直接書き換えを撤廃。公開SDKのClientMoveInputHandler::getMoveInputとMoveInputComponentの名前付きフラグ（Sneaking / PersistSneak / Sprinting / SneakDown / SprintDown）を使用。GUI / ワールド離脱で自分がセットした入力フラグだけ解除し、物理Shift/Ctrl操作を尊重する。実機確認待ち。
2. AngleSnap: MouseInputEventのActionMoveRelativeに加えてActionMoveを処理し、ClientMoveInputHandler経由でmLookDeltaのYaw/Pitch軸を固定。絶対角度スナップは引き続き不可。実機確認待ち。
3. AppleSkin: 従来のImGuiテキストHUDをやめ、LLのAfterUIRenderEventからバニラUIのhunger_rendコントロールの位置を取得して、バニラhunger_full/hunger_halfを半透明で重ね、隠し満腹度を金色の枠として描画する。取得できない属性やクリエイティブ・バー不在では描画しない。F10のAppleSkinページ/満腹度HUDは共通のON/OFF。Lamiumからコードや画像をコピーせずSDK APIで独自実装。
4. DebugScreen: Minecraft Java F3風の左上（座標・向き・ワールド）と右上（クライアント/サーバー観測値）2カラム、狭い画面は縦積み。実際に測ったサーバーTick間隔は「Observed tick interval」、実サーバーMSPTはUnavailable。DebugScreen描画中はMiniMapを非表示。DebugScreen OFFでMap設定を変更せず自動復帰。
5. ItemScroller: ユーザー希望により今回変更なし。

## 検証の境界
- xmake build PraxisCompanion は成功。TsukuyomiFeatureTestsは64件 PASS。
- 新規のネイティブフック、署名走査、パケット操作は追加していない。
- Minecraft実機での確認はまだ行っていない。特に移動コンポーネント側のゲーム入力反映、バニラHUDアイコンのdrawImageとリソースパック互換性は要検証。
- GUI全体のレンダリングテストには旧版から480x800のパック画面幅不整合が存在する。今回の成果を全回帰成功とは表現しない。
- Minecraft起動中のDLL上書きは禁止。導入時はバックアップとSHA256照合を取る。

## 確認
- ToggleSneakSprint: V/B各1回、長押し、Shift/Ctrl手動操作、F10開閉、ワールド移動
- AngleSnap: F10で機能とYaw Lockを両方ON。マウスの左右・上下を個別に確認
- AppleSkin: F10 AppleSkinをON、サバイバルで空腹度を減らし食料を持つ。バニラ肉アイコン上の半透明予測と金色の枠を確認
- DebugScreen: F10でDebugScreenをON、Java風2カラムとマップ消去、OFFでマップ復活を確認
