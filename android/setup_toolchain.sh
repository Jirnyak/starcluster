#!/bin/bash
# Разовая установка Android-тулчейна для сборки Starcluster. Ничего системного не
# трогает: всё живёт в ~/Library/Android/sdk, JDK взят из homebrew.
set -e

export JAVA_HOME=/opt/homebrew/opt/openjdk@17
export PATH="$JAVA_HOME/bin:$PATH"
export ANDROID_HOME="$HOME/Library/Android/sdk"

CMDLINE_ZIP=commandlinetools-mac-11076708_latest.zip

mkdir -p "$ANDROID_HOME/cmdline-tools"

if [ ! -x "$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager" ]; then
    echo "=== качаю cmdline-tools ==="
    cd "$ANDROID_HOME/cmdline-tools"
    curl -sSL -O "https://dl.google.com/android/repository/$CMDLINE_ZIP"
    rm -rf latest cmdline-tools
    unzip -q "$CMDLINE_ZIP"
    mv cmdline-tools latest
    rm -f "$CMDLINE_ZIP"
fi

SDKMANAGER="$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager"

echo "=== лицензии ==="
yes | "$SDKMANAGER" --licenses > /dev/null 2>&1 || true

echo "=== пакеты SDK ==="
"$SDKMANAGER" --install \
    "platform-tools" \
    "platforms;android-34" \
    "build-tools;34.0.0" \
    "ndk;26.1.10909125" \
    "emulator" \
    "system-images;android-34;google_apis;arm64-v8a"

echo "=== готово ==="
"$SDKMANAGER" --list_installed
