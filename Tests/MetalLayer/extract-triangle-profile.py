#!/usr/bin/env python3
"""Compile the production C diagnostic bridge without the iOS host lifecycle."""
import argparse
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--source", required=True, type=Path)
parser.add_argument("--output", required=True, type=Path)
args = parser.parse_args()
source = args.source.read_text(encoding="utf-8")
start = source.index('extern "C" bool MikageKRKRTakeLayerTriangleProfile(')
opening = source.index("{", start)
depth, end = 1, opening + 1
while depth:
    depth += (source[end] == "{") - (source[end] == "}")
    end += 1
args.output.write_text("// Generated from MikageKRKRRuntime.mm; do not edit.\n"
                       + source[start:end] + "\n", encoding="utf-8")
