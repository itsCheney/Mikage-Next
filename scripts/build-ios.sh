#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build
MIKAGE_SOURCE_REVISION="$(git rev-parse --short=12 HEAD)"
xcodebuild build \
  -project Mikage.xcodeproj \
  -scheme Mikage \
  -configuration Release \
  -destination 'generic/platform=iOS' \
  -derivedDataPath build/DerivedData \
  CODE_SIGNING_ALLOWED=NO CODE_SIGNING_REQUIRED=NO \
  MIKAGE_SOURCE_REVISION="${MIKAGE_SOURCE_REVISION}" \
  | tee build/device-build.log
