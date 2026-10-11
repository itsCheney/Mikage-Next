"""Compile the backend's actual C++ attribution helpers without a Metal SDK."""
import argparse
from pathlib import Path


def function(text, signature):
    start = text.index(signature)
    brace = text.index('{', start)
    depth, end = 1, brace+1
    while depth:
        depth += (text[end] == '{')-(text[end] == '}')
        end += 1
    return text[start:end]


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
text = args.source.read_text(encoding='utf-8')
args.output.write_text('// Production MetalRenderBackend attribution; no GPU implementation.\n'+
    '\n'.join(function(text, signature) for signature in (
        '    layer_hotspot::ResourceInfo* Info(Resource* resource)',
        '    static std::array<int,4> DiagnosticRect(',
        '    uint64_t Trace(layer_hotspot::Access access'))+'\n', encoding='utf-8')
