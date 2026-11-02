# Praxis Client

**Minecraft Bedrock Edition（Windows）向けの建築・操作支援クライアントMod。**

Praxis Client（PraxisCompanion）は、建築時の操作、ブロック情報の確認、インベントリ管理、表示の調整をひとつのGUIにまとめたModです。**設計図の投影は別プロジェクトの [LHolo](https://github.com/rometet/LHolo) が担当**します。

[![Release](https://img.shields.io/github/v/release/rometet/praxis?include_prereleases&label=Release&style=flat-square)](https://github.com/rometet/praxis/releases)
![Minecraft](https://img.shields.io/badge/Minecraft-1.26.51.01-555?style=flat-square)
![Platform](https://img.shields.io/badge/Windows-x64-555?style=flat-square)
[![License](https://img.shields.io/github/license/rometet/praxis?style=flat-square)](LICENSE)

[**ダウンロード**](https://github.com/rometet/praxis/releases) · [**導入方法**](#導入方法) · [**主な機能**](#主な機能) · [**操作方法**](#操作方法) · [English](docs/README_EN.md)

> [!IMPORTANT]
> 現在の公開プレリリースは **v0.1.4-rc.1（Praxis R34）** です。対象環境は **Minecraft Bedrock 1.26.51.01 / LeviLamina Client 26.51.5 / Windows x64**。**Minecraft 26.60対応版ではありません。**

## 主な機能

| 分類 | 機能 | 内容 |
| --- | --- | --- |
| 表示 | **NoRender / Fullbright** | 不要な描画の非表示、明るさの調整 |
| 表示 | **ChunkBorders** | チャンク・リージョン境界の可視化 |
| 表示 | **LightOverlay / RedstoneOverlay** | 明るさやレッドストーン状態の情報表示 |
| 表示 | **Zoom** | 視野角の変更とズーム操作 |
| カメラ | **Freelook / FreeCamera** | 視点やカメラ位置の操作 |
| 情報 | **ShulkerPreview / HUD** | シュルカーボックスの内容、情報・耐久値などの表示 |
| アイテム | **Hand Restock / Inventory Sort / Transfer** | 手持ち補充、整理、アイテム移動の補助 |
| ツール | **Tool Protection / Weapon Switch** | 耐久値を考慮した保護や持ち替えの補助 |
| ワールド表示 | **TimeChanger / WeatherChanger** | 時刻・天候の表示を調整 |
| 建築 | **Flexible Block Placement** | 回転・オフセット・隣接位置を組み合わせた設置支援（互換LHoloが必要） |
| ユーティリティ | **ブロック状態比較 / Lamium共存** | 照準先の状態を基準と比較、重複機能の確認 |
| その他 | **マップ / HUDレイアウト** | マップとHUDの表示・配置設定 |

各機能の使用可否や挙動は、ゲームのバージョン、設定、ワールド・サーバーによって異なります。実験的機能は通常機能と同じ動作保証ではありません。

## 導入方法

1. **Minecraftを終了**し、対応する **Minecraft Bedrock 1.26.51.01** と **LeviLamina Client 26.51.5** を用意します。
2. [Releases](https://github.com/rometet/praxis/releases/tag/v0.1.4-rc.1) から **PraxisCompanion-v0.1.4-rc.1-client-windows-x64.zip** をダウンロードします。
3. 既存の `mods/PraxisCompanion/`、設定、対応するDLL/PDBをバックアップします。
4. ZIP内の **`PraxisCompanion` フォルダー**を、Minecraftインスタンスの `mods/` に配置します。設定ファイルは上書きしないでください。
5. Minecraftを起動し、**F10** でPraxisのGUIを開きます。

```text
<Minecraftインスタンス>/
└── mods/
    └── PraxisCompanion/
        ├── PraxisCompanion.dll
        └── manifest.json
```

> [!NOTE]
> **LHoloは基本的なPraxis単体機能には必須ではありません。** ただし、LHoloと同時使用する場合は互換GUIブリッジが必要です。Flexible Block Placementには別途 **LHolo Flexible Placement ABI v2対応版**が必要です。LHoloはPraxisのZIPに同梱されません。

## 操作方法

Praxis GUIの起動キーは **F10** です。機能のON/OFFやキー割り当て、HUDの配置はGUIから変更できます。

### Flexible Block Placement（R34）

| 操作 | 初期キー |
| --- | --- |
| 機能のON/OFF | **P** |
| 回転モード | **R** を押している間 |
| オフセットモード | **G** を押している間 |
| 隣接配置モード | **H** を押している間 |
| 複合モード | **R / G / H** を組み合わせる |

ブロックの支持面や設置位置を確認したうえで、互換LHolo経由で設置を試みます。Minecraft Java版Tweakerooと完全に同じ動作ではなく、**すべてのブロック・BDS／マルチサーバーでの成功は保証されません**。

**GキーはFreelookと競合する場合があります。** R34ではFlexible Placementを優先してカメラの巻き戻りを防止しています。両方使用する場合はFreelook側を別のキーに変更してください。

詳しい仕様: [Flexible Placement](docs/flexible-offset-adjacent-20261026.md) · [R34の独自機能](docs/UNIQUE_FEATURES_R34.md)

## LHoloとの併用

| 用途 | 条件 |
| --- | --- |
| Praxisを単体で使う | LHolo不要 |
| LHoloの投影機能と共存させる | 互換GUIブリッジを持つLHoloが必要 |
| Flexible Block Placementを使う | LHolo Flexible Placement ABI v2が必要 |
| 実験的なリソースパック再読み込み | LHolo Resource Reload ABI v4が別途必要。さらに下記のワールド制限あり |

互換ブリッジのないLHoloが存在すると、重複した描画フックを避けるため**PraxisのGUIがブロックされる場合があります**。詳細は[共存仕様](docs/LHOLO_COEXISTENCE.md)をご覧ください。

## 実験的機能と既知の制限

**ワールド内リソースパック再読み込み（R33/R34）は、一般利用向けの機能ではありません。**

- 使用できるのは、明示的に許可された**専用の破棄可能なオフライン複製テストワールド**だけです。
- 対象は条件を満たすローカルのテクスチャパック1件のON/OFFに限られます。
- 通常のワールド、マルチサーバー、サーバー必須パック、複数パックの同時操作、シェーダーやビヘイビアパックには対応しません。
- 互換LHoloがない場合に、APIのチェックを回避して強制実行しないでください。

2026年10月21日に指定テストワールドで**パックを1件OFFにしても自分のキャラクターが表示されること**を確認しています。ただし、他の環境での動作確認を意味するものではありません。

プレリリースのため、実際のMinecraftで未検証の組み合わせがあります。**Modの動作中のDLL入れ替え・ホットアンロードは避け、変更前に設定やワールドをバックアップしてください。**

詳細: [リソース再読み込みの検証記録](docs/R33_NATIVE_RESOURCE_RELOAD.md) · [テスト計画](docs/RUNTIME_TEST_PLAN.md)

## 更新履歴・開発情報

- [リリース一覧](https://github.com/rometet/praxis/releases) / [更新履歴](CHANGELOG.md)
- [ソースからのビルド](SOURCE_BUILD.md) / [公開・配布方針](RELEASE_POLICY.md)
- [機能・実装情報](docs/ARCHITECTURE.md) / [貢献ガイド](CONTRIBUTING.md)
- [脆弱性の報告](SECURITY.md)
- [ライセンス](LICENSE) / [サードパーティ通知](THIRD_PARTY_NOTICES.md) / [同梱コンポーネントのライセンス](companion/LICENSE_COMBINED.txt)

Praxis ClientはMojang StudiosおよびMicrosoftとは関係のない、独立したサードパーティModです。配布物にはGPL-3.0系のコードに加え、MPL-2.0やその他のライセンスに従う第三者コンポーネントが含まれます。それぞれの権利表示とライセンス条件を維持してください。
