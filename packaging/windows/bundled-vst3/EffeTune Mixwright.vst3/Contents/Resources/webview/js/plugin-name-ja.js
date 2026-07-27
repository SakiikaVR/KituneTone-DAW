// KitsuneTone addition: display-layer Japanese localization for effect and
// category names. Serialized state, presets and drag&drop keep the original
// English names (they act as state keys), so names are mapped at render time
// only and only when the UI language is Japanese.
(function () {
    'use strict';
    const active = (navigator.language || '').toLowerCase().startsWith('ja');
    const names = {
        'Level Meter': 'レベルメーター',
        'Oscilloscope': 'オシロスコープ',
        'Spectrogram': 'スペクトログラム',
        'Spectrum Analyzer': 'スペクトラムアナライザー',
        'Stereo Meter': 'ステレオメーター',
        'Channel Divider': 'チャンネルディバイダー',
        'DC Offset': 'DCオフセット',
        'Matrix': 'マトリクス',
        'MultiChannel Panel': 'マルチチャンネルパネル',
        'Mute': 'ミュート',
        'Polarity Inversion': '極性反転',
        'Stereo Balance': 'ステレオバランス',
        'Volume': 'ボリューム',
        'Delay': 'ディレイ',
        'Time Alignment': 'タイムアライメント',
        'Auto Leveler': 'オートレベラー',
        'Brickwall Limiter': 'ブリックウォールリミッター',
        'Compressor': 'コンプレッサー',
        'Expander': 'エキスパンダー',
        'Gate': 'ゲート',
        'Multiband Compressor': 'マルチバンドコンプレッサー',
        'Multiband Expander': 'マルチバンドエキスパンダー',
        'Multiband Transient': 'マルチバンドトランジェント',
        'Power Amp Sag': 'パワーアンプ電源サグ',
        'Transient Shaper': 'トランジェントシェイパー',
        '15Band GEQ': '15バンドグラフィックEQ',
        '15Band PEQ': '15バンドパラメトリックEQ',
        '5Band Dynamic EQ': '5バンドダイナミックEQ',
        '5Band PEQ': '5バンドパラメトリックEQ',
        'Band Pass Filter': 'バンドパスフィルター',
        'Comb Filter': 'コムフィルター',
        'Earphone Cable Sim': 'イヤホンケーブルシミュレーター',
        'Hi Pass Filter': 'ハイパスフィルター',
        'Lo Pass Filter': 'ローパスフィルター',
        'Loudness Equalizer': 'ラウドネスイコライザー',
        'Narrow Range': 'ナローレンジ',
        'Room EQ': 'ルームEQ',
        'Tilt EQ': 'ティルトEQ',
        'Tone Control': 'トーンコントロール',
        'Bit Crusher': 'ビットクラッシャー',
        'Digital Error Emulator': 'デジタルエラーエミュレーター',
        'DSD64 IMD Simulator': 'DSD64混変調シミュレーター',
        'Hum Generator': 'ハムノイズ生成',
        'Noise Blender': 'ノイズブレンダー',
        'Simple Jitter': 'ジッター',
        'Vinyl Artifacts': 'レコードノイズ',
        'Vinyl Simulator': 'レコード再生シミュレーター',
        'Doppler Distortion': 'ドップラー歪み',
        'Pitch Shifter': 'ピッチシフター',
        'Tremolo': 'トレモロ',
        'Wow Flutter': 'ワウフラッター',
        'Oscillator': 'オシレーター',
        'Horn Resonator': 'ホーンレゾネーター',
        'Horn Resonator Plus': 'ホーンレゾネーター+',
        'Modal Resonator': 'モーダルレゾネーター',
        'Dattorro Plate Reverb': 'プレートリバーブ',
        'FDN Reverb': 'FDNリバーブ',
        'IR Reverb': 'IRリバーブ',
        'RS Reverb': 'RSリバーブ',
        'Dynamic Saturation': 'ダイナミックサチュレーション',
        'Exciter': 'エキサイター',
        'Hard Clipping': 'ハードクリッピング',
        'Harmonic Distortion': '倍音歪み',
        'Multiband Saturation': 'マルチバンドサチュレーション',
        'Saturation': 'サチュレーション',
        'Sub Synth': 'サブシンセ',
        'Crossfeed Filter': 'クロスフィードフィルター',
        'MS Matrix': 'MSマトリクス',
        'Multiband Balance': 'マルチバンドバランス',
        'Stereo Blend': 'ステレオブレンド',
        'Section': 'セクション'
    };
    const categories = {
        'Analyzer': 'アナライザー',
        'Basics': '基本',
        'Delay': 'ディレイ',
        'Dynamics': 'ダイナミクス',
        'EQ': 'EQ',
        'Lo-Fi': 'ローファイ',
        'Modulation': 'モジュレーション',
        'Others': 'その他',
        'Resonator': 'レゾネーター',
        'Reverb': 'リバーブ',
        'Saturation': 'サチュレーション',
        'Spatial': '空間系',
        'Control': '制御'
    };
    window.kitsuneLocalizedPluginName = function (name) {
        return (active && names[name]) || name;
    };
    window.kitsuneLocalizedCategoryName = function (name) {
        return (active && categories[name]) || name;
    };
})();
