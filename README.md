# tracker-effects-os

The public master insert plugins of tracker-ff (the custom firmware for the Polyend Tracker): Dirt, Tape, Lo-Fi, BBD and Phaser, ports
of the OpenTracker fx modules. Each is built into a device file (`<id>.plg`, card folder `Plugins/Master/`) and a browser file for the
browser editor (`<id>.wasm` plus the manifest `<id>.json`).

tracker-ff 1.2.0 is split into five repositories:

| Repository | Holds |
|---|---|
| tracker-ff | the firmware: plugin host, plugin SDK, the built-in engines ACD FAT VAP WTFM PERC CHIP, `.ptfx` effects |
| tracker-engines-os | the public synth engine plugins (open source) |
| tracker-engines | the private synth engine plugins (PHZ, PLK, freeware binaries only) |
| tracker-effects-os (this one) | the public master insert plugins (open source) |
| tracker-effects | the private master insert plugins Console, Tube (freeware binaries only) |

The firmware pins a release of every module repository in its `engines/external.lock.json` and checks the published files against
the plugin ids it keeps in `engines/engines.json`.

## Layout

| Path | What |
|---|---|
| `src/synth-open/plug/fx/ot_*_plug.c` | Tape, Lo-Fi, BBD, Phaser |
| `src/synth-open/plug/fx_dirt_plug.cpp` | the Dirt plugin wrapper around the firmware's Dirt kernel |
| `src/synth-open/plug/dirt_*_check.py` | Dirt checks run by the build (no DC step entering Asym, zero in gives zero out) |
| `src/synth-open/plug/fx/NOTICE.md` | the notice of every insert |
| `sdk/files/` | a pinned copy of tracker-ff files: the plugin SDK (`plug_abi.h`, `plug_rt.c`, `plug.ld`, `build_plug.py`, `emu_plug.py`, `plug_wasm.h`, `plug_rt_wasm.c`) and the Dirt kernel (`ff_dirt.h`, `ff_dirt.cpp`, `ff_dirt_ctl.cpp`). `sdk/SOURCE` names the tracker-ff commit |
| `effects.json` | the table: plugin id, version, flags, the expected emulator checksums, licence |
| `build.py`, `Makefile` | the `.plg` set build and its checks |
| `wasm/` | the `.wasm` twins and their manifests (`build_modules.py`, emcc pinned in `wasm.json`) |
| `tools/sdk_sync.py` | compares or refreshes `sdk/files/` against a tracker-ff checkout |

## Build

Needs the Arm GNU toolchain 12.3.rel1, Python 3 and for the emulator checks a Python with `unicorn`. The `.wasm` build needs emsdk
6.0.13 (`EMSDK=...` or `--emsdk`) and clang for the arm64 descriptor dump.

```bash
export TOOLCHAIN=/path/to/arm-gnu-toolchain-12.3.rel1/bin/arm-none-eabi-
make PY=/path/to/python-with-unicorn     # build/set: 5 .plg, NOTICE.md, licence texts, SET.json, SHA256SUMS
make verify
make wasm                                # build/wasm: 5 .wasm + .json, NOTICE.md, licences, MODULES.json, SHA256SUMS
make package                             # build/tracker-ff-effects-1.0.0.zip, unzip at the card root
```

Every insert is checked in the emulator at the three places the firmware loads a plugin (the output checksum must be the same and
equal to `effects.json`), with default and with moved parameters. The build is byte reproducible, and the 1.2.0 set is byte identical
to the `.plg` files of the tracker-ff 1.2.0 release.

## The SDK copy

A clone of this repository builds on its own. With a tracker-ff checkout next to it:

```bash
make sdk-check FW=../tracker-ff          # every file of sdk/files equals the checkout
make sdk-sync FW=../tracker-ff           # take the checkout's files (then rebuild, compare, bump versions if bytes changed)
```

A release is a tag `vX.Y.Z` with the files of `build/set` and `build/wasm` as release assets. tracker-ff pins the tag, its commit
and the sha256 of every file.

## Licence

MIT. The ports keep the MIT licence of opentracker-fx (Marcin Spoczynski) and the Apache 2.0 licence of the OpenTracker 3.x DSP they
use, the tracker-ff parts are MIT. Details: `NOTICE.md`, `src/synth-open/plug/fx/NOTICE.md` and `licenses/`.
