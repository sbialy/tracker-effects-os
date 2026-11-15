// A/B harness: the built in offline effects of the Sample Editor (the real objects of the firmware build, same compiler flags)
// against their .ptfx ports, in one ARM image run under Unicorn (run_ab.py). One case per run (fresh memory), so no state of the
// built in effects (Limiter keeps some between runs) leaks from one case to the next.
//
// The plugin side repeats the loader of core/effector/ptfx_loader.cpp (flat copy, bss cleared after the payload, thumb entry) and the
// driving of core/effector/mtEffectModular.cpp (init, set_param for all 12, expected length = selection + prepend + append, blocks of
// at most 8192, in = nullptr in the append phase). The host table points to the same libm functions the firmware uses.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "tracker_plugin.h"

#define private public
#define protected public
#include "mtEffectAmplifier.h"
#include "mtEffectBitcrusher.h"
#include "mtEffectChorus.h"
#include "mtEffectCompresor.h"
#include "mtEffectDelay.h"
#include "mtEffectEQ.h"
#include "mtEffectFlanger.h"
#include "mtEffectLimiter.h"
#include "mtEffectOverdrive.h"
#undef private
#undef protected

int16_t* externalRamEffectsBuff = nullptr;

// platforms/mk66/source/platform_math.cpp, which needs the Teensy arm_math.h only for this declaration
extern "C" int16_t arm_sin_q15(int16_t);
int16_t sin_q15(int16_t value) { return arm_sin_q15(value); }

// ---- stubs for what the effect objects pull in
mtDataCopyier::mtDataCopyier(uint32_t blockSize) : blockSizeInSamples(blockSize) {}
extern "C" void __cxa_pure_virtual() { for (;;) {} }
static uint8_t g_heap[4096];
static uint32_t g_heapUsed;
void* operator new(unsigned int n) { void* p = g_heap + g_heapUsed; g_heapUsed += (n + 7) & ~7u; return p; }

// ---- buffers
static const uint32_t MAXN = 2100000;
static int16_t g_in[MAXN];
static int16_t g_outA[MAXN];
static int16_t g_outB[MAXN];
static uint8_t g_scratch[640 * 1024];
extern "C" {
volatile uint32_t g_ptfxBase[16];       // set by the driver: addresses of the .ptfx files (nine slots)
volatile uint32_t g_ptfxLen[16];
}
static uint8_t g_pluginRam[9][40 * 1024] __attribute__((aligned(16)));

struct Result {
    uint32_t magic, effect, caseIdx, status;   // status 0 ok, 1 built in refused, 2 plugin load error, 3 param table mismatch
    uint32_t lenA, lenB, ndiff, maxabs, firstdiff;
    uint32_t extraAppend, nparams;
    float params[12];
    uint32_t inKind, inLen;
    uint32_t sumDiffIdx;
    uint32_t paramMismatch;
};
extern "C" Result g_result;
Result g_result;

// ---- effect table
struct EffectDef {
    const char* name;
    mtEffect* eff;
    int nparams;
    float lo[9], hi[9], step[9], def[9];
};

static const EffectDef kEffects[9] = {
    { "amplifier", &sampleEditorAmplifier, 1, { 0.01f }, { 5.0f }, { 0.01f }, { 1.0f } },
    { "overdrive", &sampleEditorOverdrive, 2, { 0.01f, 0 }, { 5.0f, 2 }, { 0.01f, 1 }, { 1.0f, 0 } },
    { "delay", &sampleEditorDelay, 2, { 0.01f, 1 }, { 0.95f, 5000 }, { 0.01f, 5 }, { 0.95f, 50 } },
    { "bitcrusher", &sampleEditorBitcrusher, 2, { 1, 500 }, { 16, 44100 }, { 1, 100 }, { 4, 44100 } },
    { "chorus", &sampleEditorChorus, 2, { 128, 1 }, { 4096, 8 }, { 128, 1 }, { 128, 1 } },
    { "flanger", &sampleEditorFlanger, 4, { 128, 1, 1, 0.1f }, { 4096, 100, 8, 5 }, { 128, 1, 1, 0.1f }, { 128, 50, 1, 1 } },
    { "equalizer", &sampleEditorEQ, 9, { -12, 10, 0.5f, -12, 100, 0.5f, -12, 8000, 0.5f }, { 12, 200, 5, 12, 8000, 5, 12, 20000, 5 },
      { 0.05f, 1, 0.05f, 0.05f, 10, 0.05f, 0.05f, 100, 0.05f }, { 0, 80, 1, 0, 800, 1, 0, 12000, 1 } },
    { "limiter", &sampleEditorLimiter, 3, { -60, 0.3f, 1 }, { 0, 10, 2000 }, { 1, 0.1f, 1 }, { -6, 3, 300 } },
    { "compressor", &sampleEditorCompresor, 4, { -60, 1, 0.3f, 1 }, { 0, 50, 100, 2000 }, { 1, 0.5f, 0.1f, 1 }, { -12, 4, 10, 50 } },
};

// ---- the plugin side
struct Plugin {
    PtfxInitFunc init;
    PtfxSetParamFunc setp;
    PtfxProcessBlockFunc proc;
    uint8_t nparams;
    float lo[12], hi[12], def[12], step[12];
    uint32_t scratch;
};
static uint32_t s_extraPrepend, s_extraAppend;
static void hostSetExtra(uint32_t pre, uint32_t app) { s_extraPrepend = pre; s_extraAppend = app; }
static const PtfxHostApi s_host = {
    44100, ::sinf, ::cosf, ::tanf, ::tanhf, ::powf, ::expf, ::sqrtf, ::logf, ::log10f, ::fminf, ::fmaxf, ::fabsf, ::memcpy, ::memset, ::rand,
    hostSetExtra,
};

static bool loadPlugin(int slot, Plugin& p)
{
    const uint8_t* f = (const uint8_t*)g_ptfxBase[slot];
    uint32_t len = g_ptfxLen[slot];
    if (!f || len < 472) return false;
    PtfxHeader h;
    memcpy(&h, f, sizeof h);
    if (h.magic != 0x58465450 || h.format_version != 1) return false;
    if (h.code_offset != 472 || h.code_offset + h.code_size != len) return false;
    if (h.code_size + h.bss_size > sizeof g_pluginRam[0] || h.scratch_size > sizeof g_scratch) return false;
    uint8_t* base = g_pluginRam[slot];
    memcpy(base, f + h.code_offset, h.code_size);
    if (h.bss_size) memset(base + h.code_size, 0, h.bss_size);
    p.init = (PtfxInitFunc)((uintptr_t)base + h.init_offset | 1);
    p.setp = (PtfxSetParamFunc)((uintptr_t)base + h.set_param_offset | 1);
    p.proc = (PtfxProcessBlockFunc)((uintptr_t)base + h.process_block_offset | 1);
    p.nparams = h.num_params;
    for (int i = 0; i < 12; i++) {
        p.lo[i] = h.params[i].min_val;
        p.hi[i] = h.params[i].max_val;
        p.def[i] = h.params[i].default_val;
        p.step[i] = h.params[i].step;
    }
    p.scratch = h.scratch_size;
    return true;
}

// ---- inputs
static uint32_t s_rng = 0x1234567u;
static uint32_t rnd() { s_rng ^= s_rng << 13; s_rng ^= s_rng >> 17; s_rng ^= s_rng << 5; return s_rng; }

static void makeInput(uint32_t kind, uint32_t n)
{
    s_rng = 0x1234567u + kind * 977u + n;
    for (uint32_t i = 0; i < n; i++) {
        int32_t v = 0;
        switch (kind) {
            case 0: v = (int32_t)(rnd() & 0xffff) - 32768; break;                                // full scale noise
            case 1: v = (int32_t)(sinf((float)i * 0.0627f) * 24000.0f); break;                   // sine
            case 2: v = ((i / 37) & 1) ? 32767 : -32768; break;                                  // hard square, clips
            case 3: v = (i % 211 == 0) ? 30000 : 0; break;                                       // impulses
            case 4: v = (int32_t)((i * 977u) & 0xffff) - 32768; break;                          // saw
            default: v = (int32_t)(rnd() % 2001) - 1000; break;                                  // quiet noise
        }
        if (v > 32767) v = 32767;
        if (v < -32768) v = -32768;
        g_in[i] = (int16_t)v;
    }
}

static float quant(float lo, float hi, float step, uint32_t r)
{
    int steps = (int)((hi - lo) / step + 0.5f);
    if (steps < 1) return lo;
    float v = lo + (float)(r % (steps + 1)) * step;
    return v > hi ? hi : v;
}


struct InputDef { uint32_t kind, len; };
static const InputDef kInputs[6] = { { 0, 8192 }, { 1, 8193 }, { 2, 3000 }, { 3, 127 }, { 0, 1 }, { 5, 129 } };
static const InputDef kInputsHeavy[6] = { { 0, 1500 }, { 1, 2000 }, { 2, 700 }, { 3, 127 }, { 0, 1 }, { 5, 129 } };   // equalizer: soft double per sample
static const InputDef kInputsDelay[6] = { { 0, 8192 }, { 1, 8193 }, { 2, 3000 }, { 3, 127 }, { 0, 1 }, { 5, 129 } };

extern "C" int run_case(uint32_t effectIdx, uint32_t caseIdx)
{
    Result& R = g_result;
    memset(&R, 0, sizeof R);
    R.magic = 0xAB0AB0u;
    R.effect = effectIdx;
    R.caseIdx = caseIdx;
    const EffectDef& E = kEffects[effectIdx];
    externalRamEffectsBuff = g_outA;
    g_heapUsed = 0;
    // the objects were constructed by the start code (init_array), no buffers are touched by the constructors we use

    Plugin P;
    if (!loadPlugin((int)effectIdx, P)) { R.status = 2; return 2; }

    // parameter table check: the ptfx header must equal the constraints of the built in effect
    for (int i = 0; i < E.nparams; i++) {
        if (P.lo[i] != E.lo[i] || P.hi[i] != E.hi[i] || P.def[i] != E.def[i] || P.step[i] != E.step[i]) R.paramMismatch |= 1u << i;
    }
    if (P.nparams != E.nparams) R.paramMismatch |= 1u << 20;

    uint32_t ps = caseIdx / 6, ii = caseIdx % 6;
    const InputDef* tab = effectIdx == 6 ? kInputsHeavy : kInputs;
    if (effectIdx == 2) tab = kInputsDelay;
    InputDef in = tab[ii];
    R.inKind = in.kind;
    R.inLen = in.len;

    // parameter set: 0 default, 1 min, 2 max, 3.. random on the step grid
    float v[12] = {};
    s_rng = 0xC0FFEE + effectIdx * 31 + ps * 7919;
    for (int i = 0; i < E.nparams; i++) {
        if (ps == 0) v[i] = E.def[i];
        else if (ps == 1) v[i] = E.lo[i];
        else if (ps == 2) v[i] = E.hi[i];
        else v[i] = quant(E.lo[i], E.hi[i], E.step[i], rnd());
    }
    // Delay: keep the tail short, the soft limit of the emulator run time (time 1 to 40 ms), except the max set which uses 5 ms
    if (effectIdx == 2) {
        if (ps == 2) v[1] = 5;
        else if (ps >= 3) v[1] = quant(1, 40, 1, rnd());
        else if (ps == 0) v[1] = 20;
    }
    // Delay cap cases (case 240 and up): feedback at its maximum and a long time, the tail (last voice - 1) * time goes far past the edit buffer
    // half (1,966,080 samples) and both sides must stop at the same length with the same samples (voice count 99 at most)
    if (effectIdx == 2 && caseIdx >= 240) {
        static const float kCapTime[3] = { 1000, 5000, 220 };
        v[0] = E.hi[0];
        v[1] = kCapTime[caseIdx - 240];
    }
    R.nparams = E.nparams;
    for (int i = 0; i < 12; i++) R.params[i] = v[i];

    makeInput(in.kind, in.len);

    // ---- built in
    mtEffect* e = E.eff;
    e->confirmed[0].area.addr = g_in;
    e->confirmed[0].area.length = in.len;
    e->confirmed[0].selection.addr = g_in;
    e->confirmed[0].selection.length = in.len;
    e->processed[0].area.addr = g_outA;
    e->processed[0].selection.addr = g_outA;
    e->processed[0].area.length = 0;
    e->processed[0].selection.length = 0;
    for (int i = 0; i < E.nparams; i++) e->setParameter(v[i], i);
    if (!e->startProcess()) { R.status = 1; return 1; }
    uint32_t guard = 0, totalA = 0;
    while (e->getProcessState() && guard++ < 400000) {
        int32_t c = e->processBlock();
        if (c > 0) totalA += (uint32_t)c;
    }
    R.lenA = totalA;

    // ---- plugin, driven like mtEffectModular
    memset(g_scratch, 0, sizeof g_scratch);
    s_extraPrepend = s_extraAppend = 0;
    P.init(g_scratch, P.scratch, &s_host);
    for (int i = 0; i < 12; i++) P.setp((uint8_t)i, i < E.nparams ? v[i] : 0.0f);
    R.extraAppend = s_extraAppend;
    uint32_t length = in.len + s_extraPrepend + s_extraAppend;
    uint32_t done = 0;
    int16_t* dst = g_outB;
    bool state = length != 0;
    while (state) {
        uint32_t remaining = length - done;
        uint32_t block = remaining > 8192 ? 8192 : remaining;
        const int16_t* inp[1] = { nullptr };
        uint32_t bodyStart = s_extraPrepend, bodyEnd = s_extraPrepend + in.len;
        if (done < bodyStart) { if (block > bodyStart - done) block = bodyStart - done; }
        else if (done < bodyEnd) {
            if (block > bodyEnd - done) block = bodyEnd - done;
            inp[0] = g_in + (done - bodyStart);
        }
        int16_t* outp[1] = { dst };
        int32_t c = P.proc(inp, outp, block, 1);
        if (c <= 0) state = false;
        else { dst += c; done += (uint32_t)c; if (done >= length) state = false; }
    }
    R.lenB = done;

    uint32_t n = std::min(R.lenA, R.lenB);
    R.firstdiff = 0xffffffffu;
    for (uint32_t i = 0; i < n; i++) {
        int32_t d = (int32_t)g_outA[i] - (int32_t)g_outB[i];
        if (d) {
            R.ndiff++;
            uint32_t a = d < 0 ? -d : d;
            if (a > R.maxabs) R.maxabs = a;
            if (R.firstdiff == 0xffffffffu) R.firstdiff = i;
        }
    }
    return 0;
}

extern "C" void (*__init_array_start[])(void);
extern "C" void (*__init_array_end[])(void);
// the sine of the Flanger port against arm_sin_q15 of the CMSIS library of the firmware, every 16 bit argument
extern "C" int32_t port_sin_q15(int16_t x);
static int sin_all()
{
    Result& R = g_result;
    memset(&R, 0, sizeof R);
    R.magic = 0xAB0AB0u;
    R.effect = 99;
    R.firstdiff = 0xffffffffu;
    for (int32_t x = -32768; x <= 32767; x++) {
        int16_t a = sin_q15((int16_t)x);
        int32_t b = port_sin_q15((int16_t)x);
        if (a != b) {
            R.ndiff++;
            uint32_t d = a > b ? a - b : b - a;
            if (d > R.maxabs) R.maxabs = d;
            if (R.firstdiff == 0xffffffffu) R.firstdiff = (uint32_t)(x + 32768);
        }
    }
    R.lenA = R.lenB = 65536;
    return 0;
}

extern "C" int entry(uint32_t effectIdx, uint32_t caseIdx)
{
    if (effectIdx == 99) return sin_all();
    for (void (**f)(void) = __init_array_start; f < __init_array_end; f++) (*f)();
    return run_case(effectIdx, caseIdx);
}
