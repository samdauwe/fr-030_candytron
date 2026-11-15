# fr-030 Candytron production data

The files below are the production's, byte for byte, and are never edited by hand; `ctest` checks them against the SHA-256 recorded in
`tests/data/candytron/assets.sha256`. They are the files of `genthree/data` in Farbrausch's fr_public, and all three occur unmodified inside the executable of the final version 1.01
(`fr030-candytron-final-101.exe`, scene.org, SHA-256 `2b437a3629f2c815be92df0765af8acd8a185eb3a59cd1fe95a11dce2a99cdc4`) once its kkrunchy packing
is undone; the offsets are image offsets of the unpacked executable (image base 0x400000).

| File | Content | Bytes | SHA-256 | In the executable at |
|---|---|---|---|---|
| `default.pac` | the compiled and packed CSL script of the production: code, immediates, function table, strings | 74,281 | `650835ca24d28d434e2d590eb454035026adcfc1e8d8824efd0e0da0dd61ac0d` | 0x42e62e |
| `josie.v2m` | the soundtrack: a V2 module (V2M version 5), with the lyrics of the speech synthesizer on channel 15 | 39,470 | `5bf17fb8d58d608cfa197d2309ecd629e5465698abf05338a4894c385ea5303b` | 0x424c00 |
| `babe.mesh.old` | the character mesh "Josie" in the compact mesh format, the older key stream | 11,244 | `90c597b9acae8c3905fa63ff965a19115ee61d2bde6ed3cab13b98ac569ae267` | 0x422014 |

The executable is **not** what the project's build files describe: `genthree/introasm.asm` includes `candytron_final_064.pac` and
`babe.mesh`, whereas the shipped executable contains `default.pac` and `babe.mesh.old`. This repository follows the executable; see
[doc/REFERENCE.md](../doc/REFERENCE.md).

## Licence

Farbrausch released the project files, meshes and music of fr-030: candytron under the **Creative Commons Attribution 3.0**
license (no share-alike):

> This directory contains the project files, meshes and music for fr-030: candytron.
> All are released under a CC-BY 3.0 license:
> http://creativecommons.org/licenses/by/3.0/

The files above are distributed under the same license, <https://creativecommons.org/licenses/by/3.0/>; attribution is the only
condition. The licence of the player's source code is in [LICENSE](../LICENSE). The executable itself is not part of this repository.
The released project is at <https://github.com/farbrausch/fr_public/tree/original/genthree/data>.

## Credits of the production

fr-030: Candytron, released at Breakpoint 2003 (2nd place in the 64k intro competition); the final version 1.01 followed the party version later.

* chaos: code (base system, 3d engine, texture generator, main tool code)
* ryg: code (mesh engine, postproc effects, exe packer, additional tool code)
* gizmo: textures, models, animations, timing
* kb: music, sound code, voice synthesizer

The production uses the GenThree engine.
