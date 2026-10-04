[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$QtRoot,
    [string]$WorkDirectory = "$env:TEMP\transcribe-package",
    [string]$Destination = "$PSScriptRoot\..\dist",
    [switch]$SkipTests
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$Project = (Resolve-Path "$PSScriptRoot\..").Path
$QtRoot = (Resolve-Path $QtRoot).Path
$Lock = Get-Content "$Project\packaging\windows\dependencies.json" -Raw | ConvertFrom-Json
if (-not [Environment]::Is64BitProcess) { throw 'Run 64-bit PowerShell on Windows x64.' }
if (-not (Test-Path "$QtRoot\bin\windeployqt.exe")) { throw 'QtRoot must be the MSVC x64 Qt SDK directory.' }
$QtVersion = (& "$QtRoot\bin\qmake.exe" -query QT_VERSION).Trim()
if ($QtVersion -ne $Lock.qt) { throw "The package requires Qt $($Lock.qt), found $QtVersion." }
$env:PATH = "$QtRoot\bin;$env:PATH"
New-Item -ItemType Directory -Force -Path $WorkDirectory, $Destination | Out-Null
$WorkDirectory = (Resolve-Path $WorkDirectory).Path
$Destination = (Resolve-Path $Destination).Path
$Stage = Join-Path $WorkDirectory ("run-" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $Stage | Out-Null
$Build = Join-Path $Stage 'build'
$Whisper = Join-Path $Stage 'whisper-source'
$WhisperBuild = Join-Path $Stage 'whisper-build'
$Bundle = Join-Path $Stage 'Transcribe'
function Invoke-Checked([string]$Executable, [string[]]$Arguments) {
    & $Executable @Arguments | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "$Executable failed with exit code $LASTEXITCODE" }
}
function Get-Verified($Dependency) {
    $Target = Join-Path $Stage $Dependency.filename
    Invoke-WebRequest -Uri $Dependency.url -OutFile ($Target + '.part')
    $Hash = (Get-FileHash ($Target + '.part') -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($Hash -ne $Dependency.sha256) { throw "SHA-256 mismatch: $($Dependency.name)" }
    Move-Item ($Target + '.part') $Target
    return $Target
}
Invoke-Checked cmake @('-S', $Project, '-B', $Build, '-G', 'Visual Studio 17 2022', '-A', 'x64',
    "-DCMAKE_PREFIX_PATH=$QtRoot", '-DTRANSCRIBE_BUILD_GUI=ON', '-DBUILD_TESTING=ON', '-DCMAKE_CXX_FLAGS=/WX')
Invoke-Checked cmake @('--build', $Build, '--config', 'Release', '--parallel', '4')
if (-not $SkipTests) { Invoke-Checked ctest @('--test-dir', $Build, '-C', 'Release', '--output-on-failure') }
Invoke-Checked cmake @('--install', $Build, '--config', 'Release', '--prefix', $Bundle)
# Keep the app/CLI beside each other; qt.conf and deployed QML/plugins stay in bin.
$Bin = Join-Path $Bundle 'bin'
$Tools = Join-Path $Bin 'tools'
$Licenses = Join-Path $Bundle 'licenses'
New-Item -ItemType Directory -Force -Path $Tools, $Licenses | Out-Null
Invoke-Checked git @('init', $Whisper)
Invoke-Checked git @('-C', $Whisper, 'remote', 'add', 'origin', $Lock.whisper.repository)
Invoke-Checked git @('-C', $Whisper, 'fetch', '--depth', '1', 'origin', $Lock.whisper.revision)
Invoke-Checked git @('-C', $Whisper, 'checkout', '--detach', $Lock.whisper.revision)
Invoke-Checked python @("$Project\scripts\patch-whisper-windows.py", $Whisper)
Invoke-Checked cmake @('-S', $Whisper, '-B', $WhisperBuild, '-G', 'Visual Studio 17 2022', '-A', 'x64',
    '-DWHISPER_BUILD_TESTS=OFF', '-DWHISPER_BUILD_EXAMPLES=ON', '-DWHISPER_SDL2=OFF',
    '-DGGML_NATIVE=OFF', '-DGGML_BACKEND_DL=ON', '-DGGML_CPU_ALL_VARIANTS=ON', '-DBUILD_SHARED_LIBS=ON',
    '-DGGML_OPENMP=OFF', '-DGGML_CUDA=OFF', '-DGGML_VULKAN=OFF', '-DGGML_OPENCL=OFF', '-DCMAKE_CXX_FLAGS=/utf-8')
Invoke-Checked cmake @('--build', $WhisperBuild, '--config', 'Release', '--parallel', '4')
Copy-Item "$WhisperBuild\bin\Release\whisper-cli.exe" $Tools
Get-ChildItem "$WhisperBuild\bin\Release" -Filter '*.dll' | Copy-Item -Destination $Tools
Copy-Item "$Whisper\LICENSE" "$Licenses\whisper-MIT.txt"
Copy-Item "$Project\packaging\windows\licenses\*" $Licenses
Copy-Item "$Project\third_party\nlohmann\LICENSE.MIT" "$Licenses\nlohmann-json-MIT.txt"
Copy-Item "$Project\LICENSE" "$Licenses\Transcribe-MIT.txt"
foreach ($Dependency in $Lock.downloads) {
    $Archive = Get-Verified $Dependency
    if ($Dependency.name -eq 'yt-dlp') { Copy-Item $Archive "$Tools\yt-dlp.exe" }
    elseif ($Dependency.name -eq 'ffmpeg') {
        $Extract = Join-Path $Stage 'ffmpeg'
        Expand-Archive -LiteralPath $Archive -DestinationPath $Extract
        $FFmpeg = @(Get-ChildItem $Extract -Recurse -Filter ffmpeg.exe)
        if ($FFmpeg.Count -ne 1) { throw 'Expected exactly one FFmpeg executable.' }
        Copy-Item "$($FFmpeg[0].Directory.FullName)\*" $Tools
        Get-ChildItem $Extract -Recurse -File | Where-Object { $_.Name -match '^(LICENSE|COPYING|README)' } |
            ForEach-Object { Copy-Item $_.FullName (Join-Path $Licenses ('FFmpeg-' + $_.Name)) }
    } else { throw "Unknown dependency $($Dependency.name)" }
}
# A ZIP must run on a machine without Visual Studio or an installed VC runtime.
# Deploy app-local CRT DLLs to both executable directories; child tools cannot
# rely on DLLs located in their parent application's bin directory.
$VsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$VisualStudio = (& $VsWhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath).Trim()
if ($LASTEXITCODE -ne 0 -or -not $VisualStudio) { throw 'Cannot locate the Visual Studio redistributable runtime.' }
$CRT = @(Get-ChildItem "$VisualStudio\VC\Redist\MSVC\*\x64\Microsoft.VC*.CRT" -Directory |
    Sort-Object FullName -Descending | Select-Object -First 1)
if ($CRT.Count -ne 1) { throw 'Cannot locate the x64 VC CRT DLLs for app-local deployment.' }
Get-ChildItem -LiteralPath ($CRT[0].FullName) -Filter '*.dll' | Copy-Item -Destination $Bin -Force
Get-ChildItem -LiteralPath ($CRT[0].FullName) -Filter '*.dll' | Copy-Item -Destination $Tools -Force
'MSVC runtime DLLs are Microsoft redistributable code from the Visual Studio SDK. Redistribution terms: https://visualstudio.microsoft.com/license-terms/ . No Visual Studio installation is required to run this package.' |
    Set-Content "$Licenses\MSVC-runtime.txt" -Encoding utf8
Copy-Item "$Project\packaging\windows\README.txt" "$Bundle\README.txt"
Copy-Item "$Project\packaging\windows\dependencies.json" "$Bundle\dependencies.json"
Copy-Item "$Project\packaging\windows\THIRD-PARTY.md" "$Bundle\THIRD-PARTY.md"
Copy-Item "$Project\scripts\patch-whisper-windows.py" "$Bundle\whisper-unicode-patch.py"
Copy-Item "$Project\packaging\windows\whisper-unicode.hpp" "$Bundle\whisper-unicode.hpp"
Invoke-Checked "$Bin\transcribe.exe" @('--version')
Invoke-Checked "$Tools\ffmpeg.exe" @('-version')
Invoke-Checked "$Tools\yt-dlp.exe" @('--version')
Invoke-Checked "$Tools\whisper-cli.exe" @('--help')
Invoke-Checked python @("$Project\tests\smoke_windows_package.py", $Bin)
$Manifest = [ordered]@{
    version = 1; platform = 'Windows 11 x64'; qt = $QtVersion
    source = (& git -C $Project rev-parse HEAD).Trim()
    dirty = [bool](& git -C $Project status --porcelain)
    whisper = $Lock.whisper.revision; tested = (-not $SkipTests)
    downloads = $Lock.downloads; files = @()
}
$Manifest.files = @(Get-ChildItem $Bundle -Recurse -File | ForEach-Object {
    [ordered]@{ path = $_.FullName.Substring($Bundle.Length + 1).Replace('\', '/'); sha256 = (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
})
$Manifest | ConvertTo-Json -Depth 8 | Set-Content "$Bundle\package-manifest.json" -Encoding utf8
$Zip = Join-Path $Destination ('Transcribe-windows-x64-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.zip')
Compress-Archive -Path $Bundle -DestinationPath $Zip -CompressionLevel Optimal
(Get-FileHash $Zip -Algorithm SHA256).Hash.ToLowerInvariant() + '  ' + (Split-Path $Zip -Leaf) |
    Set-Content ($Zip + '.sha256') -Encoding ascii
Write-Host "Package: $Zip"
Write-Host "Build evidence and staged files: $Stage"
