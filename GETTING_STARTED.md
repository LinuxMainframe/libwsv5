# Getting Started with libwsv5

Complete guide to install and use the libwsv5 library.

## Prerequisites

Install required system dependencies:

### Ubuntu/Debian
```bash
sudo apt-get update
sudo apt-get install libwebsockets-dev libcjson-dev libssl-dev build-essential cmake
```

### Fedora/RHEL/CentOS
```bash
sudo dnf install libwebsockets-devel cjson-devel openssl-devel gcc cmake make
```

### macOS
```bash
brew install libwebsockets cjson openssl cmake
```

## Installation Methods

### Option 1: System-Wide Installation (Recommended)

Install to `/usr/local` for system-wide access:

```bash
git clone https://github.com/linuxmainframe/libwsv5.git
cd libwsv5/scripts
./quick-install.sh --system
```

Then verify:
```bash
pkg-config --modversion libwsv5
```

### Option 2: Local User Installation (No sudo required)

Install to `~/.local` for personal use:

```bash
git clone https://github.com/linuxmainframe/libwsv5.git
cd libwsv5/scripts
./quick-install.sh --local
```

If you need to set environment variables:
```bash
export PKG_CONFIG_PATH=~/.local/lib/pkgconfig:$PKG_CONFIG_PATH
export LD_LIBRARY_PATH=~/.local/lib:$LD_LIBRARY_PATH
```

### Option 3: From Debian/Ubuntu Package

Download and install pre-built `.deb` packages:

```bash
# Download from GitHub releases
wget https://github.com/linuxmainframe/libwsv5/releases/download/v1.1.0/libwsv5_1.1.0_amd64.deb

# Install
sudo apt-get install ./libwsv5_1.1.0_amd64.deb

# Verify
pkg-config --modversion libwsv5
```

### Option 4: Manual Build from Source

For maximum control:

```bash
git clone https://github.com/linuxmainframe/libwsv5.git
cd libwsv5
mkdir build && cd build

# Configure
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local ..

# Build
make -j$(nproc)

# Install (with sudo for system-wide)
sudo make install

# Or install locally without sudo
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=~/.local ..
make -j$(nproc)
make install
```

## Verify Installation

### Check version
```bash
pkg-config --modversion libwsv5
```

### Get compiler flags
```bash
pkg-config --cflags libwsv5
# Output: -I/usr/local/include/libwsv5
```

### Get linker flags
```bash
pkg-config --libs libwsv5
# Output: -L/usr/local/lib -lwsv5
```

### Find installation path
```bash
pkg-config --variable=libdir libwsv5
pkg-config --variable=includedir libwsv5
```

## Using libwsv5 in Your Project

### Method 1: CMake (Recommended)

Create `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.10)
project(MyApp C)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBWSV5 REQUIRED libwsv5)

add_executable(myapp main.c)
target_link_libraries(myapp ${LIBWSV5_LIBRARIES})
target_include_directories(myapp PRIVATE ${LIBWSV5_INCLUDE_DIRS})
```

Build:
```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
./myapp
```

### Method 2: Manual Compilation with gcc/clang

```bash
# Compile with pkg-config
gcc -c main.c $(pkg-config --cflags libwsv5)
gcc main.o -o myapp $(pkg-config --libs libwsv5)

# Or explicit flags
gcc -c main.c -I/usr/local/include/libwsv5
gcc main.o -o myapp -L/usr/local/lib -lwsv5
```

### Method 3: Makefile

```makefile
CFLAGS := -Wall -Wextra $(shell pkg-config --cflags libwsv5)
LDFLAGS := $(shell pkg-config --libs libwsv5)

myapp: main.c
	gcc $(CFLAGS) -o myapp main.c $(LDFLAGS)

clean:
	rm -f myapp
```

Compile:
```bash
make
./myapp
```

## Basic Usage Example

```c
#include <libwsv5.h>
#include <stdio.h>

int main() {
    // Create OBS connection
    libwsv5_connection_t conn;
    libwsv5_error_t err;
    
    // Initialize connection
    err = libwsv5_connect(&conn, "localhost", 4455, "your_password");
    if (err != LIBWSV5_OK) {
        fprintf(stderr, "Connection failed: %s\n", libwsv5_error_string(err));
        return 1;
    }
    
    printf("Connected to OBS!\n");
    
    // Use the library...
    
    // Disconnect
    libwsv5_disconnect(&conn);
    
    return 0;
}
```

See [API_REFERENCE.md](API_REFERENCE.md) for complete API documentation.

## Troubleshooting

### "Cannot find libwsv5.h"

**Problem:** Compiler says header not found
```
fatal error: libwsv5.h: No such file or directory
```

**Solution:** Ensure library is installed:
```bash
pkg-config --modversion libwsv5
```

If that fails, run:
```bash
./scripts/quick-install.sh --system
```

Or install to custom location and set flags:
```bash
cmake -DCMAKE_INSTALL_PREFIX=~/.local ..
export PKG_CONFIG_PATH=~/.local/lib/pkgconfig:$PKG_CONFIG_PATH
```

### "Undefined reference to libwsv5_*"

**Problem:** Linker error about undefined symbols
```
undefined reference to 'libwsv5_connect'
```

**Solution:** Missing linker flags. Use `pkg-config`:
```bash
# Check which flags are needed
pkg-config --libs libwsv5

# When compiling, use:
gcc main.c $(pkg-config --libs libwsv5) -o myapp
```

### "pkg-config: command not found"

**Problem:** pkg-config not installed
```
pkg-config: command not found
```

**Solution:** Install pkg-config:
```bash
# Ubuntu/Debian
sudo apt-get install pkg-config

# Fedora/RHEL
sudo dnf install pkgconfig

# macOS
brew install pkg-config
```

### "error while loading shared libraries: libwsv5.so"

**Problem:** Runtime error - library not found
```
error while loading shared libraries: libwsv5.so: cannot open shared object file
```

**Solution:** Set library path:
```bash
# For system install
export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH

# For local install
export LD_LIBRARY_PATH=~/.local/lib:$LD_LIBRARY_PATH

# Run program
./myapp
```

Or use rpath (permanent solution in CMakeLists.txt):
```cmake
set_target_properties(myapp PROPERTIES
    BUILD_RPATH "${LIBWSV5_LIBRARY_DIRS}"
    INSTALL_RPATH "${LIBWSV5_LIBRARY_DIRS}"
)
```

### CMake can't find libwsv5

**Problem:** CMake error
```
CMake Error: Could not find package libwsv5
```

**Solution:** Set CMAKE_PREFIX_PATH:
```bash
cmake -DCMAKE_PREFIX_PATH=/usr/local ..
# Or for local install
cmake -DCMAKE_PREFIX_PATH=~/.local ..
```

## Platform-Specific Notes

### Ubuntu/Debian

Works best with:
- Ubuntu 20.04 LTS and later
- Debian 11 (Bullseye) and later
- Uses standard package locations

### Fedora/RHEL

- Fedora 34+
- RHEL 8+
- CentOS 8+
- Use `dnf` instead of `apt-get`

### macOS

- macOS 10.14 (Mojave) and later
- Uses Homebrew for dependencies
- May need to set `DYLD_LIBRARY_PATH` instead of `LD_LIBRARY_PATH`

### Windows

Not currently supported. Requires:
- libwebsockets Windows port
- Visual Studio C++ build tools
- Contributions welcome!

## Building Tests

Build and run the test suite:

```bash
# Build with tests enabled
mkdir build && cd build
cmake -DBUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release ..
make

# Run tests (requires OBS running on localhost:4455)
./test -h localhost -p 4455 -w your_obs_password
```

## Uninstallation

### System-wide installation
```bash
cd libwsv5/build
sudo make uninstall
```

### Local installation
```bash
rm -rf ~/.local/lib/libwsv5*
rm -rf ~/.local/include/libwsv5
rm ~/.local/lib/pkgconfig/libwsv5.pc
```

### Debian package
```bash
sudo apt-get remove libwsv5
```

## Next Steps

1. **Learn the API:** See [API_REFERENCE.md](API_REFERENCE.md)
2. **Examples:** Check `tests/test.c` for usage examples
3. **Architecture:** See [README.md](README.md) Architecture section
4. **Troubleshooting:** See [README.md](README.md) Troubleshooting section

## Getting Help

- **API questions:** See [API_REFERENCE.md](API_REFERENCE.md)
- **Installation issues:** Check Troubleshooting section above
- **Bug reports:** [GitHub Issues](https://github.com/linuxmainframe/libwsv5/issues)
- **Feature requests:** [GitHub Discussions](https://github.com/linuxmainframe/libwsv5/discussions)

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines on how to contribute.

## License

This project is licensed under the MIT License - see [LICENSE](LICENSE) file for details.