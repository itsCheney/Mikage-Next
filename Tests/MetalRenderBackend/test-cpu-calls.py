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
    def rows(self):
        return [{'run':'test','fields':{'message':line}} for line in subprocess.check_output(
            [os.environ['P2D_CPU_EMITTER']],encoding='utf-8').splitlines()]
    def test_production(self):
        result=cpu.summarize(self.rows());self.assertTrue(result['valid'],result['errors'])
        window=result['windows'][0];self.assertTrue(window['complete'],window['incompleteReasons'])
        self.assertEqual(len(window['steps']),1);self.assertEqual(len(window['slow']),3)
        self.assertEqual(window['slow'][0]['callID'],1);self.assertEqual(window['slow'][0]['kind'],'kag.read')
    def test_missing_and_duplicate(self):
        rows=self.rows();self.assertFalse(cpu.summarize(rows[1:])['windows'][0]['complete'])
        self.assertFalse(cpu.summarize(rows+rows[-1:])['valid'])
    def test_old_and_truncated(self):
        self.assertFalse(cpu.summarize([{'fields':{'message':'runtime.frameTime cpuMS=100'}}])['available'])
        rows=self.rows();rows[0]['fields']['message']+=' x=bad';self.assertFalse(cpu.summarize(rows)['valid'])
if __name__=='__main__':unittest.main()
