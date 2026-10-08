#!/usr/bin/env python3
"""Compile production LayerEx/NCBind boundary code with VM/texture/canvas doubles.

Only dependencies are doubled. No access, binding, clip or drawing statement is
rewritten; actual plutovg raster fidelity belongs to the native device checks.
"""
import argparse
from pathlib import Path


def block(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--base-source", required=True, type=Path)
p.add_argument("--draw-source", required=True, type=Path)
p.add_argument("--header-source", required=True, type=Path)
p.add_argument("--scoped-source", required=True, type=Path)
p.add_argument("--output", required=True, type=Path)
p.add_argument("--capture-output", type=Path)
a = p.parse_args()
base = a.base_source.read_text(encoding="utf-8")
draw = a.draw_source.read_text(encoding="utf-8")
header = a.header_source.read_text(encoding="utf-8")
scoped = a.scoped_source.read_text(encoding="utf-8")
pieces = [block(scoped, "class tTVPScopedLayerPixels :") + ";",
          block(base, "struct layerExBase_GL") + ";",
          block(header, "class LayerExDraw :") + ";"]
pieces.append(block(draw, "class LayerExDraw::InvocationPixels") + ";")
functions = ["void LayerExDraw::updateRect(", "LayerExDraw::LayerExDraw(",
             "LayerExDraw::~LayerExDraw(", "void LayerExDraw::destroyCanvas(",
             "void LayerExDraw::finishPixels(", "void LayerExDraw::reset(",
             "GdipImage* LayerExDraw::getImageForBridge(",
             "void LayerExDraw::updateViewTransform(", "void LayerExDraw::updateTransform(",
             "void LayerExDraw::clear(", "void LayerExDraw::createRecord(",
             "void LayerExDraw::recreateRecord(", "void LayerExDraw::destroyRecord(",
             "void LayerExDraw::setRecord(", "bool LayerExDraw::redraw(",
             "GdipImage* LayerExDraw::getRecordImage(", "bool LayerExDraw::redrawRecord(",
             "bool LayerExDraw::saveRecord(", "RectF LayerExDraw::measureString(",
             "RectF LayerExDraw::measureStringInternal("]
for view in ("ViewTransform", "Transform"):
    for verb in ("set", "reset", "rotate", "scale", "translate"):
        functions.append("void LayerExDraw::" + verb + view + "(")
pieces.extend(block(draw, signature) for signature in functions)
pieces.extend(["template<class T>\n" + block(draw, "class GdipWrapper") + ";",
               "template<class T>\n" + block(draw, "struct GdipTypeConvertor") + ";",
               block(draw, "bool IsArray(const tTJSVariant& var)\n{") + "\n",
               "template<class T>\n" + block(draw, "struct MatrixConvertor") + ";"])
pieces.append("NCB_TYPECONV_DSTMAP_SET(const GdipMatrix*, MatrixConvertor<const GdipMatrix>, true);")
pieces.append("template<>\n" + block(draw, "struct ncbInvocationPolicy<LayerExDraw>") + ";")
pieces.append(block(draw, "NCB_GET_INSTANCE_HOOK(LayerExDraw)") + ";")
pieces.append(block(draw, "static tjs_error GetRecordImage(") + "\n")
output = "// Generated from production C2A boundary code.\n" + "\n".join(pieces)
output = output.replace("tTJSNI_BaseLayer", "TestDrawNativeLayer").replace("tTJSNI_Layer", "TestDrawNativeLayer")
output = output.replace("tTJSNC_Layer", "TestDrawClass")
a.output.write_text(output, encoding="utf-8")
if a.capture_output:
    # Compile the same dispatch/policy and drawing functions with real pinned
    # plutovg. Only the native Layer dependency is substituted, as in C2A.
    extra=[block(header,"class Appearance\n{")+";",
           block(header,"class GdipImage\n{")+";",block(header,"class Path\n{")+";"]
    cap=output
    cap=cap.replace("// Generated from production C2A boundary code.","// Generated production C2B policy/drawing with real plutovg.")
    cap=cap.replace("class LayerExDraw :", "class LayerExDraw :",1)
    # Add complete capture class before functions that use it.
    insertion=cap.index("void LayerExDraw::updateRect(")
    cap=cap[:insertion]+block(draw,"class LayerExDraw::InvocationSpanCapture")+";\n"+cap[insertion:]
    cap="\n".join(extra)+"\n"+cap
    more=["Appearance::Appearance(","Appearance::~Appearance(","Appearance* Appearance::Clone(",
          "void Appearance::clear(","Path::Path(","Path::~Path(",
          "GdipImage::~GdipImage(","GdipImage* GdipImage::Clone(",
          "void LayerExDraw::deferCapturedUpdate(","bool LayerExDraw::spanCaptureStateSafe(",
          "bool LayerExDraw::spanCaptureTargetAliased(","RectF LayerExDraw::drawPath(",
          "RectF LayerExDraw::getPathExtents(","void LayerExDraw::draw(","void LayerExDraw::fill(",
          "RectF LayerExDraw::_drawPath(","RectF LayerExDraw::drawLine(",
          "RectF LayerExDraw::drawImageStretch(","RectF LayerExDraw::drawImageAffine("]
    cap+="\n"+"\n".join(block(draw,signature) for signature in more)+"\n"
    cap+="NCB_TYPECONV_DSTMAP_SET(const Appearance*, GdipTypeConvertor<const Appearance>, true);\n"
    cap+="NCB_TYPECONV_DSTMAP_SET(const Path*, GdipTypeConvertor<const Path>, true);\n"
    cap+="NCB_TYPECONV_DSTMAP_SET(GdipImage*, GdipTypeConvertor<GdipImage>, true);\n"
    a.capture_output.write_text(cap,encoding="utf-8")
