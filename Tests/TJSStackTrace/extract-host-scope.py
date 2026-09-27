#!/usr/bin/env python3
"""Compile the exact host tracer-owner logic without Foundation/SDL."""
import argparse
from pathlib import Path


def definition(text, signature):
    start = text.index(signature)
    opening = text.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--source", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
source = args.source.read_text(encoding="utf-8")
owner = definition(source, "struct DiagnosticScriptTraceState") + ";\n"
owner += "DiagnosticScriptTraceState diagnosticScriptTrace;\n"
owner += definition(source, "struct DiagnosticScriptTraceScope") + ";\n"
owner += definition(source, "void endDiagnosticScriptTraceSession()") + "\n"
args.output.write_text(owner, encoding="utf-8")
