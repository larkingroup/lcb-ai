param([string]$Zig = "$env:LOCALAPPDATA\LTI\tools\zig-x86_64-windows-0.16.0\zig.exe")
$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    if (!(Test-Path -LiteralPath $Zig)) { throw 'Install Zig 0.16.0 or pass -Zig with the compiler path. See README.md.' }
    New-Item -ItemType Directory -Force build | Out-Null
    $common = @('cc', '-std=c17', '-O2', '-Wall', '-Wextra', '-Werror', '-fstack-protector-strong', '-D_FORTIFY_SOURCE=2', '-DCJSON_NESTING_LIMIT=32', '-Isrc', '-Ivendor/cjson')
    $core = @('src/conversation.c', 'vendor/cjson/cJSON.c')
    & $Zig @common @core src/local.c src/winmain.c -lwinhttp -luser32 -lgdi32 '-Wl,--subsystem,windows' '-Wl,--dynamicbase' '-Wl,--nxcompat' -o build/lti-ai.exe
    if ($LASTEXITCODE) { throw 'GUI build failed' }
    & $Zig @common @core src/local.c src/cli.c -lwinhttp -o build/lti-cli.exe
    if ($LASTEXITCODE) { throw 'CLI build failed' }
    & $Zig @common -UNDEBUG @core tests/core_test.c -o build/core-test.exe
    if ($LASTEXITCODE) { throw 'Test build failed' }
    & .\build\core-test.exe
    if ($LASTEXITCODE) { throw 'Core tests failed' }
    Get-Item build/lti-ai.exe | Select-Object Name, Length
} finally { Pop-Location }
