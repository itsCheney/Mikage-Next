#!/usr/bin/env python3
"""Portable C importer and App window-ID wiring; Swift/Apple remains separate."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
header=ROOT/'Engine/KRKRRuntime/Source/host/MikageKRKRRuntime.h'
swift=(ROOT/'App/Engine/KRKRSession.swift').read_text(encoding='utf-8')
take=swift.index('if MikageKRKRTakeLayerWorkProfile(&workProfile) {')
getter=swift.index('let spanRouteWindowID = MikageKRKRLastLayerWorkProfileWindowID()',take)
event=swift.index('AppDiagnostics.shared.event("session", "layerWorkProfile"',getter)
assert take<getter<event and '"spanRouteWindowID": String(spanRouteWindowID)' in swift[event:]
assert 'MikageKRKRLastLayerWorkProfileWindowID' not in (ROOT/'App/Engine/KRKROverlay.swift').read_text(encoding='utf-8')
with tempfile.TemporaryDirectory(prefix='mikage-span-window-') as directory:
    unit=Path(directory)/'window-id.c'
    unit.write_text('#include "MikageKRKRRuntime.h"\n'
        '_Static_assert(_Generic(&MikageKRKRLastLayerWorkProfileWindowID, uint64_t (*)(void): 1, default: 0), "C accessor signature");\n',encoding='utf-8')
    subprocess.run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror','-fsyntax-only','-I',str(header.parent),str(unit)],check=True)
print('PASS C11 importer, same successful Take -> window ID -> work event wiring; HUD untouched; Swift requires Apple')
