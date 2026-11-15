# tracker-effects-os: the public master insert plugins of tracker-ff (OpenTracker fx ports). Works with Apple make 3.81.
#
#   make TOOLCHAIN=/path/to/arm-none-eabi- [PY=python-with-unicorn]   the set into build/set (.plg, NOTICE.md, licences, SET.json, SHA256SUMS)
#   make verify [OUT=...]                                               SHA256SUMS, SET.json, header, CRC and ABI of a finished set
#   make wasm [WASM_OUT=...]                                            the browser modules (.wasm + manifest), needs emsdk 6.0.13
#   make package [OUT=...]                                              build/tracker-ff-effects-<set>.zip, files under Plugins/Master/
#   make ptfx TOOLCHAIN=...                                            the Sample Editor effects (src/ptfx/se_*.c) into build/ptfx/se_*.ptfx
#   make ptfx-ab FW=../tracker-ff PY=...                                the A/B of those .ptfx against the built in effects of a tracker-ff worktree (see tools/ptfx_ab/)
#   make sdk-check FW=../tracker-ff                                     the vendored SDK (sdk/files) equals a tracker-ff checkout
#   make sdk-sync FW=../tracker-ff                                      copy the SDK files from that checkout (review the diff)
#
# TOOLCHAIN is the compiler prefix with the trailing dash (Arm GNU 12.3.rel1 gives the released bytes), there is no default.
# PY is a Python with unicorn: it runs the emulator checks (without it they are skipped and SET.json says so).
PY       ?= python3
HERE     := $(dir $(lastword $(MAKEFILE_LIST)))
OUT      ?= $(abspath $(HERE))/build/set
WASM_OUT ?= $(abspath $(HERE))/build/wasm
HOST_ABI ?= $(HERE)sdk/files/synth-open/plug/plug_abi.h
FW       ?=

PTFX_OUT ?= $(abspath $(HERE))/build/ptfx
AB_OUT   ?= $(abspath $(HERE))/build/ptfx-ab

.PHONY: all verify package wasm sdk-check sdk-sync clean ptfx ptfx-ab
all:
	python3 "$(HERE)build.py" build --out "$(OUT)" --py "$(PY)"

verify:
	python3 "$(HERE)build.py" verify "$(OUT)" --host-abi "$(HOST_ABI)"

package: verify
	python3 "$(HERE)build.py" package "$(OUT)" --host-abi "$(HOST_ABI)"

wasm:
	python3 "$(HERE)wasm/build_modules.py" --out "$(WASM_OUT)" $(if $(wildcard $(OUT)/SET.json),--inserts-plg "$(OUT)")

sdk-check:
	@test -n "$(FW)" || { echo "set FW to a tracker-ff checkout"; exit 2; }
	python3 -I "$(HERE)tools/sdk_sync.py" check "$(FW)"

sdk-sync:
	@test -n "$(FW)" || { echo "set FW to a tracker-ff checkout"; exit 2; }
	python3 -I "$(HERE)tools/sdk_sync.py" sync "$(FW)"

ptfx:
	@test -n "$(TOOLCHAIN)" || { echo "set TOOLCHAIN to the arm-none-eabi- prefix"; exit 2; }
	python3 "$(HERE)tools/build_ptfx.py" --toolchain "$(TOOLCHAIN)" --out "$(PTFX_OUT)"

# FW is a tracker-ff worktree built with the release recipe (TRIM=1), its core/build-*-trim/obj/effector objects are the built in effects
ptfx-ab: ptfx
	@test -n "$(FW)" || { echo "set FW to a tracker-ff worktree with the TRIM objects built"; exit 2; }
	sh "$(HERE)tools/ptfx_ab/build.sh" "$(abspath $(FW))" "$(TOOLCHAIN)" "$(AB_OUT)" > /dev/null
	$(PY) -I "$(HERE)tools/ptfx_ab/run_ab.py" --elf "$(AB_OUT)/ab.elf" --ptfx "$(PTFX_OUT)" --nm "$(TOOLCHAIN)nm"

clean:
	rm -rf "$(HERE)build"
