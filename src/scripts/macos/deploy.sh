#!/bin/bash
set -euo pipefail

if [[ $# -lt 3 || $# -gt 5 || (${5:-} != "" && ${5:-} != "--app-only") ]]; then
    echo "Usage: $0 <unsigned-app> <dist-directory> <version> [identity] [--app-only]" >&2
    exit 2
fi
app=$1
dist=$2
version=$3
identity=${4:-}
app_only=${5:-}
name=$(basename "$app")
if [[ ! -d "$app" || "$name" != *.app ]]; then
    echo "App bundle not found: $app" >&2
    exit 2
fi
if [[ ! "$version" =~ ^[0-9][0-9a-zA-Z.+_-]*$ ]]; then
    echo "Invalid version: $version" >&2
    exit 2
fi

if [[ -z "$identity" ]]; then
    identities=$(/usr/bin/security find-identity -v -p codesigning 2>/dev/null || true)
    identity=$(/usr/bin/awk -F '"' '/Developer ID Application:/ {print $2; exit}' <<< "$identities")
    if [[ -z "$identity" ]]; then
        identity=$(/usr/bin/awk -F '"' '/Apple Development:/ {print $2; exit}' <<< "$identities")
    fi
    identity=${identity:--}
fi

mkdir -p "$dist"
work=$(/usr/bin/mktemp -d "$dist/.deploy.XXXXXXXX")
trap 'rm -rf "$work"' EXIT
stage="$work/staging"
mkdir "$stage"
/usr/bin/ditto "$app" "$stage/$name"
/usr/bin/xattr -d com.apple.quarantine "$stage/$name" 2>/dev/null || true
script_dir=$(cd -- "$(dirname -- "$0")" && pwd)
entitlements="$script_dir/../../ui/SwiftMarkdown.entitlements"
if [[ "$identity" == "Developer ID Application:"* ]]; then
    /usr/bin/codesign --force --sign "$identity" --options runtime --timestamp --entitlements "$entitlements" "$stage/$name"
else
    /usr/bin/codesign --force --sign "$identity" --timestamp=none --entitlements "$entitlements" "$stage/$name"
fi
/usr/bin/codesign --verify --deep --strict "$stage/$name"
install_bundle() {
    if [[ -e "$dist/$name" ]]; then mv "$dist/$name" "$work/prior.app"; fi
    if ! mv "$stage/$name" "$dist/$name"; then
        if [[ -e "$work/prior.app" ]]; then mv "$work/prior.app" "$dist/$name"; fi
        return 1
    fi
}

if [[ "$app_only" == "--app-only" ]]; then
    install_bundle
    printf 'App: %s\n' "$dist/$name"
    exit 0
fi

ln -s /Applications "$stage/Applications"

image_name="Swift-Markdown-$version.dmg"
/usr/bin/hdiutil create -volname "Swift Markdown" -srcfolder "$stage" -format UDZO "$work/$image_name"
/usr/bin/hdiutil verify "$work/$image_name"
if [[ "$identity" == "Developer ID Application:"* ]]; then
    /usr/bin/codesign --force --sign "$identity" --timestamp "$work/$image_name"
    /usr/bin/codesign --verify "$work/$image_name"
fi

install_bundle
mv -f "$work/$image_name" "$dist/$image_name"
printf 'App: %s\nDMG: %s\n' "$dist/$name" "$dist/$image_name"
