#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Globulation 2 Authors
"""Quantize a cortex-mlp-f32-v1 JSON net to the cortex-i16f16-v1 blob.

Reads the trainer's f32 JSON (see docs/AI/cortex/ML_CONTRACT.md) and writes the
versioned I16F16 binary defined in FORMAT.md. Pure numpy / stdlib so the integer
math here can never drift from int_ref.py and CortexNet.cpp.
"""
import argparse
import json
import struct
import sys

MAGIC = 0x434E5831  # 'CNX1' little-endian
VERSION = 1
FRAC_BITS = 16
SCALE = 1 << FRAC_BITS
INT32_MIN = -(1 << 31)
INT32_MAX = (1 << 31) - 1


def to_i16f16(v):
    """Quantize float v to I16F16 (round-half-away-from-zero, clamp to int32)."""
    if v >= 0:
        q = int(v * SCALE + 0.5)
    else:
        q = -int(-v * SCALE + 0.5)
    if q < INT32_MIN:
        q = INT32_MIN
    elif q > INT32_MAX:
        q = INT32_MAX
    return q


def load_f32(path):
    with open(path, "r") as f:
        net = json.load(f)
    if net.get("format") != "cortex-mlp-f32-v1":
        sys.exit("error: input is not cortex-mlp-f32-v1 (got %r)" % net.get("format"))
    if net.get("activation") != "relu":
        sys.exit("error: only relu activation supported (got %r)" % net.get("activation"))
    return net


def quantize(net):
    arch = net["arch"]
    layers = net["layers"]
    if len(layers) != len(arch) - 1:
        sys.exit("error: arch/layers mismatch: arch=%r layers=%d" % (arch, len(layers)))

    out = bytearray()
    out += struct.pack("<5I", MAGIC, VERSION, FRAC_BITS, len(layers), len(arch))
    out += struct.pack("<%dI" % len(arch), *arch)

    for li, layer in enumerate(layers):
        in_dim = arch[li]
        out_dim = arch[li + 1]
        W = layer["W"]
        b = layer["b"]
        if len(W) != out_dim or any(len(row) != in_dim for row in W):
            sys.exit("error: layer %d W shape != [%d][%d]" % (li, out_dim, in_dim))
        if len(b) != out_dim:
            sys.exit("error: layer %d b len != %d" % (li, out_dim))
        out += struct.pack("<2I", in_dim, out_dim)
        flat_w = [to_i16f16(W[o][i]) for o in range(out_dim) for i in range(in_dim)]
        out += struct.pack("<%di" % len(flat_w), *flat_w)
        flat_b = [to_i16f16(b[o]) for o in range(out_dim)]
        out += struct.pack("<%di" % len(flat_b), *flat_b)

    return bytes(out)


def main():
    ap = argparse.ArgumentParser(description="Quantize cortex-mlp-f32-v1 -> cortex-i16f16-v1 blob")
    ap.add_argument("json_in", help="f32 net JSON (cortex-mlp-f32-v1)")
    ap.add_argument("blob_out", help="output I16F16 blob")
    args = ap.parse_args()

    net = load_f32(args.json_in)
    blob = quantize(net)
    with open(args.blob_out, "wb") as f:
        f.write(blob)
    print("wrote %s (%d bytes), arch=%s" % (args.blob_out, len(blob), net["arch"]))


if __name__ == "__main__":
    main()
