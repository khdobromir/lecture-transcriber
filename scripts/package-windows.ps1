[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$QtRoot,
    [string]$WorkDirectory = "$env:TEMP\transcribe-package",
    [string]$Destination = "$PSScriptRoot\..\dist",
    [switch]$SkipTests,
    [switch]$RealSmoke,
    [switch]$Release
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$Project = (Resolve-Path "$PSScriptRoot\..").Path
$QtRoot = (Resolve-Path $QtRoot).Path
$Lock = Get-Content "$Project\packaging\windows\dependencies.json" -Raw | ConvertFrom-Json
$SourceLock = Get-Content "$Project\packaging\source-inputs.json" -Raw -Encoding utf8 | ConvertFrom-Json
$BinaryHashes = @($Lock.downloads | ForEach-Object { $_.sha256 })
foreach ($Source in $SourceLock.downloads) {
    $MatchesBinary = @($Source.for_binary_sha256 | Where-Object { $BinaryHashes -contains $_ })
    if ($MatchesBinary.Count -eq 0) { throw "Source input does not match pinned binary: $($Source.name)" }
}
if ($SourceLock.qt.version -ne $Lock.qt -or @($SourceLock.qt.downloads | Where-Object { $_.revision -ne $Lock.qt }).Count -ne 0) {
    throw 'Qt source input does not match pinned SDK.'
}
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
function Get-SourceIdentity([string]$Expected = '') {
    $Arguments = @("$Project\scripts\package_source.py", $Project)
    if ($Expected) { $Arguments += @('--expect', $Expected) }
    if ($Release) { $Arguments += '--release' }
    if ($SkipTests) { $Arguments += '--skip-tests' }
    $Result = & python @Arguments
    if ($LASTEXITCODE -ne 0) { throw 'Source identity/release guard failed.' }
    return ($Result | ConvertFrom-Json)
}
$Identity = Get-SourceIdentity
$IdentityFile = Join-Path $Stage 'source-identity.json'
$Identity | ConvertTo-Json | Set-Content $IdentityFile -Encoding utf8
Invoke-Checked cmake @('-S', $Project, '-B', $Build, '-G', 'Visual Studio 17 2022', '-A', 'x64',
    "-DCMAKE_PREFIX_PATH=$QtRoot", '-DTRANSCRIBE_BUILD_GUI=ON', '-DBUILD_TESTING=ON', '-DTRANSCRIBE_WARNINGS_AS_ERRORS=ON')
Invoke-Checked cmake @('--build', $Build, '--config', 'Release', '--parallel', '4')
# The GUI test executables share gui/Release. Their import policy, like the
# shipped GUI's, excludes PATH, so deploy Qt (including QtTest) beside them
# before CTest. This test-only deployment never enters the installed bundle.
Invoke-Checked "$QtRoot\bin\windeployqt.exe" @('--release', '--qmldir', "$Project\gui", "$Build\gui\Release\test_qml.exe")
if (-not $SkipTests) { Invoke-Checked ctest @('--test-dir', $Build, '-C', 'Release', '--output-on-failure') }
Invoke-Checked cmake @('--install', $Build, '--config', 'Release', '--prefix', $Bundle)
# Keep the app/CLI beside each other; qt.conf and deployed QML/plugins stay in bin.
$Bin = Join-Path $Bundle 'bin'
$Tools = Join-Path $Bin 'tools'
$Licenses = Join-Path $Bundle 'licenses'
New-Item -ItemType Directory -Force -Path $Tools, $Licenses | Out-Null
'1' | Set-Content "$Tools\.transcribe-bundle" -Encoding ascii
Invoke-Checked git @('init', $Whisper)
# The source SHA-256 pins are Git blob bytes (LF), independent of the host's
# global Git settings. Configure this checkout before materializing the pin.
Invoke-Checked git @('-C', $Whisper, 'config', 'core.autocrlf', 'false')
Invoke-Checked git @('-C', $Whisper, 'config', 'core.eol', 'lf')
Invoke-Checked git @('-C', $Whisper, 'remote', 'add', 'origin', $Lock.whisper.repository)
Invoke-Checked git @('-C', $Whisper, 'fetch', '--depth', '1', 'origin', $Lock.whisper.revision)
Invoke-Checked git @('-C', $Whisper, 'checkout', '--detach', $Lock.whisper.revision)
Invoke-Checked python @("$Project\scripts\patch-whisper-windows.py", $Whisper)
Invoke-Checked cmake @('-S', $Whisper, '-B', $WhisperBuild, '-G', 'Visual Studio 17 2022', '-A', 'x64',
    '-DCMAKE_CXX_STANDARD=17', '-DCMAKE_CXX_STANDARD_REQUIRED=ON',
    '-DWHISPER_BUILD_TESTS=OFF', '-DWHISPER_BUILD_EXAMPLES=ON', '-DWHISPER_SDL2=OFF',
    '-DGGML_NATIVE=OFF', '-DGGML_BACKEND_DL=ON', '-DGGML_CPU_ALL_VARIANTS=ON', '-DBUILD_SHARED_LIBS=ON',
    '-DGGML_OPENMP=OFF', '-DGGML_CUDA=OFF', '-DGGML_VULKAN=OFF', '-DGGML_OPENCL=OFF')
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
foreach ($Source in @($SourceLock.downloads) + @($SourceLock.qt.downloads)) { $null = Get-Verified $Source }
Invoke-Checked python @("$Project\scripts\package_notices.py", "$Project\packaging\source-inputs.json", $Stage,
    "$Licenses\Qt-source-notices")
if (-not $SkipTests) { Invoke-Checked "$Build\Release\test_split.exe" @("$Tools\ffmpeg.exe") }
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
$null = Get-SourceIdentity $IdentityFile
$CompilerMetadata = @(Get-ChildItem "$Build\CMakeFiles" -Recurse -Filter CMakeCXXCompiler.cmake)
if ($CompilerMetadata.Count -ne 1) { throw 'Cannot identify the configured C++ compiler.' }
$CompilerVersion = [regex]::Match((Get-Content $CompilerMetadata[0].FullName -Raw), 'set\(CMAKE_CXX_COMPILER_VERSION "([^"]+)"\)').Groups[1].Value
if (-not $CompilerVersion) { throw 'Missing configured C++ compiler version.' }
$Manifest = [ordered]@{
    version = 1; platform = 'Windows 11 x64'; qt = $QtVersion
    application_version = $Identity.application_version
    source = $Identity.source; dirty = $Identity.dirty
    source_fingerprint = $Identity.source_fingerprint; release = [bool]$Release
    toolchain = [ordered]@{ compiler = "MSVC $CompilerVersion"; cmake = (& cmake --version | Select-Object -First 1); python = (& python --version) }
    whisper = $Lock.whisper.revision; tested = (-not $SkipTests)
    downloads = $Lock.downloads; source_inputs = $SourceLock; files = @()
}
$Manifest.files = @(Get-ChildItem $Bundle -Recurse -File | ForEach-Object {
    [ordered]@{ path = $_.FullName.Substring($Bundle.Length + 1).Replace('\', '/'); sha256 = (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
})
$Manifest | ConvertTo-Json -Depth 8 | Set-Content "$Bundle\package-manifest.json" -Encoding utf8
$Mode = if ($Release) { 'release' } else { 'diagnostic' }
$Name = 'Transcribe-' + $Identity.application_version + '-windows-x64-' + $Mode + '-' + $Identity.source.Substring(0, 12) + '-' + (Split-Path $Stage -Leaf) + '.zip'
$Zip = Join-Path $Stage $Name
Compress-Archive -Path $Bundle -DestinationPath $Zip -CompressionLevel Optimal
# Validate and run the actual final archive from a fresh Unicode directory.
$SmokeArguments = @("$Project\tests\smoke_windows_package.py", $Zip, $Manifest.source,
    '--backend-probe', "$Build\Release\test_backend_probe.dll",
    '--backend-dependency', "$Build\Release\test_backend_dependency.dll")
if ($RealSmoke) {
    $SmokeArguments += @('--real-gui', "$Build\gui\Release\test_gui_real.exe", '--workspace', "$Stage\real-smoke")
}
Invoke-Checked python $SmokeArguments
$WhisperArchive = Join-Path $Stage 'whisper-source.tar'
Invoke-Checked git @('-C', $Whisper, 'archive', '--format=tar', "--output=$WhisperArchive", 'HEAD')
$Inputs = [ordered]@{ downloads = @($Lock.downloads) + @($SourceLock.downloads) + @($SourceLock.qt.downloads) + @([ordered]@{
    name = 'whisper-source'; filename = 'whisper-source.tar'; revision = $Lock.whisper.revision
    url = $Lock.whisper.repository; sha256 = (Get-FileHash $WhisperArchive -Algorithm SHA256).Hash.ToLowerInvariant()
}) }
$InputsFile = Join-Path $Stage 'build-input-records.json'
$Inputs | ConvertTo-Json -Depth 8 | Set-Content $InputsFile -Encoding utf8
$InputsArchive = $Zip + '.build-inputs.tar.gz'
Invoke-Checked python @("$Project\scripts\package_inputs.py", $InputsFile, $Stage, $InputsArchive,
    '--material', "$Project\packaging\windows\dependencies.json", '--material', "$Project\scripts\patch-whisper-windows.py",
    '--material', "$Project\packaging\windows\whisper-unicode.hpp", '--material', "$Bundle\package-manifest.json",
    '--material', "$Project\packaging\source-inputs.json")
$null = Get-SourceIdentity $IdentityFile
(Get-FileHash $Zip -Algorithm SHA256).Hash.ToLowerInvariant() + '  ' + (Split-Path $Zip -Leaf) |
    Set-Content ($Zip + '.sha256') -Encoding ascii
(Get-FileHash $InputsArchive -Algorithm SHA256).Hash.ToLowerInvariant() + '  ' + (Split-Path $InputsArchive -Leaf) |
    Set-Content ($InputsArchive + '.sha256') -Encoding ascii
$Published = Join-Path $Destination $Name
if (Test-Path $Published) { throw 'Refusing to replace an existing candidate.' }
Move-Item -LiteralPath ($Zip + '.sha256'), ($Zip + '.validation.json') -Destination $Destination
Move-Item -LiteralPath $InputsArchive, ($InputsArchive + '.sha256') -Destination $Destination
Move-Item -LiteralPath $Zip -Destination $Published
Write-Host "Package: $Published"
Write-Host "Build evidence and staged files: $Stage"
