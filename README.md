# Praxis Client / PraxisCompanion

**Current preview:** v0.1.5-rc.1 — **Minecraft Bedrock for Windows 1.26.60.05**, Windows x64, **LeviLamina Client 26.60.0** (build `f28a4b5d`). This is an **experimental 26.60 port**, not a feature-complete release. The earlier [26.51 prerelease](https://github.com/rometet/praxis/releases/tag/v0.1.4-rc.1) remains available separately.

## 機能・制限

- F10でPraxis GUIを表示。起動・GUI初期化は26.60.05実機で確認済みです。
- NoRender、ChunkBorders、Fullbright、Zoom、HUD、Map、ShulkerPreview、TimeChanger、WeatherChanger、FreeLook/FreeCamera、インベントリ機能・Overlayなどの実装を含みます。**全機能を26.60の実機で個別に検証したわけではありません。**
- **Placement系はFlexibleBlockPlacementのみ表示・利用できます。** AccurateBlockPlacement / FastBlockPlacementはこのプレリリースで非表示・実行時無効化しています。FastRightClickのブロック設置実験設定も無効化しています（通常のアイテム使用は残ります）。
- **ワールド内のリソースパック即時反映は動作しません。** 選択欄を変更できても、ゲーム内のテクスチャは更新されないことを確認しました。Praxis内のワールド中「適用」「再読み込み」ボタンは無効化しています。通常のMinecraft設定で切り替え、必要ならMinecraftを再起動してください。[mcapi-requests #288](https://github.com/LiteLDev/mcapi-requests/issues/288)
- WeatherChangerの晴天固定初期化が利用不可になったログがあり、こちらも完全動作は未確認です。
- Modのホットアンロード・再有効化は非対応です。更新時はMinecraftを再起動してください。

## インストール

1. Minecraftを終了し、既存のPraxis DLL・設定・ワールドのバックアップを取ります。
2. Minecraft Bedrock for Windows **1.26.60.05** と LeviLamina Client **26.60.0** の互換ビルドを用意します（ローダーの互換性チェックがあるため、異なるビルドでは起動しない場合があります）。
3. GitHub Releaseの `PraxisCompanion-v0.1.5-rc.1-client-windows-x64.zip` を展開し、`PraxisCompanion/` を対応バージョンの `mods/` に配置します。既存の `config.json` は削除・上書きしないでください。
4. Minecraft起動後、**F10** でGUIを開きます。
5. FlexiblePlacementの回転・オフセット・隣接設置には、別途 **flexible-placement ABI v2** 対応のLHoloが必要です。LHoloは同梱されません。

## 実機検証について

特定の26.60.05環境で起動・Modロード・GUI初期化・ビルドを確認しています。各種モジュールのマルチ/BDS対応、長時間安定性、ON/OFF復帰、描画性能は保証していません。詳細は [26.60 preview limitations](docs/26.60-PREVIEW-LIMITATIONS.md) をご覧ください。

[Source build](SOURCE_BUILD.md) · [Release policy](RELEASE_POLICY.md) · [Changelog](CHANGELOG.md) · [Third-party notices](THIRD_PARTY_NOTICES.md)

The combined binary contains GPL-3.0-covered ChiyanMap-derived work and other covered source. See `LICENSE`, `companion/LICENSE_COMBINED.txt`, `licenses/`, and `THIRD_PARTY_NOTICES.md`. This independent project is not affiliated with Mojang Studios or Microsoft.
