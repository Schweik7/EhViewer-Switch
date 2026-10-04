param(
    [ValidateSet('build', 'clean', 'rebuild')]
    [string]$Target = 'build'
)

$ErrorActionPreference = 'Stop'
$projectDir = Split-Path -Parent $MyInvocation.MyCommand.Path

$devkitPro = $env:DEVKITPRO
if (-not $devkitPro -or -not (Test-Path $devkitPro)) {
    $devkitPro = @('C:\devkitPro', 'D:\devkitPro') |
        Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $devkitPro) { throw 'devkitPro not found' }

$bash = Join-Path $devkitPro 'msys2\usr\bin\bash.exe'
if (-not (Test-Path $bash)) { throw "MSYS2 bash not found: $bash" }

$drive = $projectDir.Substring(0, 1).ToLower()
$rest = $projectDir.Substring(2).Replace('\', '/')
$msysProjectDir = "/$drive$rest"
$makeTarget = switch ($Target) {
    'clean'   { 'clean' }
    'rebuild' { 'clean all' }
    default   { '' }
}

$script = @"
export DEVKITPRO=/opt/devkitpro
export DEVKITA64=/opt/devkitpro/devkitA64
export PATH=`$DEVKITPRO/tools/bin:`$DEVKITA64/bin:`$PATH
cd '$msysProjectDir' || exit 1
make $makeTarget
"@

$scriptFile = Join-Path ([System.IO.Path]::GetTempPath()) 'ehviewer_switch_build.sh'
[System.IO.File]::WriteAllText($scriptFile, $script.Replace("`r`n", "`n"))
try {
    & $bash -l $scriptFile
    if ($LASTEXITCODE -ne 0) { throw "Build failed (exit $LASTEXITCODE)" }
}
finally {
    Remove-Item $scriptFile -ErrorAction SilentlyContinue
}

$nro = Join-Path $projectDir 'EhViewerSwitch.nro'
if (Test-Path $nro) {
    $size = [math]::Round((Get-Item $nro).Length / 1MB, 1)
    Write-Host "Built: $nro ($size MB)"
}

