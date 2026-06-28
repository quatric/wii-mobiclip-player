#!/usr/bin/env bash
# Build libogg + Tremor (libvorbisidec) for PowerPC/Wii into ./portlibs/ppc.
# Needed for Vorbis .mo playback. Run once; the static libs are then linked by
# the main Makefile (LIBDIRS = portlibs/ppc).
#
# Normally you'd just `dkp-pacman -S ppc-libogg ppc-libvorbisidec`; this script
# is the rootless fallback that compiles the upstream sources directly with
# devkitPPC (no pacman / no sudo required).
set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PFX="$ROOT/portlibs/ppc"
WORK="$(mktemp -d)"
GCC=/opt/devkitpro/devkitPPC/bin/powerpc-eabi-gcc
AR=/opt/devkitpro/devkitPPC/bin/powerpc-eabi-ar
# PPC is big-endian; VAR_ARRAYS picks Tremor's C99 stack-alloc path.
CF="-O2 -mrvl -mcpu=750 -meabi -mhard-float -ffunction-sections -DVAR_ARRAYS -DWORDS_BIGENDIAN"

OGG_VER=1.3.6
curl -fsSL "https://downloads.xiph.org/releases/ogg/libogg-${OGG_VER}.tar.gz" \
    | tar xz -C "$WORK"
# Tremor (integer Vorbis); sezero mirror tracks the xiph 1.2.1 release.
git clone --depth 1 https://github.com/sezero/tremor.git "$WORK/tremor"

OGG="$WORK/libogg-${OGG_VER}"
TRE="$WORK/tremor"
cat > "$OGG/include/ogg/config_types.h" <<'EOF'
#ifndef __CONFIG_TYPES_H__
#define __CONFIG_TYPES_H__
#include <stdint.h>
typedef int16_t ogg_int16_t;  typedef uint16_t ogg_uint16_t;
typedef int32_t ogg_int32_t;  typedef uint32_t ogg_uint32_t;
typedef int64_t ogg_int64_t;  typedef uint64_t ogg_uint64_t;
#endif
EOF

mkdir -p "$PFX/lib" "$PFX/include/ogg" "$PFX/include/tremor"
INC="-I$OGG/include -I$TRE"

OBJ=""
for f in bitwise framing; do
    $GCC $CF $INC -c "$OGG/src/$f.c" -o "$WORK/$f.o"; OBJ="$OBJ $WORK/$f.o"
done
$AR rcs "$PFX/lib/libogg.a" $OBJ

OBJ=""
for f in block codebook floor0 floor1 info mapping0 mdct registry res012 \
         sharedbook synthesis window; do
    $GCC $CF $INC -c "$TRE/$f.c" -o "$WORK/$f.o"; OBJ="$OBJ $WORK/$f.o"
done
$AR rcs "$PFX/lib/libvorbisidec.a" $OBJ

cp "$OGG"/include/ogg/*.h "$PFX/include/ogg/"
cp "$TRE"/ivorbiscodec.h "$TRE"/ivorbisfile.h "$TRE"/config_types.h "$PFX/include/tremor/"
rm -rf "$WORK"
echo "Built: $PFX/lib/{libogg,libvorbisidec}.a"
