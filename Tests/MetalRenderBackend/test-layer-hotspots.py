import base64
import copy
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import unittest
sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location('hotspots', Path(__file__).resolve().parents[2]/'scripts/analyze-layer-hotspots.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def totals():
    return {**dict.fromkeys(module.COUNTERS, 0), 'saturated': 0, 'kindPixels': [0]*56,
            'ends': [0]*len(module.BOUNDARIES), 'clears': [0]*len(module.CLEARS)}


def events(payload, prefix='metal.layerHotspot', final=0, command=1):
    raw = base64.b64encode(json.dumps(payload, ensure_ascii=False).encode()).decode()
    chunks = [raw[i:i+720] for i in range(0, len(raw), 720)]
    return [{'run': 'fixture', 'uptimeSeconds': command, 'fields': {'message':
             f'{prefix} version=1 epoch=1 id={command} final={final} droppedReports=0 '
             f'part={i} parts={len(chunks)} data={chunk}'}} for i, chunk in enumerate(chunks)]


def sample():
    return dict(version=1, epoch=1, id=1, firstFrame=1, lastFrame=1, hot=0, gpuMS=1,
                drawableMS=0, frameMS=0, stageDropped=0, stageFailed=0, operationOverflow=0,
                resourceOverflow=0, outputDropped=0, totals=totals(), resources=[], operations=[])


class HotspotTests(unittest.TestCase):
    def test_production_emitter(self):
        exe = os.environ.get('P2D_HOTSPOT_EMITTER')
        if not exe:
            self.skipTest('CMake compiles production recorder/formatter')
        rows = [{'run': 'production', 'fields': {'message': line}}
                for line in subprocess.check_output([exe], text=True, encoding='utf-8').splitlines()]
        result = module.summarize(rows)
        self.assertTrue(result['valid'], result['errors'])
        self.assertEqual(len(result['samples']), 2)
        first, overflow = result['samples']
        self.assertTrue(first['hot'])
        self.assertEqual(first['resources'][1]['asset'], 'archive/街_通学路a.png')
        self.assertTrue(first['complete'], first['incompleteReasons'])
        self.assertFalse(overflow['complete'])
        self.assertIn('operationOverflow', overflow['incompleteReasons'])
        self.assertEqual(overflow['totals']['calls'], 400)
        self.assertTrue(result['epochs'][0]['finalObserved'])
        self.assertEqual(result['epochs'][0]['observedDelta']['pixels'], 16)

    def test_old_logs_are_unavailable(self):
        result = module.summarize([{'fields': {'message': 'metal.layerWork id=1 rectPixels=9'}}])
        self.assertTrue(result['valid'])
        self.assertFalse(result['available'])

    def test_missing_and_duplicate_chunks(self):
        rows = events(sample())
        self.assertGreater(len(rows), 1)
        result = module.summarize(rows[:-1])
        self.assertTrue(result['incompleteGroups'])
        self.assertEqual(result['samples'], [])
        self.assertFalse(module.summarize(rows+[rows[0]])['valid'])

    def test_malformed_payload_and_totals(self):
        value = sample();value['totals']['pixels'] = 7
        self.assertFalse(module.summarize(events(value))['valid'])
        value = sample();value['epoch'] = 2
        self.assertFalse(module.summarize(events(value))['valid'])
        rows = events(sample());rows[0]['fields']['message'] += ' version=1'
        self.assertFalse(module.summarize(rows)['valid'])

    def test_regression_final_and_epochs(self):
        first = totals();first['calls'] = 2
        self.assertFalse(module.summarize(events(first, 'metal.layerPasses')+
                                         events(totals(), 'metal.layerPasses', command=2))['valid'])
        self.assertFalse(module.summarize(events(totals(), 'metal.layerPasses', final=1)+
                                         events(totals(), 'metal.layerPasses', command=2))['valid'])
        second = events(totals(), 'metal.layerPasses', command=2)
        for row in second:row['fields']['message'] = row['fields']['message'].replace('epoch=1', 'epoch=2')
        self.assertTrue(module.summarize(events(first, 'metal.layerPasses')+second)['valid'])

    def test_overflow_and_unknown_mapping_not_complete(self):
        for key in ('operationOverflow', 'resourceOverflow', 'outputDropped', 'stageDropped', 'stageFailed'):
            value = sample();value[key] = 1
            result = module.summarize(events(value))
            self.assertTrue(result['valid'])
            self.assertFalse(result['samples'][0]['complete'])
        value = sample();value['totals']['calls']=1;value['totals']['pixels']=4;value['totals']['kindPixels'][1]=4
        value['operations']=[[1,0,1,-1,-1,0,0,0,'',1,4,0,1,-1,0,0,0,0,[0,0,2,2],[0,0,2,2],[0,0,2,2]]]
        result = module.summarize(events(value))
        self.assertTrue(result['valid'])
        self.assertFalse(result['samples'][0]['complete'])

    def test_command_id_reuse_is_scoped_by_backend_epoch(self):
        first=sample();second=copy.deepcopy(first);second['epoch']=2
        rows=events(first)+events(second)
        # Rewrite only the second sample's envelope, matching its payload.
        for row in rows[len(events(first)):]:
            row['fields']['message']=row['fields']['message'].replace('epoch=1','epoch=2')
        rows += [{'run':'fixture','fields':{'message':
                  'metal.gpuStages id=1 status=partial droppedPasses=1 failedPasses=0 p2dEpoch=1'}},
                 {'run':'fixture','fields':{'message':
                  'metal.gpuStages id=1 status=complete droppedPasses=0 failedPasses=0 p2dEpoch=2'}}]
        result=module.summarize(rows)
        self.assertTrue(result['valid'],result['errors'])
        self.assertFalse(result['samples'][0]['complete'])
        self.assertTrue(result['samples'][1]['complete'])


if __name__ == '__main__':
    unittest.main()
