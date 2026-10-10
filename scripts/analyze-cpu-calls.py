#!/usr/bin/env python3
"""Read-only bounded CPU call attribution; inclusive wall times are not additive CPU."""
import argparse
import collections
import json
from pathlib import Path
import shlex

KINDS=('step','vm','image','image.cacheHit','image.decode','script.storage','kag.load',
       'kag.read','kag.cacheHit','kag.cacheMiss','kag.labelBuild','kag.nextTag')

def summarize(events):
    windows=collections.OrderedDict();errors=[]
    for line,event in enumerate(events,1):
        message=event.get('fields',{}).get('message','')
        if not isinstance(message,str) or not message.startswith(('cpu.slow ','cpu.summary ','cpu.aggregate ')):
            continue
        try:
            if len(message.encode('utf-8'))>960:raise ValueError('line exceeds byte cap')
            tokens=shlex.split(message);prefix=tokens.pop(0);pairs=[t.split('=',1) for t in tokens]
            fields=dict(pairs)
            if len(fields)!=len(pairs):raise ValueError('duplicate field')
            stack=fields.get('stack','');kind=fields.get('kind')
            for k,v in list(fields.items()):
                if k not in ('stack','kind'):
                    fields[k]=int(v)
                    if not 0<=fields[k]<2**64:raise ValueError('unsigned field range')
            if fields['v']!=1:raise ValueError('unsupported version')
            required={'v','epoch','window'}
            if prefix=='cpu.slow':required.update(('stepID','callID','parentCallID','kind','wallNS','failed','stack'))
            elif prefix=='cpu.aggregate':required.update(('kind','calls','wallNS','maxNS','failures'))
            else:required.update(('eligible','emitted','dropped','stackFailures','callbackFailures','overheadNS'))
            if not required<=fields.keys():raise ValueError('missing fields')
            key=(event.get('run'),fields['epoch'],fields['window'])
            window=windows.setdefault(key,{'slow':[],'aggregate':{},'summary':None})
            if prefix=='cpu.slow':
                if kind not in KINDS or len(stack.encode('utf-8'))>512:raise ValueError('invalid detail kind/stack')
                if fields['wallNS']<16000000:raise ValueError('detail below threshold')
                window['slow'].append(fields)
            elif prefix=='cpu.aggregate':
                if kind not in KINDS or kind in window['aggregate']:raise ValueError('duplicate/unknown aggregate')
                window['aggregate'][kind]=fields
            else:
                if window['summary'] is not None:raise ValueError('duplicate summary')
                window['summary']=fields
        except (ValueError,KeyError,TypeError) as exc:errors.append(f'{line}: {exc}')
    output=[]
    for (run,epoch,windowID),window in windows.items():
        summary=window['summary'];reasons=[]
        if summary is None:reasons.append('missing summary')
        else:
            if summary['eligible']!=summary['emitted']+summary['dropped']:reasons.append('detail counts do not reconcile')
            if summary['emitted']!=len(window['slow']):reasons.append('missing detail lines')
            if summary['dropped']:reasons.append('bounded details dropped')
            if summary['stackFailures']:reasons.append('stack unavailable')
            if summary['callbackFailures']:reasons.append('log callback failed')
            if summary['emitted']>8:errors.append(f'{run}/{epoch}/{windowID}: window detail cap')
        if set(window['aggregate'])!=set(KINDS):reasons.append('missing aggregates')
        steps=collections.defaultdict(list)
        for call in window['slow']:steps[call['stepID']].append(call)
        if 0 in steps:reasons.append('outside host step')
        if any(len(calls)>4 for calls in steps.values()):errors.append(f'{run}/{epoch}/{windowID}: frame detail cap')
        output.append({'run':run,'epoch':epoch,'window':windowID,'complete':not reasons,
                       'incompleteReasons':reasons,**window,
                       'steps':[{'stepID':step,'calls':calls} for step,calls in steps.items()]})
    return {'available':bool(windows),'valid':not errors,'errors':errors,'windows':output,
            'basis':'Inclusive VM/native/storage timings may overlap and contain waits. Do not sum them or call them pure VM CPU. '
                    'Missing details or aggregates are incomplete evidence; remaining script cost requires replay with this build.'}

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('logs',type=Path,nargs='+');p.add_argument('--output',type=Path)
    args=p.parse_args();sources=[]
    for path in args.logs:
        events=[json.loads(line) for line in path.read_text(encoding='utf-8-sig').splitlines() if line.strip()]
        sources.append({'file':path.as_posix(),**summarize(events)})
    text=json.dumps({'tool':'analyze-cpu-calls','version':1,'sources':sources},ensure_ascii=False,indent=2)+'\n'
    if args.output:args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(text,encoding='utf-8')
    else:print(text,end='')
    return int(any(not s['valid'] for s in sources))
if __name__=='__main__':raise SystemExit(main())
