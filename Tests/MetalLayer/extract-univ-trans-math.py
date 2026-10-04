#!/usr/bin/env python3
"""Compile the production MSL integer helpers as C++ for tvpgl parity checks."""
import argparse
from pathlib import Path


def function(source, name):
    start = source.index("uint " + name + "(")
    opening = source.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end].replace("const device uchar*", "const unsigned char*")


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--source", required=True, type=Path)
parser.add_argument("--output", required=True, type=Path)
args = parser.parse_args()
source = args.source.read_text(encoding="utf-8")
args.output.write_text(
    "// Generated from MetalLayerShaders.h; do not edit.\n"
    "namespace univ_shader {\nusing uint = uint32_t;\nusing std::max;\nusing std::min;\n"
    + "\n\n".join(function(source, name) for name in
                    ["constAlphaSD", "univTransBlendARGB", "univTransPixel",
                     "layerPremulPixel", "layerPsPixel", "layerAlphaToPremulPixel", "layerMaskPixel"])
    + "\n}\n", encoding="utf-8")
