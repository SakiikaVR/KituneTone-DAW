// KitsuneTone addition: when the UI language is Japanese, effect and category
// names keep their original English text and gain a small Japanese reading
// above it (furigana-style, via <ruby>). Serialized state, presets and
// drag&drop always use the plain English names (they act as state keys), so
// this is a pure display-layer annotation.
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
        'EQ': 'イコライザー',
        'Lo-Fi': 'ローファイ',
        'Modulation': 'モジュレーション',
        'Others': 'その他',
        'Resonator': 'レゾネーター',
        'Reverb': 'リバーブ',
        'Saturation': 'サチュレーション',
        'Spatial': '空間系',
        'Control': '制御'
    };

    // Build "<ruby>English<rt>日本語</rt></ruby>" inside the element, or plain
    // English text when inactive/unknown. Returns nothing; replaces content.
    function applyRuby(element, englishName, reading) {
        while (element.firstChild) { element.removeChild(element.firstChild); }
        if (active && reading) {
            const ruby = document.createElement('ruby');
            ruby.className = 'kitsune-name';
            ruby.appendChild(document.createTextNode(englishName));
            const rt = document.createElement('rt');
            rt.textContent = reading;
            ruby.appendChild(rt);
            element.appendChild(ruby);
        } else {
            element.appendChild(document.createTextNode(englishName));
        }
    }

    window.kitsuneApplyPluginName = function (element, name) {
        applyRuby(element, name, names[name]);
    };
    window.kitsuneApplyCategoryName = function (element, name) {
        applyRuby(element, name, categories[name]);
    };
    // Plain-text fallbacks (English) for code paths that need a string.
    window.kitsuneLocalizedPluginName = function (name) { return name; };
    window.kitsuneLocalizedCategoryName = function (name) { return name; };

    if (active && document.head) {
        const style = document.createElement('style');
        style.textContent =
            'ruby.kitsune-name { ruby-position: over; }\n' +
            'ruby.kitsune-name rt { font-size: 55%; opacity: 0.8; line-height: 1; }\n';
        document.head.appendChild(style);
    }
})();
