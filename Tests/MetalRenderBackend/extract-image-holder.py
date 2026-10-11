"""Extract the actual cache holder, including CPU-to-texture ownership transfer."""
import argparse
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--source',required=True,type=Path);p.add_argument('--output',required=True,type=Path)
a=p.parse_args();s=a.source.read_text(encoding='utf-8');start=s.index('class tTVPGraphicImageData\n')
end=s.index('\n};',start)+3;a.output.write_text(s[start:end]+'\n',encoding='utf-8')
