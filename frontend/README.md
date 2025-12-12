# SNS Guardian Browser（Chromiumベース）

拡張機能ではなく、Electron（Chromium）で動くシンプルな SNS 特化ブラウザです。投稿前のリスク分析と議論パターン検知を組み込み、AI 感を抑えたシンプルデザイン（絵文字不使用）にしています。

## セットアップ
```bash
cd frontend
npm install
```

## 開発モードで起動
Vite の開発サーバーと Electron を同時起動します。デフォルトでポート 5174 を使用します。
```bash
npm run dev
```
※ 初回は依存解決後に少し時間がかかります。

## ビルド
UI のビルドのみを実行します（Electron パッケージングは別途設定する想定）。
```bash
npm run build
```
生成物は `frontend/dist` に出力されます。

## 使い方
- アドレスバーから目的の SNS（X / Mastodon / Bluesky など）へ移動します。
- 左パネルで API URL・投稿前分析・パターン検知の ON/OFF、トークンを設定できます。
- 送信ボタンが押されたタイミングでガーディアンが介入し、リスクスコアや改善案を表示します。

## デザイン方針
白基調でフラット、情報優先のシンプル UI。絵文字は使用しません。
