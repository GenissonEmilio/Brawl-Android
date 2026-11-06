param([string]$Jdk = "$PSScriptRoot/../../../../.tools/jdk17")
$ErrorActionPreference='Stop'
$classes=Join-Path $env:TEMP ('brawl-import-'+[guid]::NewGuid())
New-Item -ItemType Directory $classes | Out-Null
$source=Join-Path $PSScriptRoot '../app/src/main/java/com/wither/nsmbw'
& "$Jdk/bin/javac.exe" --release 17 -encoding UTF-8 -d $classes "$source/GameInstallTransaction.java" "$PSScriptRoot/GameInstallTransactionHostTest.java"
if ($LASTEXITCODE -ne 0) {throw 'Import regression compilation failed'}
& "$Jdk/bin/java.exe" -cp $classes com.wither.nsmbw.GameInstallTransactionHostTest
if ($LASTEXITCODE -ne 0) {throw 'Import transaction regressions failed'}
Write-Output "Temporary compiled classes: $classes"
