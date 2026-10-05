#!/usr/bin/env python3
"""Extract exact XP3 helper VM/cache teardown methods for lifetime regression."""
import argparse
from pathlib import Path


def definition(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--source", required=True, type=Path)
parser.add_argument("--output", required=True, type=Path)
args = parser.parse_args()
source = args.source.read_text(encoding="utf-8")
args.output.write_text(
    "// Generated production XP3 session teardown; do not edit.\n"
    + definition(source, "struct XP3FilterDecoder") + ";\n"
    + "static std::map<uint64_t,XP3FilterDecoder*> _thread_decoders;\n"
    + "static std::vector<XP3FilterDecoder*> _cached_decoders;\n"
    + "static tTJSCriticalSection _decoders_mtx;\n"
    + "static ttstr sXP3FilterScript;\n"
    + "static bool _ManagedDecoderInited=false,_ManagedFilterInited=false;\n"
    + "\n\n".join(definition(source, signature) for signature in [
        "static void ClearXP3Decoders()", "void TVPResetXP3FilterSession()",
        "void TVPSetXP3FilterScript(",
    ]) + "\n", encoding="utf-8")
