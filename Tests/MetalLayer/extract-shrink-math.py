#!/usr/bin/env python3
"""Extract the production integer shrink helpers for both AvgT widths."""
import argparse
import re
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--source',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args()
s=a.source.read_text(encoding='utf-8').split('// BEGIN SHRINK SCALAR:',1)[1].split('// END SHRINK SCALAR',1)[0]
s=s[s.index('struct ShrinkSum'):]
s=s.replace('texture2d<float,access::read>','const ShrinkSurface&')
s,n=re.subn(r'uint shrinkRead\([^{}]*\)\s*\{[^{}]*\}',
    'inline uint shrinkRead(const ShrinkSurface& source,int x,int y) { return source.Read(x,y); }',s,count=1)
if n!=1:raise ValueError('missing byte transport adapter')
s=s.replace('const device ','const ').replace('constant ','const ').replace('thread ','')
for function in ['void shrinkAdd(', 'ShrinkSum shrinkHorizontal(', 'void shrinkAddRow(', 'uint shrinkVertical(']:
    s=s.replace(function,'inline '+function)
result=['// Generated from production MetalShrinkShaders.h; do not edit.\n#include "LayerShrink.h"\n']
for bits in (32,64):
    result.append('namespace shrink_shader'+str(bits)+' {\nusing uint=uint32_t;using ShrinkAvg=uint'+str(bits)+'_t;\n'
        'using ShrinkAxis=TVPLayerShrinkAxis;\n'
        'struct ShrinkParams { int kind,width,height,sourceTop,sourceRows;uint hu,vu; };\n'
        'struct ShrinkSurface { const uint8_t* bytes;int pitch;uint Read(int x,int y)const {uint v;std::memcpy(&v,bytes+size_t(y)*pitch+size_t(x)*4,4);return v;} };\n'
        '#define TVP_LAYER_SHRINK(name,id) inline constexpr int TVP_SHRINK_##name=id;\n'
        '#define TVP_LAYER_SHRINK_COUNT(count)\n#include "LayerShrinkDefinitions.def"\n#undef TVP_LAYER_SHRINK_COUNT\n#undef TVP_LAYER_SHRINK\n'
        +s+'\n}\n')
a.output.write_text(''.join(result),encoding='utf-8')
