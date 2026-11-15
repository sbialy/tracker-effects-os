// Port of core/effector/mtEffectLimiter.cpp of the Polyend Tracker Community Edition firmware (tracker-ff d471709),
// Copyright (c) 2020-2026 Polyend Sp. z o.o. and Polyend Tracker Community Edition Contributors, MIT licence
// (licenses/LICENSE-tracker-ff-MIT.txt).
//
// Modified: the built in offline effect of the Sample Editor as a .ptfx card file. The envelope state starts from zero on every run (the built in effect kept g and xpeak from the previous run, a first run is equal).
// PTFX_NAME: Limiter
// PTFX_AUTHOR: Polyend
// PTFX_FLAGS: EFFECT
// PTFX_SCRATCH: 0

#include "tracker_plugin.h"

// The release firmware is built with -Os and the default fp contraction, which fuses the marked multiply adds into one vfma (one rounding).
// The plugin is built with -ffp-contract=off and states exactly those fused operations with fmaf, so the result does not depend on how a compiler
// contracts the surrounding code. tools/ptfx_ab checks it against the object of the firmware.
#define FMAF(a, b, c) __builtin_fmaf((a), (b), (c))

const PluginParam PLUGIN_PARAMS[3] = {
    PTFX_PARAM_INT("Thresh", -60.0f, 0.0f, -6.0f, 1.0f, "dB"),
    PTFX_PARAM_FLOAT("Attack", 0.3f, 10.0f, 3.0f, 0.1f, "ms"),
    PTFX_PARAM_INT("Release", 1.0f, 2000.0f, 300.0f, 1.0f, "ms")
};

static const PtfxHostApi* s_host;
static uint16_t threshold;
static float attackCoeff;
static float releaseCoeff;

static float g = 1.0f;
static uint16_t xpeak;
static uint16_t a;
static float coeff;
static float f;

void plugin_init(void* scratch_mem, uint32_t scratch_size, const PtfxHostApi* host) {
    (void)scratch_mem;
    (void)scratch_size;
    s_host = host;
    threshold = 0;
    attackCoeff = 0.0f;
    releaseCoeff = 0.0f;
    g = 1.0f;
    xpeak = 0;
    a = 0;
}

void plugin_set_param(uint8_t index, float value) {
    switch (index) {
        case 0: {
            int logarythmicThreshold = (int)value;
            float linearThreshold = s_host->powf(10.0f, logarythmicThreshold / 20.0f);
            threshold = linearThreshold * 32767;
            break;
        }
        case 1: {
            float attackMs = value;
            attackCoeff = 1 - s_host->expf(-2.2f * (1000.0f / (44100.0f * attackMs)));
            break;
        }
        case 2: {
            float releaseMs = value;
            releaseCoeff = 1 - s_host->expf(-2.2f * (1000.0f / (44100.0f * releaseMs)));
            break;
        }
        default: break;
    }
}

int32_t plugin_process_block(const int16_t** in, int16_t** out, uint32_t samples, uint8_t channels) {
    (void)channels;
    if (!in || !in[0] || !out || !out[0]) return 0;
    const int16_t* srcAddr = in[0];
    int16_t* dstAddr = out[0];
    const float timeAverageCoeff = 0.002f;

    for (uint32_t n = 0; n < samples; n++) {
        uint16_t inputAbs = (*srcAddr < 0) ? -*srcAddr : *srcAddr;
        a = FMAF(1 - timeAverageCoeff, a, timeAverageCoeff * inputAbs);
        coeff = (a > xpeak) ? attackCoeff : releaseCoeff;
        xpeak = FMAF(1 - coeff, xpeak, coeff * a);
        f = threshold / (float)xpeak;
        f = (f < 1.0f) ? f : 1.0f;
        coeff = (f < g) ? attackCoeff : releaseCoeff;
        g = FMAF(g, 1 - coeff, coeff * f);
        *dstAddr++ = g * *srcAddr++;
    }
    return (int32_t)samples;
}
