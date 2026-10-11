"""Compare the production sparse-controller trajectories at common 60/42 Hz times."""
import argparse
import csv
import io
import math
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--executable', type=Path, required=True)
parser.add_argument('--output-directory', type=Path)
args = parser.parse_args()
curves = {}
for fps in (60, 42):
    result = subprocess.run([str(args.executable.resolve()), '--trajectory', str(fps), '10'],
                            check=True, capture_output=True, text=True)
    records = list(csv.DictReader(io.StringIO(result.stdout)))
    assert len(records) == fps * 10 + 1, 'missing trajectory records'
    curves[fps] = {round(float(row['seconds']) * 6): row for row in records
                   if abs(float(row['seconds']) * 6 - round(float(row['seconds']) * 6)) < 1e-8}
    for row in records:
        assert all(math.isfinite(float(value)) for value in row.values()), 'nonfinite trajectory'
    if args.output_directory:
        args.output_directory.mkdir(parents=True, exist_ok=True)
        (args.output_directory / f'emote-{fps}.csv').write_text(result.stdout, encoding='utf-8')
assert curves[60].keys() == curves[42].keys(), 'common time grid differs'
for sample in curves[60]:
    first, second = curves[60][sample], curves[42][sample]
    assert abs(float(first['body_UD']) - float(second['body_UD'])) < 1e-8, f'value differs at {sample/6}s'
    # The end/begin times represent the same loop seam; variable continuity is
    # still checked above. Tiny roundoff may leave a cursor on either endpoint.
    a, b = float(first['timelineTime']), float(second['timelineTime'])
    assert min(abs(a-b), abs(abs(a-b)-60)) < 1e-8, f'clock differs at {sample/6}s'
print(f'PASS: 60/42 Hz production trajectories agree at {len(curves[60])} common times')
