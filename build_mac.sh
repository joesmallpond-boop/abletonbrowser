#!/bin/bash
# Builds Bash Browser on this Mac and installs the VST3 + AU for your user account.
# Needs: Xcode Command Line Tools (xcode-select --install) and CMake (brew install cmake).
set -e
cd "$(dirname "$0")"
command -v cmake >/dev/null || { echo "CMake not found. Install it with: brew install cmake"; exit 1; }
cmake -B build-mac -DCMAKE_BUILD_TYPE=Release -DSB_BUILD_TESTS=OFF
cmake --build build-mac --config Release --target BashBrowser_VST3 BashBrowser_AU BashBrowser_Standalone -j "$(sysctl -n hw.ncpu)"
ART="build-mac/BashBrowser_artefacts/Release"
mkdir -p ~/Library/Audio/Plug-Ins/VST3 ~/Library/Audio/Plug-Ins/Components
rm -rf ~/Library/Audio/Plug-Ins/VST3/"Bash Browser.vst3" ~/Library/Audio/Plug-Ins/Components/"Bash Browser.component"
cp -R "$ART/VST3/Bash Browser.vst3" ~/Library/Audio/Plug-Ins/VST3/
cp -R "$ART/AU/Bash Browser.component" ~/Library/Audio/Plug-Ins/Components/
cp -R "$ART/Standalone/Bash Browser.app" ~/Applications/ 2>/dev/null || true
codesign --force --deep -s - ~/Library/Audio/Plug-Ins/VST3/"Bash Browser.vst3" ~/Library/Audio/Plug-Ins/Components/"Bash Browser.component"
killall -9 AudioComponentRegistrar 2>/dev/null || true
echo "Installed. Rescan plug-ins in Live (Settings > Plug-Ins)."
