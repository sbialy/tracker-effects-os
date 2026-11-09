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
