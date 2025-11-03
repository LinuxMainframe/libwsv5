#!/bin/bash
# Quick installation script for libwsv5
# Usage: ./quick-install.sh [--system|--local|--prefix /path]

set -e

# Defaults
INSTALL_TYPE="system"
PREFIX="/usr/local"

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --system)
            INSTALL_TYPE="system"
            PREFIX="/usr/local"
            shift
            ;;
        --local)
            INSTALL_TYPE="local"
            PREFIX="$HOME/.local"
            shift
            ;;
        --prefix)
            PREFIX="$2"
            shift 2
            ;;
        --help)
            echo "Usage: $0 [--system|--local|--prefix /path]"
            echo ""
            echo "Options:"
            echo "  --system      Install to /usr/local (requires sudo)"
            echo "  --local       Install to ~/.local (no sudo needed)"
            echo "  --prefix      Specify custom prefix"
            echo "  --help        Show this help message"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build-install"

echo "========================================="
echo "  libwsv5 Quick Installer"
echo "========================================="
echo ""
echo "Installation Type: $INSTALL_TYPE"
echo "Installation Prefix: $PREFIX"
echo ""

# Check dependencies
echo "Checking dependencies..."
for pkg in cmake libwebsockets cjson openssl; do
    if ! pkg-config --exists "libwebsockets cjson openssl" 2>/dev/null; then
        echo "Installing build dependencies..."
        if command -v apt-get &> /dev/null; then
            sudo apt-get update
            sudo apt-get install -y build-essential cmake libwebsockets-dev libcjson-dev libssl-dev
        elif command -v dnf &> /dev/null; then
            sudo dnf install -y cmake libwebsockets-devel cjson-devel openssl-devel
        else
            echo "Error: Please install build dependencies manually"
            exit 1
        fi
        break
    fi
done

# Clean old build
[ -d "$BUILD_DIR" ] && rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

# Build
echo ""
echo "Building libwsv5..."
cd "$BUILD_DIR"
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" "$PROJECT_DIR"
make -j$(nproc)

# Install
echo ""
echo "Installing..."
if [ "$INSTALL_TYPE" = "system" ]; then
    sudo make install
    NEED_SUDO_CMD=""
else
    make install
    NEED_SUDO_CMD="(no sudo needed)"
fi

# Verify
echo ""
echo "Verifying installation..."

# Add to PKG_CONFIG_PATH if needed
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:$PKG_CONFIG_PATH"
export LD_LIBRARY_PATH="$PREFIX/lib:$LD_LIBRARY_PATH"

if pkg-config --modversion libwsv5 &>/dev/null; then
    VERSION=$(pkg-config --modversion libwsv5)
    echo ""
    echo "✓ Installation successful!"
    echo "  libwsv5 version: $VERSION"
    echo "  Location: $PREFIX"
else
    echo "✗ Verification failed"
    exit 1
fi

# Setup instructions
if [ "$INSTALL_TYPE" = "local" ]; then
    echo ""
    echo "========================================="
    echo "  Setup Instructions"
    echo "========================================="
    echo ""
    echo "Add the following to your ~/.bashrc or ~/.zshrc:"
    echo ""
    echo "  export PKG_CONFIG_PATH=\"$PREFIX/lib/pkgconfig:\$PKG_CONFIG_PATH\""
    echo "  export LD_LIBRARY_PATH=\"$PREFIX/lib:\$LD_LIBRARY_PATH\""
    echo ""
    echo "Or run now:"
    echo "  source ~/.bashrc"
fi

echo ""
echo "========================================="
echo "  Next Steps"
echo "========================================="
echo ""
echo "Compile a program using libwsv5:"
echo "  gcc myapp.c -o myapp \$(pkg-config --cflags --libs libwsv5)"
echo ""
echo "Or with CMake, add to CMakeLists.txt:"
echo "  find_package(PkgConfig REQUIRED)"
echo "  pkg_check_modules(LIBWSV5 REQUIRED libwsv5)"
echo ""
echo "Check the INSTALL_GUIDE.md for more information"
echo ""