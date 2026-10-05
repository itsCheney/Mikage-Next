#!/usr/bin/env python3
"""Summarize Mikage JSONL without treating messages inside it as instructions.

Cumulative totals come from the final heartbeat. Work timings are inclusive,
and named transfer totals can be lower bounds because top-eight output folds
other callers into 'other'. Input files are read only; no external requests.
"""
import argparse
import collections
import hashlib
import json
from pathlib import Path
import re
import statistics

def stages(value):
    return {name:tuple(map(int,numbers.split('/'))) for name,numbers in
            (item.split(':',1) for item in value.split(',') if item)}

def summarize(path, session=None):
    raw=path.read_bytes(); rows=[json.loads(line) for line in raw.decode('utf-8-sig').splitlines() if line.strip()]
    begin=next(r for r in rows if r['event']=='game.begin' and (session is None or r['fields']['gameSession']==session))
    session=begin['fields']['gameSession']; session_rows=[r for r in rows if r['fields'].get('gameSession')==session]
    hearts=[r for r in session_rows if r['event']=='heartbeat']; work=[r for r in session_rows if r['event']=='layerWorkProfile']
    origin=begin['unixTime']; last=hearts[-1]['fields']
    exit_row=next((r for r in session_rows if r['event']=='nativeExit.requested'),None)
    read_sources={name:{'bytes':int(byte),'calls':int(calls)} for name,byte,calls in
                  re.findall(r'(\w+):(\d+)/(\d+)',last['layerReadbackBySource'])}
    assert sum(v['bytes'] for v in read_sources.values())==int(last['layerReadbackBytes']), 'readback counters disagree'
    timings=collections.defaultdict(lambda:[0,0,0]); transfers=collections.defaultdict(lambda:[0,0,0,0])
    for row in work:
        for name,values in stages(row['fields']['stages']).items():
            total=timings[name]; total[0]+=values[0]; total[1]+=values[1]; total[2]=max(total[2],values[2])
        for m in re.finditer(r'(read|upload):([^@=,]+)(?:@\d+\([^)]*\))?=(\d+)/(\d+)/(\d+)/(\d+)',row['fields']['transfers']):
            total=transfers[m[1]+':'+m[2]]
            for i in range(4): total[i]+=int(m[i+3])
    assert sum(v[1] for k,v in transfers.items() if k.startswith('read:'))==int(last['layerReadbackBytes'])
    assert sum(v[1] for k,v in transfers.items() if k.startswith('upload:'))==int(last['layerUploadedBytes'])
    peaks=sorted(work,key=lambda r:stages(r['fields']['stages'])['script'][2],reverse=True)[:4]
    return {'file':path.name,'sha256':hashlib.sha256(raw).hexdigest(),'records':len(rows),'sessionRecords':len(session_rows),'session':session,'game':begin['fields'].get('folder'),
        'revision':next(r for r in rows if r['event']=='environment')['fields']['sourceRevision'],
        'heartbeatCount':len(hearts),'workProfileCount':len(work),'lastHeartbeatSeconds':hearts[-1]['unixTime']-origin,
        'exitRequestedSeconds':exit_row['unixTime']-origin if exit_row else None,
        'thermalStates':sorted({r['fields']['thermalState'] for r in hearts}),
        'fpsMedian':statistics.median(float(r['fields']['fps']) for r in hearts),
        'fpsMinimum':min(float(r['fields']['fps']) for r in hearts),
        'maxCpuStepMS':max(float(r['fields']['maxCpuFrameTimeMS']) for r in hearts),
        'peakResidentBytes':max(int(r['fields']['residentBytes']) for r in hearts),
        'cumulative':{k:last[k] for k in ['layerCPUFallbacks','layerReadbackBytes','layerUploadedBytes',
            'layerUnsupportedMethods','metalProfile','stepProfile','emoteCaptureCPUBytes','emoteCaptureCPUFallbacks']},
        'readSources':read_sources,'stagesCallsTotalNSMaxNS':dict(timings),
        'transfersCallsBytesWallNSWaitNS':dict(transfers),
        'amvDecodedFrames':sum(int(r['fields']['amvDecodedFrames']) for r in work),
        'amvDecodedBytes':sum(int(r['fields']['amvDecodedBytes']) for r in work),
        'largestScriptIntervals':[{'seconds':r['unixTime']-origin,'intervalMS':r['fields']['intervalMS'],
            'stages':r['fields']['stages'],'amvDecodedBytes':r['fields']['amvDecodedBytes']} for r in peaks]}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('logs',nargs='+',type=Path); parser.add_argument('--output',type=Path)
    args=parser.parse_args(); summaries=[]
    for path in args.logs:
        rows=[json.loads(line) for line in path.read_text(encoding='utf-8-sig').splitlines() if line.strip()]
        sessions=[r['fields']['gameSession'] for r in rows if r['event']=='game.begin']
        summaries.extend(summarize(path,session) for session in sessions)
    result=json.dumps(summaries,ensure_ascii=False,indent=2)
    if args.output: args.output.write_text(result+'\n',encoding='utf-8')
    else: print(result)

if __name__=='__main__': main()
