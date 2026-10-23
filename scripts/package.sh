#!/bin/bash
set -euo pipefail

build_dir=$1
engine=$2
settings=$3
source_dir=$4
identity=${5--}
distribution=${6:-OFF}
if [[ -z "$identity" ]]; then
    echo "A signing identity is required (use - for an ad-hoc development build)." >&2
    exit 1
fi
sign_options=(--force --sign "$identity")
if [[ "$distribution" == ON ]]; then
    if [[ "$identity" == - ]]; then
        echo "Distribution builds require a Developer ID Application certificate." >&2
        exit 1
    fi
    sign_options+=(--options runtime --timestamp)
elif [[ "$distribution" == OFF ]]; then
    # Local builds, including self-signed certificates, need no timestamp service.
    sign_options+=(--timestamp=none)
else
    echo "Distribution mode must be ON or OFF." >&2
    exit 1
fi
staging=$(mktemp -d "$build_dir/.spacewing-package.XXXXXX")
trap 'rm -rf "$staging"' EXIT
app="$staging/Spacewing.app"
mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources"
helper_app="$app/Contents/Helpers/SpacewingSettings.app"
mkdir -p "$helper_app/Contents/MacOS"
cp "$engine" "$app/Contents/MacOS/Spacewing"
cp "$settings" "$helper_app/Contents/MacOS/spacewing-settings"
cp "$build_dir/resources/SettingsInfo.plist" "$helper_app/Contents/Info.plist"
cp "$build_dir/resources/Info.plist" "$app/Contents/Info.plist"
cp "$source_dir/THIRD_PARTY_NOTICES.md" "$app/Contents/Resources/"
cp "$source_dir/resources/default.toml" "$app/Contents/Resources/"
/usr/bin/codesign "${sign_options[@]}" --identifier dev.spacewing.Settings "$helper_app"
/usr/bin/codesign "${sign_options[@]}" --identifier dev.spacewing.Spacewing "$app"
/usr/bin/codesign --verify --deep --strict "$app"
if [[ "$distribution" == ON ]]; then
    # Check the actual signature, so selecting an identity by fingerprint works
    # and a development/self-signed certificate cannot be published by mistake.
    developer_id='anchor apple generic and certificate 1[field.1.2.840.113635.100.6.2.6] exists and certificate leaf[field.1.2.840.113635.100.6.1.13] exists'
    /usr/bin/codesign --verify --strict -R="$developer_id" "$helper_app"
    /usr/bin/codesign --verify --strict -R="$developer_id" "$app"
fi

# Build and sign new inodes. Never overwrite or re-sign a running executable.
destination="$build_dir/Spacewing.app"
if [[ -e "$destination" ]]; then mv "$destination" "$staging/previous.app"; fi
if ! mv "$app" "$destination"; then
    if [[ -e "$staging/previous.app" ]]; then mv "$staging/previous.app" "$destination"; fi
    exit 1
fi
