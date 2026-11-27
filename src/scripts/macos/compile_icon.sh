#!/bin/bash
set -euo pipefail

output=$1
deployment_target=$2
catalog=$3
work=$(/usr/bin/mktemp -d /tmp/swift-markdown-icon.XXXXXXXX)
trap 'rm -rf -- "$work"' EXIT
mkdir -p "$output"
cd "$work"
tool=(/usr/bin/env -i)
if [[ -n "${DEVELOPER_DIR:-}" ]]; then tool+=("DEVELOPER_DIR=$DEVELOPER_DIR"); fi
"${tool[@]}" /usr/bin/xcrun actool \
    --compile "$work" --platform macosx \
    --minimum-deployment-target "$deployment_target" \
    --app-icon AppIcon --output-partial-info-plist "$work/AppIcon.plist" \
    "$catalog"
/bin/cp "$work/Assets.car" "$output/Assets.car"
