param([string]$Jdk = "$PSScriptRoot/../../../../.tools/jdk17", [string]$AndroidJar = "$PSScriptRoot/../../../../.tools/android-sdk/platforms/android-36/android.jar")
$ErrorActionPreference='Stop'
$classes=Join-Path $env:TEMP ('brawl-touch-'+[guid]::NewGuid())
New-Item -ItemType Directory $classes | Out-Null
try {
    $java=Join-Path $Jdk 'bin/java.exe'
    $javac=Join-Path $Jdk 'bin/javac.exe'
    $source=Join-Path $PSScriptRoot '../app/src/main/java/com/wither/nsmbw'
    & $javac --release 17 -encoding UTF-8 -d $classes "$source/BrawlTouchInput.java" "$source/MenuTouchInput.java" "$PSScriptRoot/TouchInputHostTest.java" "$PSScriptRoot/MenuTouchInputHostTest.java"
    if ($LASTEXITCODE -ne 0) { throw 'Host test compilation failed' }
    & $java -cp $classes com.wither.nsmbw.TouchInputHostTest
    if ($LASTEXITCODE -ne 0) { throw 'Host touch tests failed' }
    & $java -cp $classes com.wither.nsmbw.MenuTouchInputHostTest
    if ($LASTEXITCODE -ne 0) { throw 'Host direct-menu touch tests failed' }
    if (-not (Test-Path -LiteralPath $AndroidJar -PathType Leaf)) {
        throw "Android SDK jar is missing: $AndroidJar"
    }
    $sources=Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot '../app/src/main/java') -Filter '*.java' -Recurse | ForEach-Object FullName
    $testSources=Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot '../app/src/androidTest/java') -Filter '*.java' -Recurse | ForEach-Object FullName
    & $javac --release 17 -encoding UTF-8 -cp $AndroidJar -d $classes @sources @testSources
    if ($LASTEXITCODE -ne 0) {throw 'Android source/instrumentation compilation failed'}
    Write-Output 'PASS: complete production Java frontend and Android instrumentation compile against Android SDK'
} finally {
    Remove-Item -LiteralPath $classes -Recurse -Force -ErrorAction SilentlyContinue
}
