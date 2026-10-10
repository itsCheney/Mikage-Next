"""Compile the real async admission, worker decode and completion branches."""
import argparse
from pathlib import Path
def function(text, signature):
    start=text.index(signature);opening=text.index('{',start);depth=1;end=opening+1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[start:end]
p=argparse.ArgumentParser();p.add_argument('--source',required=True,type=Path);p.add_argument('--output',required=True,type=Path)
a=p.parse_args();s=a.source.read_text(encoding='utf-8')
names=['static int TVPLoadGraphicAsync_SizeCallback(', 'static void* TVPLoadGraphicAsync_ScanLineCallback(',
       'static void TVPLoadGraphicAsync_MetaInfoPushCallback(', 'void tTVPAsyncImageLoader::HandleLoadedImage()',
       'void tTVPAsyncImageLoader::PushLoadQueue(', 'bool tTVPAsyncImageLoader::PushPrefetch(',
       'void tTVPAsyncImageLoader::LoadingThread()', 'void tTVPAsyncImageLoader::LoadImageFromCommand(']
a.output.write_text('\n\n'.join(function(s,n) for n in names)+'\n',encoding='utf-8')
