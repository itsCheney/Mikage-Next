#!/usr/bin/env python3
"""Compare unpatched upstream pixels with patched-default/capture replay."""
import subprocess,sys,tempfile
from pathlib import Path
with tempfile.TemporaryDirectory(prefix='mikage-plutovg-exact-') as directory:
    old_path=Path(directory)/'upstream.pixels'
    new_path=Path(directory)/'patched.pixels'
    baseline=subprocess.check_output([sys.argv[1],str(old_path)],text=True)
    capture=subprocess.check_output([sys.argv[2],str(new_path)],text=True)
    old_pixels,new_pixels=old_path.read_bytes(),new_path.read_bytes()
    if not old_pixels or old_pixels!=new_pixels:
        raise SystemExit('Unpatched/patched plutovg full pixel bytes differ')
if baseline!=capture:
    raise SystemExit('Unpatched/patched plutovg oracle mismatch:\n'+baseline+'\n'+capture)
print(baseline,end='')
