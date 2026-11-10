# Benchmarks

Gosling against Tonal on the same Mac (Apple Silicon, macOS), measured with `bench/mac.sh`.
Memory is macOS `phys_footprint` 12 seconds after launch. CPU is the CPU time used in the next 20 seconds.

| | Memory | CPU in 20 s | Threads | Size on disk |
| --- | --- | --- | --- | --- |
| Gosling, idle | 68 MB | 0.00 s | 9 | 1.0 MB |
| Gosling, playing a local file | 101 MB | 0.18 s | 15 | |
| Tonal 0.11, idle | 72 MB | 0.79 s | 14 | 16 MB |

Idle memory is about the same. The difference is CPU: Gosling draws nothing when nothing changes, so it uses no CPU while idle.
Tonal playing is not measured here yet. Linux numbers use Tonal's `bench.py` on cedar and are still to come.
