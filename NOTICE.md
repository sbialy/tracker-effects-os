# Notices

tracker-effects-os holds the public master insert plugins of tracker-ff. The notice of every insert (upstream, commit, licence, what was
changed) is in `src/synth-open/plug/fx/NOTICE.md`. `make` writes the sections of the built inserts into the `NOTICE.md` next to the
`.plg` files and copies the licence texts they name from `licenses/`.

* Tape, Lo-Fi, BBD, Phaser and the Dirt kernel: ports of opentracker-fx (https://github.com/opentracker-fw/opentracker-fx), MIT,
  Copyright (c) 2026 Marcin Spoczynski, `licenses/LICENSE-OpenTracker-fx-MIT.txt`. Lo-Fi, BBD and Phaser also use OpenTracker 3.x
  DSP (https://github.com/sandlbn/polyend-tracker-re), Apache License 2.0, `licenses/LICENSE-OpenTracker-Apache-2.0.txt`.
* Dirt: `src/synth-open/plug/fx_dirt_plug.cpp` wraps the firmware's built-in Dirt kernel, `sdk/files/synth-open/ff_dirt.{h,cpp}` and
  `ff_dirt_ctl.cpp`, a pinned copy of the tracker-ff files (the plugin must sound exactly like the built-in Dirt, checksum 3b1f3dc5).
* The plugin SDK and runtime in `sdk/files/synth-open/plug/` (linked into every plugin), the wrappers, `build.py`, `wasm/` and
  `tools/`: tracker-ff, MIT, Copyright (c) 2026 sbialy, `LICENSE` and `licenses/LICENSE-tracker-ff-MIT.txt`.
* The Sample Editor effects in `src/ptfx/` (Amplifier, Overdrive, Delay, Bitcrusher, Chorus, Flanger, Equalizer, Limiter, Compressor): ports
  of `core/effector/mtEffect*.cpp` of the Polyend Tracker Community Edition firmware, MIT, Copyright (c) 2020-2026 Polyend Sp. z o.o. and Polyend
  Tracker Community Edition Contributors, as carried into tracker-ff (`licenses/LICENSE-tracker-ff-MIT.txt`). They are the offline effects of the
  Sample Editor that tracker-ff 1.4.0 moved out of its image to `Effects/*.ptfx` on the card. The header of every source file names its original and
  what changed. The Equalizer filter is eqfil of Soundpipe (https://github.com/eljeff/Soundpipe/blob/master/modules/eqfil.c, MIT), based on csound eqfil.
  The Flanger sine is `arm_sin_q15` of CMSIS-DSP (Arm Limited, Apache License 2.0, `licenses/LICENSE-OpenTracker-Apache-2.0.txt`), rewritten with its table so that
  the plugin gives the same value for every 16 bit argument as the library of the firmware. The Equalizer and the Bitcrusher are linked with
  the soft double routines of libgcc (GCC Runtime Library Exception).
