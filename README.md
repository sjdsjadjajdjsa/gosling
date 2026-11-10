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

Needs Goose (built from source), libmpv, yyjson, libcurl and pkg-config. raylib is vendored as a submodule and built static.

```sh
brew install cmake pkg-config mpv yyjson yt-dlp
git clone https://github.com/aardappel/goose ~/goose
cmake -S ~/goose -B ~/goose/build -DCMAKE_BUILD_TYPE=Release && cmake --build ~/goose/build -j
git clone --recursive https://github.com/sjdsjadjajdjsa/gosling && cd gosling
./build.sh && ./build/gosling
```

`./build/gosling <file or link>` starts playing it. `GOSLING_MPV_OPTIONS=ao=null` passes extra mpv options (comma separated), for tests and benchmarks.

## Keys

| Key | With text in search | With search empty |
| --- | --- | --- |
| Type | Search | Search |
| Enter | Search, or play the selected result | |
| Up / Down | Select a result | |
| Esc | Clear | |
| Space | | Play / pause |
| Left / Right | | Seek 5 seconds |

## Layout

- `app/`: the player. Goose source, the C shim over raylib in `app/native/`, the bundled Inter font.
- `site/`: goslingplayer.com, plain HTML/CSS/JS (`python3 -m http.server --directory site` to preview).
- `design_system/`: how the site looks and why, plus screenshot and banner tools.

## License

[FSL-1.1-MIT](LICENSE): source available now, MIT two years after each release. Inter is under the [SIL Open Font License](app/fonts/Inter-LICENSE.txt), Bricolage Grotesque under the [SIL Open Font License](site/fonts/OFL.txt).
