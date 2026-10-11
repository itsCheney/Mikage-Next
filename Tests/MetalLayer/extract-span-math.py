"""Extract the production packed integer MSL helpers for the portable oracle."""
import argparse
import pathlib
import re

parser = argparse.ArgumentParser()
parser.add_argument('--source', required=True)
parser.add_argument('--output', required=True)
args = parser.parse_args()
source = pathlib.Path(args.source)
text = source.read_text(encoding='utf-8')
scalar = text.split('// BEGIN SPAN SCALAR', 1)[1].split('// END SPAN SCALAR', 1)[0]
scalar = re.sub(r'\bdevice\s+', '', scalar)
scalar = re.sub(r'^uint (span\w+\()', r'inline uint \1', scalar, flags=re.MULTILINE)
defs = (source.parent.parent / 'LayerSpanCompositeDefinitions.def').read_text(encoding='utf-8')
constants = ''.join(f'constexpr uint TVP_SPAN_{name}={number}u;\n'
                    for name, number in re.findall(r'TVP_LAYER_SPAN\((\w+),(\d+)\)', defs))
output = '#pragma once\n#include <cstdint>\n#include "LayerSpanComposite.h"\nnamespace span_shader {\nusing uint=uint32_t; using Span=TVPLayerSpan;\n'
output += constants + scalar + '\n}\n'
pathlib.Path(args.output).write_text(output, encoding='utf-8')
