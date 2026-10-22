#!/usr/bin/env python3
"""Extract the unchanged production PNG encoder of layerExSave for portable tests."""
import argparse
from pathlib import Path


def definition(text, signature):
    start = text.index(signature)
    opening = text.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    if signature.startswith("struct "):
        end = text.index(";", end) + 1
    return text[start:end]


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--source", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
source = (args.source / "plugins/LayerExSave.cpp").read_text(encoding="utf-8")
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text("\n\n".join(definition(source, signature) for signature in [
    "void PutBE32(", "void PutBytes(", "struct Progress", "struct PngOptions",
    "void PutPngChunk(", "void PutPngXY(", "void PutPngHeader(", "inline tjs_uint8 Paeth(",
    "void FilterRow(", "bool Deflate(", "bool EncodePng("]) + "\n", encoding="utf-8")
