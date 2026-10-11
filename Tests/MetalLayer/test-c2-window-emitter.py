"""Compile the production emitter and reconcile its output with the analyzer."""
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[2]
sys.dont_write_bytecode=True
spec=importlib.util.spec_from_file_location('diagnostics',ROOT/'scripts/analyze-layer-diagnostics.py')
analysis=importlib.util.module_from_spec(spec);spec.loader.exec_module(analysis)
with tempfile.TemporaryDirectory(prefix='mikage-c2-emitter-') as directory:
    exe=Path(directory)/('emitter.exe' if os.name=='nt' else 'emitter')
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-Wall','-Wextra','-Werror',
        '-I',str(ROOT/'Engine/KRKRRuntime/Source/cpp/core/render'),str(Path(__file__).with_name('C2WindowEmitter.cpp')),
        '-o',str(exe)],check=True)
    lines=subprocess.check_output([str(exe)],text=True).splitlines()
rows=[];works=[];errors=[]
for line in lines:
    if line.startswith('WORK '):
        _,identity,rest=line.split(' ',2)
        origins,overflow=rest.split('\t')
        event={'event':'layerWorkProfile','unixTime':106,'fields':{'spanRouteWindowID':identity,'transferOrigins':origins,'originOverflow':overflow}}
        works.append(event);rows.append(event)
    else:
        assert len(line.encode('utf-8'))<=900
        rows.append({'event':'runtime.message','unixTime':105,'fields':{'message':line}})
spans=analysis.layer_spans(rows,errors,selected_work=works)
reads=analysis.cpu_read_aggregates(rows,works,errors)
details=analysis.cpu_consumers(rows,errors)
assert not errors,errors
assert spans['complete'] and spans['version']==3 and spans['representativeCoverageComplete']
assert spans['totals']['calls']==75 and spans['totals']['gpuCalls']==74 and spans['totals']['cpuCalls']==1
assert reads['complete'] and reads['namedConsumersComplete']
assert reads['totals']==dict(calls=71,bytes=1136,wallNS=7100,waitNS=3550)
assert reads['windows'][0]['originReconciliation'][0]['status']=='match'
assert reads['windows'][0]['totalC0Reconciliation']['status']=='match'
assert details['sampledReadTotals']['calls']==32 and details['sampling']['readExceeded']==39
assert not details['wholeTextureReadDetailsCovered']
print('PASS production emitter -> analyzer: 75 v3 routes / five protected methods, 71 complete reads / 32 details, exact C0, empty window, no double counting')
