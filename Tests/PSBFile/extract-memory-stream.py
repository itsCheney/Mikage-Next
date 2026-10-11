"""Extract the complete production memory-stream implementation, unchanged."""
import argparse
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--source', required=True)
parser.add_argument('--output', required=True)
args = parser.parse_args()
source = Path(args.source).read_text(encoding='utf-8')
start = source.index('tTVPMemoryStream::tTVPMemoryStream()')
end = source.index('tTVPPartialStream::tTVPPartialStream(', start)
body = source[start:end]
assert body.count('tTVPMemoryStream::') == 13, 'Production memory-stream method set changed'
target = Path(args.output)
target.parent.mkdir(parents=True, exist_ok=True)
target.write_text('// Exact production UtilStreams.cpp memory-stream slice.\n' + body,
                  encoding='utf-8')
