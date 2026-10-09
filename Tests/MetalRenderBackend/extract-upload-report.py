"""Compile the production C3 aggregate formatter without Objective-C or Metal."""
import argparse
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
source = args.source.read_text(encoding='utf-8')
start = source.index('void ReportUploads(bool final=false)')
opening = source.index('{', start)
depth, end = 1, opening + 1
while depth:
    depth += (source[end] == '{') - (source[end] == '}')
    end += 1
args.output.write_text('// Production Metal uploader aggregate; do not edit.\n' + source[start:end] + '\n', encoding='utf-8')
