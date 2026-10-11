#!/usr/bin/env python3
"""Exercise production graphic load/cache/compaction with counted storage.

The boundary doubles supply decoded bitmap bytes, metadata and an LRU store;
the actual load function and compaction policy are extracted from production.
This checks cache keys, ownership and duplicate opens, not image codecs.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

def definition(text, signature):
    start=text.index(signature); brace=text.index('{',start); depth=1; end=brace+1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}'); end+=1
    return text[start:end]

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source',type=Path,required=True)
    p.add_argument('--work-dir',type=Path)
    p.add_argument('--cxx',default=os.environ.get('CXX'))
    a=p.parse_args(); compiler=a.cxx or shutil.which('clang++') or shutil.which('g++')
    if not compiler: p.error('C++17 compiler required')
    source=(a.source/'core/media/image/TVPGraphicsLoader.cpp').read_text(encoding='utf-8')
    production='\n'.join(definition(source,s)+(';' if s.startswith('struct ') else '') for s in ['static void TVPTrimGraphicCache(',
        'static void TVPCheckGraphicCacheLimit()', 'static bool TVPCommitGraphicCache(', 'void TVPClearGraphicCache()',
        'bool TVPCheckImageCache(',
        'struct tTVPClearGraphicCacheCallback', 'void TVPTagGraphicDiagnosticAsset(',
        'int TVPLoadGraphic(iTVPBaseBitmap* dest,'])
    root=Path(__file__).resolve().parent.parent
    with tempfile.TemporaryDirectory(prefix='mikage-graphic-load-',dir=a.work_dir) as folder:
        unit=Path(folder)/'test.cpp'; exe=Path(folder)/('test.exe' if os.name=='nt' else 'test')
        unit.write_text((root/'Tests/KRKRRuntime/GraphicsLoadHarness.hpp').read_text(encoding='utf-8')+'\n'+production+'\n'+
                        (root/'Tests/KRKRRuntime/GraphicsLoadHarness.cpp').read_text(encoding='utf-8'),encoding='utf-8')
        subprocess.run([compiler,'-std=c++17','-O2','-I'+str(a.source/'core/render'),'-I'+str(a.source/'core/media/image'),str(unit),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
    return 0

if __name__=='__main__': raise SystemExit(main())
