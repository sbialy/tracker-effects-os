#!/usr/bin/env python3
"""The browser twin of the master insert plugins of tracker-effects-os: one .wasm module and one manifest per insert (plug_wasm.h of the SDK).

    python3 wasm/build_modules.py --out DIR [--only dirt,tape] [--inserts-plg DIR] [--emsdk DIR] [--wasm-abi-major N]

The table is effects.json. Inserts are built in a scratch copy of src/ and the vendored SDK (sdk/files/), like build.py. An insert has
no linked-in twin in the editor, its reference is the device build run by emu_plug.py: wasm has no fused multiply-add, so a source
the ARM compiler fuses (Dirt's 1 + 15 d^2 at -Os) can differ by one ulp in a coefficient. Every module is checked (only the five
module imports, plug_entry and plug_wasm_abi exported, no name section, no DWARF), and the descriptor is read from the plugin's own
sources (native arm64 build).

DIR gets <id>.wasm, <id>.json per module, NOTICE.md, the licence texts the modules name, MODULES.json (the set) and SHA256SUMS.
No dates, paths or host names in any file: a rebuild is a diff. Never runs an x86_64 binary (the descriptor dump is arm64).
"""
import argparse
import json
import os
import re
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ENG = os.path.dirname(HERE)
ROOT = ENG
sys.path.insert(0, HERE)
sys.path.insert(0, ENG)
import build as fxbuild  # noqa: E402  (stage, table, abi_of)
import wasm_common as wc  # noqa: E402

SO = os.path.join(ROOT, "sdk", "files", "synth-open")   # the vendored SDK headers (plug_abi.h, plug_wasm.h)
SOURCE_REPO = "tracker-effects-os"
FIELDS = ("max_voices", "volume_param", "work_bytes", "slot_fx", "params", "sections", "macros")


def check_table(t):
    """effects.json sanity: unique ids, every row public."""
    ids = [r["id"] for r in t["inserts"]]
    if len(ids) != len(set(ids)):
        sys.exit("effects.json: duplicate module ids")
    for r in t["inserts"]:
        if r.get("visibility") != "public" or "repo" in r:
            sys.exit("effects.json: %s: every module of this repository is public and names no repo" % r["id"])


def notice_section(name):
    """The section '## <name> ...' of synth-open/plug/fx/NOTICE.md up to the next '## ', as one text."""
    t = open(os.path.join(ROOT, "src", "synth-open", "plug", "fx", "NOTICE.md")).read()
    m = re.search(r"^## %s\b.*?\n(.*?)(?=^## |\Z)" % re.escape(name), t, re.M | re.S)
    if not m:
        sys.exit("src/synth-open/plug/fx/NOTICE.md has no section '## %s'" % name)
    return " ".join(m.group(1).split()).replace("licenses/", "")


def native_dump(srcs, so, tmp, tag, inc, extra_for=None):
    """Descriptor JSON of a plugin from its own sources, arm64 clang. C stays C (plug_entry keeps its unmangled name)."""
    objs = []
    for src in list(srcs) + [os.path.join(HERE, "dump_desc.cpp")]:
        o = os.path.join(tmp, "%s_%s.native.o" % (tag, os.path.basename(src)))
        cxx = src.endswith(".cpp")
        fl = (["clang++", "-std=c++20", "-fno-exceptions", "-fno-rtti"] if cxx else ["clang", "-std=c11"]) + ["-arch", "arm64", "-O1", "-include",
              os.path.join(HERE, "native_compat.h")]
        fl += (extra_for or {}).get(src, []) + inc + ["-c", src, "-o", o]
        wc.run(fl, so, os.environ)
        objs.append(o)
    dump = os.path.join(tmp, tag + "_dump")
    wc.run(["clang++", "-arch", "arm64"] + objs + ["-o", dump], so, os.environ)
    return json.loads(wc.run([dump], so, os.environ))


def link_and_check(e, objs, w, so, tmp, out, env):
    wasm = os.path.join(out, e["id"] + ".wasm")
    wc.run(["emcc"] + w["linkflags"].split() + objs + ["-o", wasm], so, env)
    data = open(wasm, "rb").read()
    imps, exps = wc.imports_exports(data)
    bad = [x for x in imps if (x[0], x[1]) not in wc.ALLOWED_IMPORTS]
    if bad:
        sys.exit("%s: imports outside the module ABI: %s" % (e["id"], bad))
    names = {n for n, _ in exps}
    if not {"plug_entry", "plug_wasm_abi"} <= names:
        sys.exit("%s: exports %s lack plug_entry or plug_wasm_abi" % (e["id"], sorted(names)))
    custom = [n for sid, n, _ in wc.wasm_sections(data) if sid == 0]
    if any(n == "name" or n.startswith(".debug") or n == "sourceMappingURL" for n in custom):
        sys.exit("%s: not stripped (custom sections %s)" % (e["id"], custom))
    return data, imps, custom, wasm



def build_insert(e, w, so, tmp, out, env, emver, abi, wabi, claim, plg_dir):
    rtflags = w["rtflags"].split()
    if claim is not None:
        rtflags += ["-DPLUG_RT_WASM_ABI=((%d<<16)|%d)" % (claim, wabi[1])]
    rt = os.path.join(tmp, e["id"] + "_rt.o")
    wc.run(["emcc"] + rtflags + ["-I", "plug", "-c", "plug/plug_rt_wasm.c", "-o", rt], so, env)
    objs = [rt]
    inc = sum((["-I", i] for i in e["includes"]), []) + ["-I", "plug"]
    for src in e["sources"]:
        o = os.path.join(tmp, "%s_%s.o" % (e["id"], os.path.basename(src)))
        cxx = src.endswith(".cpp")
        wc.run(["em++" if cxx else "emcc"] + (w["insert_cxxflags"] if cxx else w["insert_cflags"]).split() + (w["moduleflags"] if cxx else w["moduleflags_c"]).split()
               + inc + ["-c", src, "-o", o], so, env)
        objs.append(o)
    data, imps, custom, wasm = link_and_check(e, objs, w, so, tmp, out, env)
    desc = native_dump(e["sources"], so, tmp, e["id"], inc)
    notice = notice_section(e["notice_section"]) if e.get("notice_section") else None
    man = {
        "format": "ffmod", "format_version": 1, "id": e["plug_id"], "kind": "master", "name": desc["name"], "version": e["version"],
        "abi": "%d.%d" % abi[:2], "min_host": 1, "wasm_abi": "%d.%d" % (claim if claim is not None else wabi[0], wabi[1]),
        "visibility": e["visibility"], "source_repo": SOURCE_REPO,
        "licence": e["licence"], "author": desc["author"],
        "params": desc["params"], "instance_size": desc["instance_size"],
        "build": {"emcc": emver, "cxxflags": w["insert_cxxflags"], "cflags": w["insert_cflags"], "moduleflags": w["moduleflags"],
                  "rtflags": w["rtflags"], "linkflags": w["linkflags"]},
        "files": {"wasm": {"name": e["id"] + ".wasm", "size": len(data), "sha256": wc.sha(wasm)}},
        "imports": sorted("%s.%s" % (m, f) for m, f, _ in imps), "custom_sections": custom,
    }
    if notice:
        man["notice"] = notice
    if plg_dir and os.path.exists(os.path.join(plg_dir, e["id"] + ".plg")):
        plg = os.path.join(plg_dir, e["id"] + ".plg")
        man["files"]["plg"] = {"name": e["id"] + ".plg", "size": os.path.getsize(plg), "sha256": wc.sha(plg)}
    return man, len(data), notice



def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out")
    ap.add_argument("--only")
    ap.add_argument("--inserts-plg", help="the finished .plg set (build.py): its sha256 goes into the manifests")
    ap.add_argument("--emsdk", default=os.environ.get("EMSDK") or next((p for p in (os.path.join(ROOT, "agt", "emsdk"), os.path.join(ROOT, "..", "tracker-ff", "agt", "emsdk")) if os.path.isdir(p)), "emsdk"),
                    help="emsdk 6.0.13 (default: $EMSDK, <repo>/agt/emsdk, ../tracker-ff/agt/emsdk)")
    ap.add_argument("--wasm-abi-major", type=int, help="claim another module ABI major (refusal test only)")
    a = ap.parse_args()
    t = fxbuild.table()
    check_table(t)
    if not a.out:
        ap.error("--out is required")
    w = json.load(open(os.path.join(HERE, "wasm.json")))
    env, up = wc.emsdk_env(a.emsdk)
    emver = wc.run([os.path.join(up, "emcc"), "--version"], ROOT, env).splitlines()[0]
    if w["emcc"] not in emver:
        sys.exit("wasm.json pins emcc %s, found: %s" % (w["emcc"], emver))
    maj, mi, _ = fxbuild.abi_of(os.path.join(SO, "plug", "plug_abi.h"))
    wtext = open(os.path.join(SO, "plug", "plug_wasm.h")).read()
    wabi = (int(wtext.split("#define PLUG_WASM_ABI_MAJOR")[1].split()[0]), int(wtext.split("#define PLUG_WASM_ABI_MINOR")[1].split()[0]))
    out = os.path.abspath(a.out)
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    inserts = [e for e in t["inserts"] if not a.only or e["id"] in a.only.split(",")]
    recs, notices, licences = [], [], set()
    with tempfile.TemporaryDirectory(prefix="modwasm") as tmp:
        sd = os.path.join(tmp, "stage")
        fxbuild.stage(sd)
        for e in inserts:
            man, size, notice = build_insert(e, w, os.path.join(sd, "synth-open"), tmp, out, env, emver, (maj, mi), wabi, a.wasm_abi_major, a.inserts_plg)
            recs.append(man)
            notices.append((e["id"], notice))
            licences.update(e["licence_files"])
            print("%-7s insert %-7s %6d B  %s" % (e["id"], e["visibility"], size, ",".join(man["custom_sections"]) or "none"))
    for m in recs:
        with open(os.path.join(out, m["files"]["wasm"]["name"].replace(".wasm", ".json")), "w") as f:
            json.dump(m, f, indent=1)
            f.write("\n")
    for lf in sorted(licences):
        shutil.copy(os.path.join(ROOT, lf), os.path.join(out, os.path.basename(lf)))
    with open(os.path.join(out, "NOTICE.md"), "w") as f:
        f.write("# Notices for the browser modules in this directory\n\nShip every module with this file and the licence texts it names.\n\n")
        for i, n in notices:
            f.write("## %s\n\n%s\n\n" % (i, n or "Own code of tracker-ff, MIT licence (LICENSE-tracker-ff-MIT.txt)."))
    with open(os.path.join(out, "MODULES.json"), "w") as f:
        json.dump({"set": SOURCE_REPO, "set_version": t["set_version"], "abi": "%d.%d" % (maj, mi), "wasm_abi": "%d.%d" % wabi, "emcc": emver,
                   "modules": [{"id": m["files"]["wasm"]["name"][:-5], "plug_id": m["id"], "kind": m["kind"], "name": m["name"], "version": m["version"],
                                "visibility": m["visibility"], "licence": m["licence"], "wasm": m["files"]["wasm"]} for m in recs]}, f, indent=1)
        f.write("\n")
    with open(os.path.join(out, "SHA256SUMS"), "w") as f:
        for p in sorted(os.listdir(out)):
            if p != "SHA256SUMS":
                f.write("%s  %s\n" % (wc.sha(os.path.join(out, p)), p))
    print("%s: %d insert modules" % (out, len(recs)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
