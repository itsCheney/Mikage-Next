#!/usr/bin/env python3
"""Read-only bounded CPU call attribution; inclusive wall times are not additive CPU."""
import argparse
import collections
import json
from pathlib import Path
import shlex

KINDS=('step','vm','image','image.cacheHit','image.decode','script.storage','kag.load',
       'kag.read','kag.cacheHit','kag.cacheMiss','kag.labelBuild','kag.nextTag')
PSB_KINDS=('psb.load','psb.open','psb.read','psb.decompress','psb.copy','psb.tables',
           'psb.materialize','psb.invalidate','psb.destroy')

def summarize(events):
    windows=collections.OrderedDict();errors=[];scopes=set()
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
            version=fields['v']
            if version not in (1,2):raise ValueError('unsupported version')
            kinds=KINDS+(PSB_KINDS if version==2 else ())
            required={'v','epoch','window'}
            if prefix=='cpu.slow':required.update(('stepID','callID','parentCallID','kind','wallNS','failed','stack'))
            elif prefix=='cpu.aggregate':required.update(('kind','calls','wallNS','maxNS','failures'))
            else:required.update(('eligible','emitted','dropped','stackFailures','callbackFailures','overheadNS'))
            if version==2:
                if prefix=='cpu.slow':required.update(('scopeID','parentScopeID','startWindow','childWallNS',
                    'residualWallNS','bytes','vmCalls','role','stackTruncated'))
                elif prefix=='cpu.aggregate':required.add('bytes')
                else:required.update(('frameDropped','windowDropped','byteDropped','formatDropped','callbackDropped','selection'))
            if not required<=fields.keys():raise ValueError('missing fields')
            key=(event.get('run'),fields['epoch'],fields['window'])
            window=windows.setdefault(key,{'version':version,'slow':[],'aggregate':{},'summary':None})
            if window['version']!=version:raise ValueError('mixed window versions')
            if prefix=='cpu.slow':
                if kind not in kinds or len(stack.encode('utf-8'))>512:raise ValueError('invalid detail kind/stack')
                if fields['wallNS']<16000000:raise ValueError('detail below threshold')
                if fields['failed'] not in (0,1):raise ValueError('invalid failure flag')
                if version==2:
                    if kind=='vm' and not fields['vmCalls']:raise ValueError('VM detail missing its own call')
                    if not fields['scopeID'] or fields['stackTruncated'] not in (0,1):raise ValueError('invalid scope/truncation')
                    scopeKey=(key[0],key[1],fields['scopeID'])
                    if scopeKey in scopes:raise ValueError('duplicate scope')
                    if fields['parentScopeID']>=fields['scopeID']:raise ValueError('invalid parent scope order')
                    if fields['childWallNS']>fields['wallNS'] or fields['residualWallNS']!=fields['wallNS']-fields['childWallNS']:
                        raise ValueError('residual wall does not reconcile')
                    role=0 if kind=='step' else 1 if kind=='vm' and not fields['parentCallID'] else 2 if kind in PSB_KINDS[1:] else 3
                    if fields['role']!=role:raise ValueError('invalid detail role')
                    scopes.add(scopeKey)
                window['slow'].append(fields)
            elif prefix=='cpu.aggregate':
                if kind not in kinds or kind in window['aggregate']:raise ValueError('duplicate/unknown aggregate')
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
            if window['version']==2:
                drops=sum(summary[k] for k in ('frameDropped','windowDropped','byteDropped','formatDropped','callbackDropped'))
                if drops!=summary['dropped']:errors.append(f'{run}/{epoch}/{windowID}: drop reasons do not reconcile')
                if summary['selection']!=2:errors.append(f'{run}/{epoch}/{windowID}: unknown selection policy')
        expectedKinds=KINDS+(PSB_KINDS if window['version']==2 else ())
        if set(window['aggregate'])!=set(expectedKinds):reasons.append('missing aggregates')
        steps=collections.defaultdict(list)
        for call in window['slow']:steps[call['stepID']].append(call)
        if 0 in steps:reasons.append('outside host step')
        if any(len(calls)>4 for calls in steps.values()):errors.append(f'{run}/{epoch}/{windowID}: frame detail cap')
        unresolved=[]
        if window['version']==2:
            if len(steps)>2:errors.append(f'{run}/{epoch}/{windowID}: selected step cap')
            identities={call['scopeID'] for call in window['slow']}
            for step,calls in steps.items():
                if len({call['role'] for call in calls})!=len(calls):errors.append(f'{run}/{epoch}/{windowID}/{step}: duplicate role')
                for call in calls:
                    if call['startWindow']!=windowID:reasons.append('cross-window scope')
                    if call['stackTruncated']:reasons.append('truncated stack display')
                    if call['parentScopeID'] and call['parentScopeID'] not in identities:unresolved.append(call['parentScopeID'])
            if unresolved:reasons.append('bounded scope ancestry unavailable')
        output.append({'run':run,'epoch':epoch,'window':windowID,'complete':not reasons,
                       'nativePSBAvailable':window['version']==2,'residualWallAvailable':window['version']==2,
                       'vmCallCountAvailable':window['version']==2,
                       'incompleteReasons':sorted(set(reasons)),'unresolvedParentScopes':sorted(set(unresolved)),**window,
                       'steps':[{'stepID':step,'calls':calls} for step,calls in steps.items()]})
    return {'available':bool(windows),'valid':not errors,'errors':errors,'windows':output,
            'basis':'Inclusive VM/native/storage timings may overlap and contain waits. Residual wall subtracts only instrumented direct children. '
                    'Do not sum nested timings or call inclusive/residual wall pure VM CPU. '
                    'Missing details or aggregates are incomplete evidence; remaining script cost requires replay with this build.'}

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('logs',type=Path,nargs='+');p.add_argument('--output',type=Path)
    args=p.parse_args();sources=[]
    for path in args.logs:
        events=[json.loads(line) for line in path.read_text(encoding='utf-8-sig').splitlines() if line.strip()]
        sources.append({'file':path.as_posix(),**summarize(events)})
    text=json.dumps({'tool':'analyze-cpu-calls','version':2,'sources':sources},ensure_ascii=False,indent=2)+'\n'
    if args.output:args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(text,encoding='utf-8')
    else:print(text,end='')
    return int(any(not s['valid'] for s in sources))
if __name__=='__main__':raise SystemExit(main())
