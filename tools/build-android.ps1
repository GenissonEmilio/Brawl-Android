# Cross-build the real Brawl engine as libmain.so. Run game extraction and
# translation first; dependency sources, toolchains and outputs stay local.
[CmdletBinding()]
param(
    [int]$Jobs = 0,
    [int]$TranslatedJobs = 0,
    [string]$ToolsDirectory,
    [string]$SdkDirectory,
    [string]$NdkDirectory,
    [string]$DependenciesDirectory,
    [string]$BuildDirectory,
    [string]$PythonExe,
    [switch]$SkipDependencies,
    [switch]$DependenciesOnly,
    [switch]$ConfigureOnly
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0

$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$workspace = Join-Path $repo 'wiicompiled'
if (-not $ToolsDirectory) { $ToolsDirectory = Join-Path (Split-Path -Parent $repo) '.tools' }
if (-not $SdkDirectory) { $SdkDirectory = Join-Path $ToolsDirectory 'android-sdk' }
if (-not $NdkDirectory) { $NdkDirectory = Join-Path $SdkDirectory 'ndk\30.0.16248370' }
if (-not $DependenciesDirectory) { $DependenciesDirectory = Join-Path $ToolsDirectory 'brawl-deps' }
if (-not $BuildDirectory) {
    # Share native objects between the dependency check and the translated game.
    $BuildDirectory = Join-Path $workspace 'android-deps-build'
}
$BuildDirectory = [IO.Path]::GetFullPath($BuildDirectory)
$DependenciesDirectory = [IO.Path]::GetFullPath($DependenciesDirectory)
$cmake = Join-Path $SdkDirectory 'cmake\3.31.6\bin\cmake.exe'
$ninja = Join-Path $SdkDirectory 'cmake\3.31.6\bin\ninja.exe'
$toolchain = Join-Path $NdkDirectory 'build\cmake\android.toolchain.cmake'
$logs = Join-Path $BuildDirectory 'logs'
[IO.Directory]::CreateDirectory($logs) | Out-Null

function CMakePath([string]$Path) { [IO.Path]::GetFullPath($Path).Replace('\', '/') }
function Invoke-BuildStep([string]$Name, [string]$Executable, [string[]]$Arguments) {
    $log = Join-Path $logs "$Name.log"
    Write-Host "${Name}: $log"
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { & $Executable @Arguments 2>&1 | ForEach-Object { "$_" } | Out-File -LiteralPath $log -Encoding utf8 }
    finally { $ErrorActionPreference = $previousPreference }
    if ($LASTEXITCODE -ne 0) {
        Get-Content -LiteralPath $log -Tail 45
        throw "$Name failed with exit code $LASTEXITCODE; see $log"
    }
}

foreach ($required in @($cmake, $ninja, $toolchain)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Required Android build tool is missing: $required" }
}
if ($Jobs -le 0) {
    # Translated shards are memory intensive; leave headroom for the OS and linker.
    $memoryGiB = (Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory / 1GB
    $Jobs = [Math]::Max(1, [Math]::Min([Environment]::ProcessorCount, [int][Math]::Floor(($memoryGiB - 3) / 2.5)))
}
if ($TranslatedJobs -le 0) { $TranslatedJobs = $Jobs }

if (-not $SkipDependencies) {
    if (-not $PythonExe) {
        $pythonCommand = Get-Command python -ErrorAction SilentlyContinue
        if ($pythonCommand) { $PythonExe = $pythonCommand.Source }
        else { throw 'Python 3.12+ is required to verify dependencies; pass -PythonExe <python.exe>.' }
    }
    Invoke-BuildStep 'dependencies' $PythonExe @((Join-Path $PSScriptRoot 'provision-brawl-deps.py'), '--destination', $DependenciesDirectory)
}
$preload = Join-Path $DependenciesDirectory 'sources.cmake'
if (-not (Test-Path -LiteralPath $preload -PathType Leaf)) { throw "Missing verified dependency preload: $preload" }
if (-not $DependenciesOnly -and -not (Test-Path -LiteralPath (Join-Path $workspace 'generated\build_shards\shards.cmake'))) {
    throw 'Translate the private Brawl inputs and emit generated/build_shards/shards.cmake before building the engine. Use -DependenciesOnly to validate the native dependency build first.'
}

# Keep host LLVM-MinGW and system package discovery out of the cross-build.
$previousPath = $env:PATH
try {
    $env:PATH = "$(Split-Path -Parent $cmake);$env:SystemRoot\System32;$env:SystemRoot"
    $configure = @(
        '-S', (CMakePath (Join-Path $workspace 'runtime')),
        '-B', (CMakePath $BuildDirectory), '-G', 'Ninja',
        '-C', (CMakePath $preload),
        "-DCMAKE_MAKE_PROGRAM=$(CMakePath $ninja)",
        "-DCMAKE_TOOLCHAIN_FILE=$(CMakePath $toolchain)",
        '-DANDROID_ABI=arm64-v8a', '-DANDROID_PLATFORM=android-30', '-DANDROID_STL=c++_static',
        '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_POSITION_INDEPENDENT_CODE=ON',
        '-DCMAKE_POLICY_DEFAULT_CMP0168=NEW', '-DBUILD_SHARED_LIBS=OFF',
        '-DAURORA_DAWN_PROVIDER=package', '-DAURORA_DAWN_LINKAGE=static',
        '-DAURORA_SDL3_PROVIDER=vendor', '-DAURORA_SDL3_LINKAGE=static',
        "-DMKW_TRANSLATED_COMPILE_JOBS=$TranslatedJobs",
        "-DMKW_BUILD_PRODUCTS=$(if ($DependenciesOnly) { 'OFF' } else { 'ON' })"
    )
    Invoke-BuildStep 'configure' $cmake $configure
    if (-not $ConfigureOnly) {
        $targets = if ($DependenciesOnly) {
            @('aurora_gx', 'aurora_pad', 'aurora_si', 'aurora_vi', 'aurora_mtx', 'mkw_cryptopp', 'mbedtls', 'mkw_platform')
        } else { @('WiiCompiled') }
        Invoke-BuildStep 'compile' $cmake (@('--build', $BuildDirectory, '--parallel', "$Jobs", '--target') + $targets)
    }
} finally { $env:PATH = $previousPath }
if ($ConfigureOnly) { Write-Host "Configured Android ARM64 build: $BuildDirectory" }
elseif ($DependenciesOnly) { Write-Host "Built Android ARM64 native dependencies: $BuildDirectory" }
else {
    $library = Join-Path $BuildDirectory 'libmain.so'
    if (-not (Test-Path -LiteralPath $library -PathType Leaf)) { throw "Engine library missing after build: $library" }
    Write-Host "Built Brawl Android ARM64 engine: $library"
}
