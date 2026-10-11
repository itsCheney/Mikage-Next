#!/usr/bin/env python3
"""Compile the production extrans MSL scalar functions with a byte transport adapter."""
import argparse
import re
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--source', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
s = a.source.read_text(encoding='utf-8')
s = s.split('// BEGIN TRANSITION SCALAR:', 1)[1].split('// END TRANSITION SCALAR', 1)[0]
s = s[s.index('uint transitionRead('):]
s = s.replace('texture2d<float,access::read>', 'const TransitionSurface&')
s, count = re.subn(r'uint transitionRead\([^{}]*\)\s*\{[^{}]*\}',
    'inline uint transitionRead(const TransitionSurface& s,int x,int y) { return s.Read(x,y); }', s, count=1)
if count != 1:
    raise ValueError('missing texture byte adapter')
s = s.replace('constant TransitionParams&', 'const TVPLayerTransitionParams&').replace('const device ', 'const ')
s = s.replace('uint transitionBlend(', 'inline uint transitionBlend(').replace('uint transitionPixel(', 'inline uint transitionPixel(')
a.output.write_text('// Generated from production MetalTransitionShaders.h; do not edit.\n'
    '#include "LayerTransition.h"\nnamespace transition_shader {\n'
    'using uint=uint32_t; using ushort=uint16_t; using uchar=uint8_t; using std::clamp;\n'
    'using univ_shader::constAlphaSD; using univ_shader::constAlphaSDDestAlpha; using univ_shader::univTransBlendARGB;\n'
    '#define TVP_LAYER_TRANSITION(name,id,text) inline constexpr int TVP_TRANSITION_##name=id;\n'
    '#define TVP_LAYER_TRANSITION_COUNT(count)\n#include "LayerTransitionDefinitions.def"\n'
    '#undef TVP_LAYER_TRANSITION_COUNT\n#undef TVP_LAYER_TRANSITION\n'
    'struct TransitionSurface { const uint8_t* bytes; int pitch; uint Read(int x,int y) const {\n'
    'uint v; std::memcpy(&v,bytes+size_t(y)*pitch+size_t(x)*4,4); return v; } };\n'
    + s + '\n}\n', encoding='utf-8')
