$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$outputDirectory = Join-Path $projectRoot 'build-host'
New-Item -ItemType Directory -Force $outputDirectory | Out-Null

$sources = @(
    (Join-Path $projectRoot 'tests\host_tests.cpp'),
    (Join-Path $projectRoot 'source\core\CookieConfig.cpp'),
    (Join-Path $projectRoot 'source\core\FileUtil.cpp'),
    (Join-Path $projectRoot 'source\core\GalleryManifest.cpp'),
    (Join-Path $projectRoot 'source\core\GalleryParser.cpp'),
    (Join-Path $projectRoot 'source\core\HtmlUtil.cpp'),
    (Join-Path $projectRoot 'source\core\Json.cpp'),
    (Join-Path $projectRoot 'source\core\Library.cpp'),
    (Join-Path $projectRoot 'source\core\ListLayout.cpp'),
    (Join-Path $projectRoot 'source\core\History.cpp'),
    (Join-Path $projectRoot 'source\core\I18n.cpp'),
    (Join-Path $projectRoot 'source\core\Settings.cpp'),
    (Join-Path $projectRoot 'source\core\Subscriptions.cpp'),
    (Join-Path $projectRoot 'source\core\SpiderInfo.cpp'),
    (Join-Path $projectRoot 'source\core\StorageLayout.cpp'),
    (Join-Path $projectRoot 'source\net\NetworkPlan.cpp'),
    (Join-Path $projectRoot 'source\reader\ReaderCore.cpp')
)
$executable = Join-Path $outputDirectory 'host_tests.exe'

$vcvars = 'D:\vs_buildtools\VC\Auxiliary\Build\vcvars64.bat'
if (Test-Path $vcvars) {
    $sourceArguments = ($sources | ForEach-Object { '"' + $_ + '"' }) -join ' '
    $compileCommand = "call $vcvars >nul && cl.exe /nologo /std:c++17 /EHsc /utf-8 " +
        "/D_CRT_SECURE_NO_WARNINGS /W4 /WX /I`"$(Join-Path $projectRoot 'source')`" " +
        "$sourceArguments /Fo$outputDirectory\ /Fe`"$executable`""
    & cmd.exe /d /c $compileCommand
} else {
    $compiler = @(
        'D:\Program Files\JetBrains\CLion 2026.1\bin\mingw\bin\g++.exe',
        (Get-Command g++.exe -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source -First 1)
    ) | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
    if (-not $compiler) { throw 'A native Windows C++ compiler is required for host tests.' }
    & $compiler -std=c++17 -Wall -Wextra -Wpedantic -Werror `
        "-I$(Join-Path $projectRoot 'source')" @sources -o $executable
}
if ($LASTEXITCODE -ne 0) { throw 'Host test compilation failed.' }

Push-Location $projectRoot
try {
    & $executable
    if ($LASTEXITCODE -ne 0) { throw 'Host tests failed.' }
} finally {
    Pop-Location
}
