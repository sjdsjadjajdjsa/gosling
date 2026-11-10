<p align="center">
  <a href="https://goslingplayer.com">
    <picture>
      <source media="(prefers-color-scheme: dark)" srcset=".github/assets/banner-dark.png">
      <img alt="gosling, a tiny music player that gets out of the way" src=".github/assets/banner-light.png" width="100%">
    </picture>
  </a>
</p>

<p align="center">
  <b>Small. Loud enough.</b><br>
  One small app, drawn on the GPU, played by mpv.
</p>

<p align="center">
  <a href="https://goslingplayer.com">goslingplayer.com</a> ·
  <a href="#build">Build</a> ·
  <a href="#keys">Keys</a> ·
  <a href="LICENSE">FSL-1.1-MIT</a>
</p>

## Why

|  |  |
| --- | --- |
| **Light** | One small app. No browser inside, nothing running in the background. |
| **Everything** | Search songs like in [Tonal](https://tonalplayer.com), play local files and links, all through mpv. |
| **Quiet** | When nothing moves, it draws nothing. Your fan stays off. |

## Made of a few good parts

- [**Goose**](https://github.com/aardappel/goose): a memory-safe language with no heap. The whole app.
- [**raylib**](https://www.raylib.com): draws every pixel on the GPU.
- [**mpv**](https://mpv.io): plays anything you throw at it, with [yt-dlp](https://github.com/yt-dlp/yt-dlp) for streams.
- **libcurl** and **yyjson**: search.

## Build

Needs Goose (built from source), libmpv, yyjson, libcurl, SQLite, FFmpeg (libavformat, for local file tags) and pkg-config. On Linux also libsystemd (media keys). raylib is vendored as a submodule and built static.

```sh
brew install cmake pkg-config mpv yyjson yt-dlp sqlite ffmpeg
git clone https://github.com/aardappel/goose ~/goose
cmake -S ~/goose -B ~/goose/build -DCMAKE_BUILD_TYPE=Release && cmake --build ~/goose/build -j
git clone --recursive https://github.com/sjdsjadjajdjsa/gosling && cd gosling
./build.sh && ./build/gosling
```

`./package_mac.sh` makes `build/Gosling.app` with its libraries inside (needs `brew install dylibbundler`). Linux builds and releases run on GitHub Actions.

`./build/gosling <file or link>` starts playing it. `GOSLING_MPV_OPTIONS=ao=null` passes extra mpv options (comma separated), for tests and benchmarks.

## Keys

| Key | Does |
| --- | --- |
| Type, Enter | Search, or play a pasted link |
| Up / Down, Enter | Pick a song and play it |
| Esc | Clear the search |
| Space | Play / pause (with search empty) |
| Left / Right | Seek 5 seconds (with search empty) |
| Cmd/Ctrl + Left / Right | Previous / next song |
| Media keys | Play, pause, next, previous, also from Control Center or MPRIS |

Click a song to play it and the rest of the list after it. The heart likes it, the + adds it to up next or a playlist. Drop a file to play it, or a folder to add it to Local files.

## Layout

- `app/`: the player. Goose source, the C shim over raylib in `app/native/`, the bundled Inter font.
- `site/`: goslingplayer.com, plain HTML/CSS/JS (`python3 -m http.server --directory site` to preview).
- `design_system/`: how the site looks and why, plus screenshot and banner tools.

## License

[FSL-1.1-MIT](LICENSE): source available now, MIT two years after each release. Inter is under the [SIL Open Font License](app/fonts/Inter-LICENSE.txt), Bricolage Grotesque under the [SIL Open Font License](site/fonts/OFL.txt).
