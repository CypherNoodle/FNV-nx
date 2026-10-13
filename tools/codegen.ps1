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
[entrypoint.functions."0x82C17A70"]
size = 12
[entrypoint.functions."0x8282C8E0"]
size = 24
[entrypoint.functions."0x82CA7E70"]
size = 16
[entrypoint.functions."0x82CA7FD0"]
size = 40
[entrypoint.functions."0x82CA7FF8"]
size = 40
[entrypoint.functions."0x82CA8020"]
size = 40
[entrypoint.functions."0x82CA8048"]
size = 40
[entrypoint.functions."0x82CA8460"]
size = 32
[entrypoint.functions."0x82D1BD88"]
size = 44
[entrypoint.functions."0x82E42900"]
size = 80
[entrypoint.functions."0x82E7C880"]
size = 44
"@
# Original 8-byte this-pointer adjustors referenced by Xbox vtables. The first
# missing entry (82D1E938) was observed when starting a new game on Switch.
# Each entry is addi r3,r3,delta followed by a tail branch to an existing body.
$adjustors = @(
    '8286CCC8', '82D18460', '82D190B0', '82D1E918', '82D1E920',
    '82D1E928', '82D1E930', '82D1E938', '82D1E948', '82D1E950',
    '82D250B8', '82D250C0', '82D250D0', '82D250D8', '82D250E0',
    '82D25388', '82D25398', '82D253A0', '82D253A8', '82D253B0',
    '82D3A5D0', '82D4D540'
)
foreach ($address in $adjustors) {
    $manifest += "`n[entrypoint.functions.`"0x$address`"]`nsize = 8`n"
}
$manifestPath = Join-Path $local 'fnv_xbox_manifest.toml'
[IO.File]::WriteAllText($manifestPath, $manifest)
& $Rexglue --log-level info codegen $manifestPath *> (Join-Path $local 'codegen.log')
if ($LASTEXITCODE -ne 0) { throw "Codegen failed ($LASTEXITCODE); inspect local/codegen.log. No force was used." }
Write-Host 'Codegen complete. Generated game code stays private.'
