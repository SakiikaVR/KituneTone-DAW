# LMMS (VST3 / 録音 / 日本語対応 改造版)

このリポジトリは、オープンソースの DAW（デジタル・オーディオ・ワークステーション）である
[LMMS](https://lmms.io) を **個人的に実験目的で改造** したフォークです。
本家 LMMS に対して、ネイティブ VST3 対応・録音機能・フォントや日本語まわりの改善などを加えています。

> **⚠️ 実験的に作りました**
> このフォークは学習・実験目的で作成したものです。動作は無保証で、不安定な部分や未完成の機能が含まれます。
> 業務や重要な制作には本家の安定版をご利用ください。

---

## 🎯 追加した主な機能

### 1. ネイティブ VST3 対応（インストゥルメント & エフェクト）
本家 LMMS は VST2 を外部プロセスでホストしますが、本フォークでは **Steinberg VST3 SDK を用いたネイティブ VST3 ホスティング** をプロセス内で実装しました。

- **VST3 インストゥルメント（音源）** — インストゥルメント一覧の「VST3」をトラックへドラッグして使用。Vital などの VST3 シンセをプラグインのネイティブ GUI 付きでロードできます。
- **VST3 エフェクト** — エフェクトチェインの「エフェクトを追加」→「VST」タブから、システムにインストール済みの VST3 エフェクト（例: iZotope Ozone）を選択して使用できます。
- **MIDI 対応** — ノート／ベロシティ／CC／ピッチベンド／アフタータッチを VST3 イベントへ変換して送信します。
- **状態保存** — プラグインの状態はプロジェクトファイルに保存されます。
- **ネイティブ GUI** — エフェクトの「UI」ボタン、またはインストゥルメントの「GUI を表示/非表示」で、プラグイン本来の画面を開けます。
- **常に最前面（📌）** — プラグイン GUI ウィンドウ上部のピンボタンで、ウィンドウを常に手前に固定できます。

### 2. 録音機能
トランスポートの録音ボタンで、**マイク／ライン入力などのシステム標準入力デバイスから録音**できます。
録音を停止すると、WAV ファイルとして保存され、新しいサンプルトラックに自動配置されます。
（録音ファイルの保存先: ユーザーのサンプルフォルダ内 `recordings/`）

### 3. エクスプローラーからのドラッグ&ドロップ
Windows のエクスプローラーなどから **wav / mp3 / ogg / flac などのオーディオファイルを、サンプルトラックへ直接ドラッグ&ドロップ** して配置できます。複数ファイルの同時ドロップにも対応しています。

### 4. フォントを Noto Sans に統一
UI 全体のフォントを **Noto Sans**（日本語は Noto Sans JP）に変更し、フォントを同梱しました。

### 5. 日本語 UI
Qt の翻訳ファイル生成を修正し、システムロケールが日本語の環境では **UI が日本語で表示** されるようにしました。追加機能の文言も日本語化しています。

### 6. ソングエディタの初期最大化
起動時にソングエディタが最大化された状態で開くようにしました。

### 7. ARA2 検出（実験的）
ARA（Audio Random Access）対応プラグイン（例: Melodyne）を VST3 エフェクトとしてロードした際に、**ARA ファクトリの有無を検出して表示** します。

> **⚠️ ARA について**: 現状は「ARA 対応プラグインの検出」までの**実験的な実装**です。オーディオソース解析やプレイバックリージョンといった ARA の完全なホスト機能（Melodyne でピッチ編集を反映する等）は**まだ実装されていません**。

---

## 💾 ダウンロード

ビルド済みの Windows 版（64bit）は下記のリリースページから入手できます。

**➡️ [最新リリースをダウンロード](../../releases/latest)**

`lmms-vst3-jp-win64.zip` を任意の場所に展開し、`lmms.exe` を実行してください。
インストール不要のポータブル版です。

### 動作環境
- Windows 10 / 11（64bit）
- VST3 プラグインは `C:\Program Files\Common Files\VST3` などの標準フォルダを自動で検索します。

---

## 🔨 ソースからのビルド（Windows / MSVC）

<details>
<summary>ビルド手順を表示</summary>

### 必要なもの
- Visual Studio 2022（C++ デスクトップ開発ワークロード）
- [Qt 6.8.x](https://www.qt.io/)（`msvc2022_64`）
- [vcpkg](https://github.com/microsoft/vcpkg)

### 手順
```bat
:: 1. 依存関係を vcpkg で用意
vcpkg install --triplet x64-windows

:: 2. 構成（Qt と vcpkg のパスは環境に合わせて変更）
cmake -S . -B build -G Ninja ^
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DWANT_QT6=ON ^
  -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64 ^
  -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake ^
  -DVCPKG_TARGET_TRIPLET=x64-windows ^
  -DCMAKE_CXX_FLAGS_RELWITHDEBINFO="/MD /Zi /O2 /Ob1 /DNDEBUG"

:: 3. ビルド
cmake --build build
```
（開発者コマンドプロンプト `vcvars64` 環境で実行してください）

</details>

---

## 📄 ライセンス

本プロジェクトは本家 LMMS と同じく **GNU General Public License v2.0 or later（GPLv2+）** で公開されます。
全文は [LICENSE.txt](LICENSE.txt) を参照してください。

同梱・利用している主なサードパーティ:

| コンポーネント | ライセンス | 用途 |
|---|---|---|
| [LMMS](https://lmms.io) | GPLv2+ | 本体 |
| [Steinberg VST3 SDK](https://github.com/steinbergmedia/vst3sdk) | GPLv3 / Steinberg Dual License | VST3 ホスティング |
| [ARA SDK](https://github.com/Celemony/ARA_SDK) | Apache License 2.0 | ARA 検出（実験的） |
| [Noto Sans / Noto Sans JP](https://fonts.google.com/noto) | SIL Open Font License 1.1 | UI フォント |

各サードパーティのライセンス条項に従ってご利用ください。VST3 SDK は GPLv3 と Steinberg 独自ライセンスのデュアルライセンスであり、本フォークは GPL の条件下で利用しています。

---

## 🙏 謝辞

本家 [LMMS](https://lmms.io) 開発者の皆さま、および上記サードパーティの開発者の皆さまに深く感謝します。

これは LMMS の非公式な実験的フォークであり、本家 LMMS プロジェクトとは関係ありません。
