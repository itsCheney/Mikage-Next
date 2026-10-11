#!/usr/bin/env python3
"""Compile the full production plugin with real VM/bitmap test boundaries."""
import argparse
from pathlib import Path


def function(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--source", required=True, type=Path)
parser.add_argument("--scoped-source", required=True, type=Path)
parser.add_argument("--layer-source", required=True, type=Path)
parser.add_argument("--output", required=True, type=Path)
args = parser.parse_args()
plugin = args.source.read_text(encoding="utf-8")
plugin = "\n".join(line for line in plugin.splitlines()
                   if not line.startswith('#include "ncbind/')
                   and not line.startswith('#include "ScopedLayerPixels.h"')
                   and not line.startswith("NCB_ATTACH_FUNCTION("))
# Compile the original AvgT expressions at both platform widths; no pixel or
# geometry expression is rewritten by the oracle extraction.
plugin = plugin.replace("struct ShrinkCopy :", "template<typename OracleAvgT=unsigned long> struct ShrinkCopy :")
plugin = plugin.replace("typedef unsigned long AvgT;", "typedef OracleAvgT AvgT;")
scoped = function(args.scoped_source.read_text(encoding="utf-8"), "class tTVPScopedLayerPixels :") + ";\n"
layer = args.layer_source.read_text(encoding="utf-8")
bindings = function(layer, "struct ShrinkLayerBindings") + " shrinkLayerBindings;\n"
gate = function(layer, "bool TVPGetCanonicalShrinkLayer(") + "\n"
output = "// Generated from production shrinkCopy, scoped pixels, and callback gate.\n#include \"CPUConsumerTrace.h\"\n" + scoped + bindings + gate + plugin
output = output.replace("tTJSNI_BaseLayer", "TestShrinkNativeLayer").replace("tTJSNC_Layer", "TestShrinkClass")
args.output.write_text(output, encoding="utf-8")
