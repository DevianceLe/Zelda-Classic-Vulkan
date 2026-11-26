#!/usr/bin/env python3
"""Inventory of upstream's 3.0 builds and their quest file formats.

Reads upstream's history (read only) and, for every 3.0 build (the 3.0.0-prerelease.N tags on
main, which upstream's site lists as the 3.0 nightlies, and the commit our "3.0 Nightly" target
follows), takes what decides how that build writes a quest file:
  - every section version (V_*) and, while they existed, section cversions (CV_*), from zdefs.h;
  - the compat rule version (V_COMPATRULE), the compile settings version, the ZASM metadata
    version (METADATA_V), the weapon data and script config versions;
  - the header writer (writeheader in zq_class.cpp), as a hash of its code.
Builds whose values all equal the build before them share its format. Each run of builds with one
format becomes one entry, named by the date of the first build in it.

usage: zq_gen_30_builds.py <upstream repo> <out.h> [<out.tsv>]
"""
import hashlib
import re
import subprocess
import sys

NEWEST = "ef321f8"
# tilesets templates\ ships already under their own names (zq_builds_30_new.cpp's g_shipped)
SHIPPED = {"3e4a32f2c8": "classic_2.55.0.qst", "2ff8058873": "classic_2.55.2.qst", "4f906dbb85": "classic_2.55.6.qst",
           "45978a13a4": "classic_3.0.qst"}      # the commit the 3.0 Nightly writers follow (zq_writers.h)


def git(repo, *args, data=None):
    return subprocess.run(["git", "-C", repo] + list(args), input=data, capture_output=True, check=True).stdout


class Cat:
    def __init__(self, repo):
        self.p = subprocess.Popen(["git", "-C", repo, "cat-file", "--batch"], stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE)

    def blob_id(self, rev, path):
        # the file's git blob (its first 10 hex digits), "" when the build has none
        self.p.stdin.write(("%s:%s\n" % (rev, path)).encode())
        self.p.stdin.flush()
        head = self.p.stdout.readline().decode()
        if head.endswith("missing\n"):
            return ""
        n = int(head.split()[2])
        body = self.p.stdout.read(n + 1)
        # a Git LFS pointer: the file itself isn't in the history
        if body.startswith(b"version https://git-lfs"):
            return ""
        return head.split()[0][:10]

    def get(self, rev, path):
        self.p.stdin.write(("%s:%s\n" % (rev, path)).encode())
        self.p.stdin.flush()
        head = self.p.stdout.readline().decode()
        if head.endswith("missing\n"):
            return None
        n = int(head.split()[2])
        body = self.p.stdout.read(n)
        self.p.stdout.read(1)
        return body.decode("latin-1")


DEFINE = re.compile(r"^\s*#define\s+((?:V|CV)_[A-Z0-9_]+|METADATA_V)\s+(\d+)", re.M)
KEEP = ("V_ZC_COMPILERSIG", "V_GRAPHICSPACK", "V_SAVEGAME", "V_TILES_ZTILESET_MIN", "V_GUYS_ZNPC_MIN")


def defines(text):
    d = {}
    for m in DEFINE.finditer(text):
        if m.group(1) not in KEEP and m.group(1) not in d:
            d[m.group(1)] = int(m.group(2))
    return d


def function(text, name):
    m = re.search(r"^int(?:32_t)?\s+" + name + r"\s*\(", text, re.M)
    if not m:
        return ""
    i = text.index("{", m.end())
    depth = 0
    for j in range(i, len(text)):
        if text[j] == "{":
            depth += 1
        elif text[j] == "}":
            depth -= 1
            if depth == 0:
                return text[m.start():j + 1]
    return text[m.start():]


def code_hash(fn):
    # comments and spacing don't change what is written
    fn = re.sub(r"//[^\n]*", "", fn)
    fn = re.sub(r"/\*.*?\*/", "", fn, flags=re.S)
    fn = re.sub(r"\s+", "", fn)
    fn = re.sub(r"new_return\(\d+\)", "R", fn)
    fn = re.sub(r"return\d+;", "R", fn)
    return hashlib.sha1(fn.encode()).hexdigest()[:8]


def main():
    repo, out_h = sys.argv[1], sys.argv[2]
    out_tsv = sys.argv[3] if len(sys.argv) > 3 else None
    tags = [t for t in git(repo, "tag").decode().split() if t.startswith("3.0.0-prerelease.")]
    tags.sort(key=lambda t: int(re.match(r"3\.0\.0-prerelease\.(\d+)", t).group(1)))
    cat = Cat(repo)
    newest_date = git(repo, "log", "-1", "--format=%cs", NEWEST).decode().strip()
    builds = []

    for t in tags + [NEWEST]:
        if t == NEWEST:
            num, date = 10000, newest_date
            name = "main@" + NEWEST
        else:
            m = re.match(r"3\.0\.0-prerelease\.(\d+)\+(\d{4}-\d{2}-\d{2})", t)
            num, date, name = int(m.group(1)), m.group(2), t
        z = None
        for p in ("src/core/zdefs.h", "src/base/zdefs.h", "src/zdefs.h"):
            z = cat.get(t, p)
            if z:
                break
        d = defines(z)
        zc = cat.get(t, "src/zq/zq_class.cpp") or ""
        d["HDRW"] = code_hash(function(zc, "writeheader"))
        tiles = [cat.blob_id(t, tp) for tp in ("resources/modules/classic/default.qst", "resources/tilesets/cambria.qst")]
        builds.append((num, date, name, d, tiles))

    # the newest build sits in date order among the prereleases
    builds.sort(key=lambda b: (b[1], b[0]))
    keys = sorted(set(k for b in builds for k in b[3]))
    groups = []
    for b in builds:
        if groups and groups[-1][-1][3] == b[3]:
            groups[-1].append(b)
        else:
            groups.append([b])

    if out_tsv:
        with open(out_tsv, "w") as f:
            f.write("build\tdate\tgroup\t" + "\t".join(keys) + "\n")
            for gi, g in enumerate(groups):
                for b in g:
                    f.write("%s\t%s\t%d\t%s\n" % (b[2], b[1], gi, "\t".join(str(b[3].get(k, "")) for k in keys)))

    secs = ["V_HEADER", "V_RULES", "V_STRINGS", "V_MISC", "V_TILES", "V_COMBOS", "V_CSETS", "V_MAPS", "V_DMAPS",
            "V_DOORS", "V_ITEMS", "V_WEAPONS", "V_COLORS", "V_ICONS", "V_INITDATA", "V_GUYS", "V_MIDIS", "V_CHEATS",
            "V_COMBOALIASES", "V_LINKSPRITES|V_HEROSPRITES", "V_SUBSCREEN", "V_ITEMDROPSETS", "V_FFSCRIPT", "V_SFX",
            "V_FAVORITES", "V_ZINFO", "V_ADVMUSIC", "V_COMPATRULE", "METADATA_V", "V_COMPILESETTING", "V_WEAP_DATA",
            "V_SCRIPT_CONFIG"]

    def val(d, k):
        for kk in k.split("|"):
            if kk in d:
                return d[kk]
        # a version that doesn't exist yet: no section (0), or for the versions inside sections -1
        return -1 if k in ("V_COMPILESETTING", "V_WEAP_DATA", "V_SCRIPT_CONFIG") else 0

    cvs = {"V_HEADER": "CV_HEADER", "V_RULES": "CV_RULES", "V_STRINGS": "CV_STRINGS", "V_MISC": "CV_MISC",
           "V_TILES": "CV_TILES", "V_COMBOS": "CV_COMBOS", "V_CSETS": "CV_CSETS", "V_MAPS": "CV_MAPS",
           "V_DMAPS": "CV_DMAPS", "V_DOORS": "CV_DOORS", "V_ITEMS": "CV_ITEMS", "V_WEAPONS": "CV_WEAPONS",
           "V_COLORS": "CV_COLORS", "V_ICONS": "CV_ICONS", "V_INITDATA": "CV_INITDATA", "V_GUYS": "CV_GUYS",
           "V_MIDIS": "CV_MIDIS", "V_CHEATS": "CV_CHEATS", "V_COMBOALIASES": "CV_COMBOALIASES",
           "V_LINKSPRITES|V_HEROSPRITES": "CV_LINKSPRITES|CV_HEROSPRITES", "V_SUBSCREEN": "CV_SUBSCREEN",
           "V_ITEMDROPSETS": "CV_ITEMDROPSETS", "V_FFSCRIPT": "CV_FFSCRIPT", "V_SFX": "CV_SFX",
           "V_FAVORITES": "CV_FAVORITES", "V_ZINFO": "CV_ZINFO", "V_ADVMUSIC": "CV_ADVMUSIC"}

    with open(out_h, "w", newline="\n") as f:
        f.write("// Generated by tools/zq_gen_30_builds.py from upstream's history: one row per run of 3.0\n"
                "// builds that write quests in one format (every section version, the compat rule, ZASM\n"
                "// metadata and header writer the same). Do not edit.\n\n")
        f.write("struct Zq30Format\n{\n    const char *first, *last;     // upstream's builds (tags) in this run\n"
                "    const char *date;              // the first one's date\n    int count;                     // how many builds\n"
                "    int first_n, last_n;           // their prerelease numbers (10000: the 3.0 Nightly commit)\n"
                "    unsigned header_code;          // the header writer's kind (a hash of its code)\n"
                "    const char *classic, *cambria; // File > New's tilesets in the run's last build (git blobs; none: \"\")\n"
                "    // section versions, in this order:\n    //")
        f.write(" ".join(s.split("|")[0][2:] if s.startswith("V_") else s for s in secs))
        f.write("\n    int v[%d];\n    // their cversions (0 where the build has none; from 3.0's V_HEADER 9 the version again)\n"
                "    int cv[27];\n};\n\n" % len(secs))
        f.write("static const Zq30Format zq30_formats[] =\n{\n")
        for g in groups:
            d = g[0][3]
            v = [val(d, s) for s in secs]
            cv = [val(d, cvs[s]) for s in secs[:27]]
            nums = [b[0] for b in g if b[0] != 10000] or [10000]
            f.write("    { \"%s\", \"%s\", \"%s\", %d, %d, %d, 0x%su, \"%s\", \"%s\",\n      { %s },\n      { %s } },\n" % (
                g[0][2], g[-1][2], g[0][1], len(g), min(nums), max(nums), d["HDRW"], g[-1][4][0], g[-1][4][1],
                ", ".join(map(str, v)), ", ".join(map(str, cv))))
        f.write("};\n")

    blobs = sorted(set(x for g in groups for x in g[-1][4] if x))
    if len(sys.argv) > 4:
        # the tilesets themselves, as templates\<blob>.qst
        import os
        os.makedirs(sys.argv[4], exist_ok=True)
        for b in blobs:
            if b in SHIPPED:
                continue
            with open(os.path.join(sys.argv[4], b + ".qst"), "wb") as f:
                f.write(git(repo, "cat-file", "blob", b))
    print("%d builds, %d formats, %d tileset files" % (len(builds), len(groups), len(blobs)))
    prev = None
    for g in groups:
        d = g[0][3]
        ch = []
        if prev:
            for k in keys:
                if prev.get(k) != d.get(k):
                    ch.append("%s %s->%s" % (k, prev.get(k, "-"), d.get(k, "-")))
        print("%-38s %-38s %3d  %s" % (g[0][2], g[-1][2], len(g), ", ".join(ch)))
        prev = d


if __name__ == "__main__":
    main()
