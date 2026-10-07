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


class LayerDiagnosticsTests(unittest.TestCase):
    def summarize(self, records, **bounds):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'fixtures.jsonl'
            path.write_text('\n'.join(json.dumps(r, ensure_ascii=False) if isinstance(r, dict) else r for r in records), encoding='utf-8')
            return analysis.summarize(path, **bounds)

    def basic(self, *records):
        return [row('environment', 99, 'none', sourceRevision='abcdef012345'), row('game.begin', 100, folder='fixture'), *records]

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


if __name__ == '__main__':
    unittest.main()
