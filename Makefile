# tracker-effects-os: the public master insert plugins of tracker-ff (OpenTracker fx ports). Works with Apple make 3.81.
#
#   make TOOLCHAIN=/path/to/arm-none-eabi- [PY=python-with-unicorn]   the set into build/set (.plg, NOTICE.md, licences, SET.json, SHA256SUMS)
#   make verify [OUT=...]                                               SHA256SUMS, SET.json, header, CRC and ABI of a finished set
#   make wasm [WASM_OUT=...]                                            the browser modules (.wasm + manifest), needs emsdk 6.0.13
#   make package [OUT=...]                                              build/tracker-ff-effects-<set>.zip, files under Plugins/Master/
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

.PHONY: all verify package wasm sdk-check sdk-sync clean
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

clean:
	rm -rf "$(HERE)build"
