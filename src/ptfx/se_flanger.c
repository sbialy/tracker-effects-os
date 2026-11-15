// Port of core/effector/mtEffectFlanger.cpp of the Polyend Tracker Community Edition firmware (tracker-ff d471709),
// Copyright (c) 2020-2026 Polyend Sp. z o.o. and Polyend Tracker Community Edition Contributors, MIT licence
// (licenses/LICENSE-tracker-ff-MIT.txt).
//
// Modified: the built in offline effect of the Sample Editor as a .ptfx card file. The delay line lives in the plugin scratch, the output is cut to whole blocks of 128 samples like the built in effect.
// The sine of the firmware is arm_sin_q15 of CMSIS-DSP (Apache 2.0, Arm Limited), rewritten here step by step with its 260 entry table, bit exact to the library of the firmware.
// PTFX_NAME: Flanger
// PTFX_AUTHOR: Polyend
// PTFX_FLAGS: EFFECT
// PTFX_SCRATCH: 4096

#include "tracker_plugin.h"

const PluginParam PLUGIN_PARAMS[4] = {
    PTFX_PARAM_INT("Length", 128.0f, 4096.0f, 128.0f, 128.0f, "smp"),
    PTFX_PARAM_INT("Offset", 1.0f, 100.0f, 50.0f, 1.0f, "%"),
    PTFX_PARAM_INT("Depth", 1.0f, 8.0f, 1.0f, 1.0f, ""),
    PTFX_PARAM_FLOAT("Rate", 0.1f, 5.0f, 1.0f, 0.1f, "Hz")
};

// sinTableQ15 of arm_common_tables, one entry before the first sample (index 0 is sin(-1 step))
static const int16_t sinTableQ15[260] = {
    -804, 0, 804, 1608, 2411, 3212, 4011, 4808, 5602, 6393,
    7180, 7962, 8740, 9512, 10279, 11039, 11793, 12540, 13279, 14010,
    14733, 15447, 16151, 16846, 17531, 18205, 18868, 19520, 20160, 20788,
    21403, 22006, 22595, 23170, 23732, 24279, 24812, 25330, 25833, 26320,
    26791, 27246, 27684, 28106, 28511, 28899, 29269, 29622, 29957, 30274,
    30572, 30853, 31114, 31357, 31581, 31786, 31972, 32138, 32286, 32413,
    32522, 32610, 32679, 32729, 32758, 32767, 32758, 32729, 32679, 32610,
    32522, 32413, 32286, 32138, 31972, 31786, 31581, 31357, 31114, 30853,
    30572, 30274, 29957, 29622, 29269, 28899, 28511, 28106, 27684, 27246,
    26791, 26320, 25833, 25330, 24812, 24279, 23732, 23170, 22595, 22006,
    21403, 20788, 20160, 19520, 18868, 18205, 17531, 16846, 16151, 15447,
    14733, 14010, 13279, 12540, 11793, 11039, 10279, 9512, 8740, 7962,
    7180, 6393, 5602, 4808, 4011, 3212, 2411, 1608, 804, 0,
    -804, -1608, -2411, -3212, -4011, -4808, -5602, -6393, -7180, -7962,
    -8740, -9512, -10279, -11039, -11793, -12540, -13279, -14010, -14733, -15447,
    -16151, -16846, -17531, -18205, -18868, -19520, -20160, -20788, -21403, -22006,
    -22595, -23170, -23732, -24279, -24812, -25330, -25833, -26320, -26791, -27246,
    -27684, -28106, -28511, -28899, -29269, -29622, -29957, -30274, -30572, -30853,
    -31114, -31357, -31581, -31786, -31972, -32138, -32286, -32413, -32522, -32610,
    -32679, -32729, -32758, -32768, -32758, -32729, -32679, -32610, -32522, -32413,
    -32286, -32138, -31972, -31786, -31581, -31357, -31114, -30853, -30572, -30274,
    -29957, -29622, -29269, -28899, -28511, -28106, -27684, -27246, -26791, -26320,
    -25833, -25330, -24812, -24279, -23732, -23170, -22595, -22006, -21403, -20788,
    -20160, -19520, -18868, -18205, -17531, -16846, -16151, -15447, -14733, -14010,
    -13279, -12540, -11793, -11039, -10279, -9512, -8740, -7962, -7180, -6393,
    -5602, -4808, -4011, -3212, -2411, -1608, -804, 0, 804, 0,
};

static int32_t arm_sin_q15(int16_t x)
{
    int32_t index = (int32_t)x / 128;
    uint16_t fract = (uint16_t)((int32_t)(x - (int16_t)(index << 7)) << 8);
    int32_t f1 = (int16_t)fract;
    int32_t f2 = (int16_t)((uint32_t)(f1 * f1) >> 15);
    int32_t f2s = (int32_t)(f1 * f2) >> 15;
    uint16_t f3u = (uint16_t)f2s;
    int32_t f3 = (int16_t)f3u;
    int32_t fb = index < 0 ? 0 : index;
    const int16_t* t = &sinTableQ15[fb];
    int32_t y0 = t[0], y1 = t[1], y2 = t[2], y3 = t[3];

    int32_t wb = (0x7f00 - (f1 >> 1)) + 255 + ((f3 >> 1) - f2);
    int32_t wa = (f2 >> 1) - ((f1 * 0x2aaa + f3 * 0x1555) >> 15);
    int32_t wc = f1 + ((f2 - f3) >> 1);
    int32_t d = (int16_t)(uint16_t)((uint32_t)f3u - (uint32_t)fract);
    uint16_t wd = (uint16_t)(((uint32_t)(d * 0x1555)) >> 15);
    int32_t acc = (int32_t)((uint32_t)wa * (uint32_t)y0 + (uint32_t)wb * (uint32_t)y1);
    acc = (int32_t)((uint32_t)acc + (uint32_t)wc * (uint32_t)y2);
    acc = (int32_t)((uint32_t)acc + (uint32_t)y3 * (uint32_t)(int16_t)wd);
    acc >>= 15;
    if (acc > 32767) acc = 32767;
    if (acc < -32768) acc = -32768;
    return acc;
}

static int16_t* l_delayline;
static int16_t l_circ_idx;
static uint32_t l_delay_rate_index;
static int delay_length = 64;
static int offset_percent = 50;
static int delay_depth = 1;
static int delay_rate_incr;
static int delay_offset_idx;

static void update_offset(void)
{
    delay_offset_idx = (offset_percent * delay_length) / 100.0;
    if (delay_offset_idx < -1) {
        delay_offset_idx = 0;
    }
    if (delay_offset_idx >= delay_length) {
        delay_offset_idx = delay_length - 1;
    }
}

static void calculate(const int16_t* src, int16_t* dst)   // process 128 samples
{
    const int PROCESSED_BLOCK_LENGTH = 128;
    int idx;
    int16_t frac;
    int idx1;

    if (delay_offset_idx == 0) {
        for (int i = 0; i < PROCESSED_BLOCK_LENGTH; i++) {
            l_circ_idx++;
            if (l_circ_idx >= delay_length) l_circ_idx = 0;
            l_delayline[l_circ_idx] = *src;
            *dst++ = *src++;
        }
        return;
    }
    for (int i = 0; i < PROCESSED_BLOCK_LENGTH; i++) {
        l_circ_idx++;
        if (l_circ_idx >= delay_length) l_circ_idx = 0;

        l_delayline[l_circ_idx] = *src++;
        frac = (int16_t)arm_sin_q15((int16_t)((l_delay_rate_index >> 16) & 0x7fff));
        idx = (frac * delay_depth) >> 15;
        idx = l_circ_idx - (delay_offset_idx + idx);
        if (idx < 0) idx += delay_length;
        if (idx >= delay_length) idx -= delay_length;

        if (frac < 0) idx1 = idx - 1;
        else idx1 = idx + 1;

        if (idx1 < 0) idx1 += delay_length;
        if (idx1 >= delay_length) idx1 -= delay_length;

        frac = (l_delay_rate_index >> 1) & 0x7fff;
        frac = (((int)(l_delayline[idx1] - l_delayline[idx]) * frac) >> 15);
        *dst++ = (l_delayline[l_circ_idx] + l_delayline[idx] + frac) / 2;

        l_delay_rate_index += delay_rate_incr;
        if (l_delay_rate_index & 0x80000000) l_delay_rate_index &= 0x7fffffff;
    }
}

void plugin_init(void* scratch_mem, uint32_t scratch_size, const PtfxHostApi* host) {
    (void)scratch_size;
    (void)host;
    l_delayline = (int16_t*)scratch_mem;
    l_circ_idx = 0;
    l_delay_rate_index = 0;
    delay_length = 64;
    offset_percent = 50;
    delay_depth = 1;
    delay_rate_incr = 0;
    for (int i = 0; i < 2048; i++) l_delayline[i] = 0;
    update_offset();
}

void plugin_set_param(uint8_t index, float value) {
    switch (index) {
        case 0: delay_length = (int)value / 2; break;
        case 1: offset_percent = (int)value; break;
        case 2: delay_depth = (int)value; break;
        case 3: delay_rate_incr = (value * 2147483648.0) / 44117.64706; break;
        default: return;
    }
    update_offset();
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
