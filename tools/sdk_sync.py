#!/usr/bin/env python3
"""The plugin SDK this repository builds against is a pinned copy of files from tracker-ff, kept in sdk/files/ at their tracker-ff
paths. sdk/SOURCE names the tracker-ff commit the copy was taken from.

    python3 tools/sdk_sync.py check FW      every file of sdk/files/ equals the same path in the tracker-ff checkout FW (exit 1 if not)
    python3 tools/sdk_sync.py sync FW       copy those files from FW and write sdk/SOURCE (the commit of FW). Review the diff before
                                            committing: a changed header can change the .plg bytes, bump the module versions then.

The set of files is what sdk/files/ holds: the files named in sdk/headers.txt and sdk/tools.txt plus synth-open/plug/LICENSE (and,
in poly-effects-os, the Dirt kernel the firmware also builds in). A build never reads FW, so a clone of this repository alone builds.
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FILES = os.path.join(HERE, "sdk", "files")


def listed():
    out = []
    for dp, dn, fn in os.walk(FILES):
        dn[:] = [d for d in dn if d != "__pycache__"]
        out += [os.path.relpath(os.path.join(dp, f), FILES) for f in fn if not f.startswith(".")]
    return sorted(out)


def main():
    if len(sys.argv) != 3 or sys.argv[1] not in ("check", "sync"):
        sys.exit(__doc__)
    cmd, fw = sys.argv[1], os.path.abspath(os.path.expanduser(sys.argv[2]))
    if not os.path.isdir(os.path.join(fw, "synth-open", "plug")):
        sys.exit("sdk_sync: %s is not a tracker-ff checkout (no synth-open/plug)" % fw)
    bad = []
    for p in listed():
        src = os.path.join(fw, p)
        if not os.path.exists(src):
            bad.append("missing in the firmware: " + p)
        elif open(src, "rb").read() != open(os.path.join(FILES, p), "rb").read():
            if cmd == "sync":
                open(os.path.join(FILES, p), "wb").write(open(src, "rb").read())
                print("updated " + p)
            else:
                bad.append("differs: " + p)
    r = subprocess.run(["git", "-C", fw, "rev-parse", "HEAD"], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    head = r.stdout.strip() if r.returncode == 0 else "unknown"
    pinned = open(os.path.join(HERE, "sdk", "SOURCE")).read().split()[0] if os.path.exists(os.path.join(HERE, "sdk", "SOURCE")) else "none"
    if cmd == "sync" and not bad:
        with open(os.path.join(HERE, "sdk", "SOURCE"), "w") as f:
            f.write("%s tracker-ff commit the files of sdk/files/ were copied from (tools/sdk_sync.py sync)\n" % head)
    for b in bad:
        print("sdk " + cmd + " FAILED: " + b)
    if bad:
        return 1
    print("sdk %s ok: %d files of sdk/files equal %s (commit %s, sdk/SOURCE %s)" % (cmd, len(listed()), fw, head[:12], pinned[:12]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
