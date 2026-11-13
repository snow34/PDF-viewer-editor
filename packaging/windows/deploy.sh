#!/usr/bin/env bash
# Collects everything the app needs to run on a Windows PC without MSYS2.
# Run in an MSYS2 UCRT64 shell from the project folder:
#   packaging/windows/deploy.sh <build folder> <output folder>
set -euo pipefail

build=$1
out=$2

rm -rf "$out"
mkdir -p "$out"
cp "$build/prepresspdf.exe" "$out/"
cp LICENSE "$out/LICENSE.txt"

deployqt=$(command -v windeployqt6 || command -v windeployqt-qt6 || command -v windeployqt || true)
if [ -z "$deployqt" ]; then
    echo "windeployqt was not found" >&2
    exit 1
fi
"$deployqt" --no-translations --compiler-runtime "$out/prepresspdf.exe"

# windeployqt only knows about Qt. Copy every other DLL that the program and
# the Qt plugins load from MSYS2, repeating until nothing new turns up.
copy_dependencies() {
    local copied=0 file dll
    while read -r file; do
        while read -r dll; do
            if [ ! -e "$out/$(basename "$dll")" ]; then
                cp "$dll" "$out/"
                copied=1
            fi
        done < <(ldd "$file" | awk '{print $3}' | grep -i "^${MINGW_PREFIX}/" || true)
    done < <(find "$out" -name '*.exe' -o -name '*.dll')
    return $((1 - copied))
}
while copy_dependencies; do :; done

# Licence texts: the notices file, plus the licence folder of every MSYS2
# package a bundled DLL came from.
cp THIRD-PARTY-NOTICES.txt "$out/"
mkdir -p "$out/licenses"
find "$out" -name '*.dll' -printf '%f\n' | sort -u | while read -r dll; do
    [ -e "$MINGW_PREFIX/bin/$dll" ] || continue
    package=$(pacman -Qqo "$MINGW_PREFIX/bin/$dll" 2>/dev/null || true)
    name=${package#"$MINGW_PACKAGE_PREFIX"-}
    if [ -n "$name" ] && [ -d "$MINGW_PREFIX/share/licenses/$name" ] && [ ! -e "$out/licenses/$name" ]; then
        cp -r "$MINGW_PREFIX/share/licenses/$name" "$out/licenses/$name"
    fi
done

echo "Deployed to $out:"
ls "$out" "$out/licenses"
