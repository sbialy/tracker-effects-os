#!/usr/bin/env python3
"""Builds the Sample Editor effect plugins (src/ptfx/se_*.c) into .ptfx files with the builder vendored from tracker-ff
(sdk/files/editor-fx-sdk/ptfx_builder.py).

    python3 tools/build_ptfx.py --toolchain /path/arm-none-eabi- --out build/ptfx [--only delay,flanger]

The files that use double (the firmware does too, as soft double) link libgcc and call it PC relative. Output is byte reproducible.
"""
import argparse
import hashlib
import shutil
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SDK = os.path.join(ROOT, "sdk", "files", "editor-fx-sdk")

# name -> extra builder arguments. -ffp-contract=off: the fused multiply adds of the firmware are written as fmaf in the sources
NOFUSE = "--cflags=-ffp-contract=off"
SOFTDOUBLE = ["--libgcc", "--cflags=-ffp-contract=off -mno-long-calls"]
EFFECTS = {
    "amplifier": [NOFUSE],
    "overdrive": [NOFUSE],
    "delay": [NOFUSE],
    "bitcrusher": SOFTDOUBLE,
    "chorus": [NOFUSE],
    "flanger": SOFTDOUBLE,
    "equalizer": SOFTDOUBLE,
    "limiter": [NOFUSE],
    "compressor": [NOFUSE],
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--toolchain", required=True)
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "ptfx"))
    ap.add_argument("--only", default="")
    a = ap.parse_args()
    only = [n for n in a.only.split(",") if n] or list(EFFECTS)
    os.makedirs(a.out, exist_ok=True)
    for n in only:
        src = os.path.join(ROOT, "src", "ptfx", "se_%s.c" % n)
        dst = os.path.join(a.out, "se_%s.ptfx" % n)
        cmd = [sys.executable, "-I", os.path.join(SDK, "ptfx_builder.py"), src, "-o", dst, "--toolchain", a.toolchain, *EFFECTS[n]]
        r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        if r.returncode:
            sys.exit("FAILED %s\n%s%s" % (n, r.stdout, r.stderr))
        print("%-20s %6d B  sha256 %s" % ("se_%s.ptfx" % n, os.path.getsize(dst), hashlib.sha256(open(dst, "rb").read()).hexdigest()[:16]))
    # the notice travels with the files: tracker-ff lists it in its lock and lays it out under licenses/plugins/
    shutil.copyfile(os.path.join(ROOT, "src", "ptfx", "NOTICE.md"), os.path.join(a.out, "NOTICE-ptfx.md"))


if __name__ == "__main__":
    main()
