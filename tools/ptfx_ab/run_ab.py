#!/usr/bin/env python3
"""A/B of the built in Sample Editor effects against their .ptfx ports under Unicorn (see harness.cpp).

    PY=/tmp/mcvenv/bin/python
    $PY -I run_ab.py --elf OUT/ab.elf --ptfx DIR [--only delay,flanger] [--jobs 8] [--nm arm-none-eabi-nm]

DIR holds se_amplifier.ptfx se_overdrive.ptfx se_delay.ptfx se_bitcrusher.ptfx se_chorus.ptfx se_flanger.ptfx se_equalizer.ptfx
se_limiter.ptfx se_compressor.ptfx. Every case runs in a fresh machine. The report lists, per effect, how many cases are bit identical,
and for the others the first difference (length, number of samples, largest absolute error). Exit status 0 only when every case of every
effect is bit identical, has the same length and the parameter tables of the plugin equal the ones of the built in effect.
"""
import argparse
import os
import struct
import subprocess
import sys
from multiprocessing import Pool

from unicorn import UC_ARCH_ARM, UC_MODE_THUMB, Uc, UcError
from unicorn.arm_const import UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC, UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_SP, UC_CPU_ARM_CORTEX_A15

NAMES = ["amplifier", "overdrive", "delay", "bitcrusher", "chorus", "flanger", "equalizer", "limiter", "compressor"]
FILES = ["se_amplifier", "se_overdrive", "se_delay", "se_bitcrusher", "se_chorus", "se_flanger", "se_equalizer", "se_limiter", "se_compressor"]
CASES = 240
CAP_CASES = 3   # delay only, cases 240 to 242: tails past the edit buffer half (harness.cpp)
RAM, RAM_SIZE = 0x20000000, 0x2800000
PLUG, TRAP = 0x23000000, 0x23F00000
RES_FMT = "<4I5I2I12f4I"
KINDS = ["noise", "sine", "square", "impulse", "saw", "quiet"]


def elf_load(path):
    d = open(path, "rb").read()
    phoff, = struct.unpack_from("<I", d, 0x1C)
    phentsize, phnum = struct.unpack_from("<HH", d, 0x2A)
    segs = []
    for i in range(phnum):
        t, off, va, pa, fsz, msz, fl, al = struct.unpack_from("<8I", d, phoff + i * phentsize)
        if t == 1 and msz:
            segs.append((pa, d[off:off + fsz], msz))
    return segs


def symbols(nm, elf):
    out = subprocess.run([nm, elf], stdout=subprocess.PIPE, check=True, text=True).stdout
    return {p[2]: int(p[0], 16) for p in (l.split() for l in out.splitlines()) if len(p) == 3}


def run_one(job):
    elf, nm, ptfx_dir, eff, case = job
    segs = elf_load(elf)
    sym = symbols(nm, elf)
    uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
    uc.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A15) if hasattr(uc, "ctl_set_cpu_model") else None
    uc.mem_map(RAM, RAM_SIZE)
    uc.mem_map(PLUG, 0x1000000)
    for pa, data, msz in segs:
        uc.mem_write(pa, data)
    # the nine .ptfx files, one per slot
    for slot, f in enumerate(FILES):
        blob = open(os.path.join(ptfx_dir, f + ".ptfx"), "rb").read()
        uc.mem_write(PLUG + slot * 0x10000, blob)
        uc.mem_write(sym["g_ptfxBase"] + 4 * slot, struct.pack("<I", PLUG + slot * 0x10000))
        uc.mem_write(sym["g_ptfxLen"] + 4 * slot, struct.pack("<I", len(blob)))
    uc.reg_write(UC_ARM_REG_C1_C0_2, 0xF << 20)
    uc.reg_write(UC_ARM_REG_FPEXC, 0x40000000)
    uc.reg_write(UC_ARM_REG_SP, RAM + RAM_SIZE - 64)
    uc.reg_write(UC_ARM_REG_LR, TRAP | 1)
    uc.reg_write(UC_ARM_REG_R0, eff)
    uc.reg_write(UC_ARM_REG_R1, case)
    try:
        uc.emu_start(sym["entry"] | 1, TRAP, timeout=900 * 1000000)
    except UcError as e:
        return (eff, case, "FAULT %s pc=%08x" % (e, uc.reg_read(15)), None)
    raw = bytes(uc.mem_read(sym["g_result"], struct.calcsize(RES_FMT)))
    return (eff, case, None, struct.unpack(RES_FMT, raw))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--elf", required=True)
    ap.add_argument("--ptfx", required=True)
    ap.add_argument("--only", default="")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    ap.add_argument("--nm", default="arm-none-eabi-nm")
    ap.add_argument("--verbose", action="store_true")
    a = ap.parse_args()
    only = [n for n in a.only.split(",") if n] or NAMES
    jobs = [(a.elf, a.nm, a.ptfx, NAMES.index(n), c) for n in only for c in range(CASES + (CAP_CASES if n == "delay" else 0))]
    with Pool(a.jobs) as p:
        sin_res = p.apply_async(run_one, ((a.elf, a.nm, a.ptfx, 99, 0),))
        results = p.map(run_one, jobs, chunksize=1)
        sin_res = sin_res.get()
    bad = 0
    if sin_res[2]:
        print("flanger sine: " + sin_res[2])
        bad += 1
    else:
        s = sin_res[3]
        print("flanger sine (arm_sin_q15 of the library vs the port, all 65536 arguments): %d differ (max |d| %d)" % (s[6], s[7]))
        bad += 1 if s[6] else 0
    for n in only:
        e = NAMES.index(n)
        rs = [r for r in results if r[0] == e]
        ident = diff = other = 0
        notes = []
        for _, case, err, r in rs:
            if err:
                other += 1
                notes.append("case %d %s" % (case, err))
                continue
            (magic, eff, ci, status, lenA, lenB, ndiff, maxabs, first, extra, npar, *rest) = r
            params = rest[:12]
            inKind, inLen, _, pmis = rest[12:16]
            if status or pmis:
                other += 1
                notes.append("case %d status %d param table mismatch mask %#x" % (case, status, pmis))
            elif lenA == lenB and ndiff == 0:
                ident += 1
                if a.verbose:
                    notes.append("case %d ok len %d in %s/%d tail %d" % (case, lenA, KINDS[inKind], inLen, extra))
            else:
                diff += 1
                notes.append("case %d DIFF built-in %d vs plugin %d samples, %d differ (max |d| %d, first %d) in %s/%d params %s tail %d" % (
                    case, lenA, lenB, ndiff, maxabs, first if first != 0xffffffff else -1, KINDS[inKind], inLen,
                    [round(x, 4) for x in params[:npar]], extra))
        print("%-11s %2d cases: %2d bit identical, %2d different, %2d other" % (n, len(rs), ident, diff, other))
        for line in notes[:12 if not a.verbose else 1000]:
            print("    " + line)
        bad += diff + other
    print("RESULT", "PASS" if bad == 0 else "FAIL (%d cases)" % bad)
    return 0 if bad == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
