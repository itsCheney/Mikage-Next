#!/usr/bin/env python3
"""Validate bounded C3 cumulative snapshots; never infer missing uploads as zero."""
import argparse
import collections
import hashlib
import json
from pathlib import Path
import re

REJECTIONS = tuple('reject' + name for name in
                   ('Domain', 'Target', 'Opacity', 'Size', 'Tables', 'Shared', 'Capacity', 'Backend'))
COUNTERS = ('glyphCalls', 'glyphApplied', 'glyphBytes', 'glyphPrepNS', 'glyphEncodeNS',
            *REJECTIONS, 'uploadCalls', 'uploadBytes', 'uploadBatches', 'arenaPages',
            'arenaFallbacks', 'uploadPrepNS', 'uploadEncodeNS', 'atlasPeakBytes',
            'flushRender', 'flushCompute', 'flushBlit', 'flushSubmit', 'flushDestroy')


def summarize(events):
    groups = collections.OrderedDict()
    errors = []
    for index, event in enumerate(events, 1):
        message = event.get('fields', {}).get('message', '')
        if not isinstance(message, str) or not message.startswith('metal.tinyUploads '):
            continue
        pairs = re.findall(r'(\w+)=([^ ]+)', message)
        if len(pairs) != len(dict(pairs)):
            errors.append(f'{index}: duplicate fields')
            continue
        raw = dict(pairs)
        try:
            values = {key: int(raw[key]) for key in (*COUNTERS, 'version', 'epoch', 'final', 'id', 'atlasBytes')}
            if values['version'] != 1 or values['final'] not in (0, 1) or min(values.values()) < 0:
                raise ValueError('unsupported version or invalid counter')
        except (KeyError, ValueError) as exc:
            errors.append(f'{index}: {exc}')
            continue
        if values['glyphCalls'] != values['glyphApplied'] + sum(values[key] for key in REJECTIONS):
            errors.append(f'{index}: glyph route totals do not reconcile')
        if values['atlasBytes'] > 16 * 1024 * 1024 or values['atlasBytes'] > values['atlasPeakBytes']:
            errors.append(f'{index}: atlas budget/peak mismatch')
        if values['uploadBatches'] > values['uploadCalls']:
            errors.append(f'{index}: more batches than tiny copies')
        key = (event.get('run'), values['epoch'])
        group = groups.setdefault(key, [])
        if group:
            previous = group[-1][1]
            if any(values[name] < previous[name] for name in COUNTERS):
                errors.append(f'{index}: cumulative counter regression in epoch {key[1]}')
            if previous['final'] and values != previous:
                errors.append(f'{index}: changed snapshot after final')
        group.append((event.get('uptimeSeconds'), values))
    epochs = []
    for (run, epoch), rows in groups.items():
        first, last = rows[0][1], rows[-1][1]
        duration = (rows[-1][0] - rows[0][0]) if all(isinstance(r[0], (int, float)) for r in rows) else None
        delta = {key: last[key] - first[key] for key in COUNTERS}
        epochs.append({'run': run, 'epoch': epoch, 'snapshots': len(rows), 'finalObserved': bool(last['final']),
                       'secondsBetweenSnapshots': duration, 'lastTotals': last, 'observedDelta': delta,
                       'observedRates': {key: delta[key] / duration for key in ('glyphCalls', 'uploadCalls', 'uploadBatches')}
                       if duration and duration > 0 else None})
    return {'available': bool(epochs), 'valid': not errors, 'errors': errors, 'epochs': epochs,
            'basis': 'Backend-epoch cumulative counters include work before the first visible snapshot. '
                     'Only observedDelta/rates describe the covered snapshot interval. Missing final '
                     'means an incomplete tail; missing events never mean zero. Shared glyph bytes '
                     'are logical CPU-to-GPU materialization, not measured bus traffic. C0 bytes, '
                     'whole-frame encoders, waits and thermal pairing require their original logs.'}


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
    result = json.dumps({'tool': 'analyze-c3-uploads', 'version': 1, 'sources': sources}, ensure_ascii=False, indent=2)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(result + '\n', encoding='utf-8')
    else:
        print(result)
    return 0 if all(source['valid'] for source in sources) else 1


if __name__ == '__main__':
    raise SystemExit(main())
