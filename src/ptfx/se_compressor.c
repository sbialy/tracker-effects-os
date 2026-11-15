// Port of core/effector/mtEffectCompresor.cpp of the Polyend Tracker Community Edition firmware (tracker-ff d471709),
// Copyright (c) 2020-2026 Polyend Sp. z o.o. and Polyend Tracker Community Edition Contributors, MIT licence
// (licenses/LICENSE-tracker-ff-MIT.txt).
//
// Modified: the built in offline effect of the Sample Editor as a .ptfx card file. Same arithmetic per sample, the libm calls go through the host table (the firmware's own powf, log10f and expf).
// PTFX_NAME: Compressor
// PTFX_AUTHOR: Polyend
// PTFX_FLAGS: EFFECT
// PTFX_SCRATCH: 0

#include "tracker_plugin.h"

// The release firmware is built with -Os and the default fp contraction, which fuses the marked multiply adds into one vfma (one rounding).
// The plugin is built with -ffp-contract=off and states exactly those fused operations with fmaf, so the result does not depend on how a compiler
// contracts the surrounding code. tools/ptfx_ab checks it against the object of the firmware.
#define FMAF(a, b, c) __builtin_fmaf((a), (b), (c))

const PluginParam PLUGIN_PARAMS[4] = {
    PTFX_PARAM_INT("Thresh", -60.0f, 0.0f, -12.0f, 1.0f, "dB"),
    PTFX_PARAM_FLOAT("Ratio", 1.0f, 50.0f, 4.0f, 0.5f, ""),
    PTFX_PARAM_FLOAT("Attack", 0.3f, 100.0f, 10.0f, 0.1f, "ms"),
    PTFX_PARAM_INT("Release", 1.0f, 2000.0f, 50.0f, 1.0f, "ms")
};

static const PtfxHostApi* s_host;
static int threshold = -12;
static float ratio = 4.0f;
static int attack = 10;      // the knob is a float, the firmware truncates it to whole milliseconds
static int release = 50;

static float inputRms;
static float filteredLinearGain;
static float logarythmicInput;
static float logarythmicGain;
static float linearGain;
static float filterCoeff;

void plugin_init(void* scratch_mem, uint32_t scratch_size, const PtfxHostApi* host) {
    (void)scratch_mem;
    (void)scratch_size;
    s_host = host;
    threshold = -12;
    ratio = 4.0f;
    attack = 10;
    release = 50;
    inputRms = 0.0f;
    filteredLinearGain = 1.0f;
}

void plugin_set_param(uint8_t index, float value) {
    switch (index) {
        case 0: threshold = (int)value; break;
        case 1: ratio = value; break;
        case 2: attack = (int)value; break;
        case 3: release = (int)value; break;
        default: break;
    }
}

int32_t plugin_process_block(const int16_t** in, int16_t** out, uint32_t samples, uint8_t channels) {
    (void)channels;
    if (!in || !in[0] || !out || !out[0]) return 0;
    const int16_t* srcAddr = in[0];
    int16_t* dstAddr = out[0];

    const float compressorThreshold = (float)threshold;
    const float compressorSlope = 1.0f - (1.0f / ratio);
    const float attackCoeff = 1 - s_host->expf(-2.2f * (1000.0f / (44100.0f * attack)));
    const float releaseCoeff = 1 - s_host->expf(-2.2f * (1000.0f / (44100.0f * release)));
    const float timeAverageCoeff = 0.002f;

    for (uint32_t idx = 0; idx < samples; idx++) {
        float normalizedInput = srcAddr[idx] < 0 ? (float)srcAddr[idx] / (float)(-32768)
                                                 : (float)srcAddr[idx] / (float)32767;
        inputRms = FMAF(1 - timeAverageCoeff, inputRms, timeAverageCoeff * s_host->powf(normalizedInput, 2));
        logarythmicInput = 10 * s_host->log10f(inputRms);
        {
            const float g = compressorSlope * (compressorThreshold - logarythmicInput);
            // std::min<float>(g, 0.0f) is (0 < g) ? 0 : g, so a NaN stays a NaN. Ratio 1 gives 0 * inf = NaN for a silent start,
            // the sample then converts to 0 on the ARM. The built in effect does this too and the port keeps it.
            logarythmicGain = (0.0f < g) ? 0.0f : g;
        }
        linearGain = s_host->powf(10.0f, logarythmicGain / 20.0f);
        filterCoeff = linearGain < filteredLinearGain ? attackCoeff : releaseCoeff;
        filteredLinearGain = FMAF(filteredLinearGain, 1 - filterCoeff, filterCoeff * linearGain);
        dstAddr[idx] = filteredLinearGain * srcAddr[idx];
    }
    return (int32_t)samples;
}
