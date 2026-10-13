param(
    [Parameter(Mandatory=$true)][string]$GameRoot,
    [string]$Rexglue = 'rexglue'
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot
$game = (Resolve-Path -LiteralPath $GameRoot).Path.Replace('\','/')
$xex = Join-Path $GameRoot 'default.xex'
if (-not (Test-Path -LiteralPath $xex)) { throw 'Missing default.xex' }
$expected = '4bda9f98ec3c46865f78aa678016e2678bd66625f6737d41cdd17f3bab0b40ac'
if ((Get-FileHash -LiteralPath $xex -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected) {
    throw 'Unsupported XEX revision: these function boundaries require the documented executable.'
}
if ($game.Contains("'")) { throw 'Game path contains an unsupported TOML literal quote' }
$local = Join-Path $projectRoot 'local'
New-Item -ItemType Directory -Force -Path $local | Out-Null
$manifest = @"
[project]
name = "fnv_xbox"
sdk_version = "0.10.0"
game_root = '$game'
[entrypoint]
file_path = '$game/default.xex'
out_directory_path = "../generated/default"
[entrypoint.functions."0x82D1B300"]
size = 4
[entrypoint.functions."0x828F0358"]
size = 32
[entrypoint.functions."0x82DC1948"]
size = 136
[entrypoint.functions."0x82D39950"]
size = 24
"@
$manifestPath = Join-Path $local 'fnv_xbox_manifest.toml'
[IO.File]::WriteAllText($manifestPath, $manifest)
& $Rexglue --log-level info codegen $manifestPath *> (Join-Path $local 'codegen.log')
if ($LASTEXITCODE -ne 0) { throw "Codegen failed ($LASTEXITCODE); inspect local/codegen.log. No force was used." }
Write-Host 'Codegen complete. Generated game code stays private.'
