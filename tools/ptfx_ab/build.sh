#!/bin/sh
# Builds the A/B harness against a tracker-ff worktree that has the TRIM objects built (core/build-*-trim/obj/effector/mtEffect*.o)
#   build.sh <tracker-ff worktree> <arm-none-eabi- prefix> <out dir>
set -e
FW=$1; TC=$2; OUT=$3
HERE=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$OUT"
OBJ=$(ls -d "$FW"/core/build-MK66-ORIGINAL-SYNTHOPEN-rel-*-trim/obj | head -1)
cd "$FW/core"
FLAGS="-DFW_TRIM -DFW_SE_PTFX -D__NEWLIB__ -DKINETISK -D__MK66FX1M0__ -DF_CPU=180000000 -DUSB_MIDI -DTEENSYDUINO=142 -DARDUINO=10805 -DCPU_MK66FX1M0VMD18 -DFEATURE_SYNTH -DFEATURE_SYNTH_OPEN
 -I./include -I./config -I./source -I./synth -I./libs -I./libs/Audio -I./libs/ElapsedMillis -I./libs/SPI -I./libs/Audio/utility -I./interface -I./effector -I./FileManager -I./AudioEngine
 -I$FW/util/include -I$FW/util/3rdparty -std=gnu++20 -felide-constructors -includecstdint -fno-rtti -fno-exceptions -fno-common -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb
 -specs=nano.specs -c -ffunction-sections -fdata-sections -ffreestanding -fno-builtin -O2 -DNDEBUG -mfp16-format=ieee -Os"
${TC}c++ $FLAGS -I"$HERE" -o "$OUT/harness.o" "$HERE/harness.cpp"
# the sine of the Flanger port, made visible (the plugin keeps it static) and built like a plugin
SRC=$(cd "$HERE/../.." && pwd)/src/ptfx/se_flanger.c
sed 's/^static int32_t arm_sin_q15/int32_t port_sin_q15/; s/arm_sin_q15(/port_sin_q15(/g' "$SRC" > "$OUT/se_flanger_sin.c"
${TC}gcc -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 -O3 -fno-builtin -fPIC -fvisibility=hidden -mpic-data-is-text-relative -mlong-calls -I"$HERE/../../sdk/files/editor-fx-sdk" \
  -Dplugin_init=fl_init -Dplugin_set_param=fl_set_param -Dplugin_process_block=fl_process_block -DPLUGIN_PARAMS=FL_PARAMS -c "$OUT/se_flanger_sin.c" -o "$OUT/se_flanger_sin.o"
E="$OBJ/effector"
${TC}c++ -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -specs=nano.specs -specs=nosys.specs -nostartfiles -Wl,--gc-sections -Wl,-T,"$HERE/ab.ld" \
  -o "$OUT/ab.elf" "$OUT/harness.o" "$OUT/se_flanger_sin.o" \
  $E/mtEffectAmplifier.o $E/mtEffectBitcrusher.o $E/mtEffectChorus.o $E/mtEffectCompresor.o $E/mtEffectDelay.o $E/mtEffectEQ.o $E/mtEffectFlanger.o $E/mtEffectLimiter.o $E/mtEffectOverdrive.o \
  "$FW/platforms/mk66/3rdparty/teensy_core/libarm_cortexM4lf_math.a" -lm -lgcc
echo "$OUT/ab.elf"
