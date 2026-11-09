#!/usr/bin/env python3
"""Standalone build of the public master insert .plg set of tracker-effects-os. Needs no firmware build and writes nothing into the
source tree.

    TOOLCHAIN=.../arm-none-eabi- python3 build.py build --out DIR [--py PYTHON] [--only dirt,tape]
    python3 build.py verify DIR [--host-abi plug_abi.h]

build:  copies src/ and the vendored tracker-ff SDK (sdk/files/) into a scratch tree at their tracker-ff paths, builds every insert of
        effects.json with build_plug.py and checks it (the checks of the old tracker-ff synth-open/plug/build_plugs.sh):
          - no R_ARM_GOT* relocation in the linked ELF, rebase words = R_ARM_ABS32 relocations (> 0)
          - with a Python that has unicorn: emu_plug.py at the three plugin homes (SRAM_L 0x1FFF0BD0, FlexNVM 0x1001E000 with rw on the
            heap 0x20026C00, SDRAM 0x70000000) gives one checksum, equal to emu_checksum of the table, and the same with every
            parameter moved (nondefault, equal to emu_checksum_nondefault and different from the default run)
          - Dirt: 3b1f3dc5 (the device rule: the in-image Dirt gives it), no DC step when entering Asym, zero in gives zero out
        DIR gets <id>.plg, NOTICE.md and the licence texts, SET.json (flags, compiler, SDK digest, one record per .plg) and SHA256SUMS.
        Byte reproducible: no dates, no paths.
verify: SHA256SUMS, SET.json and the header, CRC and kind of every .plg of a finished set.
"""
import argparse
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
HDR = "<IHBBBBHIIIIIIIIIII8x"
HDR_FIELDS = ("magic", "header_size", "abi_major", "abi_minor", "kind", "cpu", "flags", "uid", "version", "min_host", "image_size",
              "bss_size", "rebase_count", "entry_offset", "ram_bytes", "cpu_cycles", "crc32", "rw_offset")
PLUG_MAGIC, KIND_MASTER = 0x31474C50, 1
HOMES = (["--base", "0x1FFF0BD0"], ["--base", "0x1001E000", "--rw-base", "0x20026C00"], ["--base", "0x70000000"])
DIRT_DEVICE_RULE = "3b1f3dc5"


def table():
    return json.load(open(os.path.join(HERE, "effects.json")))


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def run(cmd, **kw):
    r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, **kw)
    if r.returncode:
        sys.exit("FAILED: %s\n%s%s" % (" ".join(cmd), r.stdout, r.stderr))
    return r.stdout


def parse(path):
    data = open(path, "rb").read()
    h = dict(zip(HDR_FIELDS, struct.unpack_from(HDR, data, 0)))
    body = data[64:h["header_size"] + h["image_size"] + 4 * h["rebase_count"]]
    h["crc_ok"] = (zlib.crc32(body) & 0xFFFFFFFF) == h["crc32"]
    return h


def abi_of(header):
    import re
    t = open(header).read()
    g = lambda n: int(re.search(r"#define\s+%s\s+(\d+)" % n, t).group(1))
    return g("PLUG_ABI_MAJOR"), g("PLUG_ABI_MINOR"), g("PLUG_HOST_LEVEL")


def stage(dest):
    """src/ and sdk/files/ merged at their tracker-ff paths. A file in both is an error (the SDK copy is never overridden)."""
    n = 0
    for top in (os.path.join(HERE, "src"), os.path.join(HERE, "sdk", "files")):
        for dp, dn, fn in os.walk(top):
            dn[:] = [d for d in dn if d != "__pycache__"]
            for f in fn:
                if f.startswith("."):
                    continue
                rel = os.path.relpath(os.path.join(dp, f), top)
                d = os.path.join(dest, rel)
                if os.path.exists(d):
                    sys.exit("stage: %s is in src/ and in sdk/files/" % rel)
                os.makedirs(os.path.dirname(d), exist_ok=True)
                shutil.copy2(os.path.join(dp, f), d)
                n += 1
    return n


def sdk_digest():
    h = hashlib.sha256()
    top = os.path.join(HERE, "sdk", "files")
    for dp, dn, fn in sorted(os.walk(top)):
        dn[:] = sorted(d for d in dn if d != "__pycache__")
        for f in sorted(fn):
            p = os.path.join(dp, f)
            h.update(("%s %s\n" % (os.path.relpath(p, top), sha256(p))).encode())
    return h.hexdigest()


def have_unicorn(py):
    return subprocess.run([py, "-c", "import unicorn"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0


def emu(py, so, plg, args):
    r = subprocess.run([py, "-I", "plug/emu_plug.py", plg] + args, cwd=so, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    for l in r.stdout.splitlines():
        if l.startswith("output checksum "):
            return l.split()[2].rstrip(",")
    return None


def build(a):
    tc = os.environ.get("TOOLCHAIN")
    if not tc:
        sys.exit("build.py: set TOOLCHAIN to the arm-none-eabi- prefix (with the trailing dash)")
    t = table()
    rows = [e for e in t["inserts"] if not a.only or e["id"] in a.only.split(",")]
    out = os.path.abspath(a.out)
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    py = a.py or sys.executable
    emu_ok = have_unicorn(py) and not a.no_emu
    env = dict(os.environ, TOOLCHAIN=tc)
    fail = False
    recs = []
    with tempfile.TemporaryDirectory(prefix="fxset") as tmp:
        sd = os.path.join(tmp, "stage")
        n = stage(sd)
        so = os.path.join(sd, "synth-open")
        maj, mi, lvl = abi_of(os.path.join(so, "plug", "plug_abi.h"))
        print("staged %d files (ABI %d.%d, host level %d)" % (n, maj, mi, lvl))
        for e in rows:
            plg = os.path.join(out, e["id"] + ".plg")
            elf = os.path.join(tmp, e["id"] + ".elf")
            cmd = [sys.executable, "plug/build_plug.py", "--id", e["plug_id"], "--kind", "master", "--version", e["version"], "--opt", e["plg_opt"]]
            for k, v in e["plg_src_opt"].items():
                cmd += ["--src-opt", "%s=%s" % (k, v)]
            cmd += ["--keep", elf, "--out", plg]
            for i in e["includes"]:
                cmd += ["-I", i]
            run(cmd + e["sources"], cwd=so, env=env)
            relocs = run([tc + "objdump", "-r", elf])
            got = relocs.count("R_ARM_GOT")
            abs32 = relocs.count("R_ARM_ABS32")
            h = parse(plg)
            ok = got == 0 and h["rebase_count"] == abs32 > 0 and h["crc_ok"] and h["magic"] == PLUG_MAGIC and h["kind"] == KIND_MASTER
            ok = ok and h["abi_major"] == maj and h["min_host"] <= lvl
            note = "emulator checks skipped"
            sums = None
            if emu_ok:
                d = [emu(py, so, plg, x) for x in HOMES]
                nd = [emu(py, so, plg, x + e["nondefault"].split()) for x in HOMES]
                eok = (d[0] is not None and len(set(d)) == 1 and nd[0] is not None and len(set(nd)) == 1 and d[0] != nd[0]
                       and d[0] == e["emu_checksum"] and nd[0] == e["emu_checksum_nondefault"])
                if e["id"] == "dirt":
                    eok = eok and d[0] == DIRT_DEVICE_RULE
                    for chk in ("dirt_asym_check.py", "dirt_zero_check.py"):
                        r = subprocess.run([sys.executable, os.path.join("plug", chk), py, "plug/emu_plug.py", plg, tmp], cwd=so,
                                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
                        print("       " + r.stdout.strip().replace("\n", "\n       "))
                        eok = eok and r.returncode == 0
                ok = ok and eok
                sums = {"default": d[0], "nondefault": nd[0]}
                note = "checksum %s at the three homes, non-default %s" % (d[0], nd[0])
            fail = fail or not ok
            print("%-7s %s  image %d B, bss %d B, %d rebase words = ABS32 %d, GOT %d, %s" % (
                e["id"], "ok  " if ok else "FAIL", h["image_size"], h["bss_size"], h["rebase_count"], abs32, got, note))
            recs.append({"file": e["id"] + ".plg", "plug_id": e["plug_id"], "version": e["version"], "uid": "0x%08x" % h["uid"],
                         "flags": e["plg_opt"], "src_flags": e["plg_src_opt"], "abi": "%d.%d" % (h["abi_major"], h["abi_minor"]),
                         "min_host": h["min_host"], "image_size": h["image_size"], "bss_size": h["bss_size"],
                         "rebase_words": h["rebase_count"], "crc32": "0x%08x" % h["crc32"], "sha256": sha256(plg), "emu_checksums": sums})
    ship_notices(out, rows)
    gcc = run([tc + "gcc", "--version"]).splitlines()[0]
    with open(os.path.join(out, "SET.json"), "w") as f:
        json.dump({"set": "tracker-effects-os", "set_version": t["set_version"], "compiler": gcc, "abi": "%d.%d" % (maj, mi),
                   "sdk_sha256": sdk_digest(), "emulator_checks": "done" if emu_ok else "skipped (the Python has no unicorn or --no-emu)",
                   "inserts": recs}, f, indent=1)
        f.write("\n")
    write_sums(out)
    print("%s: %d inserts, set %s%s" % (out, len(recs), t["set_version"], ", FAILED" if fail else ""))
    return 1 if fail else 0


def ship_notices(out, rows):
    """NOTICE.md (the sections of the inserts in the set, from src/synth-open/plug/fx/NOTICE.md) and the licence texts they name."""
    import re
    text = open(os.path.join(HERE, "src", "synth-open", "plug", "fx", "NOTICE.md")).read()
    with open(os.path.join(out, "NOTICE.md"), "w") as f:
        f.write("# Notices for the master insert plugins in this directory\n\nLicence texts: the LICENSE-*.txt files here.\n\n")
        for e in rows:
            m = re.search(r"^## %s\b.*?(?=^## |\Z)" % re.escape(e["notice_section"]), text, re.M | re.S)
            if not m:
                sys.exit("NOTICE.md has no section '## %s'" % e["notice_section"])
            f.write(m.group(0).replace("licenses/", "").rstrip() + "\n\n")
        f.write("## The plugin runtime\n\n`plug_abi.h`, `plug_rt.c` and `plug.ld` of tracker-ff are linked into every plugin: MIT, "
                "LICENSE-tracker-ff-MIT.txt.\n")
    for lf in sorted({lf for e in rows for lf in e["licence_files"]} | {"licenses/LICENSE-tracker-ff-MIT.txt"}):
        shutil.copy(os.path.join(HERE, lf), os.path.join(out, os.path.basename(lf)))


def write_sums(out):
    files = sorted(os.path.relpath(os.path.join(dp, f), out) for dp, _, fn in os.walk(out) for f in fn
                   if f != "SHA256SUMS" and not f.startswith("."))
    with open(os.path.join(out, "SHA256SUMS"), "w") as f:
        for p in files:
            f.write("%s  %s\n" % (sha256(os.path.join(out, p)), p))


def verify(a):
    d = os.path.abspath(a.dir)
    errs = []
    listed = {}
    for l in open(os.path.join(d, "SHA256SUMS")):
        h, _, p = l.rstrip("\n").partition("  ")
        listed[p] = h
    for p, h in listed.items():
        if not os.path.exists(os.path.join(d, p)):
            errs.append("SHA256SUMS lists a missing file: " + p)
        elif sha256(os.path.join(d, p)) != h:
            errs.append("hash differs: " + p)
    for dp, _, fn in os.walk(d):
        for f in fn:
            p = os.path.relpath(os.path.join(dp, f), d)
            if p != "SHA256SUMS" and p not in listed and not f.startswith("."):
                errs.append("file not in SHA256SUMS: " + p)
    man = json.load(open(os.path.join(d, "SET.json")))
    host = abi_of(a.host_abi) if a.host_abi else None
    ids = {e["id"]: e for e in table()["inserts"]}
    for r in man["inserts"]:
        h = parse(os.path.join(d, r["file"]))
        if not h["crc_ok"] or h["magic"] != PLUG_MAGIC or h["kind"] != KIND_MASTER:
            errs.append(r["file"] + ": not a master insert plugin with a valid CRC")
        if "%d.%d" % (h["abi_major"], h["abi_minor"]) != r["abi"] or sha256(os.path.join(d, r["file"])) != r["sha256"]:
            errs.append(r["file"] + ": differs from SET.json")
        if host and (h["abi_major"] != host[0] or h["min_host"] > host[2]):
            errs.append(r["file"] + ": the host refuses it (abi major %d vs %d, min_host %d vs level %d)" % (h["abi_major"], host[0], h["min_host"], host[2]))
        e = ids.get(r["file"][:-4])
        if not e or "%d.%d.%d" % (h["version"] >> 16, (h["version"] >> 8) & 255, h["version"] & 255) != e["version"]:
            errs.append(r["file"] + ": not in effects.json or the version differs")
    if errs:
        print("verify FAILED: " + d)
        for e in errs:
            print("  " + e)
        return 1
    print("verify ok: %s (%d inserts, set %s, abi %s%s)" % (d, len(man["inserts"]), man["set_version"], man["abi"],
                                                            ", host header %d.%d level %d" % host if host else ""))
    return 0


def package(a):
    """DIR/../tracker-ff-effects-<set>.zip with the set under Plugins/Master/ (unzip at the card root). Reproducible."""
    import zipfile
    d = os.path.abspath(a.dir)
    if verify(a):
        return 1
    man = json.load(open(os.path.join(d, "SET.json")))
    zp = os.path.join(os.path.dirname(d), "tracker-ff-effects-%s.zip" % man["set_version"])
    names = sorted(os.path.relpath(os.path.join(dp, f), d) for dp, _, fn in os.walk(d) for f in fn if not f.startswith("."))
    with zipfile.ZipFile(zp, "w", zipfile.ZIP_DEFLATED) as z:
        for n in names:
            zi = zipfile.ZipInfo("Plugins/Master/" + n, (2026, 1, 1, 0, 0, 0))
            zi.compress_type, zi.external_attr = zipfile.ZIP_DEFLATED, 0o644 << 16
            z.writestr(zi, open(os.path.join(d, n), "rb").read())
    print("%s: %d files, sha256 %s" % (zp, len(names), sha256(zp)))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = ap.add_subparsers(dest="cmd", required=True)
    b = sp.add_parser("build")
    b.add_argument("--out", required=True)
    b.add_argument("--py", help="Python with unicorn for the emulator checks (default: the running one)")
    b.add_argument("--no-emu", action="store_true")
    b.add_argument("--only", help="comma separated insert ids")
    for name in ("verify", "package"):
        v = sp.add_parser(name)
        v.add_argument("dir")
        v.add_argument("--host-abi", help="the firmware's plug_abi.h: refuse a set its host would refuse")
    a = ap.parse_args()
    return {"build": build, "verify": verify, "package": package}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main())
