#!/bin/sh
set -eu
cd "$(dirname "$0")"
GOOSE=${GOOSE:-$(command -v goose || echo "$HOME/goose/build/goose")}
RAYLIB_LIB=$(pkg-config --variable=libdir raylib)
RAYLIB_INC=$(pkg-config --variable=includedir raylib)
export LIBRARY_PATH="$RAYLIB_LIB${LIBRARY_PATH:+:$LIBRARY_PATH}"
export CPATH="$RAYLIB_INC${CPATH:+:$CPATH}"
mkdir -p build
"$GOOSE" build -o build/gosling app/gosling.goose "$@"
strip build/gosling 2>/dev/null || true
ls -l build/gosling
