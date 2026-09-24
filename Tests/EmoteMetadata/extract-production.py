#!/usr/bin/env python3
"""Extract the production PSB reader without its renderer/decompressor dependencies."""
import argparse
from pathlib import Path


def function(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth, end = 1, opening + 1
    # These known reader functions contain no braces in string literals.
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--source", required=True, type=Path)
parser.add_argument("--output", required=True, type=Path)
args = parser.parse_args()
source = args.source.read_text(encoding="utf-8")
signatures = [
    "tTJSVariant emotefile::root()",
    "tTJSVariant emotefile::readVariableFrameList(",
    "tTJSVariant emotefile::readAllObjs(",
    "uint32_t emotefile::readListInfo(",
    "void emotefile::refreshListInfo(",
    "bool emotefile::parseObject(",
    "bool emotefile::parseList(",
]
args.output.write_text("// Generated from emotefile.cpp; do not edit.\n" +
                       "\n\n".join(function(source, s) for s in signatures) + "\n",
                       encoding="utf-8")
