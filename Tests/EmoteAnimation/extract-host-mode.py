"""Extract the actual host preference/hint methods and Emote constructor."""
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
host = (args.core.parent / 'host/MikageKRKRRuntime.mm').read_text(encoding='utf-8')
runner = (args.core / 'plugins/emoteplayer/emoterunner.cpp').read_text(encoding='utf-8')
start = block(host, 'extern "C" bool MikageKRKRStart(')
if start.index('applyEmoteAnimationModeForStart()') >= start.index('SDL_AppInit('):
    raise RuntimeError('Emote mode must be applied before startup executes game scripts')
args.output.mkdir(parents=True, exist_ok=True)
declaration = next(line for line in host.splitlines() if 'std::atomic<bool> experimentalEmote{' in line)
(args.output / 'ProductionHostMode.inc').write_text(declaration + '\n' +
    block(host, 'extern "C" void MikageKRKRSetExperimentalEmote(') + '\n' +
    block(host, 'static bool applyEmoteAnimationModeForStart()') + '\n', encoding='utf-8')
(args.output / 'ProductionModeConstructor.inc').write_text(block(runner, 'emoteengine::emoteengine()') + '\n', encoding='utf-8')
