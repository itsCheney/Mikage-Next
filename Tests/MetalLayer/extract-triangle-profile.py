#!/usr/bin/env python3
"""Compile the production C diagnostic bridge without the iOS host lifecycle."""
import argparse
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--source", required=True, type=Path)
parser.add_argument("--output", required=True, type=Path)
args = parser.parse_args()
source = args.source.read_text(encoding="utf-8")
def extract(name):
    start = source.index('extern "C" bool ' + name + '(')
    opening = source.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]

args.output.write_text("// Generated from MikageKRKRRuntime.mm; do not edit.\n"
    + extract("MikageKRKRTakeLayerTriangleProfile") + "\n"
    + extract("MikageKRKRTakeLayerWorkProfile") + "\n", encoding="utf-8")
