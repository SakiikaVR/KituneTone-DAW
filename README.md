# 狐Tone (KitsuneTone)

<p align="center">
  <img src=".github/assets/kitsunetone-logo.png" alt="狐Toneロゴ" width="480">
</p>

狐Toneは、[LMMS](https://github.com/LMMS/lmms)を基盤として独自機能を追加した、Windows向けのオリジナルDAWです。VST3を中心に、インスツルメントとエフェクトで共通化したプラグイン画面、ARA 2連携、録音、タイムストレッチ、日本語UIなどを実装しています。

> [!IMPORTANT]
> 狐ToneはLMMS公式版ではなく、LMMS開発チームによる承認・サポートを受けた製品でもありません。狐Tone固有の問題はLMMS本家ではなく、このリポジトリのIssuesへ報告してください。

## ダウンロード

[GitHub Releases](https://github.com/SakiikaVR/KituneTone-DAW/releases)から、次のWindows 64-bit版を配布します。

- インストーラ版: スタートメニュー登録、アンインストール、狐Toneプロジェクトの関連付けに対応
- ポータブル版: 任意のフォルダーへ展開して `KitsuneTone.exe` を実行

Windows 10またはWindows 11を対象としています。VST3プラグインは標準のVST3フォルダーと、アプリ内の `VST3` フォルダーから検出します。

## 狐Tone専用プロジェクト形式

狐Tone 2.1以降はLMMS形式とは分離した専用形式を使用します。

| 拡張子 | 用途 |
| --- | --- |
| `.ktpz` | 圧縮プロジェクト（標準） |
| `.ktp` | 非圧縮プロジェクト |
| `.ktt` | プロジェクトテンプレート |

XMLルート要素は `kitsunetone-project`、作成元は `KitsuneTone` として保存されます。LMMSの `.mmp`、`.mmpz`、`.mpt` は互換プロジェクトとして扱わず、直接は開きません。

## 主な機能

- VST3インスツルメントとVST3エフェクトのネイティブホスティング
- GUI、パラメーター、MIDI、トラック機能をタブ化した共通プラグイン画面
- VST3パラメーターのオートメーションと状態保存
- ARA 2対応プラグイン向けの音声ソース、再生領域、テンポ・拍子連携
- サンプルトラックとインスツルメントトラックへの録音
- ピッチを保つタイムストレッチ
- MIDI入出力とトラック間MIDIルーティング
- 音声・MIDIファイルのドラッグ＆ドロップ
- 常時最大表示のトラックエディター
- Noto Sans / Noto Sans JPを使用した日本語UI
- PortAudio（WASAPI等）とSDLによる音声出力

## Windows SmartScreenとコード署名

配布工程はAuthenticode署名に対応しています。署名には信頼されたコード署名証明書、またはAzure Artifact Signingの設定が必要です。証明書を用意し、すべてのリリースで同じ発行元としてEXE・DLL・インストーラへSHA-256署名とRFC 3161タイムスタンプを付けることで、発行元の信頼を蓄積できます。

署名されていない新しいバイナリや、ハッシュが変わった直後のビルドではSmartScreen警告を完全には防げません。狐Toneは警告を回避するような不正な仕組みを使用しません。

## ソースからのビルド

必要な主な環境:

- Visual Studio 2022
- Qt 6.8.x (`msvc2022_64`)
- vcpkg
- CMake / Ninja

```bat
vcpkg install --triplet x64-windows

cmake -S . -B build -G Ninja ^
  -DCMAKE_BUILD_TYPE=RelWithDebInfo ^
  -DWANT_QT6=ON -DWANT_SDL=ON -DWANT_JACK=OFF ^
  -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64 ^
  -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake ^
  -DVCPKG_TARGET_TRIPLET=x64-windows

cmake --build build
```

ビルド後の実行ファイルは `build/KitsuneTone.exe` です。配布パッケージの生成は `packaging/windows/build-release.ps1` を使用します。

## ライセンスと帰属

狐ToneはLMMSから派生したGPLソフトウェアです。LMMS由来のコードと狐Toneで加えた変更は、各ファイルに別の記載がない限り、[GNU General Public License version 2 or later](LICENSE.txt)（GPL-2.0-or-later）の条件で利用・再配布できます。

バイナリを再配布する場合は、対応する完全なソースコード、ビルドに必要な情報、ライセンス表示をGPLが定める方法で受領者へ提供してください。LMMSの著作権表示と貢献者への帰属は保持しています。

主な第三者コンポーネント:

| コンポーネント | ライセンス | 用途 |
| --- | --- | --- |
| [LMMS](https://github.com/LMMS/lmms) | GPL-2.0-or-later | 狐Toneの基盤 |
| [Steinberg VST 3 Plug-In SDK](https://github.com/steinbergmedia/vst3sdk) | MIT | VST3ホスティング |
| [ARA SDK](https://github.com/Celemony/ARA_SDK) | Apache-2.0 | ARA連携 |
| Noto Sans / Noto Sans JP | SIL Open Font License 1.1 | UIフォント |

LMMS本来のREADMEは [README.upstream.md](README.upstream.md) に保存しています。各第三者コンポーネントのライセンスとNOTICEも、対応するソースディレクトリに保持しています。
