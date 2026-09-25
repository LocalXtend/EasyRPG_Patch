# Builds this (RocketRPG-modified) EasyRPG Player on Windows into <Work>\dist.
# Requires MSYS2 in C:\msys64; missing MSYS2 packages are installed automatically. Reruns are incremental.
#   pwsh -File rocketrpg\build.ps1                 (work folder: rocketrpg\work)
#   pwsh -File rocketrpg\build.ps1 -Work D:\out
param([string]$Work = (Join-Path $PSScriptRoot 'work'))
$ErrorActionPreference = 'Stop'
$bash = 'C:\msys64\usr\bin\bash.exe'
if (-not (Test-Path $bash)) { throw 'MSYS2 not found (winget install MSYS2.MSYS2)' }
New-Item -ItemType Directory -Force $Work | Out-Null
$Work = (Resolve-Path $Work).Path
$env:MSYSTEM = 'UCRT64'
$env:CHERE_INVOKING = '1'
function Invoke-Msys([string]$cmd) {
    & $bash -lc $cmd
    if ($LASTEXITCODE -ne 0) { throw "MSYS2 command failed: $cmd" }
}

$pk = 'toolchain cmake ninja pkgconf SDL2 libpng zlib pixman fmt freetype harfbuzz speexdsp libsndfile mpg123 libvorbis libogg opusfile libxmp fluidsynth nlohmann-json expat icu' -split ' ' | ForEach-Object { "mingw-w64-ucrt-x86_64-$_" }
Write-Host '[1/2] MSYS2 packages...'
Invoke-Msys "pacman -S --needed --noconfirm --noprogressbar --disable-download-timeout $($pk -join ' ') >/dev/null"

# 수정하지 않은 의존 라이브러리 (MSYS2에는 git이 없어 Windows git으로 받습니다)
if (-not (Test-Path (Join-Path $Work 'liblcf'))) { git -C $Work clone -q --depth 1 --branch 0.8.1 https://github.com/EasyRPG/liblcf.git liblcf }
if (-not (Test-Path (Join-Path $Work 'inih'))) { git -C $Work clone -q --depth 1 --branch r58 https://github.com/benhoyt/inih.git inih }

Write-Host '[2/2] liblcf + Player...'
$script = & $bash -lc "cygpath -u '$(Join-Path $PSScriptRoot 'build.sh')'"
$workU = & $bash -lc "cygpath -u '$Work'"
Invoke-Msys "bash '$script' '$workU'"
Write-Host "OK -> $(Join-Path $Work 'dist')"
