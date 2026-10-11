#!/usr/bin/env python3
"""Check shared Bundle wiring; optionally validate a generated app Info.plist.

This source check does not compile SwiftUI or substitute for Apple App tests.
"""
import argparse
from pathlib import Path
import plistlib
import re


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plist', type=Path)
    parser.add_argument('--expected-revision')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    source = (root / 'App/AppBuildInfo.swift').read_text(encoding='utf-8')
    settings = (root / 'App/SettingsView.swift').read_text(encoding='utf-8')
    diagnostics = (root / 'App/AppDiagnostics.swift').read_text(encoding='utf-8')
    workflow = (root / '.github/workflows/ios.yml').read_text(encoding='utf-8')
    build = (root / 'scripts/build-ios.sh').read_text(encoding='utf-8')
    info = plistlib.loads((root / 'App/Info.plist').read_bytes())
    assert 'import Foundation' in source and 'import SwiftUI' not in source
    assert 'info["MikageSourceRevision"]' in source and '[12, 40, 64]' in source
    assert 'Process(' not in source and 'URLSession' not in source
    assert 'AppBuildInfo()' in settings and 'Text(buildInfo.versionLine)' in settings and 'Text(buildInfo.headLine)' in settings
    assert '.monospaced' in settings and 'MIKAGE NEXT · 0.1' not in settings
    assert 'AppBuildInfo()' in diagnostics and '"sourceRevision": buildInfo.sourceRevision' in diagnostics
    assert info['MikageSourceRevision'] == '$(MIKAGE_SOURCE_REVISION)'
    simulator = workflow.split('- name: Test app model on a simulator', 1)[1].split('- name:', 1)[0]
    for command in (simulator, build):
        assert 'git rev-parse --short=12 HEAD' in command
        assert 'MIKAGE_SOURCE_REVISION="${MIKAGE_SOURCE_REVISION}"' in command
    assert (root / 'Tests/MikageTests/AppBuildInfoTests.swift').exists()
    if args.plist:
        generated = plistlib.loads(args.plist.read_bytes())
        revision = generated.get('MikageSourceRevision')
        assert isinstance(revision, str) and re.fullmatch(r'[0-9A-Fa-f]{12}', revision), 'generated app lacks injected short HEAD'
        assert generated.get('CFBundleShortVersionString'), 'generated app lacks bundle version'
        if args.expected_revision:
            assert revision.lower() == args.expected_revision.lower(), 'injected app revision differs from root HEAD'
    print('PASS App build-info Bundle wiring; SwiftUI/App compilation requires Apple validation')


if __name__ == '__main__':
    main()
