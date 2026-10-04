#!/usr/bin/env python3
"""Compile the real bitmap overwrite transaction without the font rasterizers."""
import argparse
from pathlib import Path


def function(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth, end = 1, opening + 1
    # These selected methods contain no braces in comments or string literals.
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--source", required=True, type=Path)
parser.add_argument("--layer-source", required=True, type=Path)
parser.add_argument("--trans-source", required=True, type=Path)
parser.add_argument("--output", required=True, type=Path)
args = parser.parse_args()
source = args.source.read_text(encoding="utf-8")
signatures = [
    "bool tTVPNativeBaseBitmap::AssignTexture(",
    "bool tTVPNativeBaseBitmap::CopyFromGPUTarget(",
    "bool tTVPNativeBaseBitmap::CopyFromGPUTargetRegion(",
    "tjs_uint tTVPNativeBaseBitmap::GetWidth() const",
    "tjs_uint tTVPNativeBaseBitmap::GetHeight() const",
    "tjs_uint tTVPNativeBaseBitmap::GetBPP() const",
    "tjs_int tTVPNativeBaseBitmap::GetPitchBytes() const",
    "const void* tTVPNativeBaseBitmap::GetScanLine(",
    "void* tTVPNativeBaseBitmap::GetScanLineForWrite(",
    "void tTVPNativeBaseBitmap::Independ()",
    "void tTVPNativeBaseBitmap::IndependNoCopy()",
    "void tTVPNativeBaseBitmap::Recreate()",
    "void tTVPNativeBaseBitmap::Recreate(tjs_uint",
    "iTVPTexture2D* tTVPNativeBaseBitmap::GetTextureForRender(",
]
layer_source = args.layer_source.read_text(encoding="utf-8")
layer_signatures = [
    "iTVPTexture2D* tTJSNI_BaseLayer::GetMainImageTextureForCPUAccess(",
    "bool tTJSNI_BaseLayer::CopyMainImageFromGPUTarget(",
    "bool tTJSNI_BaseLayer::CopyMainImageFromGPUTargetRegion(",
    "bool tTJSNI_BaseLayer::CopyMainImageFromCPU(",
]
trans_source=args.trans_source.read_text(encoding="utf-8")
trans_signatures=["tTVPScanLineProviderForBaseBitmap::tTVPScanLineProviderForBaseBitmap(",
    "tTVPScanLineProviderForBaseBitmap::~tTVPScanLineProviderForBaseBitmap()",
    "void tTVPScanLineProviderForBaseBitmap::Attach("] + [
    "tjs_error tTVPScanLineProviderForBaseBitmap::"+name+"(" for name in
    ["AddRef","Release","GetWidth","GetHeight","GetPixelFormat","GetPitchBytes","GetScanLine","GetScanLineForWrite"]
] + ["iTVPTexture2D* tTVPScanLineProviderForBaseBitmap::GetTexture()",
     "iTVPTexture2D* tTVPScanLineProviderForBaseBitmap::GetTextureForRender()"]
args.output.write_text(
    "// Generated from LayerBitmap.cpp and tjsNativeLayer.cpp; do not edit.\n\n"
    + "\n\n".join(function(source, signature) for signature in signatures) + "\n\n"
    + "\n\n".join(function(layer_source, signature).replace("tTJSNI_BaseLayer::", "TestLayerCopy::")
                    for signature in layer_signatures) + "\n\n"
    + "\n\n".join(function(trans_source,signature) for signature in trans_signatures) + "\n",
    encoding="utf-8",
)
