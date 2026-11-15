// Port of core/effector/mtEffectEQ.cpp of the Polyend Tracker Community Edition firmware (tracker-ff d471709),
// Copyright (c) 2020-2026 Polyend Sp. z o.o. and Polyend Tracker Community Edition Contributors, MIT licence
// (licenses/LICENSE-tracker-ff-MIT.txt).
//
// Modified: the built in offline effect of the Sample Editor as a .ptfx card file. The release firmware computes the coefficients with cosf and tanf, the same host functions are used here.
// The filter loop keeps the double arithmetic of the original, so the plugin is linked with the soft double routines of libgcc.
//
// Equalizer filter based on eqfil of Soundpipe (https://github.com/eljeff/Soundpipe/blob/master/modules/eqfil.c, MIT),
// which is based on csound eqfil and the Regalia and Mitra design.
// PTFX_NAME: Equalizer
// PTFX_AUTHOR: Polyend
// PTFX_FLAGS: EFFECT
// PTFX_SCRATCH: 0

#include "tracker_plugin.h"

// The release firmware is built with -Os and the default fp contraction, which fuses the marked multiply adds into one vfma (one rounding).
// The plugin is built with -ffp-contract=off and states exactly those fused operations with fmaf, so the result does not depend on how a compiler
// contracts the surrounding code. tools/ptfx_ab checks it against the object of the firmware.
#define FMAF(a, b, c) __builtin_fmaf((a), (b), (c))

#define AUDIO_SAMPLE_RATE_EXACT 44117.64706f
#define M_PI_D 3.14159265358979323846

const PluginParam PLUGIN_PARAMS[9] = {
    PTFX_PARAM_FLOAT("Low", -12.0f, 12.0f, 0.0f, 0.05f, "dB"),
    PTFX_PARAM_INT("LowFreq", 10.0f, 200.0f, 80.0f, 1.0f, "Hz"),
    PTFX_PARAM_FLOAT("LoWidth", 0.5f, 5.0f, 1.0f, 0.05f, ""),
    PTFX_PARAM_FLOAT("Mid", -12.0f, 12.0f, 0.0f, 0.05f, "dB"),
    PTFX_PARAM_INT("MidFreq", 100.0f, 8000.0f, 800.0f, 10.0f, "Hz"),
    PTFX_PARAM_FLOAT("MiWidth", 0.5f, 5.0f, 1.0f, 0.05f, ""),
    PTFX_PARAM_FLOAT("High", -12.0f, 12.0f, 0.0f, 0.05f, "dB"),
    PTFX_PARAM_INT("HighFrq", 8000.0f, 20000.0f, 12000.0f, 100.0f, "Hz"),
    PTFX_PARAM_FLOAT("HiWidth", 0.5f, 5.0f, 1.0f, 0.05f, "")
};

enum { LEVEL_LOW, FREQ_LOW, Q_LOW, LEVEL_MID, FREQ_MID, Q_MID, LEVEL_HIGH, FREQ_HIGH, Q_HIGH };

typedef struct {
    float gain;
    float z1, z2, sr;
    float c, d;
} EqContext;

static const PtfxHostApi* g_host;
static float levelLow, levelMid, levelHigh;
static float freqLow = 10.0f, freqMid = 200.0f, freqHigh = 12000.0f;
static float qLow = 1.0f, qMid = 1.0f, qHigh = 1.0f;
static float bwLow, bwMid, bwHigh;
static EqContext ctxLow, ctxMid, ctxHigh;
static int ctxReady;

static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static float calculateBandwidth(float freq, float q) {
    const float frequencyMax = 20000.0f;
    freq = clampf(freq, 0.0f, frequencyMax);
    return clampf(freq / FMAF(freq / frequencyMax, 3.0f, q), 0.0f, frequencyMax);
}

static void initContext(EqContext* ctx, float freq, float bandwidth, float level) {
    ctx->z1 = 0;
    ctx->z2 = 0;
    // remap level +- 12db => gain 1-4 peak, 0-1 notch
    ctx->gain = level >= 0 ? (1 + level * 3 / 12) : (1 + level * 0.8 / 12);
    ctx->d = g_host->cosf(2 * (float)M_PI_D * freq / AUDIO_SAMPLE_RATE_EXACT);
    ctx->c = g_host->tanf((float)M_PI_D * bandwidth / AUDIO_SAMPLE_RATE_EXACT);
}

static float computeSample(EqContext* p, float in) {
    float z1 = p->z1, z2 = p->z2, c, d, w, a, y;
    float g;
    float out;

    c = p->c;
    d = p->d;
    a = (1.0 - c) / (1.0 + c);
    g = p->gain;

    w = in + d * (1.0 + a) * z1 - a * z2;
    y = w * a - d * (1.0 + a) * z1 + z2;
    z2 = z1;
    z1 = w;
    out = (0.5 * FMAF(in - y, g, y + in));

    p->z1 = z1;
    p->z2 = z2;

    return out;
}

void plugin_init(void* scratch_mem, uint32_t scratch_size, const PtfxHostApi* host) {
    (void)scratch_mem;
    (void)scratch_size;
    g_host = host;
    ctxReady = 0;
}

void plugin_set_param(uint8_t index, float value) {
    switch (index) {
        case LEVEL_LOW: levelLow = value; break;
        case LEVEL_MID: levelMid = value; break;
        case LEVEL_HIGH: levelHigh = value; break;
        case FREQ_LOW: freqLow = value; bwLow = calculateBandwidth(freqLow, qLow); break;
        case FREQ_MID: freqMid = value; bwMid = calculateBandwidth(freqMid, qMid); break;
        case FREQ_HIGH: freqHigh = value; bwHigh = calculateBandwidth(freqHigh, qHigh); break;
        case Q_LOW: qLow = value; bwLow = calculateBandwidth(freqLow, qLow); break;
        case Q_MID: qMid = value; bwMid = calculateBandwidth(freqMid, qMid); break;
        case Q_HIGH: qHigh = value; bwHigh = calculateBandwidth(freqHigh, qHigh); break;
        default: break;
    }
    ctxReady = 0;
}

int32_t plugin_process_block(const int16_t** in, int16_t** out, uint32_t samples, uint8_t channels) {
    (void)channels;
    if (!in || !in[0] || !out || !out[0]) return 0;
    if (!ctxReady) {
        // the built in effect builds its filters when the process starts, the parameters are final by the first block
        initContext(&ctxLow, freqLow, bwLow, levelLow);
        initContext(&ctxMid, freqMid, bwMid, levelMid);
        initContext(&ctxHigh, freqHigh, bwHigh, levelHigh);
        ctxReady = 1;
    }
    const int16_t* src = in[0];
    int16_t* dst = out[0];
    for (uint32_t i = 0; i < samples; i++) {
        float o = (float)*src / INT16_MAX;

        if (levelLow != 1) o = computeSample(&ctxLow, o);
        if (levelMid != 1) o = computeSample(&ctxMid, o);
        if (levelHigh != 1) o = computeSample(&ctxHigh, o);

        o = o > 1.0 ? 1.0 : (o < -1.0 ? -1.0 : o);

        *dst = (int16_t)(o * INT16_MAX);
        dst++;
        src++;
    }
    return (int32_t)samples;
}
