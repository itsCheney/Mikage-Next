#!/usr/bin/env python3
"""Compile the real Primary window-completion paths without the layer manager:
DrawBuffer completion (Blt/CopyRect) and the fixed update-region hook."""
import argparse
from pathlib import Path


def functions(source, signature):
    # Return every definition starting with signature (Blt has two overloads).
    found, start = [], source.find(signature)
    while start >= 0:
        opening = source.index("{", start)
        depth, end = 1, opening + 1
        # Selected methods contain no braces in comments or string literals.
        while depth:
            depth += (source[end] == "{") - (source[end] == "}")
            end += 1
        found.append(source[start:end])
        start = source.find(signature, end)
    if not found:
        raise SystemExit(f"missing production definition: {signature}")
    return found


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--manager-source", required=True, type=Path)
parser.add_argument("--bitmap-source", required=True, type=Path)
parser.add_argument("--output", required=True, type=Path)
args = parser.parse_args()
manager = args.manager_source.read_text(encoding="utf-8")
bitmap = args.bitmap_source.read_text(encoding="utf-8")
blts = functions(bitmap, "bool iTVPBaseBitmap::Blt(tjs_int x,")
if len(blts) != 2:
    raise SystemExit(f"expected two iTVPBaseBitmap::Blt overloads, found {len(blts)}")
# The layer-type Blt overload resolves its blend method through this helper.
layer_type = functions(bitmap, "bool TVPLayerTypeToBltMethod(")
completion = functions(manager, "void tTVPLayerManager::DrawCompleted(")
fixed = functions(manager, "void tTVPLayerManager::NotifyUpdateRegionFixed(")
copy = functions(manager, "bool tTVPDestTexture::CopyRect(")
args.output.write_text(
    "// Generated from LayerManager.cpp and LayerBitmap.cpp; do not edit.\n\n"
    + layer_type[0] + "\n\n"
    + "\n\n".join(blts) + "\n\n"
    + copy[0].replace("tTVPDestTexture::", "TestDestTexture::") + "\n\n"
    + completion[0].replace("tTVPLayerManager::", "TestPrimaryManager::")
                   .replace("tTVPDestTexture", "TestDestTexture") + "\n\n"
    + fixed[0].replace("tTVPLayerManager::", "TestPrimaryManager::") + "\n",
    encoding="utf-8",
)
