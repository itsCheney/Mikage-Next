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

TRANSITION_HANDLERS = ('mosaic', 'wave', 'ripple', 'turn', 'rotatezoom', 'rotatevanish', 'rotateswap')
TRANSITION_METRICS = ('frames', 'gpuCalls', 'cpuCalls', 'passthroughCalls', 'pixels',
    'parameterUploads', 'parameterBytes', 'readCalls', 'readBytes', 'readWallNS', 'readWaitNS',
    'uploadCalls', 'uploadBytes', 'uploadWallNS', 'uploadWaitNS')


def parse_transition_profile(fields, errors, context):
    """Independent subset attribution; never add these transfers to C0 totals."""
    if 'transitionProfileVersion' not in fields:
        return None
    if str(fields['transitionProfileVersion']) != '1':
        problem(errors, context, 'unknown transition profile version')
        return None
    complete = True
    try:
        rows = json.loads(fields['transitionProfiles'])
        overflow = json.loads(fields['transitionOverflow'])
        if not isinstance(rows, list) or len(rows) > 64 or not isinstance(overflow, dict):
            raise ValueError('expected bounded rows and overflow object')
    except (KeyError, TypeError, ValueError) as error:
        problem(errors, context, 'invalid transition JSON: ' + str(error))
        return {'records': [], 'overflow': None, 'dropped': None, 'complete': False}
    parsed = []
    for index, row in enumerate(rows):
        detail = context + '[%d]' % index
        if not isinstance(row, dict):
            problem(errors, detail, 'expected transition object'); complete = False; continue
        if any(not isinstance(row.get(key), str) or not row[key] for key in ('requested', 'effective', 'reason', 'metadata')):
            problem(errors, detail, 'missing transition names/reason/metadata'); complete = False; continue
        dimensions = row.get('dimensions')
        if not isinstance(dimensions, list) or len(dimensions) != 8 or any(
                not isinstance(value, int) or isinstance(value, bool) or value < 0 for value in dimensions):
            problem(errors, detail, 'expected canvas/source1/source2/output dimensions'); complete = False; continue
        try:
            metadata = json.loads(row['metadata'])
            if not isinstance(metadata, dict):
                raise ValueError('metadata is not an object')
        except ValueError:
            problem(errors, detail, 'invalid normalized transition metadata'); complete = False; continue
        counts = {key: numeric(row.get(key), errors, detail + '.' + key) for key in TRANSITION_METRICS}
        ticks = {key: numeric(row.get(key), errors, detail + '.' + key) for key in ('firstTick', 'lastTick')}
        if any(value is None for value in (*counts.values(), *ticks.values())):
            complete = False; continue
        parsed.append({**{key: row[key] for key in ('requested', 'effective', 'reason')},
            'dimensions': dimensions, 'metadata': metadata, **ticks, **counts})
    folded = {key: numeric(overflow.get(key), errors, context + '.overflow.' + key)
        for key in (*TRANSITION_METRICS, 'capacityRecords', 'oversizeRecords')}
    dropped = numeric(fields.get('transitionProfilesDropped'), errors, context + '.dropped')
    if dropped is None or any(value is None for value in folded.values()):
        complete = False
    elif dropped != folded['capacityRecords'] + folded['oversizeRecords']:
        problem(errors, context, 'transition dropped count disagrees with overflow causes'); complete = False
    return {'records': parsed, 'overflow': folded, 'dropped': dropped, 'complete': complete and dropped == 0}


SHRINK_METRICS = ('gpuCalls', 'cpuCalls', 'noopCalls', 'pixels', 'wallNS', 'prepCPUNS',
    'parameterUploads', 'parameterBytes', 'temporaryBytes', 'readCalls', 'readBytes', 'readWallNS', 'readWaitNS',
    'uploadCalls', 'uploadBytes', 'uploadWallNS', 'uploadWaitNS')


def parse_shrink_profile(fields, errors, context):
    if 'shrinkProfileVersion' not in fields:
        return None
    if str(fields['shrinkProfileVersion']) != '1':
        problem(errors, context, 'unknown shrink profile version')
        return None
    complete = True
    try:
        rows = json.loads(fields['shrinkProfiles'])
        overflow = json.loads(fields['shrinkOverflow'])
        if not isinstance(rows, list) or len(rows) > 32 or not isinstance(overflow, dict):
            raise ValueError('expected bounded shrink rows and overflow object')
    except (KeyError, TypeError, ValueError) as error:
        problem(errors, context, 'invalid shrink JSON: ' + str(error))
        return {'records': [], 'overflow': None, 'dropped': None, 'samples': [], 'sampleDrops': None, 'complete': False, 'samplesComplete': False}
    parsed = []
    for index, row in enumerate(rows):
        detail = context + '[%d]' % index
        if not isinstance(row, dict) or any(not isinstance(row.get(key), str) or not row[key]
                for key in ('method', 'reason', 'aliasClass', 'metadata')):
            problem(errors, detail, 'missing shrink method/reason/alias/metadata'); complete = False; continue
        try:
            metadata = json.loads(row['metadata'])
            if not isinstance(metadata, dict):
                raise ValueError('metadata is not an object')
        except ValueError:
            problem(errors, detail, 'invalid shrink metadata'); complete = False; continue
        metrics = {key: numeric(row.get(key), errors, detail + '.' + key) for key in SHRINK_METRICS}
        if any(value is None for value in metrics.values()):
            complete = False; continue
        parsed.append({**{key: row[key] for key in ('method', 'reason', 'aliasClass')}, 'metadata': metadata, **metrics})
    folded = {key: numeric(overflow.get(key), errors, context + '.overflow.' + key)
        for key in (*SHRINK_METRICS, 'capacityRecords', 'oversizeRecords')}
    dropped = numeric(fields.get('shrinkProfilesDropped'), errors, context + '.dropped')
    if dropped is None or any(value is None for value in folded.values()):
        complete = False
    elif dropped != folded['capacityRecords'] + folded['oversizeRecords']:
        problem(errors, context, 'shrink dropped count disagrees with overflow causes'); complete = False
    count = numeric(fields.get('shrinkReadWaitSampleCount'), errors, context + '.sampleCount')
    sample_drops = numeric(fields.get('shrinkReadWaitSamplesDropped'), errors, context + '.sampleDrops')
    encoded = fields.get('shrinkReadWaitSamplesNS')
    samples = []
    samples_complete = encoded is not None and count is not None and count <= 2048 and sample_drops is not None
    if samples_complete:
        values = str(encoded).split(',') if encoded else []
        if len(values) != count:
            problem(errors, context, 'shrink wait sample count disagrees with array'); samples_complete = False
        else:
            values = [numeric(value, errors, context + '.waitSample') for value in values]
            if any(value is None for value in values):
                samples_complete = False
            else:
                samples = values
                reads = sum(row['readCalls'] for row in parsed) + (folded['readCalls'] or 0)
                if complete and reads != count + sample_drops:
                    problem(errors, context, 'shrink read calls disagree with sampled plus dropped reads'); samples_complete = False
    else:
        problem(errors, context, 'missing or invalid bounded shrink wait samples')
    return {'records': parsed, 'overflow': folded, 'dropped': dropped, 'samples': samples, 'sampleDrops': sample_drops,
        'complete': complete and dropped == 0, 'samplesComplete': samples_complete and sample_drops == 0}


def shrink_summary(windows, missing, have_work, file_complete):
    records, overflow = {}, collections.defaultdict(int)
    samples, sample_drops = [], 0
    for window in windows:
        for row in window['records']:
            key = (row['method'], row['reason'], row['aliasClass'])
            if key not in records:
                records[key] = {**row, **{metric: 0 for metric in SHRINK_METRICS}}
            target = records[key]
            target['metadata'] = row['metadata']
            for metric in SHRINK_METRICS:
                target[metric] += row[metric]
        if window['overflow']:
            for key, value in window['overflow'].items():
                if value is not None:
                    overflow[key] += value
        samples.extend(window['samples']); sample_drops += window['sampleDrops'] or 0
    def route_groups(field):
        groups = {}
        for row in records.values():
            target = groups.setdefault(row[field], {key: 0 for key in ('gpuCalls', 'cpuCalls', 'noopCalls', 'pixels')})
            for key in target:
                target[key] += row[key]
        return groups
    complete = have_work and not missing and file_complete and all(w['complete'] for w in windows)
    wait_complete = have_work and not missing and file_complete and all(w['samplesComplete'] for w in windows)
    ordered = sorted(samples)
    return {'profileVersion': 1 if windows else None, 'records': list(records.values()),
        'byMethod': route_groups('method'), 'byReason': route_groups('reason'), 'byAliasClass': route_groups('aliasClass'),
        'complete': complete, 'lowerBound': not complete, 'missingWindows': missing, 'legacyMethod': 'unknown' if missing else None,
        'knownDropped': sum(w['dropped'] or 0 for w in windows), 'overflowKnownTotals': dict(overflow),
        'readWait': {'count': len(ordered), 'p50MS': ordered[math.ceil(len(ordered)*.50)-1]/1e6 if ordered else None,
            'p95MS': ordered[math.ceil(len(ordered)*.95)-1]/1e6 if ordered else None, 'complete': wait_complete,
            'knownSamplesDropped': sample_drops, 'source': 'raw successful reads within ShrinkScope; nearest rank'},
        'timingBasis': 'wallNS is inclusive scope wall time; prepCPUNS is CPU preparation wall time; neither is GPU execution time',
        'transferBasis': 'subset of same-window C0 totals; parameter and temporary bytes separate; never add detail to overall transfers'}


STRUCTURED_FIELD_BYTE_LIMITS = {'stages': 1023, 'transfers': 2047, 'transferOrigins': 16383,
    'originOverflow': 511, 'frameSamplesNS': 86016, 'transitionProfiles': 131071,
    'transitionOverflow': 1023, 'shrinkProfiles': 65535, 'shrinkOverflow': 1023,
    'shrinkReadWaitSamplesNS': 43008}


def parse_field_transport(row, errors, context):
    fields = row['fields']
    version = fields.get('structuredFieldLimitsVersion')
    omitted = []
    possible = []
    valid = str(version) == '1'
    if version is not None and not valid:
        problem(errors, context, 'unknown structured field limits version')
    if 'fieldsValueOmitted' in fields or 'fieldsValueOmittedKeys' in fields:
        count = numeric(fields.get('fieldsValueOmitted'), errors, context + '.valueOmitted')
        encoded = fields.get('fieldsValueOmittedKeys')
        omitted = str(encoded).split(',') if encoded else []
        if count is None or count != len(omitted) or len(set(omitted)) != len(omitted) or any(
                key not in STRUCTURED_FIELD_BYTE_LIMITS for key in omitted):
            problem(errors, context, 'invalid oversized structured field omission metadata')
        problem(errors, context, 'structured values omitted after UTF-8 cap: ' + ','.join(omitted))
        valid = False
    for key, limit in STRUCTURED_FIELD_BYTE_LIMITS.items():
        value = fields.get(key)
        if not isinstance(value, str):
            continue
        length = len(value.encode('utf-8'))
        if str(version) == '1' and length > limit:
            problem(errors, context + '.' + key, 'structured value exceeds declared UTF-8 cap')
            omitted.append(key); valid = False
        elif version is None and length == 1024:
            # Old logger clipped every value at exactly 1024 UTF-8 bytes.
            # Equality is a risk indicator, not proof that this value was cut.
            possible.append(key)
            problem(errors, context + '.' + key, 'unmarked legacy value at 1024-byte clipping boundary; possible truncation')
    truncated = 'truncated' in fields
    if truncated:
        problem(errors, context, 'whole logger record truncated: ' + str(fields['truncated'])); valid = False
    lost = numeric(row['droppedRecords'], errors, context + '.droppedRecords') if 'droppedRecords' in row else 0
    if lost:
        problem(errors, context, 'logger reports dropped preceding records; event types unknown'); valid = False
    return {'version': version, 'complete': valid and not possible, 'omittedKeys': omitted,
        'possibleLegacyClippingKeys': possible, 'wholeRecordTruncated': truncated, 'knownDroppedRecords': lost or 0}


def transition_lifecycles(rows, errors):
    result = []
    for row in rows:
        # Native messages travel through different host log adapters. Read only
        # string values; embedded text never controls this analyzer.
        for key, value in row['fields'].items():
            if not isinstance(value, str) or not value.startswith('transition.lifecycle '):
                continue
            try:
                event = json.loads(value[len('transition.lifecycle '):])
                if not isinstance(event, dict) or event.get('stage') not in ('begin', 'end'):
                    raise ValueError('invalid lifecycle object')
                result.append({'unixTime': row.get('unixTime'), **event})
            except ValueError as error:
                problem(errors, 'transition.lifecycle.' + key, str(error))
    return result


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
    frames, frame_missing, drops_missing, dropped, bad_frames, count_bad = [], 0, 0, 0, 0, 0
    reconciliations, formats, peaks = [], set(), []
    amv = {'amvDecodedFrames': [], 'amvDecodedBytes': []}
    origins_valid = True
    transition_windows, transition_missing = [], 0
    shrink_windows, shrink_missing = [], 0
    transport_windows = []
    for index, row in enumerate(work):
        fields = row['fields']; context = 'work[%d]' % index
        transport = parse_field_transport(row, errors, context + '.fieldTransport')
        transport_windows.append(transport)
        suspect = set(transport['omittedKeys'] + transport['possibleLegacyClippingKeys'])
        all_suspect = transport['wholeRecordTruncated'] or bool(transport['knownDroppedRecords'])
        shrink = parse_shrink_profile(fields, errors, context + '.shrinks')
        if shrink is None:
            shrink_missing += 1
        else:
            if all_suspect or suspect.intersection(('shrinkProfiles', 'shrinkOverflow')):
                shrink['complete'] = False
            if all_suspect or suspect.intersection(('shrinkReadWaitSamplesNS',)):
                shrink['samplesComplete'] = False
            shrink_windows.append(shrink)
        transition = parse_transition_profile(fields, errors, context + '.transitions')
        if transition is None:
            transition_missing += 1
        else:
            if all_suspect or suspect.intersection(('transitionProfiles', 'transitionOverflow')):
                transition['complete'] = False
            transition_windows.append(transition)
        stage_values = stages(fields.get('stages'), errors, context + '.stages')
        for name, values in stage_values.items():
            total = timings[name]; total[0] += values[0]; total[1] += values[1]; total[2] = max(total[2], values[2])
        if 'script' in stage_values:
            peaks.append((stage_values['script'][2], row))
        old, old_ok = parse_transfers(fields.get('transfers'), errors, context + '.transfers')
        old_ok = old_ok and not all_suspect and 'transfers' not in suspect
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
            origins_valid &= named_ok and folded_ok and not all_suspect and not suspect.intersection(('transferOrigins', 'originOverflow'))
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
        frame_begin = len(frames)
        if all_suspect or 'frameSamplesNS' in suspect:
            count_bad += 1
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
        if 'frameSampleCount' in fields:
            count = numeric(fields['frameSampleCount'], errors, context + '.frameSampleCount')
            if count is None or count > 2048 or count != len(frames) - frame_begin:
                problem(errors, context, 'frameSampleCount disagrees with bounded parsed frame sample pairs')
                count_bad += 1
        drop = numeric(fields.get('frameSamplesDropped'), errors, context + '.frameSamplesDropped')
        if drop is None:
            drops_missing += 1
        else:
            dropped += drop
    runtime = [v[0] for v in frames if v[0] > 0]; cpu = [v[1] for v in frames]
    file_complete = not any(e['context'].startswith('line ') for e in errors)
    frames_complete = bool(work) and file_complete and not (frame_missing or drops_missing or dropped or bad_frames or count_bad)
    origin_complete = bool(work) and not legacy_count and origins_valid and not texture_missing and all(r['status'] == 'match' for r in reconciliations)
    origin_complete &= file_complete
    named_complete = origin_complete and not any(folded_totals['read'] + folded_totals['upload']) and not (
        folded_totals['capacityRecords'] or folded_totals['oversizeRecords'])
    transition_rows = {}
    transition_overflow = collections.defaultdict(int)
    for window in transition_windows:
        for detail in window['records']:
            key = (detail['requested'], detail['effective'], detail['reason'], tuple(detail['dimensions']),
                json.dumps(detail['metadata'], ensure_ascii=False, sort_keys=True))
            if key not in transition_rows:
                transition_rows[key] = {**detail, **{metric: 0 for metric in TRANSITION_METRICS}}
            target = transition_rows[key]
            for metric in TRANSITION_METRICS:
                target[metric] += detail[metric]
            target['firstTick'] = min(target['firstTick'], detail['firstTick'])
            target['lastTick'] = max(target['lastTick'], detail['lastTick'])
        if window['overflow']:
            for key, value in window['overflow'].items():
                if value is not None:
                    transition_overflow[key] += value
    transition_complete = bool(work) and not transition_missing and file_complete and all(w['complete'] for w in transition_windows)
    transition_observed = {detail['effective'] for detail in transition_rows.values()}
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
            'invalidCountOrTransportWindows': count_bad,
            'unknownRuntimeIntervalSamples': len(frames) - len(runtime), 'source': 'merged raw frameSamplesNS; nearest rank'},
        'coverage': {'fromSeconds': from_seconds, 'toSeconds': to_seconds, 'relativeTo': 'game.begin unixTime',
            'selection': 'whole interval [event.unixTime - intervalMS/1000, event.unixTime] contained in requested bounds',
            'availableWorkWindows': len(all_work), 'selectedWorkWindows': len(work), 'windows': windows, 'v2Windows': v2_count,
            'legacyWindows': legacy_count, 'missingTextureWindows': texture_missing, 'missingOverflowWindows': overflow_missing,
            'fileDataComplete': file_complete}, 'heartbeatComparison': comparison, 'issues': errors,
        'structuredFields': {'version': 1 if any(str(w['version']) == '1' for w in transport_windows) else None,
            'complete': bool(work) and all(w['complete'] for w in transport_windows),
            'unmarkedLegacyWindows': sum(w['version'] is None for w in transport_windows),
            'valueOmittedWindows': sum(bool(w['omittedKeys']) for w in transport_windows),
            'wholeRecordTruncatedWindows': sum(w['wholeRecordTruncated'] for w in transport_windows),
            'knownLoggerDroppedRecords': sum(w['knownDroppedRecords'] for w in transport_windows),
            'possibleLegacyClipping': [{'index': i, 'keys': w['possibleLegacyClippingKeys']} for i, w in enumerate(transport_windows)
                if w['possibleLegacyClippingKeys']], 'windows': transport_windows},
        'shrinks': shrink_summary(shrink_windows, shrink_missing, bool(work), file_complete),
        'transitions': {'profileVersion': 1 if transition_windows else None, 'records': list(transition_rows.values()),
            'complete': transition_complete, 'lowerBound': not transition_complete, 'missingHandlerWindows': transition_missing,
            'legacyHandler': 'unknown' if transition_missing else None,
            'knownDropped': sum(w['dropped'] or 0 for w in transition_windows), 'overflowKnownTotals': dict(transition_overflow),
            'gpuPathHandlers': list(TRANSITION_HANDLERS),
            'unobservedBuiltins': [name for name in TRANSITION_HANDLERS if name not in transition_observed],
            'supportDomain': 'Observed dimensions, immutable metadata and concrete routing reasons; unobserved variants remain unverified',
            'lifecycles': transition_lifecycles(selected_rows, errors),
            'transferBasis': 'subset of same-window C0 transfer totals; parameter uploads reported separately; never add detail rows to overall transfers'},
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
