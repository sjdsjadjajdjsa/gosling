# Gosling

The lightest music player we can make. A from-scratch rewrite of [Tonal](https://gitlab.com/sjdsjadjajdjsa/tonal)
in [Goose](https://github.com/aardappel/goose), drawn on the GPU with raylib, playing through libmpv.

## Build

Needs Goose (built from source), raylib and pkg-config.

```sh
brew install cmake pkg-config raylib
git clone https://github.com/aardappel/goose ~/goose
cmake -S ~/goose -B ~/goose/build -DCMAKE_BUILD_TYPE=Release && cmake --build ~/goose/build -j
./build.sh && ./build/gosling
```

## Layout

- `app/` the player: Goose source, the C shim over raylib in `app/native/`, the bundled Inter font
- `site/` goslingplayer.com, plain HTML/CSS/JS (`python3 -m http.server --directory site` to preview)
- `design_system/` how the site looks and why, plus screenshot tools
- `bench/` memory and CPU measurements against Tonal

## License

[FSL-1.1-MIT](LICENSE). Inter is under the [SIL Open Font License](app/fonts/Inter-LICENSE.txt).
