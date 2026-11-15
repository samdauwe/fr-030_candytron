# candytron (native)

A native player for **fr-030: Candytron**, the 64k intro by Farbrausch (Breakpoint 2003, 2nd place), written in C99 on top of SDL3 and
OpenGL ES 3, for the desktop and the browser (WebAssembly, WebGL 2). Candytron runs on the **GenThree** engine,
whose demo is a compiled script (`default.pac`) that calls about 50 native functions to build bitmaps, meshes, materials, scenes and effect chains,
and animates them with splines. The player runs that script on a reimplementation of the GenThree script machine, generates the textures and meshes,
draws the frames with an OpenGL ES 3 backend and synthesizes the soundtrack with a V2 synthesizer, with results that are bit-identical to the original's
where that can be checked: the script's native calls and state, the textures, the meshes, the geometry and Direct3D state of every draw, and the audio.

Original credits: **chaos** (base system, 3d engine, texture generator, main tool code), **ryg** (mesh engine, postproc effects, exe packer),
**gizmo** (textures, models, animations, timing), **kb** (music, sound code, voice synthesizer). The player plays the final version 1.01.

**Known differences.** The pictures match the original's in most scenes. In some they do not: the translucent sheets at 76 to 82 s and 130 to 150 s
and the camouflage pattern on the character at 86 to 90 s are missing or different. The original's executable makes the same draw calls, so the difference
is in vertex data that this player does not reproduce. [doc/TESTING.md](doc/TESTING.md) describes how the pictures are held to the original's.

## Build and run

Requirements: a C99 compiler (GCC or Clang), CMake 3.25 or newer, Ninja (the presets use it), and either an installed SDL3
(3.2 or newer) or a network connection and git: without an installed SDL3 the pinned release (`CANDYTRON_SDL3_TAG`) is fetched and built
as a static library. Linux needs the usual SDL3 build dependencies (X11 and/or Wayland, ALSA, Mesa/EGL development files).
The player asks for an OpenGL ES 3.0 context and falls back to OpenGL 3.3 core. It was developed and tested on Linux (x86-64).

```sh
cmake --preset release            # build/release
cmake --build --preset release
./build/release/candytron         # plays the demo; Esc quits
```

The executable looks for `default.pac`, `josie.v2m` and `babe.mesh.old` in `assets/` next to itself, then in `./assets` (the build copies them
next to the executable); `--assets DIR` names the directory explicitly. It refuses to start unless each file has the expected size, SHA-256 and
outer structure.

| Key | Action |
|---|---|
| Space | pause |
| Left / Right | seek 10 s back / forward (the sound restarts there) |
| Home | back to the beginning |
| F | fullscreen |
| Esc | quit |

Useful options: `--size 1280x720`, `--aspect W:H` (the shape the picture is shown in; the default is the production's 4:3), `--fullscreen`,
`--no-vsync`, `--start SECONDS`, `--verify-assets`, `--version`, `--licenses` (the credits and every licence the player is built from). The options
for tests and measurements (`--exit-after`, `--gl-check`, `--capture`, `--times`, `--soak`, `--selftest`) are described at the top of
[src/player/main.c](src/player/main.c).

## WebAssembly

The same player runs in the browser (WebGL 2, Web Audio) as one page, `index.html`, plus `index.js`, `index.wasm` (about 1 MB) and `index.data` (the
production's three files, 125 kB). Requirements: [Emscripten](https://emscripten.org/docs/getting_started/downloads.html)
7.0.0 or newer, CMake 3.25+ and Ninja. SDL3 is fetched as for the native build.

```sh
git clone https://github.com/emscripten-core/emsdk.git ~/emsdk
~/emsdk/emsdk install 7.0.0 && ~/emsdk/emsdk activate 7.0.0
source ~/emsdk/emsdk_env.sh                 # sets EMSDK and puts emcc on PATH

cmake --preset web                          # build/web
cmake --build --preset web
python3 -m http.server -d build/web/dist    # then open http://localhost:8000
```

The page asks for a click (browsers only start audio after one). The keys are the native ones, the credits and licences are under the button. A browser
without WebGL 2 gets a message on the page. The demo needs about 320 MB of WebAssembly memory: the generated meshes are most of it.

Browsers have no worker thread for a static page (it needs COOP/COEP headers), so the player does everything on the main thread: the demo is generated
before the first picture, which takes about a second on a desktop. Use the Emscripten that comes with the SDK: a Binaryen from another source can
reject the flags of a newer Emscripten (`Unknown option '--enable-bulk-memory-opt'`).

### GitHub Pages

[.github/workflows/pages.yml](.github/workflows/pages.yml) builds the web player with a pinned Emscripten on pushes to `main`, on pull
requests and on manual runs, and deploys it everywhere except on pull requests. Once per repository: *Settings > Pages > Build and
deployment > Source: GitHub Actions*. The site is then at `https://<user>.github.io/<repository>/`. The build fails when `index.wasm` or
`index.data` outgrow their budgets (`WASM_BUDGET_BYTES`, `DATA_BUDGET_BYTES`).

## Tests

```sh
cmake --build --preset release --target check     # or: ./build/release/candytron_tests
ctest --preset release -j8                        # the same suites, one CTest test each
```

`candytron_tests` is a single program that needs no arguments. It runs every suite of [tests/suites](tests/suites) in a process of
its own, a few at a time, and prints one line per suite; the output of a suite is shown when it fails. The player itself is started headless (SDL
offscreen video driver, dummy audio) by some of the suites and they skip when the machine has no OpenGL ES context. The `web-tests` preset runs the
same program as WebAssembly under Node. [doc/TESTING.md](doc/TESTING.md) describes the suites, the fixtures and the sanitizer and `-march=native`
presets.

## Style

The code is C99, formatted by [.clang-format](.clang-format) (the targets exist when `clang-format` is installed):
`cmake --build build/release --target format` applies it and `--target format-check` verifies it. Every source and header file is formatted;
only the tables of the generated `text_data.h` and the list of uniforms in `gles.h` are fenced off with `clang-format off`.

## Layout

| Path | Contents |
|---|---|
| [include/candytron/core](include/candytron/core) | libraries without platform code: `sha256.h` and `assets.h` (the production data), `csl.h`, `csl_natives.h` and `csl_session.h` (the script machine, its natives and the demo's session), `rand.h`, `bitmap.h` and `text.h` (textures), `mesh.h` and `babe.h` (meshes), `render.h` (geometry device, effect chains and the Direct3D state of every draw), `v2.h` (V2 synthesizer and sequencer) |
| [include/candytron/render](include/candytron/render) | `gl.h`: the OpenGL ES 3 function table; `gles.h`: the backend that draws what `core/render.h` hands to the device; no window, no loader |
| [include/candytron/player](include/candytron/player) | `player_*.h`: the SDL-free parts of the player (command line, 4:3 view, credits, GL bookkeeping, the audio clock and the song) |
| [src/player](src/player) | `main.c`: the SDL3 application (native and Emscripten); `selftest.h`: the renderer's unit scenes (`--selftest`) |
| [web](web) | `index.html`: the page of the WebAssembly build |
| [tools](tools) | `bake_text.py`: bakes the text of the textures into `text_data.h` |
| [tests](tests) | the test runner, its suites and the fixtures (`tests/data/candytron`) |
| [cmake](cmake) | the build modules and scripts used by `CMakeLists.txt` |
| [assets](assets), [third_party/licenses](third_party/licenses) | the production data and the licences embedded in the player |
| [doc](doc) | architecture, testing and the reference implementation |

The libraries are single headers in the style of stb: define `GT3_CSL_IMPLEMENTATION`, `GT3_MESH_IMPLEMENTATION`, `V2_IMPLEMENTATION` and so on
in exactly one source file before including the header. [doc/ARCHITECTURE.md](doc/ARCHITECTURE.md) explains the design.

## Build options

| Option | Default | Meaning |
|---|---|---|
| `CANDYTRON_BUILD_PLAYER` | ON | build the SDL3 player |
| `CANDYTRON_BUILD_TESTS` | ON (top level, not Emscripten) | build `candytron_tests` and register it with CTest |
| `CANDYTRON_FETCH_SDL3` | ON | fetch SDL3 (`CANDYTRON_SDL3_TAG`, default `release-3.4.8`) when no installed SDL3 >= 3.2 is found; with OFF the player is skipped in that case |
| `CANDYTRON_WEB_MAX_MEMORY` | 1GB | Emscripten only: the largest the WebAssembly memory may grow to |
| `CANDYTRON_SANITIZE` | OFF | AddressSanitizer and UndefinedBehaviorSanitizer |

The presets are `release`, `debug`, `asan` (Clang with the sanitizers), `native-march` (`-march=native`), `web` and `web-tests`.

## Determinism

The generated textures, meshes and geometry and the audio are compared with the original's output, so the code relies on strict IEEE-754 binary32
arithmetic: no `-ffast-math`, no fused multiply-add contraction (`-ffp-contract=off`, set through `candytron::headers`) and `-frounding-math`.
The original runs the x87 unit with its own control word (24-bit significand, round down): the script is run with the rounding mode set downward
(`gt3_round_down()`), the original's power function is its x87 instruction sequence on x86 with GCC or Clang and a software equivalent elsewhere, and the
renderer's hooks switch back to round to nearest for the driver. On such a host the results are the original's bit for bit. WebAssembly cannot set the
rounding mode: there a few pixels of the textures differ in the last bit (55 of 1.8 million) and the floats that the script derives from its splines in the last
bits, and the tests allow for exactly that. The `native-march` preset builds with `-march=native` to prove that contraction stays off, and a test checks that the libraries
declare no mutable global state.

## Licences

The code in this repository is derived from the released GenThree sources (BSD 2-clause) and from the public-domain v2redux player; the player links
SDL3 (zlib); the baked text uses Liberation Sans (SIL OFL); the production data is CC BY 3.0. The notices are in
[third_party/licenses](third_party/licenses) and [assets/ATTRIBUTION.md](assets/ATTRIBUTION.md), and `candytron --licenses` prints them.
