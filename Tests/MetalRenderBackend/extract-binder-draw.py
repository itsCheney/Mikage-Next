#!/usr/bin/env python3
"""Extract production binder traversal, copy and forwarding methods for fixtures."""
import argparse
from pathlib import Path


def definition(text, signature):
    start = text.index(signature)
    opening = text.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


p = argparse.ArgumentParser()
p.add_argument("--source", required=True, type=Path)
p.add_argument("--output", required=True, type=Path)
a = p.parse_args()
s = a.source.read_text(encoding="utf-8")
macro_start = s.index("#define TVP_LAYER_FOR_EACH_CHILD_BEGIN(")
macro_end = s.index("//---------------------------------------------------------------------------", macro_start)
methods = [
    "void tTJSNI_BaseLayer::DrawSelf(",
    "void tTJSNI_BaseLayer::CopySelfForRect(",
    "void tTJSNI_BaseLayer::CopySelf(",
    "void tTJSNI_BaseLayer::InternalDrawNoCache_CPU(",
    "void tTJSNI_BaseLayer::Draw(",
    "tTVPBaseTexture* tTJSNI_BaseLayer::GetDrawTargetBitmap(",
    "void tTJSNI_BaseLayer::DrawCompleted(",
]
a.output.write_text(
    "// Extracted production methods and child iteration. Do not edit.\n"
    + s[macro_start:macro_end]
    + "\n\n".join(definition(s, signature) for signature in methods)
    + "\n#undef TVP_LAYER_FOR_EACH_CHILD_BEGIN\n#undef TVP_LAYER_FOR_EACH_CHILD_END\n",
    encoding="utf-8",
)
