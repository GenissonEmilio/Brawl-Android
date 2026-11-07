param([string]$Jdk = "$PSScriptRoot/../../../../.tools/jdk17")
$ErrorActionPreference='Stop'
$classes=Join-Path $env:TEMP ('brawl-import-'+[guid]::NewGuid())
New-Item -ItemType Directory $classes | Out-Null
try {
    $source=Join-Path $PSScriptRoot '../app/src/main/java/com/wither/nsmbw'
    $helpers=@('GameInstallTransaction','BrawlInstallValidator','SharedSettingsFile','ImportLifecycle')
    $sources=@()
    foreach($helper in $helpers) {
        $sources+=Join-Path $source "$helper.java"
        $sources+=Join-Path $PSScriptRoot "${helper}HostTest.java"
    }
    & "$Jdk/bin/javac.exe" --release 17 -encoding UTF-8 -d $classes @sources
    if ($LASTEXITCODE -ne 0) {throw 'Import/settings/lifecycle regression compilation failed'}
    foreach($helper in $helpers) {
        & "$Jdk/bin/java.exe" -Xmx128m -cp $classes "com.wither.nsmbw.${helper}HostTest"
        if ($LASTEXITCODE -ne 0) {throw "$helper regressions failed"}
    }
} finally {
    Remove-Item -LiteralPath $classes -Recurse -Force -ErrorAction SilentlyContinue
}
