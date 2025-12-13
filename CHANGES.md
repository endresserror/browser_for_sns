# 変更履歴 (CHANGES.md)

このドキュメントは、オリジナルリポジトリ [`endresserror/browser_for_sns`](https://github.com/endresserror/browser_for_sns.git) からの変更点をまとめたものです。

---

## 概要

本プロジェクトに対して、以下の大きな改善を行いました：

1. **ビルドエラーの修正**
2. **セッション永続化（ログイン状態の保持）**
3. **ガーディアンスクリプトの完全書き直し**
4. **Gemini API ネイティブ統合（CORS回避）**
5. **サイバーパンク風UIデザイン**
6. **日本語ローカライズ**
7. **設定GUIの実装**

---

## 1. ビルドエラーの修正

### 1.1 `AnalysisProvider` enum の修正

**ファイル**: `native/main_linux.cpp`

**問題**: `AnalysisProvider::Local` が使用されていたが、enum定義には `LocalHeuristic` しか存在しなかった。

**修正**:
```cpp
// 変更前
AnalysisProvider provider = AnalysisProvider::Local;

// 変更後
AnalysisProvider provider = AnalysisProvider::LocalHeuristic;
```

---

## 2. セッション永続化（ログイン状態の保持）

### 2.1 WebKitWebsiteDataManager の導入

**ファイル**: `native/main_linux.cpp`

**問題**: アプリを起動するたびにログインが必要だった。

**修正**: Cookie とセッションデータを `~/.sns_guardian_browser/` に永続化するようにした。

**効果**: 一度ログインすれば、次回以降はログイン状態が維持される。

---

## 3. ガーディアンスクリプトの完全書き直し

### 3.1 問題点

オリジナルの `build_guardian_script()` 関数は、JavaScript構文エラーが発生し、スクリプトが実行されない状態だった。

### 3.2 解決策

`on_load_changed()` 関数内で、シンプルな単一の Raw String Literal としてガーディアンスクリプト全体を再実装。

### 3.3 MutationObserver の改善

デバウンス付きDOM監視を実装し、無限ループを防止。

---

## 4. Gemini API ネイティブ統合（CORS回避）

### 4.1 問題点

WebKit2GTKのセキュリティ仕様により、JavaScript `fetch`リクエストがCORSエラーでブロックされていた。

### 4.2 解決策

Gemini API の呼び出しを **C++ネイティブ側（libcurl）** に移譲。

#### 実装の仕組み
1. **JavaScript**: `window.webkit.messageHandlers.gemini.postMessage(text)` で分析依頼
2. **C++**: 別スレッドで `perform_gemini_request()` を実行
3. **C++ (libcurl)**: Gemini API に HTTP POST リクエスト
4. **C++**: `window.geminiCallback(json)` で結果を返却
5. **JavaScript**: コールバックで結果を受け取り、モーダルに表示

### 4.3 デフォルトモデル変更

- デフォルトモデルを `gemini-2.5-flash-lite-preview-09-2025` に設定

---

## 5. サイバーパンク風UIデザイン

### 5.1 GTK CSS テーマ

- **背景色**: `#0a0a0f` (漆黒)
- **シアンアクセント**: `#00fff2`
- **マゼンタアクセント**: `#ff00ff`
- ネオングラデーション効果
- ダークテーマの入力フィールド・コンボボックス
- ホバーエフェクト付きボタン

---

## 6. 日本語ローカライズ

すべてのUI要素を日本語に翻訳：
- タブラベル: 「SNS」「設定」
- プロバイダ選択: 「ローカル」「Gemini API」「REST API」
- チェックボックス: 「高度分析」「パターン検知」
- ボタン: 「設定を適用」

---

## 7. 設定GUIの実装

### 7.1 コンパクトな設定画面

- 1画面に収まるレイアウト（スクロール不要）
- カード形式のセクション
- プロバイダ選択（コンボボックス）
- APIキー入力（マスク表示）
- モデル名入力
- 機能トグル

### 7.2 設定適用時の強制リロード

- `beforeunload` イベントをバイパス
- 確認ダイアログなしでページリロード

---

## 依存関係

新たに `libcurl` が必要：

```bash
sudo apt update
sudo apt install libcurl4-openssl-dev
```

---

## 変更ファイル一覧

| ファイル | 変更内容 |
|:---------|:---------|
| `native/main_linux.cpp` | 完全な書き直し: enum修正、セッション永続化、ガーディアンスクリプト再実装、libcurl統合、サイバーパンクUI、日本語化、設定GUI |
| `native/CMakeLists.txt` | `libcurl` のリンクを追加 |
| `CHANGES.md` | このファイル（新規作成） |

---

## ビルド方法

```bash
# 依存関係のインストール
sudo apt install libcurl4-openssl-dev

# ビルド
cd native
mkdir -p build && cd build
cmake ..
cmake --build . --config Release
./sns_guardian_browser
```

---

## 既知の制限事項

### WSL環境での日本語入力

WSL2 + WSLg 環境では、入力メソッドが WebKit2GTK と正常に連携しない場合がある。回避策：

- Windows側で日本語を入力し、`Ctrl+V` で貼り付け
- または Electron版（`frontend/`）を使用
