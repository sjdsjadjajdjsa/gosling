#!/bin/sh
set -eu
cd "$(dirname "$0")"

GOOSE=${GOOSE:-$(command -v goose || echo "$HOME/goose/build/goose")}
CC=${CC:-cc}
OPT=${OPT:--O2}
RL=third_party/raylib/src
OUT=build
OBJ=$OUT/obj
mkdir -p "$OBJ"

[ -f "$RL/rcore.c" ] || git submodule update --init third_party/raylib

case "$(uname -s)" in
Darwin)
    GLFW_LANG="-x objective-c"
    SYS_LIBS="-framework Cocoa -framework IOKit -framework OpenGL -framework CoreVideo -framework QuartzCore"
    ;;
*)
    GLFW_LANG=""
    SYS_LIBS="-lGL -lX11 -lm -ldl -lpthread"
    ;;
esac

RL_FLAGS="-DPLATFORM_DESKTOP_GLFW -DGRAPHICS_API_OPENGL_33 \
    -DSUPPORT_MODULE_RMODELS=0 -DSUPPORT_MODULE_RAUDIO=0 \
    -DSUPPORT_SCREEN_CAPTURE=0 -DSUPPORT_AUTOMATION_EVENTS=0 -DSUPPORT_COMPRESSION_API=0 \
    -DSUPPORT_FILEFORMAT_GIF=0 -DSUPPORT_FILEFORMAT_QOI=0 -DSUPPORT_FILEFORMAT_DDS=0 \
    -DSUPPORT_FILEFORMAT_JPG=1 -DSUPPORT_FILEFORMAT_BMP=0"

if [ ! -f "$OBJ/libraylib.a" ]; then
    for f in rcore rshapes rtextures rtext; do
        $CC $OPT -c "$RL/$f.c" $RL_FLAGS -I"$RL" -I"$RL/external/glfw/include" -o "$OBJ/rl_$f.o"
    done
    $CC $OPT -c $GLFW_LANG "$RL/rglfw.c" $RL_FLAGS -I"$RL" -I"$RL/external/glfw/include" -o "$OBJ/rl_rglfw.o"
    ar rcs "$OBJ/libraylib.a" "$OBJ"/rl_*.o
fi

inc() { echo "-I$(pkg-config --variable=includedir "$1")"; }
lib() { echo "-L$(pkg-config --variable=libdir "$1")"; }

"$GOOSE" -o "$OUT/gosling.c" app/gosling.goose
"$GOOSE" --emit-runtime "$OUT/goose_runtime.c"

$CC $OPT -std=c23 -w \
    "$OUT/gosling.c" "$OUT/goose_runtime.c" app/native/*.c \
    -I"$RL" -I"$RL/external/glfw/include" $(inc mpv) $(inc yyjson) \
    "$OBJ/libraylib.a" $(lib mpv) $(lib yyjson) -lmpv -lyyjson -lcurl $SYS_LIBS -lm -pthread \
    -o "$OUT/gosling"
strip "$OUT/gosling" 2>/dev/null || true
ls -l "$OUT/gosling"
