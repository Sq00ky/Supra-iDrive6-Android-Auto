#!/bin/sh
# QNX SDP 6.5, ARMv7LE. Run with the SDK environment already loaded.
set -eu
cd "$(dirname "$0")/.."
root=$(pwd)
out=$root/g30-pip/build
vendor=$root/third_party/openh264
src=$root/src/cluster/g30
qcc=${QCC:-qcc}
ar=${QNX_AR:-ntoarmv7-ar}
variant=-Vgcc_ntoarmv7le_cpp
flags="-O2 -Wall -D_QNX_SOURCE -DIDRIVER_QNX_SDK -Wc,-std=gnu++0x -march=armv7-a -mfpu=vfpv3 -mfloat-abi=softfp"
inc="-I$vendor/codec/api -I$vendor/codec/api/wels -I$vendor/codec/common/inc -I$vendor/codec/decoder/core/inc -I$vendor/codec/decoder/plus/inc"
# Ensure switching between ordinary and G30 builds cannot reuse old objects.
make clean
mkdir -p "$out/decoder"
"$qcc" $variant $flags -c "$src/bridge.cpp" -o "$out/bridge.o"
make nbt-aad QCC="$qcc" G30_BRIDGE_OBJECT="$out/bridge.o"
cp nbt-aad "$out/nbt-aad"
make rhmiapp QCC="$qcc"
cp rhmiapp/libNbtAad.so "$out/libNbtAad.so"
for source in "$vendor"/codec/common/src/*.cpp "$vendor"/codec/decoder/core/src/*.cpp \
    "$vendor"/codec/decoder/plus/src/welsDecoderExt.cpp "$src/decoder_port.cpp"; do
    name=$(basename "$source" .cpp)
    case "$name" in Wels*|wels_decoder_thread) continue;; esac
    "$qcc" $variant $flags -fno-exceptions -fno-rtti -fno-strict-aliasing $inc -c "$source" -o "$out/decoder/$name.o"
done
rm -f "$out/libopenh264-decoder.a"
"$ar" rcs "$out/libopenh264-decoder.a" "$out"/decoder/*.o
"$qcc" $variant $flags -fno-exceptions -fno-rtti $inc "$src/player.cpp" \
    "$out/libopenh264-decoder.a" -L"$root/lib" -lscreen -lsocket -lm -lang-c++ \
    -Wl,-rpath,/lib:/usr/lib -o "$out/player"
"$qcc" $variant $flags "$src/inspect.cpp" -L"$root/lib" -lscreen -lang-c++ \
    -Wl,-rpath,/lib:/usr/lib -o "$out/inspect"
cp g30-pip/service.sh "$out/service.sh"
for name in NbtAad.manifest NbtAad.xml NbtAad_ALL_Images.zip NbtAad_ALL_Texts.zip; do
    cp "rhmiapp/$name" "$out/$name"
done
cp ../LICENSE.md "$out/SUPRA-LICENSE.md"
cp "$vendor/LICENSE" "$out/OPENH264-LICENSE"
echo "G30 build complete: $out"
