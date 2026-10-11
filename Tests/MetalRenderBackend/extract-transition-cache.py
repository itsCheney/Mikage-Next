#!/usr/bin/env python3
"""Compile actual transition cache and stop lifecycle methods against fixture types."""
import argparse
from pathlib import Path

def definition(text, signature):
    start=text.index(signature)
    opening=text.index("{",start)
    depth,end=1,opening+1
    while depth:
        depth+=(text[end]=="{")-(text[end]=="}")
        end+=1
    return text[start:end]

p=argparse.ArgumentParser()
p.add_argument('--source',required=True,type=Path)
p.add_argument('--output',required=True,type=Path)
a=p.parse_args()
s=a.source.read_text(encoding='utf-8')
names=['void AllocateCache','void EnsureCacheAllocated','tjs_uint IncTransitionCacheEnabledCount',
       'void ResizeCache','void DeallocateCache','void CompactCache','tjs_uint IncCacheEnabledCount',
       'tjs_uint DecCacheEnabledCount','void SetCached','void InternalStopTransition']
a.output.write_text('// Extracted production methods. Do not edit.\n'+ '\n\n'.join(
    definition(s,ret+' tTJSNI_BaseLayer::'+name+'(') for ret,name in (n.split(' ',1) for n in names)),encoding='utf-8')
