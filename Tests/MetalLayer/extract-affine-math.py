#!/usr/bin/env python3
"""Compile the production Metal affine coordinate/byte helpers as C++."""
import argparse
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--source',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args(); s=a.source.read_text()
start=s.index('struct AffinePair {'); end=s.index('struct AffineParameters {',start)
a.output.write_text('// Generated from production MetalLayerShaders.h.\nnamespace affine_shader {\nusing std::fma; using std::max;\n'+s[start:end]+'}\n')
