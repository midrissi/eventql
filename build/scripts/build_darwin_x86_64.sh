#!/bin/bash
PACKAGE=$1
VERSION=$2

if [[ -z "$PACKAGE" || -z "$VERSION" ]]; then
  echo "usage: $0 <package> <version>" >&2
  exit 1
fi

if ! test -d $PACKAGE-$VERSION; then
  echo "ERROR: not found: $PACKAGE-$VERSION"
fi

TARGET_DIR=build/target/$PACKAGE-$VERSION-darwin_x86_64
test -d && rm -rf $TARGET_DIR
mkdir $TARGET_DIR $TARGET_DIR/dist
cd $TARGET_DIR
set -e

../../../$PACKAGE-$VERSION/configure \
    --host=x86_64-apple-darwin14 \
    CC="x86_64-apple-darwin14-cc" \
    CXX="x86_64-apple-darwin14-c++" \
    CXXFLAGS="-stdlib=libc++" \
    MOZJS_CXXFLAGS="-DXP_MACOSX=1"

make
make install DESTDIR=dist
