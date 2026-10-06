#!/usr/bin/env python3
"""Compile the production MSL integer helpers as C++ for tvpgl parity checks."""
import argparse
import re
from pathlib import Path


def function(source, name):
    # AlphaSD uses a forward declaration before layerPixel. Extract the actual
    # definition, never the prototype followed by an unrelated function body.
    match = re.search(r"\buint\s+" + re.escape(name) + r"\([^;{]*\)\s*\{", source)
    if not match:
        raise ValueError("Missing production integer helper: " + name)
    start = match.start()
    opening = match.end() - 1
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    # The shared extracted helpers are consumed by both transition and P1A
    # test translation units. Inline avoids duplicate external definitions.
    return "inline " + source[start:end].replace("const device uchar*", "const unsigned char*")


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--source", required=True, type=Path)
parser.add_argument("--output", required=True, type=Path)
args = parser.parse_args()
source = args.source.read_text(encoding="utf-8")
args.output.write_text(
    "// Generated from MetalLayerShaders.h; do not edit.\n"
    "#include \"LayerRenderOperation.h\"\n"
    "namespace univ_shader {\nusing uint = uint32_t;\nusing std::max;\nusing std::min;\n"
    + "\n\n".join(function(source, name) for name in
                    ["constAlphaSD", "constAlphaSDDestAlpha", "univTransBlendARGB", "univTransPixel",
                     "layerPremulPixel", "layerPsPixel", "layerAlphaToPremulPixel", "layerMaskPixel", "layerAddPixel",
                     "layerP1APixel", "layerPremulToAlphaPixel", "layerGammaPixel", "layerPsP1BPixel"])
    + "\n}\n", encoding="utf-8")
