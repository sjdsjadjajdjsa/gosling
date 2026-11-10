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

[ "$(uname -s)" = Darwin ] || [ -f "$RL/rcore.c" ] || git submodule update --init third_party/raylib

case "$(uname -s)" in
Darwin)
    UI="app/native/ui_mac.m"
    PLATFORM="-fobjc-arc app/native/platform_mac.m"
    SYS_LIBS="-framework Cocoa -framework QuartzCore -framework CoreText -framework ImageIO -framework MediaPlayer"
    PKGS="mpv yyjson libcurl sqlite3 libavformat libavutil"
    ;;
*)
    UI="app/native/ui.c $OBJ/libraylib.a"
    PLATFORM="app/native/platform_linux.c"
    SYS_LIBS="-lGL -lX11 -lm -ldl -lpthread"
    PKGS="mpv yyjson libcurl sqlite3 libavformat libavutil libsystemd"
    ;;
esac

RL_FLAGS="-DPLATFORM_DESKTOP_GLFW -DGRAPHICS_API_OPENGL_33 \
    -DSUPPORT_MODULE_RMODELS=0 -DSUPPORT_MODULE_RAUDIO=0 \
    -DSUPPORT_SCREEN_CAPTURE=0 -DSUPPORT_AUTOMATION_EVENTS=0 -DSUPPORT_COMPRESSION_API=0 \
    -DSUPPORT_FILEFORMAT_GIF=0 -DSUPPORT_FILEFORMAT_QOI=0 -DSUPPORT_FILEFORMAT_DDS=0 \
    -DSUPPORT_FILEFORMAT_JPG=1 -DSUPPORT_FILEFORMAT_BMP=0"

if [ "$(uname -s)" != Darwin ] && [ ! -f "$OBJ/libraylib.a" ]; then
    for f in rcore rshapes rtextures rtext; do
        $CC $OPT -c "$RL/$f.c" $RL_FLAGS -I"$RL" -I"$RL/external/glfw/include" -o "$OBJ/rl_$f.o"
    done
    $CC $OPT -c -D_GLFW_X11 "$RL/rglfw.c" $RL_FLAGS -I"$RL" -I"$RL/external/glfw/include" -o "$OBJ/rl_rglfw.o"
    ar rcs "$OBJ/libraylib.a" "$OBJ"/rl_*.o
fi

"$GOOSE" -o "$OUT/gosling.c" app/gosling.goose
"$GOOSE" --emit-runtime "$OUT/goose_runtime.c"

$CC $OPT -std=c23 -D_GNU_SOURCE -DGOSLING_DISCORD_APP="\"${GOSLING_DISCORD_APP:-1569570203829010555}\"" -w \
    "$OUT/gosling.c" "$OUT/goose_runtime.c" \
    app/native/player.c app/native/search.c app/native/lib.c app/native/art.c app/native/scan.c app/native/discord.c app/native/anim.c app/native/ytdlp.c \
    -I"$RL" -I"$RL/external/glfw/include" -Iapp/native $(pkg-config --cflags $PKGS) \
    $PLATFORM $UI \
    $(pkg-config --libs $PKGS) $SYS_LIBS -lm -pthread \
    -o "$OUT/gosling"
strip "$OUT/gosling" 2>/dev/null || true
ls -l "$OUT/gosling"
