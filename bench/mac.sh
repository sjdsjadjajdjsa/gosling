#!/bin/sh
cd "$(dirname "$0")/.."
measure() {
    "$@" >/dev/null 2>&1 &
    P=$!
    sleep 12
    F=$(footprint -p $P 2>/dev/null | awk '/phys_footprint:/{print $2, $3; exit}')
    C1=$(ps -o time= -p $P)
    sleep 20
    C2=$(ps -o time= -p $P)
    echo "memory $F, cpu $C1 -> $C2, threads $(ps -M -p $P | tail -n +2 | wc -l | tr -d ' ')"
    kill $P
    sleep 1
}
echo "gosling idle:    $(measure ./build/gosling)"
[ -n "${SONG:-}" ] && echo "gosling playing: $(measure env GOSLING_MPV_OPTIONS=ao=null ./build/gosling "$SONG")"
[ -x /Applications/Tonal.app/Contents/MacOS/tonal ] && echo "tonal idle:      $(measure /Applications/Tonal.app/Contents/MacOS/tonal)"
du -sh build/gosling /Applications/Tonal.app 2>/dev/null
