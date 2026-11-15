# Reference implementation

This project is a native C99 implementation of a demo that already exists as an executable, as released source and as recordings of that
executable. They are the reference for every result the code produces, and the tests are built on them. None of it is part of this repository except
the data and the fixtures derived from it.

| Reference | Where | What it is |
|---|---|---|
| Original source | `genthree/` of Farbrausch's [fr_public](https://github.com/farbrausch/fr_public) (tag `original`, also on `master`) | The released GenThree C++ (BSD 2-clause, Direct3D 9, x87 and MMX era): the script runtime `cslrt.cpp`, the intro player `mainplayer.cpp` and `_startintro.cpp`, the natives of `genplayer.cpp` with `genbitmap.cpp`, `genmesh.cpp`, `genmaterial.cpp`, `genfx.cpp` and `genmisc.cpp`, and the basics in `_types.cpp`. The authority for arithmetic and behaviour. |
| Executable | `fr030-candytron-final-101.exe` from scene.org (final version 1.01, 65,536 bytes, SHA-256 `2b437a3629f2c815be92df0765af8acd8a185eb3a59cd1fe95a11dce2a99cdc4`) | What ships. It is packed with an early kkrunchy. Its data is the production data of this repository, and it was recorded under Wine. |
| Oracle | built from the original source, outside this repository | The original C++ run on Linux as a test harness: it prints what the original computes (native calls, script state, textures, meshes, geometry, Direct3D state of every draw). The source of most fixtures. |
| v2redux | [spheenik/v2redux](https://github.com/spheenik/v2redux) | A public-domain, deterministic C++ port of the V2 synthesizer that plays every V2M version. The reference for the audio. |
| Recordings | made with the executable under Wine | Two recordings of 201 s (800x600 at 30 pictures a second, lossless, and the sound). The reference for pictures, which no oracle can compute: the original draws with Direct3D 9. |

The reference material is not needed to build, run or test the player. It is needed to regenerate fixtures.

## The data and the source tree

`assets/` holds `default.pac` (the compiled and packed script), `josie.v2m` (the soundtrack, a V2 module of format version 5 with the lyrics of the
speech synthesizer on channel 15) and `babe.mesh.old` (the character "Josie" in the compact mesh format). They are the files of `genthree/data` in
fr_public, byte for byte, and each occurs whole in the executable once its packing is undone (image offsets `0x42e62e`, `0x424c00` and `0x422014`).
Their sizes and SHA-256 are in [assets/ATTRIBUTION.md](../assets/ATTRIBUTION.md) and `tests/data/candytron/assets.sha256`.

**The source tree does not describe the executable.** `genthree/introasm.asm` includes `data\candytron_final_064.pac` and `data\babe.mesh`; the
executable contains `default.pac` and `babe.mesh.old`.

| | Source tree (build files) | Executable |
|---|---|---|
| script | `candytron_final_064.pac` (74,281 bytes) | `default.pac` (74,281 bytes): three 32-bit immediates and one unused padding byte of the function table differ |
| mesh | `babe.mesh` (12,738 bytes) | `babe.mesh.old` (11,244 bytes): the first 8,080 bytes are the same, the range-coded key stream differs (older quantisation tables) |

This player follows **the executable**. `tests/data/candytron/mesh/babe.mesh` is the source tree's character, kept because the oracle could not
read the executable's mesh: its mesh hashes are those of the new-format mesh (see below).

## Which code corresponds to which source

The headers follow the boundaries of the original's files.

| Here | Original source (`genthree/`) |
|---|---|
| `core/csl.h` | `cslrt.cpp` (`ScriptRuntime::PackedImport`, `Interpret`), `cslrt.hpp` |
| `core/csl_natives.h` | the natives table of `genplayer.cpp` and the glue to the script's objects of `genbitmap.cpp`, `genmesh.cpp`, `genmaterial.cpp`, `genfx.cpp`, `genmisc.cpp` |
| `core/csl_session.h` | `mainplayer.cpp`, `_startintro.cpp` (`sAPPCODE_INIT`, the paint handler) |
| `core/rand.h` | `sGetRnd` of `_types.cpp` |
| `core/bitmap.h` | `genbitmap.cpp` (the MMX loops written lane by lane), `sFPow` of `_types.cpp` |
| `core/text.h`, `core/text_data.h` | `Bitmap_Text` of `genbitmap.cpp` (the glyph coverage is baked, see below) |
| `core/mesh.h` | `genmesh.cpp` (the half-edge mesh, the recorder and the operators the intro uses) |
| `core/babe.h` | `GenMesh::ReadCompact`, `MeshCoder::Decode`, the range coder of `_types.cpp` |
| `core/render.h` | `GenMesh::Prepare`, `DoPass1`, `DoPass2` and the state of `genmaterial.cpp`, the render handlers and `FXMaster_` of `genfx.cpp`, the geometry buffers of `_startintro.cpp` (`GeoAdd`, `GeoBegin`, `GeoEnd`, `GeoDraw`) |
| `render/gles.h` | none: Direct3D 9's fixed function (lighting, texture stages, texture coordinate generation, blending) written as one GLSL ES 3.00 program |
| `core/v2.h` | v2redux (itself after kb's `synth.asm`, `v2seq` and `ronan`), the version-5 behaviours of its era ledger |
| `core/assets.h`, `core/sha256.h`, `render/gl.h`, `player/*.h`, `src/player/*` | none: player code |

The constants and tables of the Direct3D state (`GT3_D3D*`, `GT3_MBF_*`, `GT3_MBM_*`, `GT3_MTF_*`) were taken from the original's headers and code. No
code of the original's assembly is in this repository: the x87 and MMX instruction sequences are written as C that has the same semantics. The V2
synthesizer of the 2003 engine is not sample-reproducible (it seeds its noise from the cycle counter and calls the x87 `fsin` and `fpatan`); the
reference for `v2.h` is v2redux's render of version 5 modules, which replaces those with portable equivalents.

## Where the test data comes from

| Here | Made by |
|---|---|
| `tests/data/candytron/assets.sha256` | `sha256sum` of the three data files |
| `oracle/calls.txt`, `oracle/state.txt` | The oracle, on `default.pac` and the source tree's `babe.mesh`. `calls.txt` is the trace of every native call of the script (the native, its arguments, the class of its result, and for every bitmap and mesh the SHA-256 of its pixels or vertices); `state.txt` the members the script wrote into its scenes, effect chains, matrices, mesh animations and material passes, as changes after the generation, the timeline init, the pre-roll and each frame, and at each call of `RenderChain`. |
| `oracle/calls_exe_mesh.txt` | The same trace from an oracle whose key coder carries the executable's quantisation tables, so that it makes the character from `babe.mesh.old`. Everything but the mesh results equals `calls.txt`. |
| `oracle/tex.txt` | The 28 textures the script hands to the device (and the 10 render targets, the 8 text calls) with the SHA-256 of the pixels and of the mip chain. The oracle leaves the text blank (its Windows GDI is not there). |
| `oracle/geo.txt` | Every geometry the original uploaded to the device (`GeoEnd`), in order: handle, static or dynamic, vertex format, counts and the SHA-256 of vertex and index bytes. |
| `oracle/frames.txt` | For 40 frames at 0, 5, ... 195 s: the number of draw calls, the SHA-256 of the draw lines and the SHA-256 of every line of the frame (viewports, camera, matrices, lights, material, render, texture-stage and sampler states, texture bindings). |
| `oracle/josie_pcm.txt` | v2redux's render of `josie.v2m` (44.1 kHz, stereo, float32, 9,000,000 frames, the speech synthesizer on): the SHA-256 of every second and of the whole stream. |
| `golden/frame_<time>.ppm`, `golden/golden.txt` | The original's pictures at 18 song times (10, 20, ... 180 s) from the first recording, reduced from 800x600 to 100x75 by 8x8 means, and the RMSE limit for each. |

**How the oracle is built.** It copies `genthree/` and compiles the original files that make the script, bitmaps, meshes, materials and effects, with
the intro player (`-D_PLAYER`), using `clang++ -m32 -fms-extensions -fasm-blocks -O0`: clang compiles the MSVC inline assembler (MMX kernels, x87
mathematics) as it is, and the result is 32-bit x86 code on the x87 unit with the original's control word (24-bit significand, round down). What
`_startintro.cpp` does with Direct3D 9, DirectSound and the GDI is replaced by a simulated device that records; its state-stream code, viewport code and
buffer allocation are the original's. `-O0` is required: the original's recorder reads its variable arguments straight from the stack frame, and a
few functions read their arguments as one array across parameters (`FXChain_BeginViewport`, `Layer2D`, `Blend4x`, `Mesh_Cube`), which the port
must not inherit: it reads those arguments in the right slots. Every allocation is zeroed, because the script's globals and some scratch fields of the
mesh records are read without being written. The oracle is deterministic (two runs under different heap fill patterns are byte-identical).

**What the oracle cannot do.** It cannot make the text (Windows GDI, Arial), and the vendored mesh reader crashes on the executable's `babe.mesh.old`,
which is why `babe.mesh` is the source tree's. It cannot draw: the pictures are compared with the recordings.

**The rounding mode is what matters.** The script's integers and the topology of the meshes are identical in every floating-point mode. The floats
are not: the same code built for SSE2 with round to nearest makes textures that differ in up to hundreds of pixels by one level and geometry that differs
by up to a million ulp, while SSE2 with `fesetround(FE_DOWNWARD)` brings the textures to a few pixels and the geometry to a few hundred ulp of the
original's x87 build. That is why the player runs the script in round-down mode and why WebAssembly, which cannot, is compared with a tolerance.

**The text.** The wall text of the demo is drawn by Windows GDI with the font "arial". `tools/bake_text.py` renders the 7 distinct strings of the
script's 8 text calls with Liberation Sans (metric compatible with Arial, SIL Open Font License 1.1) through a model of that call (`lfHeight`, `lfWidth`
as a horizontal stretch against the font's average character width) and writes `core/text_data.h`. Under Wine the original's "arial" is Liberation
Sans, and the recordings agree with the model best at Liberation Sans's average width (1187/2048 em). The output is not bit-exact with Windows' Arial.

## Why several references

The original source is the truth for what the script does, but it cannot be compiled and run as it was: it draws with Direct3D 9. The oracle
therefore stops at the device: it records what the original *asks* of it, which is the semantic ground truth for the renderer (every draw call, its
vertices and the full Direct3D state, as the original's own state code writes them). The pictures are the part no oracle can give; they are compared
with recordings of the executable.

The recordings are not the original's output twice: two runs of the executable differ from each other, with a picture RMSE of about 4.2 levels (of 255)
and a correlation of the sound's spectral envelope of 0.887. That is the noise floor, and it is why the golden pictures are a regression bound
rather than a proof (see [TESTING.md](TESTING.md)). Where the port and the recordings still differ, the draw calls are the same, so the difference is in
vertex data the port does not reproduce: the translucent sheets at 76 to 82 s and 130 to 150 s, and the camouflage pattern on the character at 86 to
90 s.

## Regenerating a fixture

The oracle and its scripts are not part of this repository, and the fixtures are data. To regenerate them: build the oracle as above from
fr_public's `genthree/` and run it on `assets/default.pac` and the mesh named in the header of each fixture; render `josie.v2m` with v2redux
(version 5, the speech synthesizer on) and hash it per second; record the executable under Wine for the golden pictures. Each fixture starts with
comment lines that name what it was made from (the SHA-256 of the script and of the mesh). `text_data.h` is regenerated with `tools/bake_text.py`
(needs Pillow and the Liberation Sans font), followed by the `format` target.
