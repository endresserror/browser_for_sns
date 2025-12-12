# SNS Guardian Browser (Linux ネイティブ C++)
SNSに特化したシンプルブラウザです。投稿前のリスク分析と議論パターン検知をページに挿入します。絵文字を使わずミニマルなUIです。Linux (GTK + WebKit2GTK) 向けのみ対応しています。

## Linux でのビルドと実行
依存: `gtk+-3.0` と `webkit2gtk-4.0` の開発パッケージ、CMake 3.20+、g++/clang++。
```bash
sudo apt install build-essential cmake libgtk-3-dev libwebkit2gtk-4.0-dev
cd native
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
./build/sns_guardian_browser
```

## 使い方
- アドレスバーに URL を入力して「開く」を押すとページが表示されます。
- X / Mastodon / Bluesky で投稿ボタンを押すと送信前に分析モーダルが出ます。
- API ベースURLはスクリプト内デフォルト `http://localhost:8000/api/v1`（未接続時は低リスクのフォールバック応答）。

## 補足
- 旧 `frontend/` (Vite/Electron) は利用しません。ネイティブ C++ 実行ファイルをご使用ください。
