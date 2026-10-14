#!/bin/sh
# Run in the pinned switch-dev container. /work is the FNV-nx repository,
# /build is a disposable Linux build volume. No game or boot files are modified.
set -eu
pc=/work/pc
pin=cbb0e4e6e7fa7f47f328d92441f51bfb507f266f
root=/build/autorun-$pin
tools=/build/toolchains
jobs=${FNV_JOBS:-6}
output=${FNV_OUTPUT:-/work/local/runtime-stage}
mkdir -p /build/downloads "$tools"
export HOME=/tmp/fnv-build-home GIT_CONFIG_GLOBAL=/tmp/fnv-git-config
mkdir -p "$HOME"
git config --global --add safe.directory '*'
zip=/build/downloads/autorun-$pin.zip
if [ ! -f "$zip" ]; then
    curl -fL --retry 3 -o "$zip" "https://codeload.github.com/autorunhq/autorun/zip/$pin"
fi
python3 "$pc/tools/extract_source.py" "$zip" /build --sha256 817f3486d6abfca04483e91d386937d93d5c5833f1e88cedb0be28aad55157b0
llvm="$tools/llvm-mingw-20260505-ucrt-ubuntu-22.04-x86_64"
if [ ! -x "$llvm/bin/arm64ec-w64-mingw32-clang" ]; then
    archive=/build/downloads/llvm-mingw.tar.xz
    curl -fL --retry 3 -o "$archive" https://github.com/mstorsjo/llvm-mingw/releases/download/20260505/llvm-mingw-20260505-ucrt-ubuntu-22.04-x86_64.tar.xz
    printf '%s  %s\n' 986d7beef2f6d7e2f3939a1d04ac8746d614e7014557961e7e0d381d7bc5fdcd "$archive" | sha256sum -c -
    tar -xJf "$archive" -C "$tools"
fi
export PATH="$llvm/bin:$PATH" WINE_NX_JOBS="$jobs"
export WINE_NX_SOURCE_COMMIT="$pin"
python3 "$pc/tools/integrate_autorun.py" --autorun "$root"
sh "$root/horizon-wine/tools/bootstrap-box64-core.sh"
sh "$root/horizon-wine/tools/bootstrap-wma.sh"
pe="$root/horizon-wine/build-fnv-pe"
build="$root/horizon-wine/build-fnv-switch"
mkdir -p "$pe"
cd "$pe"
if [ ! -f include/config.h ]; then
    "$root/configure" --enable-archs=aarch64,arm64ec,i386 --enable-winebox64=aarch64 --enable-winebox64ec=arm64ec --disable-tests --without-x --without-freetype --without-alsa --without-pulse --without-dbus --without-fontconfig --without-udev --without-usb --without-gstreamer --enable-winegstreamer --without-vulkan
fi
make -j"$jobs" include/all
cmake -S "$root/horizon-wine" -B "$build" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$root/horizon-wine/cmake/switch-devkitA64.cmake" -DWINE_NX_PE_BUILD_DIR="$pe" -DWINE_NX_AMD64=ON -DWINE_NX_FEX=ON -DWINE_NX_BOX64_INTERPRETER=ON -DWINE_NX_BOX64_DYNAREC=ON -DWINE_NX_USB_STORAGE=ON -DWINE_NX_BOOT_BUNDLE= -DCMAKE_BUILD_TYPE=Release
cmake --build "$build" --target wine-nx-runtime-nro -j"$jobs"
python3 "$root/horizon-wine/tools/package-amd64.py" --build "$build" --stage-output "$output" --fex --vulkan --dxvk
