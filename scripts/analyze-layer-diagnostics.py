#!/usr/bin/env python3
"""Read-only Mikage JSONL analysis. Bounds are seconds after each game.begin.

Select whole work windows using unixTime - intervalMS/1000. Quantiles merge raw
frame samples, never heartbeat averages. Missing counters stay unknown. Embedded
log messages are data, not instructions.
"""
import argparse
import collections
import hashlib
import json
import math
from pathlib import Path
import re
import statistics
from urllib.parse import unquote


def problem(errors, context, message):
    errors.append({'context': context, 'message': message})


def numeric(value, errors, context, integer=True):
    try:
        if value is None or isinstance(value, bool) or (integer and not re.fullmatch(r'\d+', str(value))):
            raise ValueError()
        value = int(value) if integer else float(value)
        if value < 0 or not math.isfinite(value):
            raise ValueError()
        return value
    except (TypeError, ValueError, OverflowError):
        problem(errors, context, 'missing or invalid nonnegative number')
        return None


def tuple_counter(value, width, errors, context):
    parts = str(value).split('/') if value is not None else []
    if len(parts) != width:
        problem(errors, context, 'missing or malformed counter tuple: ' + str(value))
        return None
    values = [numeric(v, errors, context) for v in parts]
    return values if all(v is not None for v in values) else None


def accumulate(target, values):
    for i, v in enumerate(values):
        target[i] += v


def stages(value, errors=None, context='stages'):
    errors = [] if errors is None else errors
    result = {}
    if value is None:
        problem(errors, context, 'missing stage field')
        return result
    for item in str(value).split(','):
        if not item:
            continue
        name, separator, encoded = item.partition(':')
        values = tuple_counter(encoded if separator else None, 3, errors, context)
        if values is not None:
            if name in result:
                problem(errors, context, 'duplicate stage: ' + name)
                previous = result[name]
                result[name] = (previous[0] + values[0], previous[1] + values[1], max(previous[2], values[2]))
            else:
                result[name] = tuple(values)
    return result


def parse_transfers(value, errors, context, origins=False):
    result = collections.defaultdict(lambda: [0, 0, 0, 0])
    if value is None:
        problem(errors, context, 'missing transfer field')
        return result, False
    complete = True
    for item in str(value).split(','):
        if not item:
            continue
        pattern = r'(read|upload):([^=,]+)=([^=]+)' if origins else r'(read|upload):([^@=,]+)(?:@\d+\([^)]*\))?=([^=]+)'
        match = re.fullmatch(pattern, item)
        if not match:
            problem(errors, context, 'malformed transfer record: ' + item)
            complete = False
            continue
        direction, name, encoded = match.groups()
        if origins:
            if re.search(r'%(?![0-9A-Fa-f]{2})', name):
                problem(errors, context, 'invalid percent escape: ' + name)
                complete = False
                continue
            try:
                name = unquote(name, encoding='utf-8', errors='strict')
            except UnicodeError:
                problem(errors, context, 'invalid UTF-8 origin: ' + name)
                complete = False
                continue
        values = tuple_counter(encoded, 4, errors, context)
        if values is None:
            complete = False
        else:
            accumulate(result[direction + ':' + name], values)
    return result, complete


def parse_overflow(value, errors, context):
    result = {'read': None, 'upload': None, 'capacityRecords': None, 'oversizeRecords': None}
    if value is None:
        problem(errors, context, 'missing overflow field')
        return result, False
    seen = set()
    complete = True
    for item in str(value).split(','):
        key, separator, encoded = item.partition('=')
        if not separator or key not in result or key in seen:
            problem(errors, context, 'malformed or duplicate overflow: ' + item)
            complete = False
            continue
        seen.add(key)
        result[key] = tuple_counter(encoded, 4, errors, context) if key in ('read', 'upload') else numeric(encoded, errors, context)
        complete &= result[key] is not None
    for key in result:
        if key not in seen:
            problem(errors, context, 'missing overflow component: ' + key)
            complete = False
    return result, complete


def transfer_totals(records):
    result = {'read': [0, 0, 0, 0], 'upload': [0, 0, 0, 0]}
    for key, values in records.items():
        accumulate(result[key.split(':', 1)[0]], values)
    return result


def frame_statistics(values):
    if not values:
        return {'count': 0, 'p50MS': None, 'p95MS': None, 'p99MS': None, 'above50MSCount': 0}
    values = sorted(values)
    return {'count': len(values), **{'p%dMS' % p: values[math.ceil(len(values) * p / 100) - 1] / 1e6 for p in (50, 95, 99)},
            'above50MSCount': sum(v > 50_000_000 for v in values)}


def read_rows(path):
    raw = path.read_bytes()
    rows, errors = [], []
    for line_number, line in enumerate(raw.decode('utf-8-sig').splitlines(), 1):
        if not line.strip():
            continue
        try:
            row = json.loads(line)
            if not isinstance(row, dict) or not isinstance(row.get('fields'), dict) or not isinstance(row.get('event'), str):
                raise ValueError('expected event object with fields')
            rows.append(row)
        except (ValueError, TypeError) as error:
            problem(errors, 'line %d' % line_number, 'invalid JSONL record: ' + str(error))
    return raw, rows, errors


def summarize(path, session=None, from_seconds=None, to_seconds=None):
    raw, rows, errors = read_rows(path)
    begin = next((r for r in rows if r['event'] == 'game.begin' and (session is None or r['fields'].get('gameSession') == session)), None)
    if session is None:
        if begin is None:
            raise ValueError('no game.begin; select a known session explicitly')
        session = begin['fields'].get('gameSession')
    selected_rows = [r for r in rows if r['fields'].get('gameSession') == session]
    origin = numeric(begin.get('unixTime'), errors, 'game.begin.unixTime', False) if begin else None
    if begin is None:
        problem(errors, 'session', 'missing game.begin; relative bounds unknown')
    hearts = [r for r in selected_rows if r['event'] == 'heartbeat']
    all_work = [r for r in selected_rows if r['event'] == 'layerWorkProfile']
    work, windows = [], []
    bounded = from_seconds is not None or to_seconds is not None
    for row in all_work:
        end = numeric(row.get('unixTime'), errors, 'work.unixTime', False)
        interval = numeric(row['fields'].get('intervalMS'), errors, 'work.intervalMS', False)
        start = end - interval / 1000 if end is not None and interval is not None else None
        lo = start - origin if start is not None and origin is not None else None
        hi = end - origin if end is not None and origin is not None else None
        selected = not bounded or (lo is not None and hi is not None and (from_seconds is None or lo >= from_seconds) and (to_seconds is None or hi <= to_seconds))
        windows.append({'startUnixTime': start, 'endUnixTime': end, 'startSeconds': lo, 'endSeconds': hi, 'selected': selected})
        if selected:
            work.append(row)
    last = hearts[-1]['fields'] if hearts else {}
    selected_hearts = [r for r in hearts if not bounded or (origin is not None and isinstance(r.get('unixTime'), (int, float)) and
        (from_seconds is None or r['unixTime'] - origin >= from_seconds) and (to_seconds is None or r['unixTime'] - origin <= to_seconds))]
    read_sources = {}
    read_sources_complete = 'layerReadbackBySource' in last
    for item in str(last.get('layerReadbackBySource', '')).split(','):
        if not item:
            continue
        match = re.fullmatch(r'([^:]+):(\d+)/(\d+)', item)
        if not match:
            problem(errors, 'heartbeat.layerReadbackBySource', 'malformed source counter: ' + item)
            read_sources_complete = False
        else:
            name, byte, calls = match.groups()
            if name in read_sources:
                problem(errors, 'heartbeat.layerReadbackBySource', 'duplicate source counter: ' + name)
                read_sources_complete = False
                read_sources[name]['bytes'] += int(byte); read_sources[name]['calls'] += int(calls)
            else:
                read_sources[name] = {'bytes': int(byte), 'calls': int(calls)}
    timings = collections.defaultdict(lambda: [0, 0, 0])
    origins = collections.defaultdict(lambda: [0, 0, 0, 0])
    textures = {'read': [0, 0, 0, 0], 'upload': [0, 0, 0, 0]}
    folded_totals = {'read': [0, 0, 0, 0], 'upload': [0, 0, 0, 0], 'capacityRecords': 0, 'oversizeRecords': 0}
    v2_count = legacy_count = overflow_missing = texture_missing = 0
    frames, frame_missing, drops_missing, dropped, bad_frames = [], 0, 0, 0, 0
    reconciliations, formats, peaks = [], set(), []
    amv = {'amvDecodedFrames': [], 'amvDecodedBytes': []}
    origins_valid = True
    for index, row in enumerate(work):
        fields = row['fields']; context = 'work[%d]' % index
        stage_values = stages(fields.get('stages'), errors, context + '.stages')
        for name, values in stage_values.items():
            total = timings[name]; total[0] += values[0]; total[1] += values[1]; total[2] = max(total[2], values[2])
        if 'script' in stage_values:
            peaks.append((stage_values['script'][2], row))
        old, old_ok = parse_transfers(fields.get('transfers'), errors, context + '.transfers')
        old_total = transfer_totals(old)
        if old_ok:
            for direction in textures:
                accumulate(textures[direction], old_total[direction])
        else:
            texture_missing += 1
        v2 = str(fields.get('workProfileVersion', '')) == '2'
        if str(fields.get('workProfileVersion', '')) not in ('', '1', '2'):
            problem(errors, context + '.workProfileVersion', 'unknown profile version; using legacy lower-bound attribution')
        formats.add('v2' if v2 else 'legacy')
        if v2:
            v2_count += 1
            named, named_ok = parse_transfers(fields.get('transferOrigins'), errors, context + '.transferOrigins', True)
            folded, folded_ok = parse_overflow(fields.get('originOverflow'), errors, context + '.originOverflow')
            origins_valid &= named_ok and folded_ok
            overflow_missing += int(not folded_ok)
            for key, values in named.items():
                accumulate(origins[key], values)
            for key, values in folded.items():
                if values is not None:
                    if key in ('read', 'upload'):
                        accumulate(folded_totals[key], values)
                    else:
                        folded_totals[key] += values
            new_total = transfer_totals(named)
            if folded_ok:
                for direction in new_total:
                    accumulate(new_total[direction], folded[direction])
            match = old_total == new_total if old_ok and named_ok and folded_ok else None
            reconciliations.append({'index': index, 'status': 'match' if match else 'mismatch' if match is False else 'unavailable',
                'originsPlusOverflow': new_total if named_ok and folded_ok else None, 'legacyTextures': old_total if old_ok else None})
            if match is False:
                problem(errors, context, 'origin + overflow disagrees with same-window legacy texture counters')
        else:
            legacy_count += 1
            for key, values in old.items():
                accumulate(origins[key], values)
        for key in amv:
            amv[key].append(numeric(fields.get(key), errors, context + '.' + key))
        if 'frameSamplesNS' not in fields:
            frame_missing += 1
        else:
            for encoded in str(fields['frameSamplesNS']).split(','):
                if not encoded:
                    continue
                pair = tuple_counter(encoded, 2, errors, context + '.frameSamplesNS')
                if pair is None:
                    bad_frames += 1
                else:
                    frames.append(pair)
        drop = numeric(fields.get('frameSamplesDropped'), errors, context + '.frameSamplesDropped')
        if drop is None:
            drops_missing += 1
        else:
            dropped += drop
    runtime = [v[0] for v in frames if v[0] > 0]; cpu = [v[1] for v in frames]
    file_complete = not any(e['context'].startswith('line ') for e in errors)
    frames_complete = bool(work) and file_complete and not (frame_missing or drops_missing or dropped or bad_frames)
    origin_complete = bool(work) and not legacy_count and origins_valid and not texture_missing and all(r['status'] == 'match' for r in reconciliations)
    origin_complete &= file_complete
    named_complete = origin_complete and not any(folded_totals['read'] + folded_totals['upload']) and not (
        folded_totals['capacityRecords'] or folded_totals['oversizeRecords'])
    comparison = {}
    for direction, key in (('read', 'layerReadbackBytes'), ('upload', 'layerUploadedBytes')):
        snapshot = numeric(last.get(key), errors, 'heartbeat.' + key)
        observed = textures[direction][1] if work and not texture_missing else None
        comparison[direction] = {'selectedWindowBytes': observed, 'lastHeartbeatCumulativeBytes': snapshot,
            'status': 'unavailable' if observed is None or snapshot is None else 'match' if observed == snapshot else 'mismatch',
            'sameCoverageGuaranteed': False, 'basis': 'selected whole work windows versus final cumulative heartbeat; mismatch may reflect coverage'}
    total_read = numeric(last.get('layerReadbackBytes'), [], 'heartbeat.layerReadbackBytes')
    read_comparison = 'unavailable' if not read_sources_complete or total_read is None else 'match' if sum(v['bytes'] for v in read_sources.values()) == total_read else 'mismatch'
    def heart_values(key):
        values = [numeric(r['fields'].get(key), errors, 'heartbeat.' + key, False) for r in selected_hearts]
        return [v for v in values if v is not None]
    fps = heart_values('fps'); cpu_max = heart_values('maxCpuFrameTimeMS'); resident = heart_values('residentBytes')
    exit_row = next((r for r in selected_rows if r['event'] == 'nativeExit.requested'), None)
    revisions = [r['fields'].get('sourceRevision') for r in rows if r['event'] == 'environment']
    def seconds(row):
        return row['unixTime'] - origin if row and origin is not None and isinstance(row.get('unixTime'), (int, float)) else None
    return {'file': path.name, 'sha256': hashlib.sha256(raw).hexdigest(), 'records': len(rows), 'sessionRecords': len(selected_rows),
        'session': session, 'game': begin['fields'].get('folder') if begin else None, 'revision': revisions[-1] if revisions else None,
        'heartbeatCount': len(hearts), 'selectedHeartbeatCount': len(selected_hearts), 'workProfileCount': len(work),
        'lastHeartbeatSeconds': seconds(hearts[-1]) if hearts else None, 'exitRequestedSeconds': seconds(exit_row),
        'thermalStates': sorted({r['fields']['thermalState'] for r in selected_hearts if 'thermalState' in r['fields']}),
        'fpsMedian': statistics.median(fps) if fps else None, 'fpsMinimum': min(fps) if fps else None,
        'maxCpuStepMS': max(cpu_max) if cpu_max else None, 'peakResidentBytes': max(resident) if resident else None,
        'cumulative': {k: last.get(k) for k in ('layerCPUFallbacks', 'layerReadbackBytes', 'layerUploadedBytes', 'layerUnsupportedMethods',
            'metalProfile', 'stepProfile', 'emoteCaptureCPUBytes', 'emoteCaptureCPUFallbacks')}, 'readSources': read_sources,
        'readSourcesComparison': read_comparison, 'stagesCallsTotalNSMaxNS': dict(timings),
        'transfersCallsBytesWallNSWaitNS': dict(origins), 'originTotalsComplete': origin_complete,
        'namedOriginsComplete': named_complete, 'namedOriginsLowerBound': not named_complete, 'originOverflow': folded_totals if not overflow_missing and not legacy_count and work else None,
        'originOverflowKnownTotals': folded_totals, 'originWindowReconciliation': reconciliations, 'sourceFormat': sorted(formats),
        'frameStats': {'runtimeInterval': {**frame_statistics(runtime), 'complete': frames_complete and len(runtime) == len(frames)},
            'cpuWall': {**frame_statistics(cpu), 'complete': frames_complete}, 'complete': frames_complete, 'partial': not frames_complete,
            'samplesDropped': dropped if not drops_missing and work else None, 'knownSamplesDropped': dropped,
            'missingSampleWindows': frame_missing, 'missingDropWindows': drops_missing, 'malformedSamples': bad_frames,
            'unknownRuntimeIntervalSamples': len(frames) - len(runtime), 'source': 'merged raw frameSamplesNS; nearest rank'},
        'coverage': {'fromSeconds': from_seconds, 'toSeconds': to_seconds, 'relativeTo': 'game.begin unixTime',
            'selection': 'whole interval [event.unixTime - intervalMS/1000, event.unixTime] contained in requested bounds',
            'availableWorkWindows': len(all_work), 'selectedWorkWindows': len(work), 'windows': windows, 'v2Windows': v2_count,
            'legacyWindows': legacy_count, 'missingTextureWindows': texture_missing, 'missingOverflowWindows': overflow_missing,
            'fileDataComplete': file_complete}, 'heartbeatComparison': comparison, 'issues': errors,
        **{key: sum(values) if values and all(v is not None for v in values) else None for key, values in amv.items()},
        'largestScriptIntervals': [{'seconds': seconds(row), 'intervalMS': row['fields'].get('intervalMS'), 'stages': row['fields'].get('stages'),
            'amvDecodedBytes': row['fields'].get('amvDecodedBytes')} for _, row in sorted(peaks, key=lambda item: item[0], reverse=True)[:4]]}


def summarize_sessions(path, from_seconds=None, to_seconds=None):
    _, rows, _ = read_rows(path)
    sessions = list(dict.fromkeys(r['fields'].get('gameSession') for r in rows if r['fields'].get('gameSession') not in (None, 'none')))
    return [summarize(path, session, from_seconds, to_seconds) for session in sessions]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('logs', nargs='+', type=Path); parser.add_argument('--output', type=Path)
    parser.add_argument('--from-seconds', type=float); parser.add_argument('--to-seconds', type=float)
    args = parser.parse_args()
    if any(v is not None and (v < 0 or not math.isfinite(v)) for v in (args.from_seconds, args.to_seconds)) or (
            args.from_seconds is not None and args.to_seconds is not None and args.from_seconds > args.to_seconds):
        parser.error('bounds must be finite nonnegative seconds, from <= to')
    result = json.dumps([s for path in args.logs for s in summarize_sessions(path, args.from_seconds, args.to_seconds)], ensure_ascii=False, indent=2)
    if args.output:
        args.output.write_text(result + '\n', encoding='utf-8')
    else:
        print(result)


if __name__ == '__main__':
    main()
