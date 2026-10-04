import argparse
from pathlib import Path

def definition(text,signature):
    start=text.index(signature); brace=text.index('{',start); depth=1; end=brace+1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}'); end+=1
    return text[start:end]

p=argparse.ArgumentParser(); p.add_argument('--source',type=Path,required=True); p.add_argument('--output',type=Path,required=True)
p.add_argument('--codec-output',type=Path,required=True)
a=p.parse_args(); source=a.source.read_text(encoding='utf-8')
declarations='\n'.join(definition(source,s)+';' for s in ['struct AlphaMovieHeader','struct AlphaMovieFrame','class tTJSNI_AlphaMovie'])
methods='\n'.join(definition(source,s) for s in ['tTJSNI_AlphaMovie::tTJSNI_AlphaMovie()',
    'tTJSNI_AlphaMovie::~tTJSNI_AlphaMovie()','void tTJSNI_AlphaMovie::open(',
    'const std::vector<uint8_t>& tTJSNI_AlphaMovie::DecodeFrame(','void tTJSNI_AlphaMovie::clear()'])
a.output.write_text('// Extracted production declarations and loader; do not edit.\n#define private public\n'+declarations+'\n#undef private\n'+methods+'\n',encoding='utf-8')
a.codec_output.write_text('// Extracted production AMV pixel codec; do not edit.\n'+source[source.index('typedef long JLONG;'):source.index('struct AlphaMovieHeader')],encoding='utf-8')
