param(
    [Parameter(Mandatory=$true)][string]$HorizonSdk,
    [Parameter(Mandatory=$true)][string]$NvkSdk,
    [Parameter(Mandatory=$true)][string]$DevkitPro,
    [Parameter(Mandatory=$true)][string]$GeneratedDir,
    [string]$CMake = 'cmake',
    [string]$Ninja = 'ninja',
    [ValidateRange(1,16)][int]$Jobs = 4,
    [switch]$ConfigureOnly
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
$sdk = (Resolve-Path -LiteralPath $HorizonSdk).Path
$nvk = (Resolve-Path -LiteralPath $NvkSdk).Path
$devkit = (Resolve-Path -LiteralPath $DevkitPro).Path
$generated = (Resolve-Path -LiteralPath $GeneratedDir).Path
foreach ($required in @(
    (Join-Path $sdk 'cmake/rexglue_switch.cmake'),
    (Join-Path $nvk 'lib/libvulkan.a'),
    (Join-Path $devkit 'libnx/switch.specs'),
    (Join-Path $generated 'sources.cmake')
)) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Missing prerequisite: $required" }
}
$ninjaPath = (Get-Command $Ninja -ErrorAction Stop).Source
$build = Join-Path $root 'out/switch'
& $CMake -S $root -B $build -G Ninja '-DCMAKE_BUILD_TYPE=Release' `
    "-DCMAKE_TOOLCHAIN_FILE=$root/cmake/switch-devkitA64.cmake" `
    "-DCMAKE_MAKE_PROGRAM=$ninjaPath" "-DDEVKITPRO=$devkit" `
    "-DFNV_HORIZON_SDK=$sdk" "-DREXGLUE_SWITCH_NVK_SDK=$nvk" `
    "-DFNV_GENERATED_DIR=$generated" '-DFNV_HOST_BOOT_TRACE=OFF'
if ($LASTEXITCODE -ne 0) { throw "Switch configure failed ($LASTEXITCODE)" }
if ($ConfigureOnly) { return }
& $CMake --build $build --parallel $Jobs
if ($LASTEXITCODE -ne 0) { throw "Switch build failed ($LASTEXITCODE)" }
$nro = Join-Path $build 'fnv-nx.nro'
if (-not (Test-Path -LiteralPath $nro)) { throw 'Build did not produce fnv-nx.nro' }
Write-Host "Built $nro; hardware execution still needs verification."
