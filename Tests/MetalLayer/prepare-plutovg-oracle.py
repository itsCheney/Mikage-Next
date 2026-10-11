#!/usr/bin/env python3
"""Copy the hash-pinned upstream tree and strictly apply the production patch."""
from pathlib import Path
import argparse, re, shutil, hashlib, tarfile, urllib.request, json
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--source',type=Path)
p.add_argument('--archive',type=Path)
p.add_argument('--download',action='store_true')
p.add_argument('--patch',required=True,type=Path)
p.add_argument('--output',required=True,type=Path)
a=p.parse_args()
if a.source is None or not (a.source/'include/plutovg.h').is_file():
    archive=a.archive or a.output.parent/'plutovg-1.3.3.tar.gz'
    if not archive.is_file():
        if not a.download: raise SystemExit('Required plutovg archive/source missing; use --download to fetch pinned v1.3.3')
        archive.parent.mkdir(parents=True,exist_ok=True)
        archive.write_bytes(urllib.request.urlopen('https://github.com/sammycage/plutovg/archive/refs/tags/v1.3.3.tar.gz',timeout=90).read())
    if hashlib.sha512(archive.read_bytes()).hexdigest()!='57349751bfc9020e63042f472d828ddc509f724d0c82cdaf34e2047f6333de192c5a4cc6aed376e98a000bd68b3b05f1c00ca84bf7163a16c6c2860bc33fb8d0':
        raise SystemExit('Pinned official plutovg 1.3.3 SHA512 mismatch')
    requested=a.source
    a.source=a.output.parent/'upstream'/'plutovg-1.3.3'
    a.source.parent.mkdir(parents=True,exist_ok=True)
    with tarfile.open(archive) as tar:
        for entry in tar.getmembers():
            target=(a.source.parent/entry.name).resolve()
            if not target.is_relative_to(a.source.parent.resolve()): raise SystemExit('Unsafe archive member')
        tar.extractall(a.source.parent,filter='data')
    if requested and requested!=a.source:
        shutil.copytree(a.source,requested,dirs_exist_ok=True);a.source=requested
if not (a.source/'include/plutovg.h').is_file():
    raise SystemExit('Required official plutovg 1.3.3 source is missing')
manifest=json.loads(Path(__file__).with_name('plutovg-1.3.3-source-hashes.json').read_text(encoding='utf-8'))
for name,digest in manifest.items():
    path=a.source/name
    if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest()!=digest:
        raise SystemExit('Pinned upstream oracle source SHA256 mismatch: '+name)
shutil.copytree(a.source,a.output,dirs_exist_ok=True)
lines=a.patch.read_text(encoding='utf-8').splitlines(keepends=True)
i=0
while i<len(lines):
    if not lines[i].startswith('--- a/'):
        raise SystemExit('Unexpected patch header: '+lines[i])
    name=lines[i][6:].strip(); i+=2
    path=a.output/name
    old=path.read_text(encoding='utf-8').splitlines(keepends=True)
    out=[];cursor=0
    while i<len(lines) and lines[i].startswith('@@ '):
        m=re.match(r'@@ -(\d+)(?:,\d+)? \+\d+(?:,\d+)? @@',lines[i])
        if not m: raise SystemExit('Malformed hunk')
        start=int(m.group(1))-1;out+=old[cursor:start];cursor=start;i+=1
        while i<len(lines) and not lines[i].startswith(('@@ ','--- a/')):
            marker,text=lines[i][0],lines[i][1:]
            if marker in ' -':
                if cursor>=len(old) or old[cursor]!=text:
                    raise SystemExit(f'Patch source mismatch at {name}:{cursor+1}')
                cursor+=1
            if marker in ' +':out.append(text)
            i+=1
    out+=old[cursor:];path.write_text(''.join(out),encoding='utf-8')
print('Applied production span capture patch to pinned upstream source')
