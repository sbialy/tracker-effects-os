// Port of core/effector/mtEffectDelay.cpp of the Polyend Tracker Community Edition firmware (tracker-ff d471709),
// Copyright (c) 2020-2026 Polyend Sp. z o.o. and Polyend Tracker Community Edition Contributors, MIT licence
// (licenses/LICENSE-tracker-ff-MIT.txt).
//
// Modified: the built in offline effect of the Sample Editor as a .ptfx card file. The built in effect reads its source through voice pointers and ends when the last voice has consumed the whole sample.
// Here the plugin reads the selection through the pointer of the first block (the blocks of the body phase are contiguous), and asks the host for the tail with set_extra_samples. The tail is (last voice - 1) * time + 1 samples,
// which is what the built in effect really renders (its expected length has no + 1 and is only an estimate).
// PTFX_NAME: Delay
// PTFX_AUTHOR: Polyend
// PTFX_FLAGS: EFFECT
// PTFX_SCRATCH: 0

#include "tracker_plugin.h"

// The release firmware is built with -Os and the default fp contraction, which fuses the marked multiply adds into one vfma (one rounding).
// The plugin is built with -ffp-contract=off and states exactly those fused operations with fmaf, so the result does not depend on how a compiler
// contracts the surrounding code. tools/ptfx_ab checks it against the object of the firmware.
#define FMAF(a, b, c) __builtin_fmaf((a), (b), (c))

#define MAX_DELAY_VOICE_NUMBER 99
#define EDIT_HALF ((15u * 256u * 1024u) / 2u)   // EDIT_BUFF_SIZE / 2 of the firmware

const PluginParam PLUGIN_PARAMS[2] = {
    PTFX_PARAM_FLOAT("Feedbck", 0.01f, 0.95f, 0.95f, 0.01f, ""),
    PTFX_PARAM_INT("Time", 1.0f, 5000.0f, 50.0f, 5.0f, "ms")
};

static const PtfxHostApi* g_host;
static float feedback = 0.95f;
static int timeMs = 50;
static uint8_t lastVoiceIdx;

static const int16_t* srcBase;
static uint32_t inLength;          // samples of the selection seen so far
static int inDone;                 // the append phase started, inLength is final
static uint32_t sourcePos[MAX_DELAY_VOICE_NUMBER];
static float feedbackVoiceMult[MAX_DELAY_VOICE_NUMBER];
static uint32_t lastIndexInVoice[MAX_DELAY_VOICE_NUMBER];
static uint8_t firstActiveVoiceIdx;
static uint8_t lastActiveVoiceIdx;
static uint32_t processedSamples;
static int ended;

static uint32_t timeInSamples(void) {
    return (uint32_t)((float)timeMs * 44.1f);
}

static void calcLastVoiceIdx(void) {
    uint8_t maxVoiceNumber = 0;
    float feedbackGain = 1.0;
    while (feedbackGain > 0.01f) {
        feedbackGain *= feedback;
        maxVoiceNumber++;
        if (maxVoiceNumber == MAX_DELAY_VOICE_NUMBER) break;
    }
    lastVoiceIdx = maxVoiceNumber - 1;
}

static void updateTail(void) {
    uint32_t tail = 0;
    if (lastVoiceIdx >= 1) tail = (uint32_t)(lastVoiceIdx - 1) * timeInSamples() + 1u;
    if (g_host && g_host->set_extra_samples) g_host->set_extra_samples(0, tail);
}

static void updateVoices(void) {
    uint32_t t = timeInSamples();
    for (uint8_t i = 0; i < MAX_DELAY_VOICE_NUMBER; i++) {
        feedbackVoiceMult[i] = g_host->powf(feedback, i);
        lastIndexInVoice[i] = i * t;
    }
}

void plugin_init(void* scratch_mem, uint32_t scratch_size, const PtfxHostApi* host) {
    (void)scratch_mem;
    (void)scratch_size;
    g_host = host;
    feedback = 0.95f;
    timeMs = 50;
    srcBase = 0;
    inLength = 0;
    inDone = 0;
    ended = 0;
    firstActiveVoiceIdx = 0;
    lastActiveVoiceIdx = 0;
    processedSamples = 0;
    for (uint8_t i = 0; i < MAX_DELAY_VOICE_NUMBER; i++) sourcePos[i] = 0;
    calcLastVoiceIdx();
    updateVoices();
}

void plugin_set_param(uint8_t index, float value) {
    switch (index) {
        case 0:
            feedback = value;
            calcLastVoiceIdx();
            break;
        case 1:
            timeMs = (uint16_t)value;
            break;
        default: return;
    }
    if (!g_host) return;   // the parameter screen sets values before the first init, the host re-applies all of them after init
    updateVoices();
    updateTail();
}

int32_t plugin_process_block(const int16_t** in, int16_t** out, uint32_t samples, uint8_t channels) {
    (void)channels;
    if (!out || !out[0] || ended) return 0;
    int16_t* dst = out[0];

    if (in && in[0]) {
        if (!srcBase) srcBase = in[0];
        inLength += samples;
    } else if (!inDone) {
        inDone = 1;
        // the first voice that ran out of source during the body phase is retired before the next output
        if (sourcePos[firstActiveVoiceIdx] >= inLength) firstActiveVoiceIdx++;
        if (firstActiveVoiceIdx > MAX_DELAY_VOICE_NUMBER - 1) firstActiveVoiceIdx = MAX_DELAY_VOICE_NUMBER - 1;
    }
    if (!srcBase) return 0;

    int32_t count = 0;
    for (uint32_t n = 0; n < samples; n++) {
        // the built in effect checks the last voice against the end of the source and the half of the edit buffer
        if (inDone && sourcePos[lastVoiceIdx] >= inLength) { ended = 1; break; }
        if (processedSamples > EDIT_HALF) { ended = 1; break; }

        int32_t currentSum = 0;
        for (uint8_t i = firstActiveVoiceIdx; i <= lastActiveVoiceIdx; i++) {
            currentSum = (int32_t)FMAF((float)srcBase[sourcePos[i]], feedbackVoiceMult[i], (float)currentSum);
            sourcePos[i]++;
        }
        if (currentSum > 32767) currentSum = 32767;
        else if (currentSum < -32768) currentSum = -32768;
        *dst++ = (int16_t)currentSum;
        count++;
        processedSamples++;

        if (processedSamples > lastIndexInVoice[lastActiveVoiceIdx]) lastActiveVoiceIdx++;
        if (inDone && sourcePos[firstActiveVoiceIdx] >= inLength) firstActiveVoiceIdx++;
        if (lastActiveVoiceIdx > MAX_DELAY_VOICE_NUMBER - 1) lastActiveVoiceIdx = MAX_DELAY_VOICE_NUMBER - 1;
        if (firstActiveVoiceIdx > MAX_DELAY_VOICE_NUMBER - 1) firstActiveVoiceIdx = MAX_DELAY_VOICE_NUMBER - 1;
    }
    return count;
}
