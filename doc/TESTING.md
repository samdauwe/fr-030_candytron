# Testing

## The runner

```sh
./build/release/candytron_tests                    # every suite, a few at a time; no arguments needed
./build/release/candytron_tests --list             # the names of the suites
./build/release/candytron_tests csl_ player_       # the suites whose name contains one of the words
./build/release/candytron_tests v2                 # exactly the suite named v2 (a full suite name selects only that suite)
./build/release/candytron_tests --jobs 2 --verbose
cmake --build build/release --target check         # builds and runs it
ctest --test-dir build/release -j8                 # one CTest test per suite, plus the two static checks
```

[tests/test_main.c](../tests/test_main.c) is the only `main` of the test program. The suites are the functions
`int suite_<name>(int argc, char **argv)` of [tests/suites](../tests/suites); [tests/suites.def](../tests/suites.def) lists them with
their arguments (the directories of the production data and of the fixtures), time limit and flags. CMake reads the same file to register
the CTest tests, so adding a suite means writing the function and adding one line. A few entries run one function with another argument (the
two `csl_trace` entries, the two `render_geo` entries, the `player_cli` cases).

A suite returns `0` on success, `77` (`CANDYTRON_TEST_SKIP`) when it cannot run on this machine (no OpenGL context, no player executable) and
anything else on failure.

On POSIX systems the runner starts every suite in a child process:

* a crash, an `exit()` or a hang (the time limit is enforced) in one suite cannot touch another;
* the output of a suite is captured and printed only when it fails (or with `--verbose`);
* up to `--jobs` suites run at once (default: the number of processors, at most 8; the option accepts 1 to 32); suites marked
  `SUITE_SERIAL` measure wall-clock behaviour and run alone afterwards;
* `CANDYTRON_TEST_TIMEOUT_SCALE=N` multiplies the time limits (slow machines, sanitizer builds; CTest sets it for the `asan` preset).

Where there are no processes (Emscripten, Windows) the suites run one after the other in the runner's own process.

Each suite includes the library headers with `*_IMPLEMENTATION` and `*_STATIC`, so it has a private copy of the code it tests and can use its
internals. Suites that use the production data read `assets/`; the fixtures are in `tests/data/candytron` (see [REFERENCE.md](REFERENCE.md) for where
they come from).

## Suites

The library suites are unit tests of one header, or comparisons of the whole demo with the oracle's fixtures.

| Suite | Checks |
|---|---|
| `sha256` | `sha256.h` against the FIPS 180-4 vectors, reference values at the block boundaries (1, 55, 56, 57, 63, 64, 65, ... bytes) and incremental updates |
| `assets` | size, SHA-256 and outer structure of `default.pac` (the 28-byte header and its streams: 23,708 code bytes, 10,283 immediates, 58 functions, 4,348 variable references, 253 string bytes), `josie.v2m` (480 ticks per beat, 206,898 ticks, 1 global event) and `babe.mesh.old` (the `ryg` trailer); a flipped bit, a short file and a long file are refused; the fixture `assets.sha256` and the table in `assets.h` agree; a directory with one changed byte is refused by name and nothing stays allocated |
| `bitmap` | `rand.h` and `bitmap.h` on values worked out by hand: the generator, the 15-bit colour lanes, the fade and the operators (saturation, the `>> 16` multiplications, invert, gray), the 8-bit conversion, the original's per-byte mip filter, `gt3_fpow` (hardware and software, rounding down, the 0 and NaN cases), the Perlin tables |
| `csl_vm` | `csl.h` on hand-assembled packed scripts: integer and float arithmetic (16.16, `MulDiv`, the truncating `F2I`), control flow, calls and locals, strings, object members, the errors (an empty stack, the null object, a cut-off script), every limit of the loader and the damaged-script cases, and the object collector (what a global reaches stays, the rest goes, handles are reused) |
| `mesh` | `mesh.h` and `babe.h` on meshes small enough to work out by hand: the cube's counts and half-edge consistency, the creased cube, Subdivide (`2V + E + F` vertices), Triangulate, normals, SelectCube, SelectRandom as a function of the seed, the recorder (a mesh in store mode replays to the buffer it made when the labels are at their defaults; moving a label moves the mesh); the character: `babe.mesh` and `babe.mesh.old` decode to 954 vertices, 1,855 edges, 970 faces, each ending on its `ryg` trailer, every truncation is refused and flipped bits are survived (under the address sanitizer) |
| `csl_trace` | runs `default.pac` as the player does (generation, timeline init, the 104-step pre-roll, 40 frames at 0, 5, ... 195 s) with the natives and compares **all 2,802 lines of the native-call trace** (2,654 calls) with `oracle/calls.txt`: the native, its arguments, the class of its result and, for every bitmap and mesh, the SHA-256 of its pixels or vertices (129 bitmaps, 240 meshes); then **all 1,343 lines of the state dump** with `oracle/state.txt`; the progress counter ends at the original's 2,397 steps. The mesh is `tests/data/candytron/mesh/babe.mesh` |
| `csl_trace_exe_mesh` | the same with the executable's character (`assets/babe.mesh.old`, **what the player ships**) against `oracle/calls_exe_mesh.txt`; all 2,802 lines and the vertex hashes of the meshes are equal |
| `textures` | the 28 textures the script hands to the device, hashed as the oracle does (pixels and the whole mip chain), equal `oracle/tex.txt`; a second run as WebAssembly will run it (round to nearest, software power function) against the first: 55 of 1,835,008 pixels differ, all by 1; a third run with the baked text: all 8 text calls are answered and only the 5 textures with text change |
| `render_geo` | the script runs with the renderer of `render.h` behind its natives and what the device is given equals the oracle's: **all 3,367 geometry uploads** of `oracle/geo.txt` in order (handle, static or dynamic, vertex format, counts, the SHA-256 of vertex and index bytes), **the 1,550 draw calls of the 40 frames** frame by frame, the SHA-256 of the DRAW lines of every frame and the SHA-256 of **every line** of every frame (viewports, camera, matrices, lights, material, render, texture-stage and sampler states, texture bindings, draws): 1,538 viewports, 40 of 40 frames equal |
| `render_geo_collect` | the same with the object collector on, as the player runs it: equal hashes, and 791 objects live in every picture (a picture creates no objects) |
| `v2` | `v2.h` renders `josie.v2m` (format version 5, speech synthesizer on) **bit for bit as v2redux does**: all 9,000,000 frames in chunks of irregular size, against the SHA-256 of every second and of the whole stream in `oracle/josie_pcm.txt`. Also the song's SHA-256, format version (5) and length (195,879 ms); a muted song is silent, the polyphony stays within 32 voices, two starts at 30 s render the same, a stop silences, 400 damaged files are refused or played without a crash or a leak, every allocation failure of `v2_open()` leaves nothing allocated |
| `player_args` | every option of the command line and every error message |
| `player_view` | the 4:3 rectangle in windows of every shape, including degenerate ones |
| `player_credits` | the credits of the production, the five embedded notices (GenThree, the production data, Liberation Sans, the V2 synthesizer, SDL3) with their texts; the data notice says CC BY and not share-alike |
| `player_glcheck` | GL object accounting and error polling |
| `player_audio` | the picture clock against a simulated device (rates 44.1 and 48 kHz, periods 256 to 2048 frames, jittery polls, pauses, an underrun, a seek): it never runs backwards, never leads the audio by more than the extrapolation cap and lags by at most one period plus one poll interval; the song: silence before it plays, the frame counter, chunked rendering equals one call, a second song replaces the first, a refused song leaves silence, the output is clipped to ±1, every allocation failure of the start |

The player suites start `candytron` headless (SDL's offscreen video driver and dummy audio driver; `LSAN_OPTIONS` points at `tests/lsan.supp` for the
Mesa and EGL allocations that are not freed by design) and judge its exit status and report. The cases with a window skip when there is no OpenGL ES
context; Mesa's software rasteriser is enough.

| Suite | Checks |
|---|---|
| `player_licenses`, `player_version`, `player_bad_args` | `--licenses` prints the credits and notices, `--version` its line, an unknown option is refused; none of them opens a window |
| `assets_player_verify` | `--verify-assets` reports the same sizes and structure numbers as the `assets` suite |
| `assets_player_missing`, `assets_player_tampered`, `assets_player_tampered_live` | the player refuses missing data and a song with one changed byte, naming the file, with `--verify-assets` and on the live path, before any window |
| `player_window` | one second of the demo in a 320x240 window with `--gl-check`: no GL error, every GL object created is deleted |
| `player_audio_live` | the same with the dummy audio driver: after one second 30,000 to 100,000 frames of the song were rendered and the last picture is at song time 0.5 to 1.3 s |
| `player_start` | `--start 100`: the song and the picture begin there; after a second the audio has rendered from 100 s on and the last picture is at 100.5 to 101.5 s |
| `player_soak` | `--soak 2`: the report lines, no GL error. The judgement of `--soak` needs two loops of the song (392 s) and is made by hand, see Soak |
| `player_selftest` | `candytron --selftest --gl-check`: ten unit scenes drawn through `gt3_render` and the original's state streams, read back from the screen target and compared with CPU formulas (tolerance 1 to 3 of 255): the half pixel, blending (alpha, add, multiply, smooth), the stage chains (modulate, add, subtract, add smooth), point and bilinear sampling, texture matrices and coordinates from the normal, depth read and write, culling, the alpha test, vertex lighting (directional, point, spot), a render target as a texture. 54 checks, no GL error, no leaked object |
| `player_capture` | `--capture DIR --times 20,60`: both pictures exist, are not black, differ from each other, and a second run makes the same SHA-256 (the pictures come from the script's time, not from the clock) |
| `player_golden` | `--capture DIR --times 10,20, ... 180`: the 18 pictures, each reduced to 100x75, are within an RMSE limit of the original's picture at that song time (see Pixel goldens) |

## Static checks

CTest also registers two checks that are not suites:

* `headers_no_mutable_statics` fails when a header of `include/candytron/core` declares mutable static state: the libraries are reentrant, every
  instance owns its state.
* `assets_sha256` compares the SHA-256 of the three data files, computed by CMake itself, with `tests/data/candytron/assets.sha256`: an independent
  check of both the files and of `sha256.h`.

## Pixel goldens

Only the original's executable can say what the pictures should look like, and two of its own runs differ from each other by a picture RMSE of about
4.2 levels (see [REFERENCE.md](REFERENCE.md)). `tests/data/candytron/golden` holds the original's pictures at 18 song times, reduced to 100x75 with
8x8 means, and `golden.txt` an RMSE limit per time. The limit is the port's RMSE when it was made plus the larger of 3 levels and 30 %, so it is a
*regression bound*: it fails when a picture moves away from the original's, and it says nothing better than "as good as it was" where the port is known
to differ (the limits are wide between 50 and 150 s). The full-resolution comparison with the recordings, with the noise floor as the threshold, is not a
test and not part of this repository. Hashes of the pictures are not committed because a driver's output is not bit-stable across drivers.

## Sanitizers and `-march=native`

```sh
cmake --preset asan && cmake --build --preset asan && ctest --preset asan    # clang, AddressSanitizer + UndefinedBehaviorSanitizer
cmake --preset native-march && cmake --build --preset native-march && ctest --preset native-march
```

The `asan` preset builds the runner and the player with `-fsanitize=address,undefined` (recoverable UB checks are fatal) and runs the same suites with
the time limits multiplied by 4; the `v2` suite and the trace and geometry suites take about a minute each. The `native-march` preset builds with `-march=native`: the
suites compare results bit for bit, so they fail if the compiler contracts a multiply and an add into a fused operation.

## WebAssembly

```sh
export EMSDK=$HOME/emsdk && source $EMSDK/emsdk_env.sh
cmake --preset web-tests && cmake --build --preset web-tests && ctest --preset web-tests
```

The runner is built with Emscripten and run under Node (CTest starts it through `CMAKE_CROSSCOMPILING_EMULATOR`; `NODERAWFS` lets it read the data and
fixtures by their absolute paths). The 16 suites that do not need the player executable run and pass, the 14 suites that need the player executable are reported as skipped, and the
two static checks run. The call trace, the state, the draw calls, the meshes and the synthesizer (the 9,000,000 frames and the SHA-256 of every
second) pass in WebAssembly's round-to-nearest with musl's mathematics; what needs the original's rounding mode (the 64-bit bitmap pixels, the
vertex and draw-line hashes, and the exactness of the state) is compared natively only. In round to nearest the call trace is still exact, and the
state lines may differ in the last bits of a spline-derived translation: the test tolerates 4,096 ulp there (`CANDYTRON_TEST_NEAREST=1` selects that
mode natively too).

## Soak

`candytron --soak S` makes S seconds of pictures at 30 a second (`--soak-rate`), as fast as possible, the song time wrapping at its end, and after each
loop compares the heap and the number of live objects with those at the end of the first loop. It needs at least two loops of the 196 s song:

```sh
./build/release/candytron --soak 400 --gl-check      # about 10 s: "the heap grew by ... bytes ...: ok"
```
