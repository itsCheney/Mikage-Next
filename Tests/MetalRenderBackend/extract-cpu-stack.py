"""Extract the production bounded stack writer and position freeze."""
import argparse
from pathlib import Path

def function(text, signature):
    start=text.index(signature);opening=text.index('{',start);depth=1;end=opening+1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[start:end]

p=argparse.ArgumentParser();p.add_argument('--source',required=True,type=Path);p.add_argument('--output',required=True,type=Path)
a=p.parse_args();text=a.source.read_text(encoding='utf-8')
a.output.write_text('\n'.join(function(text,s) for s in ('    void FreezeCodePointer() noexcept',
    '    bool GetTraceUTF8(char* output, tjs_uint capacity, tjs_uint frames)'))+'\n',encoding='utf-8')
