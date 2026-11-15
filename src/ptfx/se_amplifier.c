// Port of core/effector/mtEffectAmplifier.cpp of the Polyend Tracker Community Edition firmware (tracker-ff d471709),
// Copyright (c) 2020-2026 Polyend Sp. z o.o. and Polyend Tracker Community Edition Contributors, MIT licence
// (licenses/LICENSE-tracker-ff-MIT.txt).
//
// Modified: the built in offline effect of the Sample Editor as a .ptfx card file. Same arithmetic per sample, no change in output.
// PTFX_NAME: Amplifier
// PTFX_AUTHOR: Polyend
// PTFX_FLAGS: EFFECT
// PTFX_SCRATCH: 0

#include "tracker_plugin.h"

const PluginParam PLUGIN_PARAMS[1] = {
    PTFX_PARAM_FLOAT("Amp", 0.01f, 5.0f, 1.0f, 0.01f, "")
};

static float amp = 1.0f;

void plugin_init(void* scratch_mem, uint32_t scratch_size, const PtfxHostApi* host) {
    (void)scratch_mem;
    (void)scratch_size;
    (void)host;
    amp = 1.0f;
}

void plugin_set_param(uint8_t index, float value) {
    if (index == 0) amp = value;
}

int32_t plugin_process_block(const int16_t** in, int16_t** out, uint32_t samples, uint8_t channels) {
    (void)channels;
    if (!in || !in[0] || !out || !out[0]) return 0;
    const int16_t* src = in[0];
    int16_t* dst = out[0];
    for (uint32_t i = 0; i < samples; i++) {
        int32_t processVal = amp * (*src);
        if (processVal > 32767) processVal = 32767;
        if (processVal < -32768) processVal = -32768;
        *dst = (int16_t)processVal;
        dst++;
        src++;
    }
    return (int32_t)samples;
}
