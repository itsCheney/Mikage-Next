"""Compile the exact host pointer-admission method against an observable SDL queue."""
import argparse
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--source', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
text = args.source.read_text(encoding='utf-8')
start = text.index('static bool pollKRKREvent(')
opening = text.index('{', start)
depth, end = 1, opening + 1
while depth:
    depth += (text[end] == '{') - (text[end] == '}')
    end += 1
args.output.write_text(text[start:end] + '\n', encoding='utf-8')
