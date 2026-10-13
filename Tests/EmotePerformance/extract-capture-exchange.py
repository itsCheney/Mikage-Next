"""Extract the production D3DAdaptor declaration and capture/exchange methods."""
import argparse
from pathlib import Path


def block(text, signature):
    start = text.index(signature)
    opening = text.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


parser = argparse.ArgumentParser()
parser.add_argument('--core', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
emote = args.core / 'plugins/emoteplayer'
header = (emote / 'emoteplayerclass.h').read_text(encoding='utf-8')
source = (emote / 'emoteplayerclass.cpp').read_text(encoding='utf-8')
methods = ['void D3DAdaptor::captureInto(', 'bool D3DAdaptor::tryExchange(',
           'void D3DAdaptor::reclaimCanvas(', 'void D3DAdaptor::prepareForDraw(',
           'void D3DAdaptor::unloadUnusedTextures()']
frames = header[header.index('struct EmoteDrawnFrame'):header.index('// D3DAdaptor\nclass D3DAdaptor')]
args.output.write_text('// Generated from emoteplayerclass.h/.cpp; do not edit.\n' + frames +
                       block(header, 'class D3DAdaptor\n{') + ';\n\n' +
                       '\n\n'.join(block(source, m) for m in methods) + '\n', encoding='utf-8')
