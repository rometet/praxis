# はじめての開発 / Start here

Minecraft Bedrock や C++ の内部を知らなくても参加できます。日本語・English どちらでも歓迎します。

## 1. 何から始めるか

- コード不要: [good first issue](https://github.com/rometet/praxis/issues?q=is%3Aissue+is%3Aopen+label%3A%22good+first+issue%22) から説明文、操作手順、再現報告を選ぶ。
- 初めての C++: 既存の設定・純粋なポリシー関数・テストを読む。ネイティブ hook をいきなり変更する必要はない。
- ゲームで試す: [Releases](https://github.com/rometet/praxis/releases) の対応バージョンを使い、テスト用ワールドで再現手順・期待値・実測値を報告する。

## 2. ソースの地図

| Path | Responsibility |
| --- | --- |
| `companion/src/core/` | 機能の状態、設定、判定ロジック |
| `companion/src/gui/` | F10 のメニューと UI |
| `companion/src/platform/` | Minecraft / LeviLamina 接続。上級者向け |
| `companion/src/map/`, `render/` (under companion/src) | マップ・描画 |
| `companion/src/resourcepacks/` | リソースパック処理 |
| `companion/tests/` | 既存の C++ 回帰テスト |
| `companion/xmake.lua` | ビルド・テストターゲット |

詳細: [architecture](ARCHITECTURE.md), [feature matrix](FEATURE_PORT_MATRIX.md), [runtime test plan](RUNTIME_TEST_PLAN.md).

## 3. 最初の PR

GitHub 上で文書を編集して Fork / Create pull request するだけでも参加できます。
ローカルの場合は GitHub の Fork を作成し、次の手順で開始します。

```powershell
git clone https://github.com/rometet/praxis.git
cd praxis
git remote rename origin upstream
git remote add origin https://github.com/YOUR-GITHUB-NAME/praxis.git
git fetch upstream main
git switch -c docs/my-first-change upstream/main
# Edit one file; inspect before committing.
git diff
git add <changed-file>
git commit -m "docs: clarify one behavior"
git push -u origin HEAD
```

GitHub で upstream の main への PR を作成。Issue 番号・変更理由・確認方法・未確認項目を書く。
1 PR = 1 目的。個人情報、ゲームデータ、認証情報、DLL/PDB、ログをコミットしない。

## 4. ビルド（C++を変更するときだけ）

Windows x64, Git, VS 2022 C++ Build Tools, Windows SDK, LLVM clang-cl, xmake, Python。
現在の main は Minecraft 1.26.60.05 / LL 26.60.0 の公開ソース。
正確な依存関係は [SOURCE_BUILD](../SOURCE_BUILD.md) と [CI](../.github/workflows/client-2660-build.yml) を優先する。

```powershell
cd companion
xmake f -p windows -a x64 -m release --target_type=client -y
xmake b -j 4 PraxisCompanion
xmake b -j 4 PraxisCompanionInventorySortTests
xmake run PraxisCompanionInventorySortTests
```

Known blocker: [#5](https://github.com/rometet/praxis/issues/5) tracks the upstream LeviLamina build failure. If blocked, include the command and sanitized error, not a success claim. Unit tests do not establish gameplay correctness. Never hot-unload the DLL.

## 5. Current development lanes

See [open PRs](https://github.com/rometet/praxis/pulls). As of 2026-11-25: #8 TabTPS protocol/server, #9 QuickTrade, #10 CommandKeys, #11 integration, #12 TabTPS source parity. They are overlapping drafts, not five independently mergeable releases. Ask in the relevant PR before modifying the same files. New native work starts from main or an explicitly documented dependency; do not merge an unrelated WIP branch.

[Contribution rules](../CONTRIBUTING.md) | [Security](../SECURITY.md) | [License](../LICENSE)
\n