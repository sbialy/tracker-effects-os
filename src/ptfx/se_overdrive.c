// Port of core/effector/mtEffectOverdrive.cpp of the Polyend Tracker Community Edition firmware (tracker-ff d471709),
// Copyright (c) 2020-2026 Polyend Sp. z o.o. and Polyend Tracker Community Edition Contributors, MIT licence
// (licenses/LICENSE-tracker-ff-MIT.txt).
//
// Modified: the built in offline effect of the Sample Editor as a .ptfx card file. Same arithmetic per sample (the FW_TRIM float expf variant), the Type knob is a list.
// PTFX_NAME: Overdrive
// PTFX_AUTHOR: Polyend
// PTFX_FLAGS: EFFECT
// PTFX_SCRATCH: 0

#include "tracker_plugin.h"

// The release firmware is built with -Os and the default fp contraction, which fuses the marked multiply adds into one vfma (one rounding).
// The plugin is built with -ffp-contract=off and states exactly those fused operations with fmaf, so the result does not depend on how a compiler
// contracts the surrounding code. tools/ptfx_ab checks it against the object of the firmware.
#define FMAF(a, b, c) __builtin_fmaf((a), (b), (c))

const PluginParam PLUGIN_PARAMS[2] = {
    PTFX_PARAM_FLOAT("Gain", 0.01f, 5.0f, 1.0f, 0.01f, ""),
    PTFX_PARAM_LIST("Type", 0, "Quad|Exp|Recip")
};

#define QUADRATIC   0
#define EXPONENTIAL 1
#define RECIPROCAL  2

static const PtfxHostApi* s_host;
static float gain = 1.0f;
static int type = 0;

void plugin_init(void* scratch_mem, uint32_t scratch_size, const PtfxHostApi* host) {
    (void)scratch_mem;
    (void)scratch_size;
    s_host = host;
    gain = 1.0f;
    type = 0;
}

void plugin_set_param(uint8_t index, float value) {
    switch (index) {
        case 0: gain = value; break;
        case 1: type = (int)value; break;
        default: break;
    }
}

int32_t plugin_process_block(const int16_t** in, int16_t** out, uint32_t samples, uint8_t channels) {
    (void)channels;
    if (!in || !in[0] || !out || !out[0]) return 0;
    const int16_t* src = in[0];
    int16_t* dst = out[0];

    float gainNorm = gain;
    switch (type) {
        case EXPONENTIAL: gainNorm *= 4; break;
        case RECIPROCAL: gainNorm *= 10; break;
    }

    for (uint32_t i = 0; i < samples; i++) {
        int16_t outVal = 0;
        switch (type) {
            case QUADRATIC: {
                const int32_t x = *src * gainNorm;
                const int32_t inAbs = x >= 0 ? x : -x;
                const int32_t inSign = x >= 0 ? INT16_MAX : INT16_MIN;
                if (inAbs > (int32_t)2 * INT16_MAX / 3) {
                    outVal = inSign;                                     // y = 1
                } else if (inAbs > INT16_MAX / 3) {
                    int32_t temp = 2 * (int32_t)INT16_MAX - 3 * (int32_t)inAbs;   // y = (3 - (2 - 3x)^2) / 3
                    outVal = (3 * (int32_t)INT16_MAX - (temp * temp / 0x8000)) / 3;
                    if (inSign < 0) outVal = -outVal;
                } else {
                    outVal = 2 * x;                                      // y = 2x
                }
                break;
            }
            case EXPONENTIAL: {
                const float x = (float)*src / INT16_MAX;
                const float inSign = x >= 0 ? 1 : -1;
                outVal = (inSign * ((float)2.7182818284590452354 - s_host->expf(FMAF(inSign, -(gainNorm * x), 1.0f))) /
                          (float)2.7182818284590452354) * INT16_MAX;
                break;
            }
            default: {   // RECIPROCAL
                const float x = (float)*src / INT16_MAX;
                const float inSign = x == 0 ? 0 : (x >= 0 ? 1 : -1);
                // the firmware calls abs() on a float here and gets the int overload: the product is truncated, the
                // division is an integer division (1 - 1 / (n + 1) is 0 for n = 0 and 1 above, so this mode hard clips).
                // Kept as it is, the .ptfx is bit equal to the built in effect it replaces.
                int n = (int)(gainNorm * x);
                if (n < 0) n = -n;
                outVal = inSign * (1 - 1 / (n + 1)) * INT16_MAX;
                break;
            }
        }
        *dst = (int16_t)outVal;
        dst++;
        src++;
    }
    return (int32_t)samples;
}
