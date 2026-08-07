#!/bin/bash
# Сборка apk. Тулчейн ставится один раз через setup_toolchain.sh.
set -e
cd "$(dirname "$0")"

export JAVA_HOME=/opt/homebrew/opt/openjdk@17
export PATH="$JAVA_HOME/bin:$PATH"
export ANDROID_HOME="$HOME/Library/Android/sdk"

./gradlew "${@:-assembleDebug}"

echo
echo "apk: $(pwd)/app/build/outputs/apk/debug/app-debug.apk"
ls -lh app/build/outputs/apk/debug/app-debug.apk 2>/dev/null || true
