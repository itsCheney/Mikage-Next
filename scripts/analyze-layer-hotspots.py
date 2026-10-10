#!/usr/bin/env python3
"""Read-only P2D attribution. Missing, bounded, or partial data never means zero."""
import argparse
import base64
import collections
import hashlib
import json
from pathlib import Path
import re

KINDS = {1: 'Copy', 2: 'CopyColor', 3: 'CopyMask', 4: 'CopyOpaque', 5: 'Fill',
         6: 'FillColor', 7: 'FillMask', 8: 'Alpha', 9: 'ConstAlpha', 10: 'ColorMap'}
ACCESS = ('rect', 'read', 'snapshot', 'upload', 'copy', 'affine', 'perspective',
          'span', 'shrink', 'transition', 'window', 'mesh', 'create', 'clear', 'pass', 'end')
BOUNDARIES = ('target', 'upload', 'blit', 'compute', 'mesh', 'submit', 'destroy',
              'read', 'snapshot', 'window', 'explicitClear', 'initialization', 'other')
CLEARS = ('layerCreate', 'otherCreate', 'firstRead', 'firstWrite',
          'fusedInitialization', 'fullFill', 'explicit')
COUNTERS = ('calls', 'pixels', 'scaledPixels', 'aliasPixels', 'snapshotBytes', 'render',
            'compute', 'blit', 'draws', 'creates', 'clearBytes', 'elidedInitialization',
            'fusedInitialization', 'fastFills', 'sampleFailures', 'diagnosticGaps')


def integer(value):
    if type(value) is not int or value < 0 or value > 2**64-1:
        raise ValueError('invalid unsigned counter')
    return value


def unique_object(pairs):
    value = {}
    for key, item in pairs:
        if key in value:
            raise ValueError('duplicate JSON key')
        value[key] = item
    return value


def totals(value):
    for key in (*COUNTERS, 'saturated'):
        integer(value[key])
    for key, length in (('ends', len(BOUNDARIES)), ('clears', len(CLEARS))):
        if len(value[key]) != length:
            raise ValueError(f'{key} length')
        for number in value[key]:
            integer(number)
    if not value['kindPixels'] or len(value['kindPixels']) > 256:
        raise ValueError('kindPixels length')
    for number in value['kindPixels']:
        integer(number)
    if not value['saturated'] and sum(value['kindPixels']) != value['pixels']:
        raise ValueError('kind pixels do not reconcile')
    return value


def summarize(events):
    groups = collections.OrderedDict()
    errors = []
    gpu = {}
    for index, event in enumerate(events, 1):
        message = event.get('fields', {}).get('message', '')
        if not isinstance(message, str):
            continue
        if message.startswith(('metal.gpuCommandBuffer ', 'metal.gpuStages ', 'metal.layerWork ')):
            fields = dict(re.findall(r'(\w+)=([^ ]+)', message))
            gpu.setdefault((event.get('run'), fields.get('p2dEpoch'), fields.get('id')), {})[message.split()[0]] = fields
        if not message.startswith(('metal.layerHotspot ', 'metal.layerPasses ')):
            continue
        try:
            if len(message.encode('utf-8')) > 960:
                raise ValueError('message exceeds production byte cap')
            pairs = re.findall(r'(\w+)=([^ ]+)', message)
            raw = dict(pairs)
            if len(pairs) != len(raw):
                raise ValueError('duplicate field')
            if int(raw['version']) != 1:
                raise ValueError('unsupported version')
            prefix = message.split()[0]
            epoch, command = integer(int(raw['epoch'])), integer(int(raw['id']))
            part, parts = integer(int(raw['part'])), integer(int(raw['parts']))
            if parts == 0 or parts > 64 or part >= parts:
                raise ValueError('invalid chunk index/count')
            final = int(raw.get('final', 0))
            lost = integer(int(raw.get('droppedReports', 0)))
            if final not in (0, 1):
                raise ValueError('invalid final flag')
            key = (event.get('run'), prefix, epoch, command, final)
            group = groups.setdefault(key, {'parts': parts, 'rows': {}, 'lost': lost,
                                            'time': event.get('uptimeSeconds')})
            if group['parts'] != parts or group['lost'] != lost:
                raise ValueError('inconsistent chunk headers')
            if part in group['rows']:
                raise ValueError('duplicate chunk')
            group['rows'][part] = raw['data']
        except (KeyError, TypeError, ValueError) as exc:
            errors.append(f'{index}: {exc}')
    samples, reports, incomplete = [], [], []
    for (run, prefix, epoch, command, final), group in groups.items():
        key_text = f'{run}/{epoch}/{command}/{prefix}'
        if len(group['rows']) != group['parts']:
            incomplete.append({'key': key_text, 'reason': 'missing chunks'})
            continue
        try:
            data = ''.join(group['rows'][p] for p in range(group['parts']))
            value = json.loads(base64.b64decode(data, validate=True).decode('utf-8'), object_pairs_hook=unique_object)
            if prefix == 'metal.layerPasses':
                reports.append({'run': run, 'epoch': epoch, 'id': command, 'final': bool(final),
                                'droppedReports': group['lost'], 'time': group['time'],
                                'totals': totals(value)})
                continue
            if (value['version'], value['epoch'], value['id']) != (1, epoch, command):
                raise ValueError('payload identity differs from header')
            total = totals(value['totals'])
            if len(value['resources']) > 64 or len(value['operations']) > 256:
                raise ValueError('sample exceeds production capacity')
            reasons = []
            for name in ('operationOverflow', 'resourceOverflow', 'outputDropped', 'stageDropped', 'stageFailed'):
                if integer(value[name]):
                    reasons.append(name)
            if total['saturated']:
                reasons.append('saturated counters')
            resources = []
            generations = set()
            for r in value['resources']:
                if len(r) != 13 or any(type(n) is not int or n < 0 for n in r[:11]):
                    raise ValueError('malformed resource row')
                if r[0] in generations:
                    raise ValueError('duplicate resource generation')
                generations.add(r[0])
                resource = dict(zip(('generation', 'session', 'texture', 'parentSession', 'parent',
                                     'creatorLayer', 'assetHash', 'width', 'height', 'bpp',
                                     'assetTruncated', 'asset', 'role'), r))
                resources.append(resource)
                if r[10]:
                    reasons.append('truncated asset')
            contributions = collections.defaultdict(lambda: {'pixels': 0, 'kindPixels': collections.Counter()})
            ordered = []
            previous = 0
            mapped_pixels = 0
            for op in value['operations']:
                if len(op) != 21 or not 0 <= op[1] < len(ACCESS):
                    raise ValueError('malformed operation row')
                if op[0] <= previous:
                    raise ValueError('operation order regression')
                previous = op[0]
                for rectangle in op[18:21]:
                    if len(rectangle) != 4 or any(type(n) is not int for n in rectangle):
                        raise ValueError('malformed rectangle')
                ti, si = op[3:5]
                if any(type(i) is not int or i < -1 or i >= len(resources) for i in (ti, si)):
                    raise ValueError('unresolved resource index')
                target = resources[ti] if ti >= 0 else None
                source = resources[si] if si >= 0 else None
                if op[2] > 0:
                    if not target or not target['texture'] or not op[7]:
                        reasons.append('unknown resource or current Layer')
                    if source and not source['texture']:
                        reasons.append('unknown source identity')
                    pixels = integer(op[10]);mapped_pixels += pixels
                    contribution = contributions[ti]
                    contribution['pixels'] += pixels
                    contribution['kindPixels'][KINDS.get(op[2], str(op[2]))] += pixels
                ordered.append({'sequence': op[0], 'access': ACCESS[op[1]], 'kind': KINDS.get(op[2], str(op[2])),
                                'target': target['generation'] if target else None,
                                'source': source['generation'] if source else None,
                                'targetVersion': op[5], 'sourceVersion': op[6], 'currentLayer': op[7],
                                'role': op[8], 'encoder': op[9], 'pixels': op[10], 'snapshotBytes': op[11],
                                'fullCoverage': bool(op[12]), 'opaque': op[13], 'readsTarget': bool(op[14]),
                                'scaled': bool(op[15]), 'alias': bool(op[16]),
                                'reason': (CLEARS[op[17]] if op[1] == 14 and op[12] and 0 <= op[17] < len(CLEARS)
                                           else 'load' if op[1] == 14
                                           else BOUNDARIES[op[17]] if 0 <= op[17] < len(BOUNDARIES) else 'unknown'),
                                'destination': op[18], 'sourceRect': op[19], 'clip': op[20]})
            if mapped_pixels > total['pixels']:
                raise ValueError('sample details exceed aggregate pixels')
            if mapped_pixels != total['pixels']:
                reasons.append('partial operation pixels')
            existing = gpu.get((run, str(epoch), str(command)), {})
            stage = existing.get('metal.gpuStages', {})
            if stage and (stage.get('status') != 'stage_boundary' and stage.get('status') != 'complete' or
                          int(stage.get('droppedPasses', 0)) or int(stage.get('failedPasses', 0))):
                # Keep the original status: command GPU duration is independent
                # from stage validity and overlapping stage times are not added.
                reasons.append('partial or unavailable GPU stage timing')
            samples.append({'run': run, 'epoch': epoch, 'id': command, 'firstFrame': value['firstFrame'],
                            'lastFrame': value['lastFrame'], 'hot': bool(value['hot']), 'gpuMS': value['gpuMS'],
                            'drawableMS': value['drawableMS'] if value.get('drawableObserved', 0) else None, 'frameMS': value['frameMS'],
                            'complete': not reasons, 'incompleteReasons': sorted(set(reasons)),
                            'totals': total, 'kindPixels': {KINDS.get(k, str(k)): n for k, n in
                                                          enumerate(total['kindPixels']) if n},
                            'resources': resources, 'targetContributions': [
                                {'resource': resources[i] if i >= 0 else None, **c}
                                for i, c in sorted(contributions.items(), key=lambda row: -row[1]['pixels'])],
                            'operations': ordered, 'gpuObservations': existing})
        except (KeyError, TypeError, ValueError, UnicodeError) as exc:
            errors.append(f'{key_text}: {exc}')
    epochs = []
    report_groups = collections.defaultdict(list)
    for r in reports:
        report_groups[(r['run'], r['epoch'])].append(r)
    for (run, epoch), rows in report_groups.items():
        previous = None
        for row in rows:
            if previous:
                if previous['final']:
                    errors.append(f'{run}/{epoch}: aggregate after final')
                for name in COUNTERS:
                    if row['totals'][name] < previous['totals'][name]:
                        errors.append(f'{run}/{epoch}: {name} regression')
                for name in ('kindPixels', 'ends', 'clears'):
                    if len(previous['totals'][name]) != len(row['totals'][name]) or any(
                            a > b for a, b in zip(previous['totals'][name], row['totals'][name])):
                        errors.append(f'{run}/{epoch}: {name} regression')
            previous = row
        first, last = rows[0], rows[-1]
        if last['final'] and any(s['run']==run and s['epoch']==epoch and s['id']>last['id'] for s in samples):
            errors.append(f'{run}/{epoch}: sample command exceeds final command')
        epochs.append({'run': run, 'epoch': epoch, 'snapshots': len(rows), 'finalObserved': last['final'],
                       'droppedReports': last['droppedReports'], 'lastTotals': last['totals'],
                       'observedDelta': {k: last['totals'][k]-first['totals'][k] for k in COUNTERS},
                       'passEndReasons': dict(zip(BOUNDARIES, last['totals']['ends'])),
                       'clearOrigins': dict(zip(CLEARS, last['totals']['clears']))})
    return {'available': bool(groups), 'valid': not errors, 'errors': errors,
            'incompleteGroups': incomplete, 'epochs': epochs, 'samples': samples,
            'basis': 'Sample details are bounded and contributions are lower bounds when incomplete. '
                     'Asset names describe provenance, not exclusive ownership of composite pixels. '
                     'Aggregate deltas cover only visible snapshots; missing final means an incomplete tail. '
                     'GPU command time is never apportioned by pixels/draws or summed with overlapping stages. '
                     'nextDrawable is presentation backpressure, separately from readback and script CPU time. '
                     'These observations do not establish nominal matched before/after performance.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('logs', nargs='+', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    sources = []
    for path in args.logs:
        data = path.read_bytes()
        events = [json.loads(line) for line in data.decode('utf-8-sig').splitlines() if line.strip()]
        sources.append({'file': path.as_posix(), 'sha256': hashlib.sha256(data).hexdigest(), **summarize(events)})
    text = json.dumps({'tool': 'analyze-layer-hotspots', 'version': 1, 'sources': sources}, ensure_ascii=False, indent=2)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text+'\n', encoding='utf-8')
    else:
        print(text)
    return 0 if all(s['valid'] for s in sources) else 1


if __name__ == '__main__':
    raise SystemExit(main())
