#!/usr/bin/env python3
"""Compile unchanged production Image algorithms, policy, getter and method list."""
import argparse,re
from pathlib import Path

def block(text,signature):
    start=text.index(signature);opening=text.index('{',start);depth=1;end=opening+1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[start:end]

p=argparse.ArgumentParser();p.add_argument('--core',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
a=p.parse_args();c=a.core
base=(c/'plugins/LayerExBase.hpp').read_text(encoding='utf-8')
scoped=(c/'plugins/ScopedLayerPixels.h').read_text(encoding='utf-8')
header=(c/'plugins/LayerExImage.h').read_text(encoding='utf-8')
source=(c/'plugins/LayerExImage.cpp').read_text(encoding='utf-8')
methods=re.findall(r'NCB_METHOD\((\w+)\)',block(source,'NCB_ATTACH_CLASS_WITH_HOOK(layerExImage, Layer)'))
assert methods==['light','colorize','modulate','noise','generateWhiteNoise']
algorithms=re.sub(r'^#include[^\n]*\n','',source[:source.index('static const char* copyright')],flags=re.M)
pieces=[block(scoped,'class tTVPScopedLayerPixels :')+';',block(base,'struct layerExBase_GL')+';',
        'typedef unsigned char BYTE; typedef unsigned short WORD;',block(header,'class layerExImage :')+';',
        algorithms,'template<> '+block(source,'struct ncbInvocationPolicy<layerExImage>')+';',
        block(source,'NCB_GET_INSTANCE_HOOK(layerExImage)')+';',
        'template<class Registration> void RegisterImageMethods(Registration& registration) {\n'
        'ncbRegistClass<Registration> r(registration,true);\n'+
        '\n'.join('r.Method(TJS_N("'+m+'"),&layerExImage::'+m+');' for m in methods)+'\n}']
result='\n'.join(pieces).replace('tTJSNI_BaseLayer','TestImageLayer').replace('tTJSNI_Layer','TestImageLayer').replace('tTJSNC_Layer','TestImageClass')
a.output.write_text('// Production Image code, native Layer dependency doubled only.\n'+result,encoding='utf-8')
