#!/usr/bin/env python3
import importlib.util,json,os,subprocess,sys,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];sys.dont_write_bytecode=True
spec=importlib.util.spec_from_file_location('diagnostics',ROOT/'scripts/analyze-layer-diagnostics.py')
analysis=importlib.util.module_from_spec(spec);spec.loader.exec_module(analysis)
with tempfile.TemporaryDirectory(prefix='mikage-image-emitter-') as directory:
    exe=Path(directory)/('emitter.exe' if os.name=='nt' else 'emitter')
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-Wall','-Wextra','-Werror',
        '-I',str(ROOT/'Engine/KRKRRuntime/Source/cpp/core/render'),str(Path(__file__).with_name('C2ImageWindowEmitter.cpp')),'-o',str(exe)],check=True)
    lines=subprocess.check_output([str(exe)],text=True,timeout=30).splitlines()
rows=[];works=[];errors=[]
for line in lines:
    if line.startswith('WORK '):
        _,identity,rest=line.split(' ',2);origins,overflow=rest.split('\t')
        e={'event':'layerWorkProfile','unixTime':106,'fields':dict(spanRouteWindowID=identity,transferOrigins=origins,originOverflow=overflow)}
        works.append(e);rows.append(e)
    else:
        assert len(line.encode('utf-8'))<=900
        rows.append({'event':'runtime.message','unixTime':105,'fields':{'message':line}})
images=analysis.layer_images(rows,works,errors)
reads=analysis.cpu_read_aggregates(rows,works,errors)
assert not errors,errors
assert images['complete'] and images['breakdownComplete'] and reads['complete']
assert images['totals']==dict(records=73,readCalls=71,readBytes=71*4096,readWallNS=7100,readWaitNS=6390,
    parameterBytes=768,calls=72,constructors=1,gpuCalls=1,cpuCalls=71,noopCalls=0,errorCalls=0)
assert reads['totals']['bytes']==images['totals']['readBytes']
assert reads['totals']['waitNS']==images['totals']['readWaitNS']
assert images['windows'][1]['footer']['calls']==0
broken=rows.copy();broken.pop(next(i for i,r in enumerate(broken) if '"stage":"invoke"' in r['fields'].get('message','')))
errors=[];assert not analysis.layer_images(broken,works,errors)['complete'] and errors
print('PASS production Image emitter -> parser, 71 CPU + 1 GPU, exact C0/read subset, empty/missing window')
