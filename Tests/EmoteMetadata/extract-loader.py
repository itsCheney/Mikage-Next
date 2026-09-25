#!/usr/bin/env python3
"""Build the test unit from exact production loader, cache and stream bodies."""
import argparse
from pathlib import Path


def definition(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth, end = 1, opening + 1
    # Selected functions/classes contain no braces in string literals.
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--source", required=True, type=Path)
parser.add_argument("--header", required=True, type=Path)
parser.add_argument("--output-dir", required=True, type=Path)
args = parser.parse_args()
source = args.source.read_text(encoding="utf-8")
header = args.header.read_text(encoding="utf-8")
out = args.output_dir
out.mkdir(parents=True, exist_ok=True)
(out / "ProductionFileClass.inc").write_text(
    definition(header, "class emotefile\n{") + ";\n", encoding="utf-8")
# The compression/decryption helpers are self-contained before animation code.
(out / "ProductionDecode.inc").write_text(
    source[source.index("struct EMoteCTX"):source.index("using namespace PSB;")], encoding="utf-8")
# Root adds the new shared helpers contiguously before the animation readers.
shared_start = source.index("struct EmoteDecodedResource")
shared_end = source.index("#pragma region Base", shared_start)
(out / "ProductionSharedResource.inc").write_text(source[shared_start:shared_end], encoding="utf-8")
signatures = [
    "emotefile::emotefile()", "emotefile::~emotefile()", "void emotefile::setSeed(",
    "void emotefile::setFun(", "bool emotefile::load(",
    "void emotefile::LoadDecodedResource(",
    "std::shared_ptr<const EmoteDecodedResource> emotefile::SnapshotDecodedResource(",
]
(out / "ProductionLoad.inc").write_text("\n\n".join(definition(source, s) for s in signatures) + "\n", encoding="utf-8")
