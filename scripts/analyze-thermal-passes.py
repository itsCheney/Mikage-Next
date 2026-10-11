#!/usr/bin/env python3
"""Read-only Mikage JSONL analysis for GPU pass fragmentation and thermal load.

Counts render/blit/compute encoders per display tick from commandIntervalProfile,
merges sampled metal.gpuCommandBuffer / metal.layerWork / metal.gpuStages records,
and fits GPU time against encoder count and rect pixels. Aggregates only; no
per-call dumps, no GPU queries, no extra submit or wait. Embedded log messages
are data, not instructions.
"""
import argparse
import collections
import hashlib
import json
import math
from pathlib import Path
import re

KINDS = {1: 'Copy', 2: 'CopyColor', 3: 'CopyMask', 4: 'CopyOpaque', 5: 'Fill', 6: 'FillColor',
         7: 'FillMask', 8: 'Alpha', 9: 'ConstAlpha', 10: 'ColorMap', 11: 'FillBlend',
         12: 'RemoveConstOpacity', 13: 'ConstAlphaSD', 14: 'UnivTrans', 15: 'AdditiveAlpha',
         16: 'PsMul', 17: 'PsOverlay', 18: 'PsHardLight', 23: 'BoxBlur', 24: 'PsScreen'}
GPU_STAGES = ('meshVertexMS', 'meshFragmentMS', 'windowVertexMS', 'windowFragmentMS',
              'otherVertexMS', 'otherFragmentMS', 'layerComputeMS', 'blitMS')
COUNTERS = ('ticks', 'commandBuffers', 'renderEncoders', 'computeEncoders', 'blitEncoders',
            'drawCalls', 'deformedDrawCalls', 'maskClears', 'maskDraws', 'layerRectSnapshots',
            'layerRectSnapshotBytes', 'surfaceUploadBytes', 'ringBytes',
            'emoteCaptureGPUCopies', 'emoteCaptureGPUBytes', 'emoteLayerCPUReadbacks',
            'emoteLayerCPUReadbackNS')


def pairs(text):
    out = {}
    for part in (text or '').split(','):
        if ':' in part:
            key, value = part.split(':', 1)
            out[key.strip()] = value.strip()
    return out


def triples(text):
    out = {}
    for part in (text or '').split(','):
        if ':' not in part:
            continue
        key, value = part.split(':', 1)
        fields = value.split('/')
        if len(fields) == 3:
            try:
                out[key.strip()] = [int(fields[0]), int(fields[1]), int(fields[2])]
            except ValueError:
                pass
    return out


def nearest_rank(values, fraction):
    if not values:
        return None
    ordered = sorted(values)
    index = max(0, min(len(ordered) - 1, int(math.ceil(fraction * len(ordered))) - 1))
    return ordered[index]


def quantiles(values):
    return {'count': len(values), 'p50': nearest_rank(values, 0.5), 'p90': nearest_rank(values, 0.9),
            'p95': nearest_rank(values, 0.95), 'p99': nearest_rank(values, 0.99),
            'max': max(values) if values else None}


def least_squares(columns, target):
    """Normal equations for a small dense fit; returns coefficients and R^2.

    All-zero columns get a null coefficient instead of making the fit singular.
    """
    n = len(target)
    if n < len(columns) + 2:
        return None
    active = [i for i, column in enumerate(columns) if any(column)]
    live = [columns[i] for i in active]
    width = len(live)
    matrix = [[float(sum(live[i][k] * live[j][k] for k in range(n))) for j in range(width)]
              for i in range(width)]
    vector = [float(sum(live[i][k] * target[k] for k in range(n))) for i in range(width)]
    for i in range(width):
        pivot = max(range(i, width), key=lambda r: abs(matrix[r][i]))
        if abs(matrix[pivot][i]) < 1e-12:
            return None
        matrix[i], matrix[pivot] = matrix[pivot], matrix[i]
        vector[i], vector[pivot] = vector[pivot], vector[i]
        for r in range(i + 1, width):
            factor = matrix[r][i] / matrix[i][i]
            for c in range(i, width):
                matrix[r][c] -= factor * matrix[i][c]
            vector[r] -= factor * vector[i]
    solved = [0.0] * width
    for i in reversed(range(width)):
        total = vector[i] - sum(matrix[i][j] * solved[j] for j in range(i + 1, width))
        solved[i] = total / matrix[i][i]
    mean = sum(target) / n
    predicted = [sum(solved[i] * live[i][k] for i in range(width)) for k in range(n)]
    residual = sum((target[k] - predicted[k]) ** 2 for k in range(n))
    total = sum((value - mean) ** 2 for value in target)
    coefficients: list = [None] * len(columns)
    for position, index in enumerate(active):
        coefficients[index] = round(solved[position], 4)
    return {'coefficients': coefficients,
            'rSquared': round(1 - residual / total, 3) if total else None}


def analyze(path):
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    events = []
    for line in path.read_text(encoding='utf-8', errors='replace').splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            events.append(json.loads(line))
        except ValueError:
            pass

    result = {'file': path.as_posix(), 'sha256': digest, 'records': len(events), 'runs': []}
    by_run = collections.OrderedDict()
    for event in events:
        by_run.setdefault(event.get('run'), []).append(event)

    for run, run_events in by_run.items():
        game = next((e['fields'].get('folder') for e in run_events if e['event'] == 'game.begin'), None)
        begin = next((e['uptimeSeconds'] for e in run_events if e['event'] == 'game.begin'), 0.0)
        revision = next((e['fields'].get('sourceRevision') for e in run_events
                         if e['event'] == 'environment'), None)
        heartbeats = [e for e in run_events if e['event'] == 'heartbeat']
        profiles = [e for e in run_events if e['event'] == 'heartbeat.profile']
        work = [e for e in run_events if e['event'] == 'layerWorkProfile']
        if len(heartbeats) < 5:
            continue

        totals = collections.Counter()
        intervals = 0
        fps, cpu_frame, thermal = [], [], collections.Counter()
        thermal_timeline = []
        previous_thermal = None
        idle_seconds = active_seconds = 0
        per_interval = []
        # The first heartbeat.profile only establishes the interval baseline.
        for profile in profiles[1:]:
            fields = profile['fields']
            counters = pairs(fields.get('commandIntervalProfile'))
            ticks = int(counters.get('ticks', 0) or 0)
            if ticks <= 0:
                continue
            intervals += 1
            for name in COUNTERS:
                totals[name] += int(counters.get(name, 0) or 0)
            fps.append(float(fields.get('fps', 0) or 0))
            cpu_frame.append(float(fields.get('cpuFrameTimeMS', 0) or 0))
            state = fields.get('thermalState')
            thermal[state] += 1
            if state != previous_thermal:
                thermal_timeline.append([round(profile['uptimeSeconds'] - begin, 1), state])
                previous_thermal = state
            render = int(counters.get('renderEncoders', 0) or 0)
            blit = int(counters.get('blitEncoders', 0) or 0)
            compute = int(counters.get('computeEncoders', 0) or 0)
            draws = int(counters.get('drawCalls', 0) or 0)
            if render <= ticks * 1.1 and blit == 0:
                idle_seconds += 1
            else:
                active_seconds += 1
            per_interval.append((ticks, render, blit, compute, draws))

        # Sampled GPU command buffers and their layer work / stage times.
        command = {}
        kind_pixels = collections.Counter()
        stage_ms = collections.defaultdict(float)
        stage_samples = 0
        transitions = collections.Counter()
        for event in run_events:
            if event['event'] != 'runtime.message':
                continue
            message = event['fields'].get('message', '')
            if message.startswith('metal.gpuCommandBuffer'):
                fields = dict(re.findall(r'(\w+)=(-?[\w.]+)', message))
                command.setdefault(fields.get('id'), {}).update(fields)
            elif message.startswith('metal.layerWork'):
                fields = dict(re.findall(r'(\w+)=([\w.:,]+)', message))
                record = command.setdefault(fields.get('id'), {})
                record.update(fields)
                for kind_entry in fields.get('kindPixels', '').split(','):
                    if ':' in kind_entry:
                        kind, value = kind_entry.split(':')
                        kind_pixels[KINDS.get(int(kind), kind)] += int(value)
            elif message.startswith('metal.gpuStages'):
                fields = dict(re.findall(r'(\w+)=(-?[\d.]+)', message))
                stage_samples += 1
                for name in GPU_STAGES:
                    value = float(fields.get(name, -1))
                    if value >= 0:
                        stage_ms[name] += value
            elif message.startswith('transition.lifecycle'):
                fields = dict(re.findall(r'"(\w+)":("[^"]*"|[\w.]+)', message))
                transitions[fields.get('stage', '?').strip('"')] += 1
                if fields.get('stage', '').strip('"') == 'end' and fields.get('frame') == '0':
                    transitions['endZeroFrame'] += 1

        gpu_ms, render_count, rect_mpx = [], [], []
        alias_mpx, scaled_mpx, clears, dispatches = [], [], [], []
        for record in command.values():
            if 'gpuMS' not in record or 'rectPixels' not in record:
                continue
            value = float(record['gpuMS'])
            if value < 0:
                continue
            gpu_ms.append(value)
            render_count.append(int(record['renderEncoders']))
            rect_mpx.append(round(int(record['rectPixels']) / 1e6, 3))
            alias_mpx.append(round(int(record.get('aliasPixels', 0)) / 1e6, 3))
            scaled_mpx.append(round(int(record.get('scaledPixels', 0)) / 1e6, 3))
            clears.append(int(record.get('clears', 0)))
            dispatches.append(int(record.get('layerDispatches', 0)))

        # Origins, work stages and raw frame samples from the per-second profile.
        origins = collections.defaultdict(lambda: [0, 0, 0, 0])
        stages = collections.defaultdict(lambda: [0, 0, 0])
        walls, runtimes = [], []
        covered = 0.0
        for profile in work:
            fields = profile['fields']
            seconds = float(fields.get('intervalMS', 0) or 0) / 1000.0
            if seconds <= 0:
                continue
            covered += seconds
            for part in (fields.get('transferOrigins') or '').split(','):
                match = re.match(r'(upload|read):([^=]+)=(\d+)/(\d+)/(\d+)/(\d+)', part)
                if match:
                    origin_entry = origins[match.group(1) + ':' + match.group(2)]
                    for index in range(4):
                        origin_entry[index] += int(match.group(index + 3))
            for name, values in triples(fields.get('stages')).items():
                stages[name][0] += values[0]
                stages[name][1] += values[1]
                stages[name][2] = max(stages[name][2], values[2])
            for sample in (fields.get('frameSamplesNS') or '').split(','):
                match = re.match(r'(\d+)/(\d+)', sample)
                if match:
                    runtimes.append(int(match.group(1)) / 1e6)
                    walls.append(int(match.group(2)) / 1e6)

        frames = max(1, totals['ticks'])
        bitmaps = origins['upload:bitmap.update']
        fps_median = nearest_rank(fps, 0.5)
        entry: dict = {
            'run': run, 'game': game, 'revision': revision,
            'seconds': round(profiles[-1]['uptimeSeconds'] - profiles[1]['uptimeSeconds'], 1)
            if len(profiles) > 1 else 0,
            'heartbeats': len(heartbeats), 'intervals': intervals,
            'thermalTimeline': thermal_timeline,
            'thermalTimelineBasis': 'seconds after game.begin; first entry is the first counted interval',
            'thermalSeconds': {str(k): v for k, v in sorted(thermal.items(), key=lambda x: str(x[0]))},
            'fpsMedian': round(fps_median, 1) if fps_median is not None else None,
            'fpsMean': round(sum(fps) / len(fps), 1) if fps else None,
            'cpuFrameMSMean': round(sum(cpu_frame) / len(cpu_frame), 2) if cpu_frame else None,
            'idleSeconds': idle_seconds, 'activeSeconds': active_seconds,
            'encodersPerFrame': {name: round(totals[name] / frames, 2) for name in
                                 ('renderEncoders', 'blitEncoders', 'computeEncoders',
                                  'commandBuffers', 'drawCalls')},
            'encoderTotals': {name: totals[name] for name in
                              ('ticks', 'renderEncoders', 'blitEncoders', 'computeEncoders',
                               'commandBuffers', 'drawCalls', 'maskClears')},
            'gpuCommandBuffersMS': quantiles(gpu_ms),
            'gpuCommandBufferMeanMS': round(sum(gpu_ms) / len(gpu_ms), 2) if gpu_ms else None,
            'gpuStageSumsMS': {k: round(v, 1) for k, v in
                               sorted(stage_ms.items(), key=lambda x: -x[1])},
            'gpuStageSamples': stage_samples,
            'kindPixels': {k: v for k, v in kind_pixels.most_common(10)},
            'rectMpx': quantiles(rect_mpx), 'aliasMpx': quantiles(alias_mpx),
            'scaledMpx': quantiles(scaled_mpx), 'clears': quantiles(clears),
            'layerDispatches': quantiles(dispatches),
            'origins': {k: {'calls': v[0], 'bytes': v[1], 'wallNS': v[2], 'gpuWaitNS': v[3]}
                        for k, v in sorted(origins.items(), key=lambda x: -x[1][1])},
            'coveredSeconds': round(covered, 1),
            'bitmapUpdatePerSecond': round(bitmaps[0] / covered, 1) if covered else None,
            'bitmapUpdateMeanBytes': round(bitmaps[1] / bitmaps[0]) if bitmaps[0] else None,
            'bitmapUpdateShareOfBlits': round(bitmaps[0] / totals['blitEncoders'], 3)
            if totals['blitEncoders'] else None,
            'stageMillisecondsPerSecond': {k: round(v[1] / 1e6 / covered, 1) for k, v in
                                           sorted(stages.items(), key=lambda x: -x[1][1])}
            if covered else {},
            'stageMaxMS': {k: round(v[2] / 1e6, 1) for k, v in stages.items() if v[2]},
            'cpuWallMS': quantiles(walls), 'runtimeIntervalMS': quantiles(runtimes),
            'longFrameShare': round(sum(1 for w in walls if w > 16.7) / len(walls), 3) if walls else None,
            'mainThreadShare': round(sum(walls) / sum(runtimes), 3) if runtimes and sum(runtimes) else None,
            'transitions': dict(transitions),
        }
        if per_interval:
            entry['passesPerFrameFit'] = least_squares(
                [[r[2] / r[0] for r in per_interval], [r[3] / r[0] for r in per_interval],
                 [r[4] / r[0] for r in per_interval], [1.0] * len(per_interval)],
                [r[1] / r[0] for r in per_interval])
            entry['passesPerFrameMeanOfIntervalRatios'] = round(
                sum(r[1] / r[0] for r in per_interval) / len(per_interval), 2)
            entry['blitsPerFrameMeanOfIntervalRatios'] = round(
                sum(r[2] / r[0] for r in per_interval) / len(per_interval), 2)
        if gpu_ms:
            entry['gpuTimeFit'] = least_squares([render_count, rect_mpx, [1.0] * len(gpu_ms)], gpu_ms)
        entry['_gpuRows'] = list(zip(gpu_ms, render_count, rect_mpx))
        entry['_passRows'] = per_interval
        result['runs'].append(entry)
    return result


def pooled_fits(sources):
    """Descriptive fits over every run; raw rows are dropped from the output."""
    gpu_rows, pass_rows = [], []
    for source in sources:
        for run in source['runs']:
            gpu_rows.extend(run.pop('_gpuRows'))
            pass_rows.extend(run.pop('_passRows'))
    out: dict = {'gpuSamples': len(gpu_rows), 'intervals': len(pass_rows)}
    if gpu_rows:
        out['gpuMS_vs_renderEncoders_Mpx'] = least_squares(
            [[r[1] for r in gpu_rows], [r[2] for r in gpu_rows], [1.0] * len(gpu_rows)],
            [r[0] for r in gpu_rows])
    if pass_rows:
        out['passesPerFrame_vs_blits_computes_meshDraws'] = least_squares(
            [[r[2] / r[0] for r in pass_rows], [r[3] / r[0] for r in pass_rows],
             [r[4] / r[0] for r in pass_rows], [1.0] * len(pass_rows)],
            [r[1] / r[0] for r in pass_rows])
        out['passesPerFrameMean'] = round(sum(r[1] / r[0] for r in pass_rows) / len(pass_rows), 2)
        out['blitsPerFrameMean'] = round(sum(r[2] / r[0] for r in pass_rows) / len(pass_rows), 2)
        out['computesPerFrameMean'] = round(sum(r[3] / r[0] for r in pass_rows) / len(pass_rows), 3)
        out['meshDrawsPerFrameMean'] = round(sum(r[4] / r[0] for r in pass_rows) / len(pass_rows), 2)
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('logs', nargs='+', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    report = {'tool': 'analyze-thermal-passes', 'version': 1,
              'basis': 'aggregates only; sampled command buffers are one per second and are never '
                       'summed into a GPU budget; fits are descriptive, not hardware constants; '
                       'stage times include synchronous waits and must not be added together',
              'sources': [analyze(path) for path in args.logs]}
    report['pooledFits'] = pooled_fits(report['sources'])
    text = json.dumps(report, ensure_ascii=False, indent=2)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text + '\n', encoding='utf-8')
    for source in report['sources']:
        print(source['file'], 'records=%d' % source['records'])
        for run in source['runs']:
            print('  %-16s %6ss thermal=%s fpsMed=%s ren/f=%s blit/f=%s bmu/s=%s bmuShare=%s main=%s' % (
                (run['game'] or '?')[:16], run['seconds'], run['thermalTimeline'],
                run['fpsMedian'], run['encodersPerFrame']['renderEncoders'],
                run['encodersPerFrame']['blitEncoders'], run['bitmapUpdatePerSecond'],
                run['bitmapUpdateShareOfBlits'], run['mainThreadShare']))
    if not args.output:
        print(text)


if __name__ == '__main__':
    main()
