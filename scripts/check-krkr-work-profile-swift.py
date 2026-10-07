#!/usr/bin/env python3
"""Type-check the production Swift reader against the real C header via ClangImporter."""
import argparse
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--swiftc')
    parser.add_argument('--sdk', choices=('macosx', 'iphonesimulator'), default='iphonesimulator')
    args = parser.parse_args()
    xcrun = shutil.which('xcrun')
    swiftc = args.swiftc or (subprocess.check_output([xcrun, '--find', 'swiftc'], text=True).strip() if xcrun else shutil.which('swiftc'))
    if not swiftc or not xcrun:
        parser.error('Apple Swift/Xcode is required; portable C++ checks do not validate ClangImporter')
    root = Path(__file__).resolve().parents[1]
    header = root / 'Engine/KRKRRuntime/Source/host/MikageKRKRRuntime.h'
    reader = root / 'App/Engine/KRKRLayerWorkSample.swift'
    sdk = subprocess.check_output([xcrun, '--sdk', args.sdk, '--show-sdk-path'], text=True).strip()
    with tempfile.TemporaryDirectory(prefix='mikage-work-profile-swift-') as folder:
        module = Path(folder)
        (module / 'module.modulemap').write_text('module KRKRRuntime { header "' + header.as_posix() + '" export * }\n', encoding='utf-8')
        smoke = module / 'ProfileImportSmoke.swift'
        smoke.write_text('''import KRKRRuntime
func smoke() {
  var profile = MikageKRKRLayerWorkProfile()
  profile.transitionProfileVersion = 1
  profile.transitionProfilesDropped = 0
  let copied = KRKRLayerWorkSample(profile: &profile)
  let _: String = copied.transitionProfiles
  let _: String = copied.transitionOverflow
  withUnsafePointer(to: &profile) { pointer in
    let _: UnsafePointer<CChar>? = MikageKRKRLayerWorkProfileTransitions(pointer)
    let _: UnsafePointer<CChar>? = MikageKRKRLayerWorkProfileTransitionOverflow(pointer)
  }
}
''', encoding='utf-8')
        command = [swiftc, '-typecheck', '-swift-version', '5', '-sdk', sdk, '-I', str(module)]
        if args.sdk == 'iphonesimulator':
            architecture = 'arm64' if platform.machine().lower() in ('arm64', 'aarch64') else 'x86_64'
            command.extend(['-target', architecture + '-apple-ios16.0-simulator'])
        subprocess.run(command + [str(reader), str(smoke)], check=True)
    print('PASS production LayerWork Swift reader and C header import (' + args.sdk + ')')


if __name__ == '__main__':
    main()
