param([string]$Compiler = "$PSScriptRoot/../../../../.tools/llvm-mingw/bin/x86_64-w64-mingw32-clang++.exe")
$ErrorActionPreference='Stop'
$testBinary=Join-Path $env:TEMP ('brawl-native-touch-'+[guid]::NewGuid()+'.exe')
& $Compiler -std=c++17 -Wall -Wextra -Werror "$PSScriptRoot/touch_pad_state_test.cpp" -o $testBinary
if ($LASTEXITCODE -ne 0) {throw 'Native touch state compilation failed'}
$env:PATH=(Split-Path $Compiler -Parent)+';'+$env:PATH
& $testBinary
if ($LASTEXITCODE -ne 0) {throw 'Native touch state regression failed'}
Write-Output "Temporary native test: $testBinary"
