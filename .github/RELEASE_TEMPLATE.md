# Release v1.1.0

## Overview
Describe the major changes, improvements, and features in this release.

## Key Features
- Feature 1
- Feature 2
- Feature 3

## Improvements
- Improvement 1
- Improvement 2

## Bug Fixes
- Bug fix 1
- Bug fix 2

## Installation

### From .deb Package (Ubuntu/Debian)
```bash
sudo apt-get install ./libwsv5_1.1.0_amd64.deb
```

### From Source
```bash
git clone https://github.com/linuxmainframe/libwsv5.git
cd libwsv5
git checkout v1.1.0

mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local ..
make -j$(nproc)
sudo make install
```

### Verify Installation
```bash
pkg-config --modversion libwsv5
```

## Documentation
- [Installation Guide](INSTALL_GUIDE.md)
- [API Reference](API_REFERENCE.md)
- [Packaging Guide](PACKAGING.md)

## Downloads
- `libwsv5_1.1.0_amd64.deb` - Debian/Ubuntu package
- `libwsv5-1.1.0.tar.gz` - Source code (tar.gz)
- `libwsv5-1.1.0.zip` - Source code (zip)

## Checksums
All downloads should be verified against SHA256SUMS file:
```bash
sha256sum -c SHA256SUMS
```

## Support
For issues, questions, or contributions:
- [GitHub Issues](https://github.com/linuxmainframe/libwsv5/issues)
- [GitHub Discussions](https://github.com/linuxmainframe/libwsv5/discussions)

## Contributors
Thank you to all contributors in this release!

---
**Release Date:** January XX, 2024