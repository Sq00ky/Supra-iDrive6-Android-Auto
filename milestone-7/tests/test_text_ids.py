#!/usr/bin/env python3
# Every text id NbtAad.xml asks for must exist in every language of NbtAad_ALL_Texts.zip: the NBTApps installer refuses
# a release otherwise ("This release is internally inconsistent: NbtAad.xml asks for text ... but de.txt ... does not
# define them", #332), and the HMI would show rows with no label.
import os, re, sys, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
RHMI = os.path.join(HERE, "..", "rhmiapp")
xml = open(os.path.join(RHMI, "NbtAad.xml"), encoding="utf-8", errors="replace").read()
need = sorted(set(int(x) for x in re.findall(r'\btextId="(\d+)"', xml)))
bad = 0
with zipfile.ZipFile(os.path.join(RHMI, "NbtAad_ALL_Texts.zip")) as z:
    names = sorted(z.namelist())
    for n in names:
        have = set(int(m) for m in re.findall(r"^(\d+)=", z.read(n).decode("utf-8", "replace"), re.M))
        miss = [i for i in need if i not in have]
        if miss:
            print("[FAIL] %s is missing text ids %s" % (n, miss))
            bad += 1
if not names:
    print("[FAIL] NbtAad_ALL_Texts.zip has no language files")
    bad += 1
if bad:
    sys.exit(1)
print("[PASS] test_text_ids: %d text ids present in all %d languages" % (len(need), len(names)))
