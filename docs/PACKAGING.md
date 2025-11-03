# libwsv5 Packaging Guide

This document explains how to create and distribute libwsv5 packages for different platforms.

## Overview

libwsv5 supports multiple distribution methods:
- **System Installation** - Local system installation via CMake
- **.deb Packages** - For Debian/Ubuntu systems
- **Source Archives** - For building from source
- **GitHub Releases** - Pre-built binaries for easy download

## System-Wide Installation

### Install from Source

```bash
# Clone the repository
git clone https://github.com/linuxmainframe/libwsv5.git
cd libwsv5

# Install dependencies
sudo apt-get install build-essential cmake libwebsockets-dev libcjson-dev libssl-dev

# Build and install
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local ..
make -j$(nproc)
sudo make install

# Verify installation
ls -la /usr/local/include/libwsv5/
ls -la /usr/local/lib/libwsv5.pc
```

### Using pkg-config

After installation, you can use pkg-config to find the library:

```bash
# Get compiler flags
pkg-config --cflags libwsv5

# Get linker flags
pkg-config --libs libwsv5

# Check version
pkg-config --modversion libwsv5
```

### Example Compilation

```c
// example.c
#include <libwsv5.h>
#include <stdio.h>

int main() {
    obsws_init();
    printf("libwsv5 initialized\n");
    obsws_cleanup();
    return 0;
}
```

Compile with system headers:

```bash
gcc example.c -o example $(pkg-config --cflags --libs libwsv5)
./example
```

## Creating .deb Packages

### Prerequisites

```bash
sudo apt-get install build-essential cmake debhelper devscripts
```

### Build .deb Package

```bash
cd libwsv5/build

# Configure for system installation
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr ..

# Build
make -j$(nproc)

# Create .deb package
cpack -G DEB

# Verify the package
ls -lh libwsv5_*.deb
```

### Install .deb Package

```bash
# Install locally
sudo dpkg -i libwsv5_1.1.0_amd64.deb

# Or with automatic dependency resolution
sudo apt-get install ./libwsv5_1.1.0_amd64.deb
```

### Package Information

View package contents:

```bash
dpkg-deb -c libwsv5_1.1.0_amd64.deb
```

List installed files:

```bash
dpkg -L libwsv5
```

### Create Source .deb

For Debian package repositories, you'll also need:

```bash
cpack --config CPackSourceConfig.cmake -G TGZ
```

## GitHub Releases

### Prepare Artifacts

```bash
cd libwsv5
mkdir -p release-artifacts

# Build and package
mkdir build-release && cd build-release
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr ..
make -j$(nproc)
cpack -G DEB

# Move artifacts
cd ..
cp build-release/libwsv5_*.deb release-artifacts/
cp build-release/libwsv5_*.tar.gz release-artifacts/
cp build-release/libwsv5_*.zip release-artifacts/

# Create checksums
cd release-artifacts
sha256sum * > SHA256SUMS
cd ..
```

### Upload to GitHub

Via GitHub CLI:

```bash
# Tag the release
git tag -a v1.1.0 -m "Release version 1.1.0"
git push origin v1.1.0

# Create release and upload artifacts
gh release create v1.1.0 release-artifacts/*
```

Or manually via GitHub web interface:

1. Go to https://github.com/linuxmainframe/libwsv5/releases
2. Click "Draft a new release"
3. Set tag to `v1.1.0`
4. Upload files from `release-artifacts/` folder
5. Include release notes

## Version Management

### Update Version

Update the version in `CMakeLists.txt`:

```cmake
project(libwsv5 C VERSION 1.2.0 DESCRIPTION "OBS WebSocket v5 Protocol C Library")
```

This automatically updates:
- Package version in CPack
- pkg-config file
- Doxygen documentation

### Update Changelog

Keep `CHANGELOG.md` updated with:

```markdown
## [1.2.0] - 2024-01-XX

### Added
- New feature description

### Fixed
- Bug fix description

### Changed
- Behavior change description
```

## Continuous Integration

### GitHub Actions Example

Create `.github/workflows/package.yml`:

```yaml
name: Build Packages

on:
  push:
    tags:
      - 'v*'

jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      
      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y build-essential cmake libwebsockets-dev libcjson-dev libssl-dev
      
      - name: Build
        run: |
          mkdir build && cd build
          cmake -DCMAKE_BUILD_TYPE=Release ..
          make -j$(nproc)
      
      - name: Package
        run: |
          cd build
          cpack -G DEB
      
      - name: Create Release
        uses: softprops/action-gh-release@v1
        with:
          files: build/libwsv5_*.deb
        env:
          GITHUB_TOKEN: ${{ secrets.GITHUB_TOKEN }}
```

## Installation Verification

### Verify Library Installation

```bash
# Check header files
ls -la /usr/local/include/libwsv5/libwsv5.h

# Check libraries
ls -la /usr/local/lib/libwsv5.a
ls -la /usr/local/lib/libwsv5.so

# Check pkg-config
pkg-config --list-all | grep libwsv5
```

### Test Compilation

```bash
# Create test program
cat > test_install.c << 'EOF'
#include <libwsv5.h>
#include <stdio.h>

int main() {
    printf("libwsv5 header found and included!\n");
    return 0;
}
EOF

# Compile with system headers
gcc -c test_install.c -I/usr/local/include
gcc test_install.o -L/usr/local/lib -lwsv5 -o test_install -lm

# Run
./test_install
```

## Troubleshooting

### CMake can't find dependencies

```bash
# Install dev packages
sudo apt-get install libwebsockets-dev libcjson-dev libssl-dev

# Try specifying paths explicitly
cmake -DCMAKE_INSTALL_PREFIX=/usr ..
```

### pkg-config doesn't find library

```bash
# Check pkg-config path
echo $PKG_CONFIG_PATH

# Add to path if needed
export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH

# Verify
pkg-config --modversion libwsv5
```

### .deb installation issues

```bash
# Check dependencies
dpkg -I libwsv5_*.deb

# Install missing dependencies
sudo apt-get install -f

# Re-install package
sudo dpkg -i libwsv5_*.deb
```

## Distribution Best Practices

1. **Always build in Release mode** for production packages
2. **Include checksums** with distributed files
3. **Test installation** on clean systems
4. **Document dependencies** clearly
5. **Maintain changelog** for all releases
6. **Use semantic versioning** (MAJOR.MINOR.PATCH)
7. **Sign releases** if possible (GPG)
8. **Include license** in all packages

## Additional Resources

- [CMake CPack Documentation](https://cmake.org/cmake/help/latest/module/CPack.html)
- [Debian Package Format](https://www.debian.org/doc/debian-policy/)
- [pkg-config Documentation](https://www.freedesktop.org/wiki/Software/pkg-config/)
- [GitHub Releases API](https://docs.github.com/en/rest/releases)