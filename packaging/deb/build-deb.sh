#!/bin/sh
# Builds the Debian package from the AppDir linuxdeploy fills for the
# AppImage. Only Qt and its ICU come along, in /opt/guckste: distributions
# ship a Qt too old for Guckste. Every other library comes from the system
# and becomes a dependency, which dpkg-shlibdeps works out, so this has to
# run on the oldest system the package is for.
#
# Usage: build-deb.sh <AppDir> <version> <output.deb>
set -eu

if [ $# -ne 3 ]; then
    echo "Usage: $0 <AppDir> <version> <output.deb>" >&2
    exit 2
fi
appdir=$1
version=$2
output=$3
logo=$(cd "$(dirname "$0")/../../src/logo" && pwd)

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
# Under debian/ dpkg-shlibdeps takes the libraries in it as the package's own
root=$work/debian/guckste
prefix=$root/opt/guckste

mkdir -p "$prefix/lib" "$root/usr/bin" "$root/DEBIAN"
cp -a "$appdir/usr/bin" "$appdir/usr/plugins" "$appdir/usr/qml" "$appdir/usr/translations" "$prefix/"
# Qt's own FFmpeg comes along for the recorder
cp -a "$appdir"/usr/lib/libQt6* "$appdir"/usr/lib/libicu* "$appdir"/usr/lib/libav* "$appdir"/usr/lib/libsw* "$prefix/lib/"
ln -s /opt/guckste/bin/guckste "$root/usr/bin/guckste"
install -Dm644 "$logo/guckste.desktop" "$root/usr/share/applications/guckste.desktop"
install -Dm644 "$logo/guckste.svg" "$root/usr/share/icons/hicolor/scalable/apps/guckste.svg"
chmod -R u+rwX,go+rX,go-w "$root"

printf 'Source: guckste\n\nPackage: guckste\nArchitecture: amd64\n' > "$work/debian/control"
libs=$(cd "$work" && find debian/guckste/opt -type f \( -name '*.so*' -o -path '*/bin/guckste' \) \
    -exec dpkg-shlibdeps -O -l"$prefix/lib" {} + | sed -n 's/^shlibs:Depends=//p')

cat > "$root/DEBIAN/control" <<EOF2
Package: guckste
Version: $version
Architecture: amd64
Maintainer: Henning Gruendl <henning.gruendl@qt.io>
Installed-Size: $(du -sk "$root" | cut -f1)
Depends: $libs
Section: graphics
Priority: optional
Homepage: https://github.com/iam-peter/guckste
Description: Screenshot tool with annotations and pixel measurements
 Guckste sits in the tray and takes screenshots of a region, a screen or
 all screens, to draw on, copy and save. Its editor enlarges a screenshot
 pixel for pixel and puts measurement marks on it that count the pixels.
EOF2

dpkg-deb --root-owner-group -Zxz -b "$root" "$output"
