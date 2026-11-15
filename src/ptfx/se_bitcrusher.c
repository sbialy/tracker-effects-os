// Port of core/effector/mtEffectBitcrusher.cpp of the Polyend Tracker Community Edition firmware (tracker-ff d471709),
// Copyright (c) 2020-2026 Polyend Sp. z o.o. and Polyend Tracker Community Edition Contributors, MIT licence
// (licenses/LICENSE-tracker-ff-MIT.txt).
//
// Modified: the built in offline effect of the Sample Editor as a .ptfx card file. The sample rate step uses the soft double of libgcc, as the firmware does.
// PTFX_NAME: Bitcrusher
// PTFX_AUTHOR: Polyend
// PTFX_FLAGS: EFFECT
// PTFX_SCRATCH: 0

#include "tracker_plugin.h"

const PluginParam PLUGIN_PARAMS[2] = {
    PTFX_PARAM_INT("Bits", 1.0f, 16.0f, 4.0f, 1.0f, ""),
    PTFX_PARAM_INT("Rate", 500.0f, 44100.0f, 44100.0f, 100.0f, "Hz")
};

static uint8_t bits = 4;
static uint16_t rate = 44100;

void plugin_init(void* scratch_mem, uint32_t scratch_size, const PtfxHostApi* host) {
    (void)scratch_mem;
    (void)scratch_size;
    (void)host;
    bits = 4;
    rate = 44100;
}

void plugin_set_param(uint8_t index, float value) {
    switch (index) {
        case 0: bits = (uint8_t)value; break;
        case 1: rate = (uint16_t)value; break;
        default: break;
    }
}

int32_t plugin_process_block(const int16_t** in, int16_t** out, uint32_t samples, uint8_t channels) {
    (void)channels;
    if (!in || !in[0] || !out || !out[0]) return 0;

    int n = (44117.64706 / rate) + 0.5;
    if (n < 1)
        n = 1;
    else if (n > 64)
        n = 64;
    const int sampleStep = n;
    uint8_t b = bits;
    if (b > 16)
        b = 16;
    else if (b == 0)
        b = 1;
    const uint8_t crushBits = b;

    const int16_t* src = in[0];
    int16_t* dst = out[0];
    if (crushBits == 16 && sampleStep <= 1) {
        for (uint32_t i = 0; i < samples; i++) dst[i] = src[i];
    } else if (sampleStep <= 1) {
        const uint8_t shift = 16 - crushBits;
        for (uint32_t i = 0; i < samples; i++) {
            int32_t sampleSquidge = (*src++) >> shift;
            *dst++ = (int16_t)(sampleSquidge << shift);
        }
    } else {
        const uint8_t shift = (crushBits < 16) ? (16 - crushBits) : 0;
        uint32_t i = 0;
        while (i < samples) {
            int32_t sampleSqueeze = *src;
            if (shift > 0) {
                sampleSqueeze = (sampleSqueeze >> shift) << shift;
            }
            uint32_t step = (samples - i < (uint32_t)sampleStep) ? (samples - i) : (uint32_t)sampleStep;
            for (uint32_t j = 0; j < step; j++) {
                *dst++ = (int16_t)sampleSqueeze;
                src++;
            }
            i += step;
        }
    }
    return (int32_t)samples;
}
