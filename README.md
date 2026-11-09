# Gosling

The lightest music player we can make. A from-scratch rewrite of [Tonal](https://gitlab.com/sjdsjadjajdjsa/tonal)
in [Goose](https://github.com/aardappel/goose), drawn on the GPU with raylib, playing through libmpv.

## Build

Needs Goose (built from source), libmpv, yyjson, libcurl and pkg-config. raylib is vendored as a submodule and built static.

```sh
brew install cmake pkg-config mpv yyjson
git clone https://github.com/aardappel/goose ~/goose
cmake -S ~/goose -B ~/goose/build -DCMAKE_BUILD_TYPE=Release && cmake --build ~/goose/build -j
git submodule update --init
./build.sh && ./build/gosling
```

`./build/gosling <file or link>` starts playing it. `GOSLING_MPV_OPTIONS=ao=null` passes extra mpv options (comma separated), for tests and benchmarks.

Keys: type to search, Enter to search or play the selected result, Up/Down to select, Esc to clear. With the search box empty: Space plays/pauses, Left/Right seek 5 seconds.

## Layout

- `app/` the player: Goose source, the C shim over raylib in `app/native/`, the bundled Inter font
- `site/` goslingplayer.com, plain HTML/CSS/JS (`python3 -m http.server --directory site` to preview)
- `design_system/` how the site looks and why, plus screenshot tools
- `bench/` memory and CPU measurements against Tonal

## License

[FSL-1.1-MIT](LICENSE). Inter is under the [SIL Open Font License](app/fonts/Inter-LICENSE.txt).
