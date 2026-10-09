import importlib.util
import os
import subprocess
import sys
from pathlib import Path
import unittest
sys.dont_write_bytecode = True

spec = importlib.util.spec_from_file_location('c3', Path(__file__).resolve().parents[2] / 'scripts/analyze-c3-uploads.py')
c3 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(c3)


def snapshot(t=1, epoch=1, final=0, **updates):
    counters = dict.fromkeys(c3.COUNTERS, 0)
    counters.update(version=1, epoch=epoch, final=final, id=t, atlasBytes=0)
    counters.update(updates)
    return {'run': 'r', 'uptimeSeconds': t, 'fields': {'message': 'metal.tinyUploads ' +
            ' '.join(f'{key}={value}' for key, value in counters.items())}}


class UploadParserTests(unittest.TestCase):
    def test_production_emitter(self):
        executable = os.environ.get('C3_UPLOAD_EMITTER')
        if not executable:
            self.skipTest('Run the CMake test to compile the production formatter')
        lines = subprocess.check_output([executable], text=True).splitlines()
        self.assertEqual(len(lines), 2)
        events = [{'run': 'production', 'uptimeSeconds': index * 2, 'fields': {'message': line}}
                  for index, line in enumerate(lines)]
        result = c3.summarize(events)
        self.assertTrue(result['valid'], result['errors'])
        self.assertEqual(result['epochs'][0]['observedDelta']['glyphCalls'], 4)
        self.assertEqual(result['epochs'][0]['lastTotals']['glyphBytes'], 96)
        self.assertEqual(result['epochs'][0]['lastTotals']['uploadBatches'], 1)
        self.assertTrue(result['epochs'][0]['finalObserved'])

    def test_empty_is_unavailable(self):
        self.assertFalse(c3.summarize([])['available'])

    def test_exact_routes_and_partial_interval(self):
        result = c3.summarize([snapshot(glyphCalls=3, glyphApplied=2, rejectCapacity=1),
                               snapshot(3, final=1, glyphCalls=7, glyphApplied=5, rejectCapacity=2)])
        self.assertTrue(result['valid'])
        self.assertEqual(result['epochs'][0]['observedDelta']['glyphCalls'], 4)
        self.assertEqual(result['epochs'][0]['observedRates']['glyphCalls'], 2)
        self.assertTrue(result['epochs'][0]['finalObserved'])

    def test_reset_epoch_does_not_regress(self):
        self.assertTrue(c3.summarize([snapshot(glyphCalls=4, glyphApplied=4), snapshot(2, epoch=2)])['valid'])

    def test_invalid_totals(self):
        for update in ({'glyphCalls': 3}, {'atlasBytes': 16 * 1024 * 1024 + 1}, {'uploadBatches': 1}):
            self.assertFalse(c3.summarize([snapshot(**update)])['valid'])

    def test_missing_duplicate_unknown_negative(self):
        for suffix in (' version=1', ' bad=x'):
            event = snapshot()
            event['fields']['message'] += suffix
            # Unknown additive fields are compatible; duplicates are not.
            self.assertEqual(c3.summarize([event])['valid'], suffix == ' bad=x')
        for update in ({'version': 2}, {'glyphCalls': -1}):
            self.assertFalse(c3.summarize([snapshot(**update)])['valid'])
        event = snapshot();event['fields']['message'] = event['fields']['message'].replace('glyphBytes=0', '')
        self.assertFalse(c3.summarize([event])['valid'])

    def test_regression_and_late_after_final(self):
        self.assertFalse(c3.summarize([snapshot(glyphCalls=1, glyphApplied=1), snapshot(2)])['valid'])
        self.assertFalse(c3.summarize([snapshot(final=1), snapshot(2)])['valid'])
        self.assertTrue(c3.summarize([snapshot(final=1), snapshot(final=1)])['valid'])


if __name__ == '__main__':
    unittest.main()
