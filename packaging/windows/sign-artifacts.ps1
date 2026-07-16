[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string[]]$Path
)

$ErrorActionPreference = "Stop"

$pfx = $env:KITSUNETONE_SIGN_PFX
if ([string]::IsNullOrWhiteSpace($pfx)) {
    Write-Host "KITSUNETONE_SIGN_PFX is not set; artifacts will remain unsigned."
    exit 0
}
if (-not (Test-Path -LiteralPath $pfx -PathType Leaf)) {
    throw "Signing certificate not found: $pfx"
}

$signtool = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin" `
    -Filter signtool.exe -Recurse -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -match '\\x64\\signtool\.exe$' } |
    Sort-Object FullName -Descending |
    Select-Object -First 1 -ExpandProperty FullName
if (-not $signtool) {
    throw "signtool.exe was not found. Install the Windows SDK."
}

$timestampUrl = if ($env:KITSUNETONE_TIMESTAMP_URL) {
    $env:KITSUNETONE_TIMESTAMP_URL
} else {
    "http://timestamp.digicert.com"
}

foreach ($item in $Path) {
    $resolved = (Resolve-Path -LiteralPath $item).Path
    $arguments = @(
        "sign", "/fd", "SHA256",
        "/tr", $timestampUrl, "/td", "SHA256",
        "/f", $pfx
    )
    if ($env:KITSUNETONE_SIGN_PASSWORD) {
        $arguments += @("/p", $env:KITSUNETONE_SIGN_PASSWORD)
    }
    $arguments += $resolved

    & $signtool @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Signing failed: $resolved"
    }
    & $signtool verify /pa /v $resolved
    if ($LASTEXITCODE -ne 0) {
        throw "Signature verification failed: $resolved"
    }
}
