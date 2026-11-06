#!/usr/bin/env python3
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
"""Decode the messages our code builds with protoc and the schemas in proto/.

Fails when a message has a field the schema doesn't list, a field with the wrong wire type (protoc shows both by number),
or bytes that change when protoc encodes the message again.

It checks only what tests/dump_messages.cpp can build, against numbers we wrote down ourselves. It can't say a field
means what we think, or that the phone agrees. Run from milestone-7: test_schema.py <dump_messages binary>
"""
import os
import re
import subprocess
import sys

PROTO_DIR = "proto"

# Each one is a separate run of dump_messages, for settings that change the manifest's shape.
SETTINGS = [
    ("default", {}),
    ("bluetooth channel", {"NBT_BT_ADDR": "AA:BB:CC:DD:EE:FF", "NBT_SDR_DROP": "none"}),
    ("nothing dropped", {"NBT_SDR_DROP": "none"}),
    ("system audio sink", {"NBT_SYSTEM_SINK": "1"}),
    ("bluetooth audio", {"NBT_AUDIO_BT": "1"}),
    ("reference identity", {"NBT_SDR_IDENTITY": "1"}),
    ("negative insets", {"NBT_VID_INSETS": "-1,-2,-3,-4"}),
    ("BMW, wide screen", {"NBT_VEHICLE": "bmw", "NBT_SCREEN": "1440x540"}),
    ("rail status hidden", {"NBT_RAIL_HIDE": "all"}),
    ("480p at 60 fps", {"NBT_VID_RES": "1", "NBT_VID_FPS": "60"}),
    ("1080p at 30 fps", {"NBT_VID_RES": "3", "NBT_VID_FPS": "30"}),
]
MANIFEST = "nbt.manifest.ServiceDiscoveryResponse"
WIRE_VARINT, WIRE_LENGTH = 0, 2
CHANNEL_KEY = (1 << 3) | WIRE_LENGTH
# Text-format lines for a field the schema doesn't list start with its number: `99: 1` or `99 {`.
UNKNOWN_FIELD = re.compile(r"^\s*\d+(?::| \{)", re.M)


def protoc(mode, type_name, proto_file, data):
    return subprocess.run(["protoc", "-I", PROTO_DIR, "--%s=%s" % (mode, type_name), proto_file],
                          input=data, stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def varint(b, i):
    value = shift = 0
    while True:
        c = b[i]
        i += 1
        value |= (c & 0x7F) << shift
        shift += 7
        if not c & 0x80:
            return value, i


def channels_first(manifest):
    """The manifest as protoc writes it: every channel (field 1, length-delimited) first, the rest as they were.
    We write the channels in trailingChannels after field 17, so protoc moves those. Nothing else may move."""
    i, channels, others = 0, [], []
    while i < len(manifest):
        start = i
        key, i = varint(manifest, i)
        if key & 7 == WIRE_VARINT:
            _, i = varint(manifest, i)
        elif key & 7 == WIRE_LENGTH:
            n, i = varint(manifest, i)
            i += n
        else:
            raise ValueError("wire type %d in a manifest" % (key & 7))
        (channels if key == CHANNEL_KEY else others).append(manifest[start:i])
    return b"".join(channels + others)


def check(proto_file, type_name, raw):
    """Returns the problem as text, or None."""
    decoded = protoc("decode", type_name, proto_file, raw)
    if decoded.returncode:
        return "protoc can't decode it: %s" % decoded.stderr.decode(errors="replace").strip()
    text = decoded.stdout.decode(errors="replace")
    unknown = sorted(set(m.group(0).strip() for m in UNKNOWN_FIELD.finditer(text)))
    if unknown:
        return "fields the schema doesn't list: %s\n%s" % (unknown, text)
    encoded = protoc("encode", type_name, proto_file, decoded.stdout)
    if encoded.returncode:
        return "protoc can't encode it again: %s" % encoded.stderr.decode(errors="replace").strip()
    expected = channels_first(raw) if type_name == MANIFEST else raw
    if encoded.stdout != expected:
        return "bytes change when encoded again\n want %s\n  got %s" % (expected.hex(), encoded.stdout.hex())
    return None


def main():
    dump = sys.argv[1]
    base = {k: v for k, v in os.environ.items() if not k.startswith("NBT_")}
    base["NBT_CARPLAY_INI"] = "/nonexistent/carplay.ini"

    checked, failed = {}, 0
    used = set()
    for name, settings in SETTINGS:
        output = subprocess.run([dump], env={**base, **settings}, check=True, stdout=subprocess.PIPE, universal_newlines=True).stdout
        for line in output.splitlines():
            if not line.startswith("MSG\t"):
                continue
            _, proto_file, type_name, label, body = line.split("\t")
            used.add(type_name)
            key = (type_name, body)
            if key in checked:
                continue
            problem = check(proto_file, type_name, bytes.fromhex(body))
            checked[key] = problem
            if problem:
                failed += 1
                print("FAIL %s, %s, setting %r: %s" % (type_name, label, name, problem))

    declared = set()
    for proto_file in sorted(os.listdir(PROTO_DIR)):
        source = open(os.path.join(PROTO_DIR, proto_file)).read()
        package = re.search(r"^package ([\w.]+);", source, re.M).group(1)
        declared.update("%s.%s" % (package, m) for m in re.findall(r"^message (\w+)", source, re.M))
    missing = used - declared
    if missing:
        failed += 1
        print("FAIL types dump_messages names but no schema declares: %s" % sorted(missing))

    print("%d different messages decoded, %d of %d message types seen directly" % (len(checked), len(used), len(declared)))
    print("Not seen directly (nested in one that is, or checked by no test):")
    for name in sorted(declared - used):
        print("  " + name)
    if not checked:
        failed += 1
        print("FAIL dump_messages printed nothing")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
