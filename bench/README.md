# Benchmarks

Gosling against Tonal on the same Mac (Apple Silicon, macOS), measured with `bench/mac.sh`.
Memory is macOS `phys_footprint` 12 seconds after launch. CPU is the CPU time used in the next 20 seconds.

| | Memory | CPU in 20 s | Threads | Size on disk |
| --- | --- | --- | --- | --- |
| Gosling, idle | 31 MB (20 MB minimized) | 0.00 s | 5 | 0.3 MB |
| Gosling, playing a local file | 69 MB (30 MB minimized) | 0.18 s | 14 | |
| Tonal 0.11, idle | 72 MB | 0.79 s | 14 | 16 MB |

## Playing

Both apps search "lofi chill" on YouTube Music and play the first song, with audio sent to mpv's null output. Two runs each, same Mac, same minute. Tonal runs mpv as a second process, so its numbers are Tonal plus its mpv. Tonal was a release build with the `debug-hooks` feature, which only adds the start-a-search-from-the-command-line hook.

| | Memory | CPU in 20 s |
| --- | --- | --- |
| Gosling | 80 to 83 MB | 0.32 to 0.37 s |
| Tonal 0.11.5 (55 MB) + its mpv (226 to 229 MB) | 280 to 284 MB | 0.92 to 1.27 s |

Playing, Gosling uses under 30% of the memory and about a third of the CPU. Since the switch from raylib to native macOS drawing, Gosling idles at 31 MB, and mpv only starts when the first song plays. An empty native window on macOS is 18 MB, so that is the floor.
