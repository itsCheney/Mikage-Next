import importlib.util
import os
from pathlib import Path
import subprocess
import unittest
import sys
sys.dont_write_bytecode=True
spec=importlib.util.spec_from_file_location('cpu',Path(__file__).resolve().parents[2]/'scripts/analyze-cpu-calls.py')
cpu=importlib.util.module_from_spec(spec);spec.loader.exec_module(cpu)
class CPUCallsTests(unittest.TestCase):
    def rows(self,*args):
        return [{'run':'test','fields':{'message':line}} for line in subprocess.check_output(
            [os.environ['P2D_CPU_EMITTER'],*args],encoding='utf-8').splitlines()]
    def test_production(self):
        result=cpu.summarize(self.rows());self.assertTrue(result['valid'],result['errors'])
        window=result['windows'][0];self.assertTrue(window['complete'],window['incompleteReasons'])
        self.assertEqual(window['version'],2)
        self.assertEqual(len(window['steps']),1);self.assertEqual(len(window['slow']),4)
        self.assertEqual({c['role'] for c in window['slow']},{0,1,2,3})
        native=next(c for c in window['slow'] if c['kind']=='psb.materialize')
        self.assertEqual(native['bytes'],2048)
        self.assertEqual(native['vmCalls'],0)
        self.assertEqual(next(c for c in window['slow'] if c['role']==0)['vmCalls'],2)
        self.assertEqual(next(c for c in window['slow'] if c['role']==1)['vmCalls'],2)
        self.assertEqual(window['aggregate']['psb.materialize']['bytes'],2048)
    def test_late_heavy_step(self):
        result=cpu.summarize(self.rows('--late'));self.assertTrue(result['valid'],result['errors'])
        window=result['windows'][0];self.assertFalse(window['complete'])
        self.assertIn(3,{s['stepID'] for s in window['steps']})
        late=next(s for s in window['steps'] if s['stepID']==3)
        self.assertEqual({c['role'] for c in late['calls']},{0,1,2,3})
        self.assertEqual(len(window['steps']),2)
        self.assertGreater(window['summary']['windowDropped'],0)
    def test_v1_compatibility(self):
        messages=['cpu.slow v=1 epoch=1 window=1 stepID=1 callID=1 parentCallID=0 kind=vm wallNS=20000000 failed=0 stack="old.tjs:1[run]"',
                  'cpu.summary v=1 epoch=1 window=1 eligible=1 emitted=1 dropped=0 stackFailures=0 callbackFailures=0 overheadNS=0']
        messages += [f'cpu.aggregate v=1 epoch=1 window=1 kind={kind} calls=0 wallNS=0 maxNS=0 failures=0' for kind in cpu.KINDS]
        result=cpu.summarize([{'run':'old','fields':{'message':m}} for m in messages])
        self.assertTrue(result['valid'],result['errors']);self.assertTrue(result['windows'][0]['complete'])
    def test_v2_rejections(self):
        mutations=(('residualWallNS=0','residualWallNS=1'),('role=0','role=5'),
                   ('selection=2','selection=3'),('formatDropped=0','formatDropped=1'))
        for old,new in mutations:
            rows=self.rows();changed=False
            for row in rows:
                m=row['fields']['message']
                if old in m:row['fields']['message']=m.replace(old,new,1);changed=True;break
            self.assertTrue(changed,(old,new))
            self.assertFalse(cpu.summarize(rows)['valid'],(old,new))
    def test_v2_incomplete(self):
        for old,new,reason in (('startWindow=1','startWindow=0','cross-window scope'),
                               ('stackTruncated=0','stackTruncated=1','truncated stack display')):
            rows=self.rows();rows[0]['fields']['message']=rows[0]['fields']['message'].replace(old,new)
            result=cpu.summarize(rows);self.assertTrue(result['valid'],result['errors'])
            self.assertIn(reason,result['windows'][0]['incompleteReasons'])
    def test_duplicate_scope_and_missing_phase(self):
        rows=self.rows();self.assertFalse(cpu.summarize(rows+rows[:1])['valid'])
        rows=[r for r in self.rows() if not r['fields']['message'].startswith('cpu.aggregate v=2 epoch=1 window=1 kind=psb.read ')]
        result=cpu.summarize(rows);self.assertTrue(result['valid']);self.assertFalse(result['windows'][0]['complete'])
    def test_missing_and_duplicate(self):
        rows=self.rows();self.assertFalse(cpu.summarize(rows[1:])['windows'][0]['complete'])
        self.assertFalse(cpu.summarize(rows+rows[-1:])['valid'])
    def test_old_and_truncated(self):
        self.assertFalse(cpu.summarize([{'fields':{'message':'runtime.frameTime cpuMS=100'}}])['available'])
        rows=self.rows();rows[0]['fields']['message']+=' x=bad';self.assertFalse(cpu.summarize(rows)['valid'])
if __name__=='__main__':unittest.main()
