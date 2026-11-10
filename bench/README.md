# Benchmarks

Gosling against Tonal on the same Mac (Apple Silicon, macOS), measured with `bench/mac.sh`.
Memory is macOS `phys_footprint` 12 seconds after launch. CPU is the CPU time used in the next 20 seconds.

| | Memory | CPU in 20 s | Threads | Size on disk |
| --- | --- | --- | --- | --- |
| Gosling, idle | 62 MB | 0.00 s | 9 | 1.0 MB |
| Gosling, playing a local file | 101 MB | 0.18 s | 15 | |
| Tonal 0.11, idle | 72 MB | 0.79 s | 14 | 16 MB |

## Playing

Both apps search "lofi chill" on YouTube Music and play the first song, with audio sent to mpv's null output. Two runs each, same Mac, same minute. Tonal runs mpv as a second process, so its numbers are Tonal plus its mpv. Tonal was a release build with the `debug-hooks` feature, which only adds the start-a-search-from-the-command-line hook.

| | Memory | CPU in 20 s |
| --- | --- | --- |
| Gosling | 104 to 105 MB | 0.30 s |
| Tonal 0.11.5 (55 MB) + its mpv (226 to 229 MB) | 280 to 284 MB | 0.92 to 1.27 s |

Playing, Gosling uses under 40% of the memory and about a third of the CPU. On macOS an empty raylib window alone is 51 MB and an empty native window 18 MB, so those are the floors for each approach. Idle memory is about the same; idle CPU is zero for Gosling because it draws nothing when nothing changes. Linux numbers use Tonal's `bench.py` on cedar and are still to come.
