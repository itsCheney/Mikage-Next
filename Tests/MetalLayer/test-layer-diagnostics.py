#!/usr/bin/env python3
"""Independent JSONL fixtures for coverage, attribution, and raw frame statistics."""
import importlib.util
import json
import sys
from pathlib import Path
import tempfile
import unittest
from urllib.parse import quote

ROOT = Path(__file__).resolve().parents[2]
sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location('layer_diagnostics', ROOT / 'scripts/analyze-layer-diagnostics.py')
analysis = importlib.util.module_from_spec(spec)
spec.loader.exec_module(analysis)


def row(event, timestamp, session='A', **fields):
    return {'event': event, 'unixTime': timestamp, 'fields': {'gameSession': session, **fields}}


def work(timestamp=105, session='A', **overrides):
    fields = {'intervalMS': '5000', 'workProfileVersion': '2', 'stages': 'script:1/10/8,software:2/30/20',
        'transfers': 'read:load@1(2x2)=1/10/15/2,read:load@2(2x2)=1/10/15/2,upload:update@3(2x2)=1/10/11/2',
        'transferOrigins': 'read:load=2/20/30/4,upload:update=1/10/11/2',
        'originOverflow': 'read=0/0/0/0,upload=0/0/0/0,capacityRecords=0,oversizeRecords=0',
        'frameSamplesNS': '16000000/1000000,17000000/2000000', 'frameSamplesDropped': '0',
        'amvDecodedFrames': '0', 'amvDecodedBytes': '0'}
    fields.update(overrides)
    return row('layerWorkProfile', timestamp, session, **fields)


def heart(timestamp=106, session='A', **overrides):
    fields = {'layerReadbackBytes': '20', 'layerUploadedBytes': '10', 'layerReadbackBySource': 'fallback:20/2',
        'fps': '60', 'maxCpuFrameTimeMS': '4', 'residentBytes': '100', 'thermalState': 'nominal'}
    fields.update(overrides)
    return row('heartbeat', timestamp, session, **fields)


def transition_fields(records=(), dropped=0, **overflow_overrides):
    overflow = {key: 0 for key in (*analysis.TRANSITION_METRICS, 'capacityRecords', 'oversizeRecords')}
    overflow.update(overflow_overrides)
    return {'transitionProfileVersion': '1', 'transitionProfiles': json.dumps(list(records), ensure_ascii=False),
        'transitionProfilesDropped': str(dropped), 'transitionOverflow': json.dumps(overflow)}


def transition_record(**overrides):
    record = {key: 0 for key in analysis.TRANSITION_METRICS}
    record.update(requested='custom.alias', effective='mosaic', reason='applied', metadata='{"maxsize":31}',
        dimensions=[127, 91, 131, 97, 139, 101, 17, 13], firstTick=500, lastTick=700, frames=3,
        gpuCalls=5, pixels=105, parameterUploads=5, parameterBytes=420)
    record.update(overrides)
    return record


def shrink_fields(records=(), samples=(), sample_drops=0, dropped=0, **overflow_overrides):
    overflow = {key: 0 for key in (*analysis.SHRINK_METRICS, 'capacityRecords', 'oversizeRecords')}
    overflow.update(overflow_overrides)
    return {'shrinkProfileVersion': '1', 'shrinkProfiles': json.dumps(list(records)),
        'shrinkProfilesDropped': str(dropped), 'shrinkOverflow': json.dumps(overflow),
        'shrinkReadWaitSampleCount': str(len(samples)), 'shrinkReadWaitSamplesNS': ','.join(map(str, samples)),
        'shrinkReadWaitSamplesDropped': str(sample_drops)}


def shrink_record(**overrides):
    record = {key: 0 for key in analysis.SHRINK_METRICS}
    record.update(method='shrinkCopy', reason='applied', aliasClass='distinct', metadata='{"sourceROI":[1,2,3,4]}', gpuCalls=1, pixels=35)
    record.update(overrides)
    return record


def consumer_event(timestamp=102, phase='read', **overrides):
    event = {'phase': phase, 'generation': 1, 'sessionID': 7, 'traceID': 11, 'readID': 21,
        'textureID': 441, 'contentVersion': 4, 'method': 'Layer.saveLayerImage', 'access': 'read',
        'nativeEntry': 'native.saveLayerImage', 'owner': 12, 'source': 'load', 'width': 2, 'height': 2,
        'lastWriter': 'Layer.shrinkCopy', 'lastWrite': [0, 0, 2, 2],
        'lastSubmittedID': None, 'renderFrame': None, 'calls': 1, 'bytes': 10, 'wallNS': 15, 'waitNS': 2}
    if phase == 'caller':
        event = {'phase': phase, 'generation': 1, 'sessionID': 7, 'traceID': 11, 'readID': 21,
                 'traceState': 'captured', 'positions': 'unverified', 'trace': 'save.ks:23'}
    if phase == 'shrink':
        event = {'phase': phase, 'generation': 1, 'sessionID': 7, 'traceID': 10, 'textureID': 441,
                 'contentVersion': 4, 'method': 'Layer.shrinkCopy', 'nativeEntry': 'native.shrinkCopy',
                 'outputROI': [0, 0, 2, 2], 'width': 2, 'height': 2}
    if phase == 'budget':
        event = {'phase': phase, 'generation': 1, 'readRecords': 1, 'readExceeded': 0,
                 'callerRecords': 1, 'callerExceeded': 0, 'producerRecords': 1,
                 'producerExceeded': 0, 'oversizeRecords': 0}
    event.update(overrides)
    prefix = 'metal.cpuProducer ' if phase == 'shrink' else 'metal.cpuConsumer '
    return row('native.log', timestamp, message=prefix + json.dumps(event, ensure_ascii=False))


def span_event(phase, timestamp=106, **overrides):
    event = {'version': 2, 'phase': phase, 'generation': 1, 'windowID': 50}
    if phase in ('aggregate', 'sample'):
        event.update(method='drawLine', route='gpu', reason='applied', spanCount=4,
                     sourceBytes=16, parameterBytes=188, scratchBytes=1024)
    if phase == 'aggregate':
        event['calls'] = 1
    elif phase == 'sample':
        event.update(sampleIndex=0, traceID=40, sessionID=None, textureID=None, contentVersion=None)
    else:
        event.update(calls=1, gpuCalls=1, cpuCalls=0, noopCalls=0, spanCount=4,
                     sourceBytes=16, parameterBytes=188, scratchBytes=1024, aggregateRows=1,
                     samples=1, repeatedOmitted=0, capacityOmitted=0, overflow=False, invalidRecords=0)
    event.update(overrides)
    return row('native.log', timestamp, message='metal.layerSpan ' + json.dumps(event))


class LayerDiagnosticsTests(unittest.TestCase):
    def read_aggregates(self, calls=71):
        base = dict(version=2, generation=1, windowID=50)
        metrics = dict(calls=calls, bytes=calls*10, wallNS=calls*15, waitNS=calls*2)
        events = [dict(base, phase='aggregate', groupIndex=0, method='drawImageStretch', nativeEntry='method',
                       access='write', source='load', **metrics)]
        events += [dict(base, phase='overflow', reason=reason, calls=0, bytes=0, wallNS=0, waitNS=0)
                   for reason in ('capacity', 'oversize')]
        events += [dict(base, phase='window', aggregateRows=1, capacityRecords=0, oversizeRecords=0,
                        repeatedReads=calls-1, readRecords=min(calls,32), readExceeded=max(0,calls-32),
                        callerRecords=8, callerExceeded=max(0,min(calls,32)-8), producerRecords=0,
                        producerExceeded=0, overflow=False, **metrics)]
        return [row('native.log', 106, message='metal.cpuConsumerAggregate '+json.dumps(e)) for e in events]

    def test_complete_cpu_reads_survive_detail_budget_and_match_c0(self):
        event=work(spanRouteWindowID='50',transfers='read:load=71/710/1065/142',
                   transferOrigins='read:load=71/710/1065/142')
        result=self.summarize(self.basic(event,*self.read_aggregates(),heart()))
        aggregate=result['cpuReadAggregates']
        self.assertTrue(aggregate['complete'])
        self.assertEqual(aggregate['totals']['calls'],71)
        self.assertEqual(aggregate['windows'][0]['originReconciliation'][0]['status'],'match')
        self.assertEqual(aggregate['byConsumer'][0]['bytes'],710)

    def test_cpu_read_missing_duplicate_and_generation_rows_are_incomplete(self):
        for mutate in ('missing','duplicate','generation','total'):
            events=self.read_aggregates()
            if mutate=='missing': events.pop(0)
            elif mutate=='duplicate': events.insert(0,events[0])
            else:
                e=json.loads(events[0]['fields']['message'].split(' ',1)[1])
                e['generation' if mutate=='generation' else 'bytes']+=1
                events[0]['fields']['message']='metal.cpuConsumerAggregate '+json.dumps(e)
            result=self.summarize(self.basic(work(spanRouteWindowID='50'),*events,heart()))
            self.assertFalse(result['cpuReadAggregates']['complete'],mutate)

    def test_legacy_cpu_read_aggregates_stay_unknown(self):
        result=self.summarize(self.basic(work(),consumer_event(),heart()))
        self.assertIsNone(result['cpuReadAggregates']['totals'])

    def test_cpu_read_overflow_totals_match_c0_without_inventing_origin(self):
        events=self.read_aggregates()
        for index in (1,3):
            e=json.loads(events[index]['fields']['message'].split(' ',1)[1])
            if index==1: e.update(calls=1,bytes=10,wallNS=15,waitNS=2)
            else: e.update(calls=72,bytes=720,wallNS=1080,waitNS=144,capacityRecords=1,readExceeded=40)
            events[index]['fields']['message']='metal.cpuConsumerAggregate '+json.dumps(e)
        result=self.summarize(self.basic(work(spanRouteWindowID='50',transfers='read:load=72/720/1080/144',
            transferOrigins='read:load=72/720/1080/144'),*events,heart()))['cpuReadAggregates']
        self.assertTrue(result['complete'])
        self.assertFalse(result['namedConsumersComplete'])
        self.assertEqual(result['windows'][0]['totalC0Reconciliation']['status'],'match')
        self.assertEqual(result['byConsumer'][0]['calls'],71)

    def span_summary(self, events, **bounds):
        return self.summarize(self.basic(work(spanRouteWindowID='50'), *events, heart()), **bounds)['layerSpans']

    def test_span_v3_five_methods_and_protected_slots(self):
        events=[]
        for index,method in enumerate(analysis.SPAN_METHODS_V3):
            events += [span_event('aggregate',version=3,method=method),
                       span_event('sample',version=3,method=method,sampleIndex=index*3)]
        events += [span_event('window',version=3,calls=5,gpuCalls=5,aggregateRows=5,samples=5,
                             spanCount=20,sourceBytes=80,parameterBytes=940,scratchBytes=5120)]
        result=self.span_summary(events)
        self.assertTrue(result['complete'])
        self.assertTrue(result['representativeCoverageComplete'])
        self.assertEqual(result['version'],3)
        self.assertEqual(result['totals']['calls'],5)
        events[3]['fields']['message']=events[3]['fields']['message'].replace('"sampleIndex": 3','"sampleIndex": 9')
        self.assertFalse(self.span_summary(events)['representativeCoverageComplete'])

    def test_span_v2_71_calls_have_totals_without_adding_representative(self):
        metrics = {name: value * 71 for name, value in
                   dict(spanCount=4, sourceBytes=16, parameterBytes=188, scratchBytes=1024).items()}
        result = self.span_summary([span_event('aggregate', calls=71, **metrics), span_event('sample'),
                                    span_event('window', calls=71, gpuCalls=71, repeatedOmitted=70, **metrics)])
        self.assertTrue(result['totalsComplete'])
        self.assertTrue(result['breakdownComplete'])
        self.assertTrue(result['representativeCoverageComplete'])
        self.assertTrue(result['complete'])
        self.assertEqual(result['totals']['calls'], 71)
        self.assertEqual(result['byRoute'][0]['calls'], 71)
        self.assertEqual(len(result['records']), 1)
        self.assertEqual(result['detailOmissions']['repeatedOmitted'], 70)

    def test_span_v2_thousand_gpu_calls_after_cpu_keep_both_routes(self):
        gpu = dict(spanCount=4000, sourceBytes=16000, parameterBytes=188000, scratchBytes=1024000)
        result = self.span_summary([
            span_event('aggregate', route='cpu', reason='record', calls=1, spanCount=0, sourceBytes=0, parameterBytes=0, scratchBytes=0),
            span_event('aggregate', calls=1000, **gpu),
            # Both route decisions may share an enclosing ConsumerScope trace.
            span_event('sample', route='cpu', reason='record', sampleIndex=1, traceID=40,
                       spanCount=0, sourceBytes=0, parameterBytes=0, scratchBytes=0),
            span_event('sample'),
            span_event('window', calls=1001, gpuCalls=1000, cpuCalls=1, aggregateRows=2, samples=2,
                       repeatedOmitted=999, **gpu)])
        self.assertTrue(result['complete'])
        self.assertEqual(result['totals']['gpuCalls'], 1000)
        self.assertEqual(result['totals']['cpuCalls'], 1)
        self.assertEqual({r['route']: r['calls'] for r in result['byRoute']}, {'cpu': 1, 'gpu': 1000})

    def test_span_v2_capacity_omissions_leave_aggregation_complete(self):
        events = [span_event('aggregate', reason='reason%d' % i) for i in range(35)]
        # One protected method/route slot and 23 first-other-reason slots.
        events += [span_event('sample', reason='reason%d' % i, traceID=40 + i,
                              sampleIndex=0 if i == 0 else i + 8) for i in range(24)]
        metrics = {name: value * 35 for name, value in
                   dict(spanCount=4, sourceBytes=16, parameterBytes=188, scratchBytes=1024).items()}
        events.append(span_event('window', calls=35, gpuCalls=35, aggregateRows=35, samples=24,
                                 capacityOmitted=11, **metrics))
        result = self.span_summary(events)
        self.assertTrue(result['totalsComplete'])
        self.assertTrue(result['breakdownComplete'])
        self.assertTrue(result['complete'])
        self.assertFalse(result['representativeCoverageComplete'])
        self.assertEqual(result['detailOmissions']['unrepresentedGroups'], 11)
        self.assertEqual(result['detailOmissions']['capacityOmitted'], 11)
        self.assertEqual(result['totals']['calls'], 35)

    def test_span_v2_empty_window_is_known_zero(self):
        result = self.span_summary([span_event('window', calls=0, gpuCalls=0, spanCount=0,
                                   sourceBytes=0, parameterBytes=0, scratchBytes=0,
                                   aggregateRows=0, samples=0)])
        self.assertTrue(result['complete'])
        self.assertTrue(result['representativeCoverageComplete'])
        self.assertEqual(result['totals']['calls'], 0)
        self.assertEqual(result['records'], [])

    def test_span_v2_all_nine_protected_slots_and_23_reason_slots(self):
        keys = [(method, route, 'applied') for method in analysis.SPAN_METHODS for route in analysis.SPAN_ROUTES]
        keys += [('drawLine', 'gpu', 'extra%d' % i) for i in range(23)]
        events = []
        for index, (method, route, reason) in enumerate(keys):
            events += [span_event('aggregate', method=method, route=route, reason=reason),
                       span_event('sample', method=method, route=route, reason=reason,
                                  sampleIndex=index, traceID=100 + index)]
        metrics = {name: value * 32 for name, value in
                   dict(spanCount=4, sourceBytes=16, parameterBytes=188, scratchBytes=1024).items()}
        events.append(span_event('window', calls=32, gpuCalls=26, cpuCalls=3, noopCalls=3,
                                 aggregateRows=32, samples=32, **metrics))
        result = self.span_summary(events)
        self.assertTrue(result['complete'])
        self.assertTrue(result['representativeCoverageComplete'])
        self.assertEqual(len(result['records']), 32)
        self.assertEqual(result['totals']['calls'], 32)

    def test_span_v2_missing_footer_and_work_id_are_unknown(self):
        result = self.span_summary([span_event('aggregate'), span_event('sample')])
        self.assertFalse(result['totalsComplete'])
        self.assertIsNone(result['totals'])
        self.assertIsNone(result['knownTotals'])
        self.assertIsNone(result['windows'][0]['totals'])
        self.assertTrue(any('missing native window footer' in i['message'] for i in result['issues']))
        result = self.summarize(self.basic(work(), span_event('window')))['layerSpans']
        self.assertFalse(result['totalsComplete'])
        self.assertEqual(result['coverage']['missingWorkWindowIDs'], 1)
        self.assertIsNone(result['totals'])
        result = self.span_summary([])
        self.assertEqual(result['version'], 2)
        self.assertIsNone(result['totals'])

    def test_span_v2_sample_loss_is_separate_from_totals_and_breakdown(self):
        result = self.span_summary([span_event('aggregate'), span_event('window')])
        self.assertTrue(result['totalsComplete'])
        self.assertTrue(result['breakdownComplete'])
        self.assertFalse(result['representativeCoverageComplete'])
        self.assertEqual(result['detailOmissions']['missingSamples'], 1)
        self.assertTrue(any('missing sample' in i['message'] for i in result['issues']))

    def test_span_v2_aggregate_loss_and_metric_mismatch_are_explicit(self):
        for events in ([span_event('sample'), span_event('window')],
                       [span_event('aggregate', spanCount=3), span_event('sample'), span_event('window')],
                       [span_event('aggregate', route='cpu'), span_event('sample'), span_event('window')]):
            result = self.span_summary(events)
            self.assertTrue(result['totalsComplete'])
            self.assertFalse(result['breakdownComplete'])
            self.assertTrue(result['issues'])
        result = self.span_summary([span_event('aggregate'), span_event('sample'),
                                    span_event('window', gpuCalls=0)])
        self.assertFalse(result['totalsComplete'])
        self.assertTrue(any('route counts' in i['message'] for i in result['issues']))

    def test_span_v2_duplicate_footer_group_sample_and_work_id(self):
        base = [span_event('aggregate'), span_event('sample'), span_event('window')]
        for phase, field in (('window', 'totalsComplete'), ('aggregate', 'breakdownComplete'),
                             ('sample', 'representativeCoverageComplete')):
            result = self.span_summary([*base, span_event(phase)])
            self.assertFalse(result[field])
            self.assertTrue(any('duplicate' in i['message'] for i in result['issues']))
            self.assertEqual(result['byRoute'][0]['calls'], 1)
        result = self.summarize(self.basic(work(spanRouteWindowID='50'), work(spanRouteWindowID='50'), *base))['layerSpans']
        self.assertFalse(result['totalsComplete'])
        self.assertEqual(result['coverage']['duplicateWorkWindowIDs'], [50])

    def test_span_v2_duplicate_sample_slot_and_missing_protected_slot(self):
        metrics = {name: value * 2 for name, value in
                   dict(spanCount=4, sourceBytes=16, parameterBytes=188, scratchBytes=1024).items()}
        result = self.span_summary([span_event('aggregate'), span_event('aggregate', reason='other'),
            span_event('sample'), span_event('sample', reason='other', traceID=41),
            span_event('window', calls=2, gpuCalls=2, aggregateRows=2, samples=2, **metrics)])
        self.assertFalse(result['representativeCoverageComplete'])
        self.assertTrue(result['breakdownComplete'])
        self.assertTrue(any('duplicate sample' in i['message'] for i in result['issues']))
        result = self.span_summary([span_event('aggregate'), span_event('sample', sampleIndex=9), span_event('window')])
        self.assertFalse(result['representativeCoverageComplete'])
        self.assertTrue(any('protected' in i['message'] for i in result['issues']))

    def test_span_v1_old_39_record_budget_stays_unknown(self):
        result = self.summarize(self.basic(work(), consumer_event(phase='budget'), heart()))['layerSpans']
        self.assertIsNone(result['version'])
        self.assertFalse(result['complete'])
        self.assertTrue(result['lowerBound'])

    def test_span_v2_malformed_fields_overflow_invalid_and_omissions(self):
        base = [span_event('aggregate'), span_event('sample')]
        for override in ({'overflow': True}, {'invalidRecords': 1}, {'repeatedOmitted': 1},
                         {'samples': 33}, {'overflow': 'false'}, {'calls': -1}):
            result = self.span_summary([*base, span_event('window', **override)])
            self.assertFalse(result['totalsComplete'])
            self.assertIsNone(result['totals'])
            self.assertTrue(result['issues'])
        for override in ({'sampleIndex': 32}, {'sampleIndex': 1}, {'textureID': 1},
                         {'reason': 'x' * 49}, {'sourceBytes': 200}, {'traceID': False}):
            result = self.span_summary([span_event('aggregate'), span_event('sample', **override), span_event('window')])
            self.assertFalse(result['representativeCoverageComplete'])
            self.assertTrue(result['totalsComplete'])
            self.assertGreater(result['malformedRecords'], 0)
        sample = json.loads(span_event('sample')['fields']['message'].split(' ', 1)[1])
        del sample['sessionID']
        result = self.span_summary([span_event('aggregate'),
            row('native.log', 106, message='metal.layerSpan ' + json.dumps(sample)), span_event('window')])
        self.assertFalse(result['representativeCoverageComplete'])

    def test_span_v2_native_invalid_label_keeps_footer_totals_and_invalid_count(self):
        # Native Record counts an invocation in totals before dictionary lookup;
        # an invalid label has no route/group/sample or omission attribution.
        metrics = {name: value * 2 for name, value in
                   dict(spanCount=4, sourceBytes=16, parameterBytes=188, scratchBytes=1024).items()}
        result = self.span_summary([span_event('aggregate', calls=2, **metrics), span_event('sample'),
            span_event('window', calls=3, gpuCalls=2, repeatedOmitted=1, invalidRecords=1, **metrics)])
        self.assertFalse(result['totalsComplete'])
        self.assertIsNone(result['totals'])
        self.assertEqual(result['knownTotals']['calls'], 3)
        self.assertEqual(result['detailOmissions']['invalidRecords'], 1)
        self.assertEqual(result['windows'][0]['footer']['calls'], 3)
        self.assertTrue(any('invalidRecords' in i['message'] for i in result['issues']))

    def test_span_v2_overflow_preserves_raw_footer_even_with_inconsistent_byte_ratio(self):
        maximum = (1 << 64) - 1
        result = self.span_summary([span_event('aggregate'), span_event('sample'),
            span_event('window', calls=maximum, gpuCalls=maximum, sourceBytes=maximum,
                       parameterBytes=1, overflow=True)])
        self.assertFalse(result['totalsComplete'])
        self.assertFalse(result['breakdownComplete'])
        self.assertIsNone(result['totals'])
        self.assertEqual(result['knownTotals']['sourceBytes'], maximum)
        self.assertEqual(result['knownTotals']['parameterBytes'], 1)
        self.assertTrue(result['windows'][0]['footer']['overflow'])
        self.assertTrue(any('countersOverflow' in i['message'] for i in result['issues']))

    def test_span_v2_whole_window_selection_ignores_emission_timestamps(self):
        events = [span_event('aggregate', timestamp=109), span_event('sample', timestamp=99),
                  span_event('window', timestamp=109)]
        result = self.span_summary(events, from_seconds=0, to_seconds=5)
        self.assertTrue(result['totalsComplete'])
        self.assertEqual(result['totals']['calls'], 1)
        self.assertEqual(len(result['records']), 1)
        result = self.span_summary(events, from_seconds=1, to_seconds=5)
        self.assertFalse(result['totalsComplete'])
        self.assertEqual(result['windows'], [])
        self.assertEqual(result['records'], [])
        self.assertIsNone(result['totals'])

    def test_span_v2_multiple_windows_do_not_mix_generations_or_unselected_details(self):
        first = [span_event('aggregate'), span_event('sample'), span_event('window')]
        second = [span_event(phase, windowID=51, generation=2, timestamp=115)
                  for phase in ('aggregate', 'sample', 'window')]
        result = self.summarize(self.basic(work(spanRouteWindowID='50'), *first,
            work(timestamp=110, spanRouteWindowID='51'), *second), from_seconds=5, to_seconds=10)['layerSpans']
        self.assertTrue(result['complete'])
        self.assertEqual(result['totals']['calls'], 1)
        self.assertEqual(result['windows'][0]['generation'], 2)
        self.assertEqual(result['windows'][0]['windowID'], 51)
        result = self.span_summary([span_event('aggregate', generation=2), span_event('sample'), span_event('window')])
        self.assertFalse(result['breakdownComplete'])
        self.assertTrue(any('generation' in i['message'] for i in result['issues']))

    def test_span_v2_file_loss_and_oversize_are_incomplete(self):
        result = self.summarize(self.basic(work(spanRouteWindowID='50'), span_event('aggregate'),
            span_event('sample'), span_event('window'), '{truncated json'))['layerSpans']
        self.assertFalse(result['totalsComplete'])
        oversized = span_event('sample', ignored='x' * 900)
        result = self.span_summary([span_event('aggregate'), oversized, span_event('window')])
        self.assertFalse(result['representativeCoverageComplete'])
        self.assertEqual(result['malformedRecords'], 1)

    def test_span_route_packet_metrics_are_not_transfers(self):
        span = {'phase': 'route', 'version': 1, 'generation': 1, 'traceID': 40,
                'method': 'drawLine', 'route': 'gpu', 'reason': 'applied',
                'sessionID': 7, 'textureID': 441, 'contentVersion': 5,
                'spanCount': 4, 'sourceBytes': 16, 'parameterBytes': 188, 'scratchBytes': 1024}
        records = [row('game.begin', 100), row('native.log', 102, message='metal.layerSpan ' + json.dumps(span)),
                   consumer_event(104, phase='budget', readRecords=0, callerRecords=0, producerRecords=0,
                                  spanRouteRecords=1, spanRouteExceeded=0), work(), heart()]
        summary = self.summarize(records)
        result = summary['layerSpans']
        self.assertTrue(result['complete'])
        self.assertEqual(result['byRoute'][0]['parameterBytes'], 188)
        self.assertEqual(result['byRoute'][0]['calls'], 1)
        self.assertEqual(summary['cpuConsumers']['sampledReadTotals']['bytes'], 0)
        self.assertEqual(self.summarize([row('game.begin', 100), work(), heart()])['layerSpans']['version'], None)

    def test_span_route_loss_unknown_target_and_validation(self):
        span = {'phase': 'route', 'version': 1, 'generation': 1, 'traceID': 40,
                'method': 'drawPath', 'route': 'cpu', 'reason': 'record',
                'sessionID': None, 'textureID': None, 'contentVersion': None,
                'spanCount': 0, 'sourceBytes': 0, 'parameterBytes': 0, 'scratchBytes': 0}
        event = row('native.log', 102, message='metal.layerSpan ' + json.dumps(span))
        budget = consumer_event(104, phase='budget', spanRouteRecords=1, spanRouteExceeded=3)
        result = self.summarize([row('game.begin', 100), event, budget])['layerSpans']
        self.assertFalse(result['complete'])
        self.assertEqual(result['sampling']['spanRouteExceeded'], 3)
        self.assertIsNone(result['records'][0]['textureID'])
        duplicate = self.summarize([row('game.begin', 100), event, event, budget])['layerSpans']
        self.assertEqual(duplicate['malformedRecords'], 1)
        span['textureID'] = 1
        invalid = row('native.log', 103, message='metal.layerSpan ' + json.dumps(span))
        self.assertEqual(self.summarize([row('game.begin', 100), invalid])['layerSpans']['malformedRecords'], 1)

    def summarize(self, records, **bounds):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'fixtures.jsonl'
            path.write_text('\n'.join(json.dumps(r, ensure_ascii=False) if isinstance(r, dict) else r for r in records), encoding='utf-8')
            return analysis.summarize(path, **bounds)

    def basic(self, *records):
        return [row('environment', 99, 'none', sourceRevision='abcdef012345'), row('game.begin', 100, folder='fixture'), *records]

    def test_production_texture_lease_commas_and_other_reconcile(self):
        event = work(transfers='read:bitmap.scanline@441(496x279,lease=0)=1/553536/13731833/11703750,'
            'read:bitmap.scanline@8670(496x279,lease=1)=1/553536/6772375/6684167,'
            'upload:bitmap.update@5(2x2,lease=0)=1/16/90/0,upload:other=2/32/100/0',
            transferOrigins='read:bitmap.scanline=2/1107072/20504208/18387917,'
                'upload:bitmap.update=3/48/190/0')
        result = self.summarize(self.basic(event, heart(layerReadbackBytes='1107072', layerUploadedBytes='48')))
        self.assertTrue(result['originTotalsComplete'])
        self.assertEqual(result['originWindowReconciliation'][0]['status'], 'match')
        self.assertEqual(result['transfersCallsBytesWallNSWaitNS']['read:bitmap.scanline'],
            [2, 1107072, 20504208, 18387917])
        self.assertEqual(result['heartbeatComparison']['read']['status'], 'match')
        self.assertFalse(any('malformed transfer' in issue['message'] for issue in result['issues']))

    def test_texture_metadata_malformed_tail_stays_incomplete(self):
        errors = []
        values, complete = analysis.parse_transfers('read:bitmap.scanline@1(2x2,lease=0)=1/16/5/2,'
            'read:bitmap.scanline@2(2x2,lease=0)=1/16/5', errors, 'texture')
        self.assertFalse(complete)
        self.assertEqual(values['read:bitmap.scanline'], [1, 16, 5, 2])
        self.assertTrue(errors)

    def test_cpu_consumer_exact_version_link_and_subset_no_double_count(self):
        result = self.summarize(self.basic(consumer_event(101, 'shrink'), consumer_event(),
            consumer_event(102.1, 'caller'), consumer_event(104, 'budget'), work(), heart()))
        detail = result['cpuConsumers']
        self.assertTrue(detail['wholeTextureReadDetailsCovered'])
        self.assertEqual(detail['correlatedShrinkReads'], 1)
        self.assertEqual(detail['records'][0]['caller']['trace'], 'save.ks:23')
        self.assertEqual(detail['records'][0]['shrinkProducer']['textureID'], 441)
        self.assertEqual(detail['sampledReadTotals'], {'calls': 1, 'bytes': 10, 'wallNS': 15, 'waitNS': 2})
        self.assertEqual(detail['byConsumer'][0]['method'], 'Layer.saveLayerImage')
        self.assertEqual(result['transfersCallsBytesWallNSWaitNS']['read:load'], [2, 20, 30, 4])

    def test_cpu_consumer_dimensions_do_not_prove_generation_session_or_version(self):
        for key in ('generation', 'sessionID', 'contentVersion', 'textureID'):
            result = self.summarize(self.basic(consumer_event(101, 'shrink', **{key: 99}), consumer_event(), work(), heart()))
            self.assertEqual(result['cpuConsumers']['correlatedShrinkReads'], 0, key)
        result = self.summarize(self.basic(consumer_event(103, 'shrink'), consumer_event(), work(), heart()))
        self.assertEqual(result['cpuConsumers']['correlatedShrinkReads'], 0)

    def test_cpu_consumer_sampling_loss_and_legacy_unknown_are_explicit(self):
        result = self.summarize(self.basic(work(), heart()))
        self.assertIsNone(result['cpuConsumers']['sampledReadTotals'])
        self.assertEqual(result['cpuConsumers']['legacyConsumer'], 'unknown')
        result = self.summarize(self.basic(consumer_event(), consumer_event(104, 'budget', readExceeded=3,
            producerRecords=0, callerRecords=0, callerExceeded=1, oversizeRecords=2), work(), heart()))
        detail = result['cpuConsumers']
        self.assertFalse(detail['wholeTextureReadDetailsCovered'])
        self.assertEqual(detail['sampling']['readExceeded'], 3)
        self.assertEqual(detail['sampling']['oversizeRecords'], 2)
        self.assertIsNone(detail['records'][0]['caller'])

    def test_cpu_consumer_duplicates_malformed_and_orphan_callers_report(self):
        result = self.summarize(self.basic(consumer_event(), consumer_event(), consumer_event(103, 'caller', readID=999),
            row('native.log', 104, message='metal.cpuConsumer {bad'), work(), heart()))
        self.assertEqual(len(result['cpuConsumers']['records']), 1)
        self.assertEqual(result['cpuConsumers']['malformedRecords'], 2)
        self.assertEqual(result['cpuConsumers']['orphanCallers'], 1)
        self.assertTrue(any('duplicate native' in e['message'] for e in result['issues']))
        bad = consumer_event(waitNS=-1)
        result = self.summarize(self.basic(bad, work(), heart()))
        self.assertEqual(result['cpuConsumers']['records'], [])

    def test_cpu_consumer_bounds_and_caller_certainty_preserved(self):
        result = self.summarize(self.basic(consumer_event(102), consumer_event(102.1, 'caller',
            traceState='unavailable', positions='unavailable', trace=''), work(), heart()), from_seconds=2, to_seconds=5)
        self.assertEqual(result['cpuConsumers']['records'][0]['caller']['positions'], 'unavailable')
        result = self.summarize(self.basic(consumer_event(102), work(), heart()), from_seconds=3, to_seconds=5)
        self.assertFalse(result['cpuConsumers']['observed'])

    def test_shrink_raw_wait_quantiles_and_subset_do_not_double_count(self):
        first = shrink_record(readCalls=99, readBytes=20, readWaitNS=99_000_000, parameterBytes=420)
        last = shrink_record(readCalls=1, readBytes=20, readWaitNS=100_000_000, aliasClass='safeAlias')
        result = self.summarize(self.basic(work(**shrink_fields([first], [1_000_000]*99)),
            work(110, **shrink_fields([last], [100_000_000])), heart(111)))
        shrinks = result['shrinks']
        self.assertTrue(shrinks['complete']); self.assertTrue(shrinks['readWait']['complete'])
        self.assertEqual(shrinks['readWait']['count'], 100)
        self.assertEqual(shrinks['readWait']['p50MS'], 1); self.assertEqual(shrinks['readWait']['p95MS'], 1)
        self.assertEqual(shrinks['byAliasClass']['safeAlias']['gpuCalls'], 1)
        self.assertEqual(shrinks['byMethod']['shrinkCopy']['gpuCalls'], 2)
        self.assertEqual(result['transfersCallsBytesWallNSWaitNS']['read:load'], [4, 40, 60, 8])
        self.assertEqual(result['frameStats']['runtimeInterval']['count'], 4)

    def test_shrink_legacy_is_unknown_and_malformed_samples_stay_partial(self):
        result = self.summarize(self.basic(work(), heart()))
        self.assertEqual(result['shrinks']['legacyMethod'], 'unknown')
        self.assertIsNone(result['shrinks']['readWait']['p95MS'])
        event = work(**shrink_fields([shrink_record(readCalls=2)], [0], sample_drops=1))
        result = self.summarize(self.basic(event, heart()))
        self.assertFalse(result['shrinks']['readWait']['complete'])
        self.assertEqual(result['shrinks']['readWait']['p50MS'], 0)
        self.assertEqual(result['shrinks']['readWait']['knownSamplesDropped'], 1)
        event['fields']['shrinkReadWaitSampleCount'] = '2049'
        result = self.summarize(self.basic(event, heart()))
        self.assertEqual(result['shrinks']['readWait']['count'], 0)
        self.assertTrue(any('bounded shrink wait' in i['message'] for i in result['issues']))

    def test_shrink_geometry_is_last_metadata_not_aggregate_key(self):
        first = shrink_record(reason='aliasDependency', aliasClass='unsafeAlias', cpuCalls=1, gpuCalls=0)
        last = shrink_record(reason='aliasDependency', aliasClass='unsafeAlias', cpuCalls=1, gpuCalls=0, metadata='{"sourceROI":[5,6,7,8]}')
        result = self.summarize(self.basic(work(**shrink_fields([first])), work(110, **shrink_fields([last])), heart(111)))
        self.assertEqual(len(result['shrinks']['records']), 1)
        self.assertEqual(result['shrinks']['byReason']['aliasDependency']['cpuCalls'], 2)
        self.assertEqual(result['shrinks']['records'][0]['metadata'], {'sourceROI': [5, 6, 7, 8]})

    def test_shrink_overflow_preserves_metrics_and_quantile_samples(self):
        event = work(**shrink_fields([], [7], dropped=1, capacityRecords=1, cpuCalls=1, readCalls=1, readWaitNS=7))
        result = self.summarize(self.basic(event, heart()))
        self.assertFalse(result['shrinks']['complete'])
        self.assertTrue(result['shrinks']['readWait']['complete'])
        self.assertEqual(result['shrinks']['overflowKnownTotals']['readWaitNS'], 7)
        self.assertEqual(result['shrinks']['knownDropped'], 1)

    def test_optional_frame_count_validates_pairs_without_changing_legacy(self):
        result = self.summarize(self.basic(work(frameSampleCount='2', structuredFieldLimitsVersion='1'), heart()))
        self.assertTrue(result['frameStats']['complete'])
        self.assertTrue(result['structuredFields']['complete'])
        result = self.summarize(self.basic(work(frameSampleCount='3'), heart()))
        self.assertFalse(result['frameStats']['complete'])
        self.assertEqual(result['frameStats']['cpuWall']['count'], 2)
        self.assertTrue(any('frameSampleCount disagrees' in item['message'] for item in result['issues']))

    def test_logger_omission_and_whole_record_guard_are_explicit(self):
        event = work(structuredFieldLimitsVersion='1', fieldsValueOmitted='1', fieldsValueOmittedKeys='frameSamplesNS')
        del event['fields']['frameSamplesNS']
        result = self.summarize(self.basic(event, heart()))
        self.assertEqual(result['structuredFields']['valueOmittedWindows'], 1)
        self.assertFalse(result['frameStats']['complete'])
        self.assertEqual(result['frameStats']['missingSampleWindows'], 1)
        self.assertTrue(any('values omitted' in item['message'] for item in result['issues']))
        event = row('layerWorkProfile', 105, intervalMS='5000', truncated='record exceeded segment limit')
        result = self.summarize(self.basic(event, heart()))
        self.assertEqual(result['structuredFields']['wholeRecordTruncatedWindows'], 1)
        self.assertFalse(result['originTotalsComplete'])
        self.assertFalse(result['frameStats']['complete'])

    def test_unmarked_1024_boundary_is_possible_clipping_not_asserted_corruption(self):
        fields = shrink_fields()
        fields['shrinkProfiles'] = '[]' + ' ' * 1022
        result = self.summarize(self.basic(work(**fields), heart()))
        self.assertEqual(result['structuredFields']['possibleLegacyClipping'][0]['keys'], ['shrinkProfiles'])
        self.assertFalse(result['shrinks']['complete'])
        self.assertTrue(result['shrinks']['readWait']['complete'])
        self.assertTrue(any('possible truncation' in item['message'] for item in result['issues']))
        result = self.summarize(self.basic(work(structuredFieldLimitsVersion='1', **fields), heart()))
        self.assertTrue(result['shrinks']['complete'])
        self.assertFalse(result['structuredFields']['possibleLegacyClipping'])

    def test_logger_drops_and_bad_transport_cap_are_visible(self):
        event = work(structuredFieldLimitsVersion='1')
        event['droppedRecords'] = 2
        result = self.summarize(self.basic(event, heart()))
        self.assertEqual(result['structuredFields']['knownLoggerDroppedRecords'], 2)
        self.assertFalse(result['frameStats']['complete'])
        event = work(structuredFieldLimitsVersion='1', shrinkProfiles='[]' + ' ' * 65534, **{
            key: value for key, value in shrink_fields().items() if key != 'shrinkProfiles'})
        result = self.summarize(self.basic(event, heart()))
        self.assertFalse(result['structuredFields']['complete'])
        self.assertFalse(result['shrinks']['complete'])
        self.assertTrue(any('declared UTF-8 cap' in item['message'] for item in result['issues']))

    def test_v2_origin_is_not_counted_again_as_texture(self):
        result = self.summarize(self.basic(work(), heart()))
        self.assertEqual(result['transfersCallsBytesWallNSWaitNS']['read:load'], [2, 20, 30, 4])
        self.assertEqual(result['originWindowReconciliation'][0]['status'], 'match')
        self.assertTrue(result['originTotalsComplete'])
        self.assertTrue(result['namedOriginsComplete'])
        self.assertFalse(result['namedOriginsLowerBound'])
        self.assertEqual(result['heartbeatComparison']['read']['status'], 'match')

    def test_legacy_origins_are_lower_bounds_without_asserting_coverage(self):
        legacy = work()
        for key in ('workProfileVersion', 'transferOrigins', 'originOverflow', 'frameSamplesNS', 'frameSamplesDropped'):
            del legacy['fields'][key]
        result = self.summarize(self.basic(legacy, heart(layerReadbackBytes='900', layerReadbackBySource='fallback:900/2')))
        self.assertEqual(result['sourceFormat'], ['legacy'])
        self.assertTrue(result['namedOriginsLowerBound'])
        self.assertFalse(result['originTotalsComplete'])
        self.assertEqual(result['heartbeatComparison']['read']['status'], 'mismatch')
        self.assertFalse(result['heartbeatComparison']['read']['sameCoverageGuaranteed'])
        self.assertIsNone(result['frameStats']['samplesDropped'])
        self.assertIsNone(result['frameStats']['runtimeInterval']['p95MS'])

    def test_overflow_preserves_all_metrics_and_percent_decodes_long_origin(self):
        name = '载入,origin=path/%:' + 'a' * 700
        event = work(transfers='read:load@1(2x2)=1/10/38/17,read:other=1/5/12/3',
            transferOrigins='read:' + quote(name, safe='') + '=1/10/38/17',
            originOverflow='read=1/5/12/3,upload=0/0/0/0,capacityRecords=1,oversizeRecords=1')
        result = self.summarize(self.basic(event, heart(layerReadbackBytes='15', layerUploadedBytes='0', layerReadbackBySource='fallback:15/2')))
        self.assertEqual(result['transfersCallsBytesWallNSWaitNS']['read:' + name], [1, 10, 38, 17])
        self.assertEqual(result['originOverflow']['read'], [1, 5, 12, 3])
        self.assertTrue(result['originTotalsComplete'])
        self.assertFalse(result['namedOriginsComplete'])
        self.assertTrue(result['namedOriginsLowerBound'])
        self.assertEqual(result['originWindowReconciliation'][0]['status'], 'match')

    def test_mismatch_is_reported_not_asserted_away(self):
        result = self.summarize(self.basic(work(transferOrigins='read:load=2/19/30/4,upload:update=1/10/11/2'), heart()))
        self.assertEqual(result['originWindowReconciliation'][0]['status'], 'mismatch')
        self.assertFalse(result['originTotalsComplete'])
        self.assertTrue(any('disagrees' in item['message'] for item in result['issues']))

    def test_missing_frames_are_unknown_instead_of_zero(self):
        missing = work()
        del missing['fields']['frameSamplesNS']; del missing['fields']['frameSamplesDropped']
        result = self.summarize(self.basic(missing, heart()))
        self.assertTrue(result['frameStats']['partial'])
        self.assertEqual(result['frameStats']['missingSampleWindows'], 1)
        self.assertIsNone(result['frameStats']['samplesDropped'])
        self.assertIsNone(result['frameStats']['cpuWall']['p50MS'])

    def test_dropped_and_unknown_runtime_frames_are_explicit(self):
        result = self.summarize(self.basic(work(frameSamplesNS='0/0,50000000/50000000,50000001/50000001', frameSamplesDropped='7'), heart()))
        stats = result['frameStats']
        self.assertTrue(stats['partial'])
        self.assertEqual(stats['samplesDropped'], 7)
        self.assertEqual(stats['unknownRuntimeIntervalSamples'], 1)
        self.assertEqual(stats['runtimeInterval']['count'], 2)
        self.assertEqual(stats['cpuWall']['count'], 3)
        self.assertEqual(stats['cpuWall']['p50MS'], 50)
        self.assertEqual(stats['runtimeInterval']['above50MSCount'], 1)
        self.assertEqual(stats['cpuWall']['above50MSCount'], 1)

    def test_raw_merge_p95_counterexample(self):
        # Averaging the p95s 1ms and 100ms yields 50.5ms. The actual merged
        # nearest-rank p95 of these 100 samples is 1ms, so only RAW merging works.
        first = ','.join(['1000000/1000000'] * 99)
        result = self.summarize(self.basic(work(frameSamplesNS=first), work(110, frameSamplesNS='100000000/100000000'), heart(111)))
        self.assertEqual(result['frameStats']['runtimeInterval']['count'], 100)
        self.assertEqual(result['frameStats']['runtimeInterval']['p95MS'], 1)
        self.assertEqual(result['frameStats']['runtimeInterval']['p99MS'], 1)
        self.assertEqual(result['frameStats']['runtimeInterval']['above50MSCount'], 1)
        self.assertEqual(result['frameStats']['cpuWall']['p95MS'], 1)

    def test_bounds_select_whole_windows_and_exclude_initial_prebegin_span(self):
        result = self.summarize(self.basic(work(101), work(105), work(110), work(115), heart(116)), from_seconds=0, to_seconds=10)
        self.assertEqual(result['workProfileCount'], 2)
        self.assertEqual([w['selected'] for w in result['coverage']['windows']], [False, True, True, False])
        self.assertEqual(result['coverage']['windows'][0]['startSeconds'], -4)
        self.assertEqual(result['transfersCallsBytesWallNSWaitNS']['read:load'][1], 40)
        self.assertEqual(result['frameStats']['cpuWall']['count'], 4)
        tighter = self.summarize(self.basic(work(105), work(110), heart(111)), from_seconds=1, to_seconds=10)
        self.assertEqual(tighter['workProfileCount'], 1)

    def test_multiple_sessions_remain_separate(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'sessions.jsonl'
            records = self.basic(work(), heart()) + [row('game.begin', 200, 'B', folder='second'), work(205, 'B'), heart(206, 'B')]
            path.write_text('\n'.join(json.dumps(r) for r in records), encoding='utf-8')
            results = analysis.summarize_sessions(path, 0, 5)
        self.assertEqual([r['session'] for r in results], ['A', 'B'])
        self.assertEqual([r['workProfileCount'] for r in results], [1, 1])
        self.assertEqual([r['frameStats']['cpuWall']['count'] for r in results], [2, 2])

    def test_malformed_records_are_visible_and_partial(self):
        malformed = work(transferOrigins='read:bad%=2/20/30/4,upload:update=1/10/11/2',
            transfers='read:load@1(2x2)=1/10/15/2,BROKEN,upload:update@3(2x2)=1/10/11/2',
            frameSamplesNS='16000000/1,broken,17000000/2')
        result = self.summarize(self.basic(malformed, '{broken JSON', heart()))
        self.assertGreaterEqual(len(result['issues']), 4)
        self.assertFalse(result['originTotalsComplete'])
        self.assertFalse(result['coverage']['fileDataComplete'])
        self.assertEqual(result['frameStats']['malformedSamples'], 1)
        self.assertTrue(result['frameStats']['partial'])
        self.assertEqual(result['originWindowReconciliation'][0]['status'], 'unavailable')
        self.assertIsNone(result['heartbeatComparison']['read']['selectedWindowBytes'])

    def test_missing_overflow_does_not_become_zero(self):
        event = work(); del event['fields']['originOverflow']
        result = self.summarize(self.basic(event, heart()))
        self.assertIsNone(result['originOverflow'])
        self.assertFalse(result['originTotalsComplete'])
        self.assertEqual(result['coverage']['missingOverflowWindows'], 1)

    def test_no_heartbeat_preserves_missing_cumulative_values(self):
        result = self.summarize(self.basic(work()))
        self.assertIsNone(result['cumulative']['layerReadbackBytes'])
        self.assertIsNone(result['fpsMedian'])
        self.assertEqual(result['heartbeatComparison']['read']['status'], 'unavailable')

    def test_transition_subset_and_parameters_do_not_duplicate_overall_transfers(self):
        detail = transition_record(readCalls=2, readBytes=20, readWallNS=30, readWaitNS=4)
        result = self.summarize(self.basic(work(**transition_fields([detail])), heart()))
        self.assertEqual(result['transfersCallsBytesWallNSWaitNS']['read:load'], [2, 20, 30, 4])
        transitions = result['transitions']
        self.assertTrue(transitions['complete'])
        self.assertEqual(transitions['records'][0]['parameterBytes'], 420)
        self.assertEqual(transitions['records'][0]['dimensions'], detail['dimensions'])
        self.assertEqual(transitions['records'][0]['metadata'], {'maxsize': 31})
        self.assertIn('rotateswap', transitions['unobservedBuiltins'])
        self.assertNotIn('mosaic', transitions['unobservedBuiltins'])

    def test_unknown_provider_and_lifecycle_keep_names_and_normalized_parameters(self):
        name = 'plugin,"未知\\handler'
        detail = transition_record(requested=name, effective=name, reason='provider.cpu', cpuCalls=5, gpuCalls=0)
        lifecycle = row('native.log', 102, message='transition.lifecycle ' + json.dumps({
            'stage': 'begin', 'requested': name, 'effective': name, 'canvas': [127, 91], 'tick': 500}, ensure_ascii=False))
        result = self.summarize(self.basic(lifecycle, work(**transition_fields([detail])), heart()))
        self.assertEqual(result['transitions']['records'][0]['effective'], name)
        self.assertEqual(result['transitions']['lifecycles'][0]['requested'], name)

    def test_old_logs_have_unknown_handlers_without_guessing_from_bytes(self):
        result = self.summarize(self.basic(work(), heart()))
        self.assertFalse(result['transitions']['complete'])
        self.assertEqual(result['transitions']['legacyHandler'], 'unknown')
        self.assertEqual(result['transitions']['records'], [])
        self.assertEqual(result['transitions']['missingHandlerWindows'], 1)

    def test_transition_windows_merge_counts_and_preserve_coverage_bounds(self):
        first = work(**transition_fields([transition_record()]))
        second = work(110, **transition_fields([transition_record(firstTick=800, lastTick=900)]))
        result = self.summarize(self.basic(first, second, heart(111)))
        detail = result['transitions']['records'][0]
        self.assertEqual(detail['gpuCalls'], 10)
        self.assertEqual(detail['frames'], 6)
        self.assertEqual((detail['firstTick'], detail['lastTick']), (500, 900))
        bounded = self.summarize(self.basic(first, second, heart(111)), from_seconds=5, to_seconds=10)
        self.assertEqual(bounded['transitions']['records'][0]['frames'], 3)

    def test_transition_overflow_and_malformed_rows_remain_explicit_lower_bounds(self):
        event = work(**transition_fields([transition_record()], 2, capacityRecords=1, oversizeRecords=1, readBytes=99))
        result = self.summarize(self.basic(event, heart()))
        self.assertFalse(result['transitions']['complete'])
        self.assertEqual(result['transitions']['knownDropped'], 2)
        self.assertEqual(result['transitions']['overflowKnownTotals']['readBytes'], 99)
        broken = transition_record(parameterBytes=-1, dimensions=[1, 2])
        result = self.summarize(self.basic(work(**transition_fields([broken])), heart()))
        self.assertFalse(result['transitions']['complete'])
        self.assertTrue(any('dimensions' in issue['message'] for issue in result['issues']))
        bad_json = transition_fields(); bad_json['transitionProfiles'] = '{broken'
        result = self.summarize(self.basic(work(**bad_json), heart()))
        self.assertFalse(result['transitions']['complete'])
        mismatch = transition_fields([], 2, capacityRecords=1)
        result = self.summarize(self.basic(work(**mismatch), heart()))
        self.assertTrue(any('dropped count disagrees' in issue['message'] for issue in result['issues']))


if __name__ == '__main__':
    unittest.main()
