# Uploads the built NRO and a GitHub-shaped latest.json to the update mirror
# (https://download.psyventures.cn/ehviewer/, served from /var/www/download).
# The app checks this mirror first and the GitHub release second, so publish
# both: `gh release create vX.Y.Z ...` and then this script.
#
#   ./tools/publish_mirror.ps1 -SshHost root@<vps> [-Notes "what changed"]
param(
    [Parameter(Mandatory = $true)][string]$SshHost,
    [string]$Notes = ""
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$nro = Join-Path $root 'EhViewerSwitch.nro'
$version = (Select-String -Path (Join-Path $root 'Makefile') -Pattern '^APP_VERSION\s*:=\s*(\S+)').Matches[0].Groups[1].Value
if (-not (Test-Path $nro)) { throw "Build first: $nro is missing" }
$size = (Get-Item $nro).Length

$release = [ordered]@{
    tag_name = "v$version"
    body     = $Notes
    html_url = "https://github.com/Schweik7/EhViewer-Switch/releases/tag/v$version"
    assets   = @([ordered]@{
        name                 = 'EhViewerSwitch.nro'
        size                 = $size
        browser_download_url = 'https://download.psyventures.cn/ehviewer/EhViewerSwitch.nro'
    })
}
$json = Join-Path ([IO.Path]::GetTempPath()) 'ehviewer-latest.json'
[IO.File]::WriteAllText($json, ($release | ConvertTo-Json -Depth 4), (New-Object Text.UTF8Encoding $false))

# Upload the NRO under a temporary name first so a half-written file is never
# served, then switch latest.json last.
ssh $SshHost 'mkdir -p /var/www/download/ehviewer'
scp $nro "${SshHost}:/var/www/download/ehviewer/EhViewerSwitch.nro.part"
ssh $SshHost "cd /var/www/download/ehviewer && test `$(stat -c %s EhViewerSwitch.nro.part) -eq $size && mv EhViewerSwitch.nro.part EhViewerSwitch.nro"
scp $json "${SshHost}:/var/www/download/ehviewer/latest.json"
Write-Host "Published v$version ($size bytes) to the mirror."
