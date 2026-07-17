[CmdletBinding()]
param(
    [string]$BuildDir,
    [string]$OutputDir,
    [string]$Version = "2.1.3",
    [string]$MakeNsis
)

$ErrorActionPreference = "Stop"
if (-not $BuildDir) {
    $BuildDir = Join-Path $PSScriptRoot "..\..\build"
}
if (-not $OutputDir) {
    $OutputDir = Join-Path $PSScriptRoot "..\..\release"
}
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$build = (Resolve-Path -LiteralPath $BuildDir).Path
$output = [System.IO.Path]::GetFullPath($OutputDir)
if (-not $output.StartsWith($repoRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "OutputDir must stay inside the repository: $output"
}

$appDir = Join-Path $output "KitsuneTone"
if (Test-Path -LiteralPath $output) {
    Remove-Item -LiteralPath $output -Recurse -Force
}
New-Item -ItemType Directory -Path $appDir | Out-Null

$runtimeFiles = @(
    "KitsuneTone.exe",
    "D3Dcompiler_47.dll", "dxcompiler.dll",
    "fftw3f.dll", "FLAC.dll", "libmp3lame.DLL", "mpg123.dll",
    "lilv-0.dll", "serd-0.dll", "sord-0.dll", "sratom-0.dll", "zix-0.dll",
    "ogg.dll", "opus.dll", "portaudio.dll", "samplerate.dll", "SDL2.dll",
    "sndfile.dll", "vorbis.dll", "vorbisenc.dll", "vorbisfile.dll",
    "Qt6Core.dll", "Qt6Gui.dll", "Qt6Network.dll", "Qt6Svg.dll",
    "Qt6Widgets.dll", "Qt6Xml.dll",
    "vc_redist.x64.exe"
)
foreach ($file in $runtimeFiles) {
    $source = Join-Path $build $file
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Required runtime file is missing: $source"
    }
    Copy-Item -LiteralPath $source -Destination $appDir
}

foreach ($directory in @(
    "generic", "iconengines", "imageformats", "networkinformation",
    "platforms", "styles", "tls"
)) {
    $source = Join-Path $build $directory
    if (Test-Path -LiteralPath $source -PathType Container) {
        Copy-Item -LiteralPath $source -Destination $appDir -Recurse
    }
}

$pluginDir = Join-Path $appDir "plugins"
New-Item -ItemType Directory -Path $pluginDir | Out-Null
foreach ($plugin in @(
    "vst3base.dll", "vst3instrument.dll", "vst3effect.dll",
    "midiimport.dll", "midiexport.dll"
)) {
    Copy-Item -LiteralPath (Join-Path $build "plugins\$plugin") -Destination $pluginDir
}

$vst3Dir = Join-Path $appDir "VST3"
New-Item -ItemType Directory -Path $vst3Dir | Out-Null
Copy-Item -LiteralPath (Join-Path $build "plugins\TriangleSynth.vst3") -Destination $vst3Dir

$dataDir = Join-Path $appDir "data"
New-Item -ItemType Directory -Path $dataDir | Out-Null
foreach ($directory in @("backgrounds", "fonts", "locale", "themes")) {
    $source = Join-Path $repoRoot "data\$directory"
    if (Test-Path -LiteralPath $source -PathType Container) {
        $destination = Join-Path $dataDir $directory
        New-Item -ItemType Directory -Path $destination -Force | Out-Null
        Get-ChildItem -LiteralPath $source -Force |
            Where-Object { $_.Name -ne "CMakeLists.txt" } |
            Copy-Item -Destination $destination -Recurse -Force
    }
}
$builtJapaneseLocale = Join-Path $repoRoot "data\locale\ja.qm"
if (Test-Path -LiteralPath $builtJapaneseLocale -PathType Leaf) {
    $localeDir = Join-Path $dataDir "locale"
    New-Item -ItemType Directory -Path $localeDir -Force | Out-Null
    Copy-Item -LiteralPath $builtJapaneseLocale -Destination $localeDir -Force
}
$requiredDataFiles = @(
    "themes\default\style.css",
    "themes\default\project_new.png",
    "fonts\NotoSansJP-Regular.otf",
    "locale\ja.qm"
)
foreach ($relativePath in $requiredDataFiles) {
    $packagedPath = Join-Path $dataDir $relativePath
    if (-not (Test-Path -LiteralPath $packagedPath -PathType Leaf)) {
        throw "Required UI asset is missing from the package: $packagedPath"
    }
}
$themeAssetCount = @(Get-ChildItem -LiteralPath (Join-Path $dataDir "themes\default") -Recurse -File).Count
if ($themeAssetCount -lt 250) {
    throw "Default theme is incomplete: only $themeAssetCount files were packaged."
}
$projectsDir = Join-Path $dataDir "projects"
$templatesDir = Join-Path $projectsDir "templates"
New-Item -ItemType Directory -Path $templatesDir -Force | Out-Null
Get-ChildItem -LiteralPath (Join-Path $repoRoot "data\projects\templates") -Filter *.ktt -File |
    Copy-Item -Destination $templatesDir

Copy-Item -LiteralPath (Join-Path $repoRoot "cmake\nsis\lmms.exe.manifest") `
    -Destination (Join-Path $appDir "KitsuneTone.exe.manifest")
foreach ($file in @(
    "README.md", "README.upstream.md", "LICENSE.txt", "AUTHORS", "CONTRIBUTORS"
)) {
    if (Test-Path -LiteralPath (Join-Path $repoRoot $file)) {
        Copy-Item -LiteralPath (Join-Path $repoRoot $file) -Destination $appDir
    }
}

$manualDir = Join-Path $appDir "docs"
New-Item -ItemType Directory -Path $manualDir | Out-Null
Copy-Item -LiteralPath (Join-Path $repoRoot "docs\USER_MANUAL.md") -Destination $manualDir

$licensesDir = Join-Path $appDir "licenses"
New-Item -ItemType Directory -Path $licensesDir | Out-Null
$thirdPartyLicenses = @{
    "VST3-SDK-LICENSE.txt" = "src\3rdparty\vst3sdk\LICENSE.txt"
    "VST3-PUBLIC-SDK-LICENSE.txt" = "src\3rdparty\vst3sdk\public.sdk\LICENSE.txt"
    "ARA-NOTICE.txt" = "src\3rdparty\ara\NOTICE.txt"
    "ARA-API-LICENSE.txt" = "src\3rdparty\ara\ARA_API\LICENSE.txt"
    "ARA-LIBRARY-LICENSE.txt" = "src\3rdparty\ara\ARA_Library\LICENSE.txt"
    "NOTO-OFL.txt" = "data\fonts\OFL.txt"
    "QT-LGPL-3.0.txt" = "src\3rdparty\qt5-x11embed\LICENSE.LGPLv3"
}
foreach ($entry in $thirdPartyLicenses.GetEnumerator()) {
    Copy-Item -LiteralPath (Join-Path $repoRoot $entry.Value) `
        -Destination (Join-Path $licensesDir $entry.Key)
}

$runtimePackages = @(
    "fftw3", "libflac", "mp3lame", "mpg123", "libogg", "opus",
    "portaudio", "libsamplerate", "libsndfile", "sdl2", "libvorbis",
    "lilv", "serd", "sord", "sratom", "zix"
)
foreach ($package in $runtimePackages) {
    $copyright = Join-Path $repoRoot "vcpkg_installed\x64-windows\share\$package\copyright"
    if (Test-Path -LiteralPath $copyright -PathType Leaf) {
        Copy-Item -LiteralPath $copyright `
            -Destination (Join-Path $licensesDir "runtime-$package.txt")
    }
}

$signScript = Join-Path $PSScriptRoot "sign-artifacts.ps1"
$signTargets = Get-ChildItem -LiteralPath $appDir -Recurse -File |
    Where-Object { $_.Extension -in @(".exe", ".dll", ".vst3") } |
    Select-Object -ExpandProperty FullName
& $signScript -Path $signTargets

$zipPath = Join-Path $output "KitsuneTone-$Version-win64-portable.zip"
$portableModeFile = Join-Path $appDir "portable_mode.txt"
try {
    New-Item -ItemType File -Path $portableModeFile -Force | Out-Null
    Compress-Archive -LiteralPath $appDir -DestinationPath $zipPath -CompressionLevel Optimal
}
finally {
    Remove-Item -LiteralPath $portableModeFile -Force -ErrorAction SilentlyContinue
}

if (-not $MakeNsis) {
    $MakeNsis = @(
        "${env:ProgramFiles(x86)}\NSIS\makensis.exe",
        "$env:ProgramFiles\NSIS\makensis.exe",
        (Join-Path $repoRoot "tools\nsis\makensis.exe"),
        (Join-Path $repoRoot "tools\nsis\nsis-3.12\makensis.exe")
    ) | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
}
if (-not $MakeNsis) {
    throw "makensis.exe was not found. Install NSIS or pass -MakeNsis."
}

$installerPath = Join-Path $output "KitsuneTone-$Version-win64-setup.exe"
$nsi = Join-Path $PSScriptRoot "KitsuneTone.nsi"
& $MakeNsis "/DAPPDIR=$appDir" "/DOUTFILE=$installerPath" $nsi
if ($LASTEXITCODE -ne 0) {
    throw "NSIS installer creation failed."
}
& $signScript -Path $installerPath

Write-Host "Portable:  $zipPath"
Write-Host "Installer: $installerPath"
