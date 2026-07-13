# 狐Tone (KituneTone)

<p align="center">
  <img src=".github/assets/kitsunetone-logo.png" alt="狐Toneロゴ" width="480">
</p>

**狐Tone** は、オープンソースDAW [LMMS](https://lmms.io/) をフォークして生まれた、独自拡張を持つオリジナルDAWです。
VST3、オーディオ録音、日本語環境向けの改善などを追加しています。

LMMSのソースコードと自由ソフトウェアとしての成果を受け継ぎながら、Windowsでの制作体験と新しいプラグイン連携を実験・開発しています。

> [!IMPORTANT]
> 狐ToneはLMMSから派生した非公式フォークです。LMMSプロジェクトによる公式製品ではなく、LMMSの開発チームによる承認・サポートを受けたものでもありません。

> [!WARNING]
> 現在は開発・実験段階です。未完成または不安定な機能が含まれ、動作は保証されません。大切なプロジェクトは必ずバックアップしてください。

## 狐Toneの主な機能

### ネイティブVST3ホスティング

- VST3インストゥルメントとVST3エフェクトをプロセス内でホスト
- ノート、ベロシティ、CC、ピッチベンド、アフタータッチをVST3イベントとして送信
- プラグインの状態をプロジェクトに保存
- プラグイン本来のGUIを表示
- ピンボタンでプラグインGUIを常に最前面に固定

VST3インストゥルメントは、インストゥルメント一覧の「VST3」からトラックへ追加できます。
VST3エフェクトは、エフェクトチェインの「エフェクトを追加」から選択できます。

### オーディオ録音

トランスポートの録音ボタンから、マイクやライン入力などの入力デバイスを録音できます。
録音停止後はWAVファイルとして保存され、新しいサンプルトラックへ自動的に配置されます。

録音ファイルは、ユーザーのサンプルフォルダー内にある `recordings/` へ保存されます。

### ARA 2ホスティング（実験的）

MelodyneなどのARA対応VST3プラグインを、サンプルトラックのエフェクトとしてホストできます。

- トラック上のオーディオクリップをARAオーディオソースとして自動登録
- クリップ全体へのランダムアクセス
- DAWのテンポと拍子をmusical contextとして連携
- DAWとプラグインの再生ヘッドを同期
- トラック内の複数クリップを個別のARAリージョンとして配置

ARA連携は実験的な実装です。基本的な音声取り込み、波形表示、テンポ・再生同期には対応していますが、
プラグインによって動作が異なります。編集結果の完全な反映や、ARA編集内容をプロジェクトへ永続化する機能は未完成です。

### ファイルのドラッグ＆ドロップ

Windowsのエクスプローラーなどから、WAV、MP3、Ogg Vorbis、FLACなどのオーディオファイルを
サンプルトラックへ直接配置できます。複数ファイルの同時ドロップにも対応しています。

### 日本語環境とUIの改善

- UIフォントをNoto Sans / Noto Sans JPへ統一
- 日本語ロケールでUIの日本語翻訳を適用
- 狐Toneで追加した機能の日本語表示
- 起動時にソングエディタを最大化

### 構成の整理

このフォークでは、安定性を優先してCarla、Carla Rack、Carla Patchbayをビルド対象から外しています。

## ARAの使い方

1. サンプルトラックにオーディオクリップを配置します。
2. そのトラックのエフェクトチェインへARA対応VST3エフェクトを追加します。
3. エフェクトの「UI」ボタンからプラグイン画面を開きます。
4. プラグイン側に波形が表示されたことを確認し、狐Tone側で再生します。

プラグインを追加した後にクリップを配置した場合は、プラグインUIを一度閉じてから開き直すと再取り込みされます。
診断情報は、通常 `ドキュメント\lmms\lmms_ara_debug.log` に出力されます。

## ダウンロード

Windows 10 / 11（64bit）向けのビルドは、[Releases](https://github.com/SakiikaVR/KituneTone-DAW/releases) から入手できます。
配布アーカイブを任意の場所へ展開し、`lmms.exe` を実行してください。

VST3プラグインは、`C:\Program Files\Common Files\VST3` などの標準フォルダーから自動検出されます。

## ソースからビルドする

### 必要なもの

- Visual Studio 2022（C++によるデスクトップ開発）
- [Qt 6.8.x](https://www.qt.io/)（`msvc2022_64`）
- [vcpkg](https://github.com/microsoft/vcpkg)
- CMakeとNinja

### ビルド例

Visual Studioのx64 Native Tools Command Promptで実行してください。Qtとvcpkgのパスは環境に合わせて変更します。

```bat
vcpkg install --triplet x64-windows

cmake -S . -B build -G Ninja ^
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DWANT_QT6=ON ^
  -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64 ^
  -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake ^
  -DVCPKG_TARGET_TRIPLET=x64-windows ^
  -DCMAKE_CXX_FLAGS_RELWITHDEBINFO="/MD /Zi /O2 /Ob1 /DNDEBUG"

cmake --build build
```

LMMS本来のビルド情報については、[LMMSのビルド手順](https://github.com/LMMS/lmms/wiki/Compiling) も参照してください。

## ライセンスと帰属

狐ToneはLMMSを改変した派生作品です。LMMS由来のコードおよびこのリポジトリで加えた変更は、
特記がない限り **GNU General Public License version 2 or later（GPL-2.0-or-later）** の条件で利用できます。
ライセンス本文は [LICENSE.txt](LICENSE.txt) を参照してください。

GPLに基づいてバイナリを再配布する場合は、対応する完全なソースコード、ビルドに必要な情報、ライセンス表示を、
GPLが定める方法で受領者へ提供してください。サードパーティ製ファイルについては、それぞれのファイルに付属するライセンスが優先されます。

主な由来と同梱コンポーネントは次のとおりです。

| コンポーネント | ライセンス | 帰属・用途 |
| --- | --- | --- |
| [LMMS](https://github.com/LMMS/lmms) | GPL-2.0-or-later | 狐Toneの基盤。著作権はLMMSの各貢献者に帰属します |
| [Steinberg VST 3 Plug-In SDK](https://github.com/steinbergmedia/vst3sdk) | MIT | VST3ホスティング。リポジトリ内の各 `LICENSE.txt` を参照してください |
| [ARA SDK](https://github.com/Celemony/ARA_SDK) | Apache-2.0 | ARA連携。`src/3rdparty/ara/NOTICE.txt` と各ライセンスを参照してください |
| [Noto Sans / Noto Sans JP](https://github.com/notofonts) | SIL Open Font License 1.1 | UIフォント。`data/fonts/OFL.txt` を参照してください |

LMMSの元のREADMEは [README.upstream.md](README.upstream.md) に保存しています。
LMMS、Steinberg、Celemony、Notoおよび各製品名は、権利者を示すためにのみ記載しています。

## 謝辞

狐ToneはLMMSと、その長年にわたる貢献者の成果なしには成立しません。
LMMS開発者、翻訳者、テスター、アーティスト、そして各サードパーティプロジェクトの皆さまに感謝します。

狐Tone固有の変更に関する不具合や要望は、[このリポジトリのIssues](https://github.com/SakiikaVR/KituneTone-DAW/issues) へお願いします。
LMMS本家へ狐Tone固有のサポート依頼を送らないようご協力ください。
