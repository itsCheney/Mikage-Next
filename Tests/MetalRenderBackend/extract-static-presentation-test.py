"""Exercise the native presentation fixture with a deterministic clock/display boundary."""
import argparse
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument('--source',required=True,type=Path)
p.add_argument('--output',required=True,type=Path)
a=p.parse_args()
s=a.source.read_text(encoding='utf-8')
assert 'WaitForStaticPresentation' in s and 'std::chrono::steady_clock' in s
a.output.write_text(s.replace('std::chrono::steady_clock','FixtureClock'),encoding='utf-8')
