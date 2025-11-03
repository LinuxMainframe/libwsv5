#!/bin/bash
# Install libwsv5 to user's local directory
# This script installs without requiring sudo

set -e

# Determine user directory
USER_PREFIX="${1:-$HOME/.local}"

echo "Installing libwsv5 to: $USER_PREFIX"
echo ""

# Check if directory is writable
if [ ! -w "$USER_PREFIX" ] && [ ! -d "$USER_PREFIX" ]; then
    mkdir -p "$USER_PREFIX" || {
        echo "Error: Cannot create directory $USER_PREFIX"
        exit 1
    }
fi

# Build directory
BUILD_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/build-local"

if [ -d "$BUILD_DIR" ]; then
    echo "Cleaning old build..."
    rm -rf "$BUILD_DIR"
fi

mkdir -p "$BUILD_DIR"

# Configure
echo "Configuring build..."
cd "$BUILD_DIR"
cmake -DCMAKE_BUILD_TYPE=Release \
       -DCMAKE_INSTALL_PREFIX="$USER_PREFIX" \
       ..

# Build
echo "Building..."
make -j$(nproc)

# Install
echo "Installing..."
make install

# Set up environment
echo ""
echo "Installation complete!"
echo ""
echo "To use libwsv5, add the following to your ~/.bashrc:"
echo ""
echo "  export PKG_CONFIG_PATH=\"$USER_PREFIX/lib/pkgconfig:\$PKG_CONFIG_PATH\""
echo "  export LD_LIBRARY_PATH=\"$USER_PREFIX/lib:\$LD_LIBRARY_PATH\""
echo "  export CPATH=\"$USER_PREFIX/include:\$CPATH\""
echo ""
echo "Or apply now:"
export PKG_CONFIG_PATH="$USER_PREFIX/lib/pkgconfig:$PKG_CONFIG_PATH"
export LD_LIBRARY_PATH="$USER_PREFIX/lib:$LD_LIBRARY_PATH"

# Verify
echo "Verifying installation..."
if pkg-config --modversion libwsv5 2>/dev/null; then
    echo "✓ Installation verified!"
else
    echo "✗ Installation failed or pkg-config not found"
    exit 1
fi