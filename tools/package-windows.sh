#!/bin/sh
# Cross-compiles Kestrel for Windows (mingw-w64) and assembles a portable zip.
# Expects Qt for Windows in $QT (built from source, see README) and the
# dependency libraries (zlib, opus, libsodium, openssl) in $PREFIX.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
QT=${QT:-/home/claude/xbuild/qt}
PREFIX=${PREFIX:-/home/claude/xbuild/prefix}
TOOLCHAIN=${TOOLCHAIN:-/home/claude/xbuild/mingw.cmake}
BUILD=$ROOT/build-windows
OUT=$ROOT/dist/Kestrel
GCC_DLLS=/usr/lib/gcc/x86_64-w64-mingw32/13-posix

mkdir -p "$BUILD"
cd "$BUILD"
PKG_CONFIG_LIBDIR=$PREFIX/lib/pkgconfig PKG_CONFIG_PATH=$PREFIX/lib/pkgconfig \
cmake -G Ninja "$ROOT" \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$QT;$PREFIX" \
    -DQT_HOST_PATH=/usr \
    -DOPENSSL_ROOT_DIR="$PREFIX" -DOPENSSL_USE_STATIC_LIBS=TRUE \
    -DZLIB_ROOT="$PREFIX" -DZLIB_USE_STATIC_LIBS=ON \
    -DPKG_CONFIG_EXECUTABLE=/usr/bin/pkg-config
PKG_CONFIG_LIBDIR=$PREFIX/lib/pkgconfig ninja kestrel
x86_64-w64-mingw32-strip kestrel.exe

rm -rf "$OUT"
mkdir -p "$OUT"
cp kestrel.exe "$OUT/Kestrel.exe"

# Qt libraries
for m in Core Gui Network WebSockets Qml QmlModels QmlWorkerScript Quick QuickControls2 QuickControls2Impl \
         QuickTemplates2 QuickLayouts QuickDialogs2 QuickDialogs2QuickImpl QuickDialogs2Utils Widgets Svg OpenGL; do
    [ -f "$QT/bin/Qt6$m.dll" ] && cp "$QT/bin/Qt6$m.dll" "$OUT/"
done
# plugins
mkdir -p "$OUT/plugins"
for p in platforms/qwindows.dll styles/qwindowsvistastyle.dll imageformats/qjpeg.dll imageformats/qgif.dll \
         imageformats/qsvg.dll imageformats/qwebp.dll imageformats/qico.dll iconengines/qsvgicon.dll \
         tls/qschannelbackend.dll tls/qcertonlybackend.dll networkinformation/qnetworklistmanager.dll; do
    if [ -f "$QT/plugins/$p" ]; then
        mkdir -p "$OUT/plugins/$(dirname $p)"
        cp "$QT/plugins/$p" "$OUT/plugins/$p"
    fi
done
# QML modules we import
mkdir -p "$OUT/qml"
for q in QtQml QtQuick/Controls QtQuick/Templates QtQuick/Layouts QtQuick/Window QtQuick/Dialogs QtQuick/tooling; do
    if [ -d "$QT/qml/$q" ]; then
        mkdir -p "$OUT/qml/$q"
        cp -r "$QT/qml/$q/." "$OUT/qml/$q/"
    fi
done
cp "$QT/qml/QtQuick/qmldir" "$OUT/qml/QtQuick/" 2>/dev/null || true
cp "$QT/qml/QtQuick/"*.dll "$OUT/qml/QtQuick/" 2>/dev/null || true
cp "$QT/qml/QtQuick/"*.qmltypes "$OUT/qml/QtQuick/" 2>/dev/null || true
# we only use the Basic style; drop the others to save space
for s in Fusion Imagine Material Universal FluentWinUI3 Windows macOS iOS; do rm -rf "$OUT/qml/QtQuick/Controls/$s"; done
find "$OUT/qml" -name "*.qml" -path "*designer*" -delete 2>/dev/null || true

# mingw runtime used by Qt
cp "$GCC_DLLS/libgcc_s_seh-1.dll" "$GCC_DLLS/libstdc++-6.dll" "$OUT/"
cp /usr/x86_64-w64-mingw32/lib/libwinpthread-1.dll "$OUT/"

cat > "$OUT/qt.conf" <<EOF2
[Paths]
Prefix = .
Plugins = plugins
QmlImports = qml
EOF2

cp "$ROOT/README.md" "$OUT/README.txt"
cp "$ROOT/LICENSE" "$OUT/LICENSE.txt"
cd "$ROOT/dist"
rm -f Kestrel-windows-x64.zip
find Kestrel -name "*.dll" -exec x86_64-w64-mingw32-strip --strip-unneeded {} ;
zip -9 -qr Kestrel-windows-x64.zip Kestrel
ls -la Kestrel-windows-x64.zip
du -sh Kestrel
