# libwsv5 Distribution & Deployment Guide

Complete guide for distributing and deploying libwsv5 to users.

## Overview

libwsv5 is now ready for public distribution with multiple installation methods:

1. **System Installation** - Users compile and install locally
2. **.deb Packages** - Pre-built Debian/Ubuntu packages
3. **Source Archives** - For building from source
4. **GitHub Releases** - Easy download and installation

## For Users: Installation Methods

### Method 1: System Installation (Most Flexible)

Users can build and install from source:

```bash
git clone https://github.com/linuxmainframe/libwsv5.git
cd libwsv5
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local ..
make -j$(nproc)
sudo make install
```

**Pros:** Works on any system, control over build flags
**Cons:** Requires build tools, compilation time

See [INSTALL_GUIDE.md](INSTALL_GUIDE.md) for detailed instructions.

### Method 2: Quick Installation Script

Automated setup with dependency detection:

```bash
git clone https://github.com/linuxmainframe/libwsv5.git
cd libwsv5/scripts
./quick-install.sh --system   # Install to /usr/local (sudo required)
./quick-install.sh --local    # Install to ~/.local (no sudo)
```

### Method 3: .deb Package (Ubuntu/Debian Users)

Pre-built packages from GitHub releases:

```bash
# Download and install
wget https://github.com/linuxmainframe/libwsv5/releases/download/v1.1.0/libwsv5_1.1.0_amd64.deb
sudo apt-get install ./libwsv5_1.1.0_amd64.deb

# Verify
pkg-config --modversion libwsv5
```

**Pros:** Fast installation, automatic dependency handling
**Cons:** Ubuntu/Debian only (currently)

### Method 4: Package Manager (Future)

Once in distributions' package repositories:

```bash
# Ubuntu/Debian
sudo apt-get install libwsv5

# Fedora
sudo dnf install libwsv5
```

## For Developers: Creating Packages

### Generate All Packages

Use the automated build script:

```bash
cd libwsv5
./scripts/build-packages.sh
```

This creates:
- `.deb` package for Debian/Ubuntu
- `.tar.gz` source archive
- `.zip` source archive
- `SHA256SUMS` checksums

### Manual .deb Generation

```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr ..
make -j$(nproc)
cpack -G DEB
```

### Manual Source Archive Generation

```bash
cd build
cpack --config CPackSourceConfig.cmake -G TGZ
cpack --config CPackSourceConfig.cmake -G ZIP
```

## GitHub Release Process

### 1. Prepare Release

Update version in `CMakeLists.txt`:
```cmake
project(libwsv5 C VERSION 1.2.0 DESCRIPTION "...")
```

Update `CHANGELOG.md` and `.github/RELEASE_TEMPLATE.md`

### 2. Create and Push Tag

```bash
git tag -a v1.2.0 -m "Release version 1.2.0"
git push origin v1.2.0
```

### 3. Automated Package Generation

The GitHub Actions workflow automatically:
- Builds the library
- Creates .deb, .tar.gz, and .zip packages
- Generates SHA256 checksums
- Creates GitHub release with all artifacts

No manual action needed after tag push!

### 4. Manual Upload (If Needed)

If not using GitHub Actions:

```bash
# Build packages
./scripts/build-packages.sh

# Create release and upload
gh release create v1.2.0 release-artifacts/*
```

## Package Contents

### libwsv5_1.1.0_amd64.deb

Installs to standard Debian locations:
- Headers: `/usr/include/libwsv5/libwsv5.h`
- Static lib: `/usr/lib/libwsv5.a`
- Shared lib: `/usr/lib/libwsv5.so`
- pkg-config: `/usr/lib/pkgconfig/libwsv5.pc`

### Source Archives

- `libwsv5-1.1.0.tar.gz` - Compressed tarball
- `libwsv5-1.1.0.zip` - ZIP archive

Both include:
- All source files
- CMakeLists.txt
- Documentation
- Examples and tests
- Build scripts

### SHA256SUMS

Checksums for verification:
```bash
sha256sum -c SHA256SUMS
```

## Documentation for Users

### README.md

Main project documentation with:
- Features overview
- Quick start guide
- Basic usage examples
- Architecture overview
- Troubleshooting

### INSTALL_GUIDE.md

Comprehensive installation guide with:
- Step-by-step installation for each method
- Platform-specific instructions (Ubuntu, Fedora, macOS)
- Installation verification
- Troubleshooting common issues
- Usage examples with pkg-config and CMake

### API_REFERENCE.md

Complete API documentation:
- All functions
- All types and enums
- Error codes
- Configuration options
- Usage patterns

### PACKAGING.md

Package creation and distribution guide:
- How to build packages
- Version management
- CI/CD setup
- Distribution best practices

## Continuous Integration

### GitHub Actions Workflow

Automatically builds and releases when pushing version tags:

```yaml
# Triggered by: git push origin v1.1.0
- Builds on Ubuntu 22.04
- Creates .deb package
- Generates source archives
- Creates GitHub release
- Uploads all artifacts
```

Configure in `.github/workflows/package-release.yml`

## Quality Assurance

### Pre-Release Checklist

Before releasing:

- [ ] Version updated in CMakeLists.txt
- [ ] CHANGELOG.md updated
- [ ] RELEASE_TEMPLATE.md reviewed
- [ ] All tests passing: `./test -h localhost -p 4455`
- [ ] Documentation up to date
- [ ] .deb package tested on clean Ubuntu system
- [ ] SHA256 checksums generated and verified
- [ ] GitHub Actions workflow configured correctly

### Testing Package Installation

On a clean Ubuntu system:

```bash
# Download .deb
wget https://github.com/linuxmainframe/libwsv5/releases/download/v1.1.0/libwsv5_1.1.0_amd64.deb

# Install
sudo apt-get install ./libwsv5_1.1.0_amd64.deb

# Test compilation
gcc -c test.c -I/usr/include $(pkg-config --cflags libwsv5)
gcc test.o -o test $(pkg-config --libs libwsv5)

# Verify
./test
```

## Distribution Channels

### 1. GitHub Releases

**Location:** https://github.com/linuxmainframe/libwsv5/releases

**Artifacts:**
- .deb packages
- Source archives
- Documentation
- Release notes

**Advantages:**
- Easy automatic updates via CI/CD
- Version history
- Download statistics
- Pre-release option

### 2. Package Repositories

Future distribution options:

**Ubuntu/Debian PPA**
```bash
sudo add-apt-repository ppa:linuxmainframe/libwsv5
sudo apt-get update
sudo apt-get install libwsv5
```

**Fedora COPR**
```bash
sudo dnf copr enable linuxmainframe/libwsv5
sudo dnf install libwsv5
```

**Homebrew (macOS)**
```bash
brew install libwsv5
```

### 3. Website/Documentation

- Download page with installation instructions
- Version history
- Dependencies list
- Supported platforms

## Maintenance

### Security Updates

When releasing security updates:

1. Fix vulnerability in code
2. Add entry to CHANGELOG.md marking as security fix
3. Increment patch version: `1.1.0` → `1.1.1`
4. Tag and push for automatic release

### Dependency Updates

Keep aware of:
- libwebsockets updates/patches
- cJSON updates/patches
- OpenSSL security notices
- CMake version requirements

Update README.md with new minimum versions if needed.

## User Feedback

### Bug Reports

Include in issue reports:
- Installation method used
- Ubuntu/Debian version: `lsb_release -a`
- Build environment: `pkg-config --modversion libwebsockets`
- Steps to reproduce
- Expected vs actual behavior

### Feature Requests

For feature requests:
- Check existing issues first
- Describe use case
- Propose API if applicable
- Link to similar libraries

### Success Stories

Share use cases and feedback:
- How you're using libwsv5
- Performance metrics if relevant
- Integration examples
- Feedback on documentation

## Example Workflows

### End-User: Install and Use

```bash
# 1. Install
./scripts/quick-install.sh --system

# 2. Create project
mkdir my-obs-app && cd my-obs-app

# 3. Create CMakeLists.txt
cat > CMakeLists.txt << 'EOF'
cmake_minimum_required(VERSION 3.10)
project(MyApp C)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBWSV5 REQUIRED libwsv5)

add_executable(myapp main.c)
target_link_libraries(myapp ${LIBWSV5_LIBRARIES})
target_include_directories(myapp PRIVATE ${LIBWSV5_INCLUDE_DIRS})
EOF

# 4. Build and run
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
```

### Maintainer: Release New Version

```bash
# 1. Update version and changelog
vim CMakeLists.txt CHANGELOG.md .github/RELEASE_TEMPLATE.md

# 2. Commit changes
git add -A
git commit -m "Prepare release v1.2.0"

# 3. Tag and push
git tag -a v1.2.0 -m "Release version 1.2.0"
git push origin main v1.2.0

# 4. GitHub Actions automatically builds and releases
# (Check: https://github.com/linuxmainframe/libwsv5/releases)
```

## Support Resources

- **GitHub Issues:** Bug reports and feature requests
- **GitHub Discussions:** Questions and general discussion
- **API Reference:** Detailed function documentation
- **Installation Guide:** Step-by-step setup instructions
- **Examples:** Code examples in tests/ directory

---

For questions or issues, visit: https://github.com/linuxmainframe/libwsv5