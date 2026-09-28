#!/usr/bin/env python3
"""Extract unchanged production input/attachment methods for portable tests."""
import argparse
from pathlib import Path
import re


def definition(text, signature):
    start = text.index(signature)
    opening = text.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--source", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
render = args.source / "core/render"
manager = (render / "LayerManager.cpp").read_text(encoding="utf-8")
header = (render / "LayerManager.h").read_text(encoding="utf-8")
basic = (render / "DrawDevice.cpp").read_text(encoding="utf-8")
d3d = (args.source / "plugins/DrawDeviceD3D/DrawDeviceD3D.cpp").read_text(encoding="utf-8")
window = (args.source / "core/main/TVPWindow.h").read_text(encoding="utf-8")

args.output.mkdir(parents=True, exist_ok=True)
members = re.search(r"bool PointerPresentationHitTestingEnabled\s*=\s*[^;]+;", header).group()
members += "\n" + definition(header, "void SetPointerPresentationHitTestingEnabled(bool enabled)")
members += "\n" + definition(header, "class iTVPLayerTreeOwner* GetLayerTreeOwner() const")
(args.output / "ProductionManagerMembers.inc").write_text(members, encoding="utf-8")
(args.output / "ProductionWindowMembers.inc").write_text(
    definition(window, "bool HasNativePointerPresentation() const"), encoding="utf-8")

methods = []
for name in ["RegisterSelfToWindow", "UnregisterSelfFromWindow", "NotifyMouseCursorChange",
             "SetMouseCursor", "NotifyHintChange", "SetHint", "SetLayerTreeOwner",
             "PrimaryClick", "PrimaryDoubleClick", "PrimaryMouseDown", "PrimaryMouseUp",
             "PrimaryMouseMove", "ReleaseCapture", "PrimaryTouchDown"]:
    methods.append(definition(manager, "void tTVPLayerManager::" + name + "("))
methods.append(definition(manager, "tTJSNI_BaseLayer* tTVPLayerManager::GetMostFrontChildAt("))
for source, cls in [(basic, "tTVPBasicDrawDevice"), (d3d, "DrawDeviceD3D")]:
    for name in ["SetWindowInterface", "UpdatePointerPresentationHitTesting", "AddLayerManager",
                 "RemoveLayerManager"]:
        methods.append(definition(source, "void " + cls + "::" + name + "("))
    methods.append(definition(source, cls + "::~" + cls + "()"))
(args.output / "ProductionInputMethods.inc").write_text("\n\n".join(methods), encoding="utf-8")
