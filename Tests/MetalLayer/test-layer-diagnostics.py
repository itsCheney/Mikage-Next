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


class LayerDiagnosticsTests(unittest.TestCase):
    def summarize(self, records, **bounds):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'fixtures.jsonl'
            path.write_text('\n'.join(json.dumps(r, ensure_ascii=False) if isinstance(r, dict) else r for r in records), encoding='utf-8')
            return analysis.summarize(path, **bounds)

    def basic(self, *records):
        return [row('environment', 99, 'none', sourceRevision='abcdef012345'), row('game.begin', 100, folder='fixture'), *records]

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
