#!/usr/bin/env bash
# Builds this (RocketRPG-modified) EasyRPG Player with the MSYS2 UCRT64 toolchain.
# liblcf 0.8.1 and inih r58 are unmodified upstream and are fetched into the work folder.
# Usage (MSYS2 UCRT64 shell, or via build.ps1): rocketrpg/build.sh [work-dir]
#   -> <work-dir>/dist/Player.exe + the MSYS2 DLLs it needs   (default work-dir: rocketrpg/work)
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$(cygpath -m "$HERE/..")"          # C:/... form: the native CMake does not understand /c/... paths
WORK="${1:-$HERE/work}"
mkdir -p "$WORK"
cd "$WORK"
ROOT="$(cygpath -m "$PWD")"
PREFIX="$ROOT/prefix"
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:${PKG_CONFIG_PATH:-}"

[ -d liblcf ] || git clone -q --depth 1 --branch 0.8.1 https://github.com/EasyRPG/liblcf.git liblcf
[ -d inih ]   || git clone -q --depth 1 --branch r58   https://github.com/benhoyt/inih.git inih

if [ ! -f prefix/lib/libinih.a ]; then
  mkdir -p prefix/include prefix/lib
  gcc -O2 -c inih/ini.c -o inih/ini.o
  ar rcs prefix/lib/libinih.a inih/ini.o
  cp inih/ini.h prefix/include/
fi

if [ ! -f prefix/lib/liblcf.a ]; then
  cmake -S liblcf -B liblcf-build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_PREFIX_PATH="$PREFIX" \
    -DBUILD_SHARED_LIBS=OFF -DLIBLCF_WITH_ICU=ON -DLIBLCF_WITH_XML=OFF \
    -DLIBLCF_ENABLE_TOOLS=OFF -DLIBLCF_ENABLE_TESTS=OFF \
    -DINIH_LIBRARY="$PREFIX/lib/libinih.a" -DINIH_INCLUDE_DIR="$PREFIX/include"
  cmake --build liblcf-build
  cmake --install liblcf-build
fi

cmake -S "$SRC" -B player-build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$PREFIX" \
  -DPLAYER_BUILD_LIBLCF=OFF \
  -DPLAYER_ENABLE_TESTS=OFF \
  -DCMAKE_EXE_LINKER_FLAGS="-static-libgcc -static-libstdc++" \
  -DCMAKE_CXX_STANDARD_LIBRARIES="-lSDL2 -lkernel32 -luser32 -lgdi32 -lwinspool -lshell32 -lole32 -loleaut32 -luuid -lcomdlg32 -ladvapi32"
cmake --build player-build

# Player.exe + the MSYS2 DLLs it needs -> dist/
rm -rf dist
mkdir -p dist
cp player-build/Player.exe dist/
ldd player-build/Player.exe | grep -i '/ucrt64/' | awk '{print $3}' | sort -u | while read -r dll; do cp "$dll" dist/; done
strip dist/Player.exe
echo "files: $(ls dist | wc -l)"
du -sh dist
