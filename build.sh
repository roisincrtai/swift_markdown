#!/bin/bash
set -euo pipefail

root=$(cd -- "$(dirname -- "$0")" && pwd)
preset=${1:-macos}
if [[ $# -gt 1 ]]; then
    echo "Usage: $0 [macos|macos-debug|macos-ninja|core]" >&2
    exit 2
fi

work=$(/usr/bin/mktemp -d /tmp/swift-markdown-build.XXXXXXXX)
trap 'rm -rf -- "$work"' EXIT
cd "$work"

case "$preset" in
    macos|macos-debug)
        configuration=Release
        if [[ "$preset" == macos-debug ]]; then configuration=Debug; fi
        cmake -S "$root" --preset macos -DCMAKE_XCODE_GENERATE_SCHEME=ON
        /usr/bin/xcodebuild -project "$root/build/artifacts/SwiftMarkdown.xcodeproj" \
            -scheme ALL_BUILD -configuration "$configuration" -destination 'generic/platform=macOS' \
            -derivedDataPath "$root/build/artifacts/DerivedData"
        ;;
    macos-ninja)
        cmake -S "$root" --preset macos-ninja
        cmake --build "$root/build/native"
        ;;
    core)
        cmake -S "$root" --preset core
        cmake --build "$root/build/core"
        ;;
    *)
        echo "Unknown build preset: $preset" >&2
        exit 2
        ;;
esac
