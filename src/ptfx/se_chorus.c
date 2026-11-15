// Port of core/effector/mtEffectChorus.cpp of the Polyend Tracker Community Edition firmware (tracker-ff d471709),
// Copyright (c) 2020-2026 Polyend Sp. z o.o. and Polyend Tracker Community Edition Contributors, MIT licence
// (licenses/LICENSE-tracker-ff-MIT.txt).
//
// Modified: the built in offline effect of the Sample Editor as a .ptfx card file. The delay line lives in the plugin scratch, the output is cut to whole blocks of 128 samples like the built in effect.
// PTFX_NAME: Chorus
// PTFX_AUTHOR: Polyend
// PTFX_FLAGS: EFFECT
// PTFX_SCRATCH: 4096

#include "tracker_plugin.h"

const PluginParam PLUGIN_PARAMS[2] = {
    PTFX_PARAM_INT("Length", 128.0f, 4096.0f, 128.0f, 128.0f, "smp"),
    PTFX_PARAM_INT("Voices", 1.0f, 8.0f, 1.0f, 1.0f, "")
};

static int16_t* l_delayline;
static int16_t l_circ_idx;
static int num_chorus = 1;
static int delay_length = 64;

static void calculate(const int16_t* src, int16_t* dst) {   // process 128 samples
    const int PROCESSED_BLOCK_LENGTH = 128;
    int sum;
    int c_idx;

    if (num_chorus <= 1) {
        for (int i = 0; i < PROCESSED_BLOCK_LENGTH; i++) {
            l_circ_idx++;
            if (l_circ_idx >= delay_length) {
                l_circ_idx = 0;
            }
            l_delayline[l_circ_idx] = *src++;
        }
        src -= PROCESSED_BLOCK_LENGTH;
        for (int i = 0; i < PROCESSED_BLOCK_LENGTH; i++) {
            *dst++ = *src++;
        }
        return;
    }

    uint32_t tmp = delay_length / (num_chorus - 1) - 1;
    for (int i = 0; i < PROCESSED_BLOCK_LENGTH; i++) {
        l_circ_idx++;
        if (l_circ_idx >= delay_length) {
            l_circ_idx = 0;
        }
        l_delayline[l_circ_idx] = *src++;
        sum = 0;
        c_idx = l_circ_idx;
        for (int k = 0; k < num_chorus; k++) {
            sum += l_delayline[c_idx];
            if (num_chorus > 1) c_idx -= tmp;
            if (c_idx < 0) c_idx += delay_length;
        }
        *dst++ = sum / num_chorus;
    }
}

void plugin_init(void* scratch_mem, uint32_t scratch_size, const PtfxHostApi* host) {
    (void)scratch_size;
    (void)host;
    l_delayline = (int16_t*)scratch_mem;
    l_circ_idx = 0;
    num_chorus = 1;
    delay_length = 64;
    for (int i = 0; i < 2048; i++) l_delayline[i] = 0;
}

void plugin_set_param(uint8_t index, float value) {
    switch (index) {
        case 0: delay_length = (int)value / 2; break;
        case 1: num_chorus = (int)value; break;
        default: break;
    }
}

int32_t plugin_process_block(const int16_t** in, int16_t** out, uint32_t samples, uint8_t channels) {
    (void)channels;
    if (!in || !in[0] || !out || !out[0]) return 0;
    const int16_t* src = in[0];
    int16_t* dst = out[0];

    uint32_t processingNumber = samples / 128;
    for (int i = 0; i < (int)processingNumber; i++) {
        calculate(src, dst);
        src += 128;
        dst += 128;
    }
    return (int32_t)(processingNumber * 128);
}
