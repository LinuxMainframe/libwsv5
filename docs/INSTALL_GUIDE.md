# libwsv5 Installation Guide

Complete guide for installing libwsv5 to use `#include <libwsv5.h>` syntax in your code.

## Quick Start

### Option 1: System Installation (Recommended)

```bash
# Clone repository
git clone https://github.com/linuxmainframe/libwsv5.git
cd libwsv5

# Install dependencies
sudo apt-get install build-essential cmake libwebsockets-dev libcjson-dev libssl-dev

# Build and install to system
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local ..
make -j$(nproc)
sudo make install

# Verify installation
pkg-config --modversion libwsv5
```

After installation, you can use `#include <libwsv5.h>` directly in your code.

### Option 2: Using Pre-built .deb Package

```bash
# Download from GitHub releases
wget https://github.com/linuxmainframe/libwsv5/releases/download/v1.1.0/libwsv5_1.1.0_amd64.deb

# Install with dependency resolution
sudo apt-get install ./libwsv5_1.1.0_amd64.deb
```

## Installation Methods

### 1. System-Wide Installation (Linux/macOS)

#### Prerequisites

**Ubuntu/Debian:**
```bash
sudo apt-get update
sudo apt-get install build-essential cmake
sudo apt-get install libwebsockets-dev libcjson-dev libssl-dev
```

**Fedora/RHEL:**
```bash
sudo dnf groupinstall "Development Tools"
sudo dnf install cmake libwebsockets-devel cjson-devel openssl-devel
```

**macOS:**
```bash
brew install cmake libwebsockets cjson openssl
```

#### Installation Steps

```bash
# Clone the repository
git clone https://github.com/linuxmainframe/libwsv5.git
cd libwsv5

# Create build directory
mkdir build && cd build

# Configure (choose prefix for installation location)
# For system-wide: /usr or /usr/local
# For user-only: ~/usr or ~/.local
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local ..

# Build the library
make -j$(nproc)

# Install to system
sudo make install

# Verify installation
pkg-config --modversion libwsv5
```

#### Installation Prefix Options

**System-wide (/usr)** - Requires sudo, available to all users:
```bash
cmake -DCMAKE_INSTALL_PREFIX=/usr ..
```

**Local system (/usr/local)** - Requires sudo, allows easy uninstall:
```bash
cmake -DCMAKE_INSTALL_PREFIX=/usr/local ..
```

**User directory (~/usr)** - No sudo needed, personal installation:
```bash
cmake -DCMAKE_INSTALL_PREFIX=~/usr ..
export PKG_CONFIG_PATH=~/usr/lib/pkgconfig:$PKG_CONFIG_PATH
```

**Conda/Virtual environment** - Isolated installation:
```bash
cmake -DCMAKE_INSTALL_PREFIX=$CONDA_PREFIX ..
```

### 2. Debian Package Installation

#### Building .deb Package

```bash
# In the build directory
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr ..
make -j$(nproc)

# Create .deb package
cpack -G DEB

# Install the .deb
sudo dpkg -i libwsv5_1.1.0_amd64.deb

# If dependencies are missing, use apt-get to fix
sudo apt-get install -f
```

#### Installing from .deb

```bash
# If you have a .deb file
sudo apt-get install ./libwsv5_1.1.0_amd64.deb

# Or with dpkg
sudo dpkg -i libwsv5_1.1.0_amd64.deb
sudo apt-get install -f  # Install any missing dependencies
```

### 3. Build from Source (Local)

If you don't want system-wide installation:

```bash
# Clone and build
git clone https://github.com/linuxmainframe/libwsv5.git
cd libwsv5

mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)

# Library is now in: build/libwsv5.a (static) or build/libwsv5.so (shared)
# Header is in: libwsv5.h (parent directory)
```

Then link it manually in your project by setting compiler and linker flags appropriately (see "Using the Library" section).

## Using the Library

### Method 1: System-Installed (Recommended)

After system installation, use directly:

```c
#include <libwsv5.h>
#include <stdio.h>

int main() {
    obsws_init();
    printf("Connected to libwsv5!\n");
    obsws_cleanup();
    return 0;
}
```

Compile with:

```bash
# Using pkg-config (recommended)
gcc myapp.c -o myapp $(pkg-config --cflags --libs libwsv5)

# Or explicitly
gcc myapp.c -o myapp -I/usr/local/include/libwsv5 -L/usr/local/lib -lwsv5 -lm
```

### Method 2: CMake Project with System-Installed Library

**CMakeLists.txt:**
```cmake
cmake_minimum_required(VERSION 3.10)
project(MyApp C)

# Find the installed library
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBWSV5 REQUIRED libwsv5)

# Create executable
add_executable(myapp main.c)

# Link the library
target_link_libraries(myapp ${LIBWSV5_LIBRARIES})
target_include_directories(myapp PRIVATE ${LIBWSV5_INCLUDE_DIRS})
target_compile_options(myapp PRIVATE ${LIBWSV5_CFLAGS_OTHER})
```

### Method 3: CMake with Local Build

**CMakeLists.txt:**
```cmake
cmake_minimum_required(VERSION 3.10)
project(MyApp C)

# Use ExternalProject to build libwsv5
include(ExternalProject)
ExternalProject_Add(libwsv5_build
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libwsv5
    CMAKE_ARGS -DCMAKE_BUILD_TYPE=Release
    BUILD_COMMAND make
    INSTALL_COMMAND ""
)

# Add executable
add_executable(myapp main.c)

# Link
target_link_libraries(myapp ${CMAKE_CURRENT_SOURCE_DIR}/libwsv5/build/libwsv5.a)
target_include_directories(myapp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/libwsv5)
add_dependencies(myapp libwsv5_build)
```

### Method 4: Makefile Project

**Makefile:**
```makefile
CFLAGS := $(shell pkg-config --cflags libwsv5)
LIBS := $(shell pkg-config --libs libwsv5)

myapp: main.o
	gcc main.o $(LIBS) -o myapp

main.o: main.c
	gcc -c main.c $(CFLAGS)

clean:
	rm -f *.o myapp
```

## Verification

### Check Installation

```bash
# Find header file
find /usr -name "libwsv5.h" 2>/dev/null

# List installed files
dpkg -L libwsv5  # If installed via .deb

# Check pkg-config
pkg-config --list-all | grep libwsv5
```

### Test Compilation

```bash
# Create test program
cat > test_compile.c << 'EOF'
#include <libwsv5.h>
#include <stdio.h>

int main() {
    printf("libwsv5 header found!\n");
    return 0;
}
EOF

# Try to compile
gcc -c test_compile.c -I/usr/local/include

# If successful, clean up
rm -f test_compile.c test_compile.o
```

### Verify Dependencies

```bash
# Check all dependencies are met
ldd /usr/local/lib/libwsv5.so

# Or check .deb package
apt-cache depends libwsv5
```

## Uninstalling

### From System Installation

```bash
# If installed to /usr/local
sudo rm -rf /usr/local/include/libwsv5
sudo rm -f /usr/local/lib/libwsv5.*
sudo rm -f /usr/local/lib/pkgconfig/libwsv5.pc
```

### From .deb Package

```bash
# List installed package
dpkg -l | grep libwsv5

# Remove package
sudo apt-get remove libwsv5

# Or with dpkg
sudo dpkg -r libwsv5
```

## Troubleshooting

### Issue: CMake can't find dependencies

**Solution:**
```bash
# Install development packages
sudo apt-get install libwebsockets-dev libcjson-dev libssl-dev

# On Fedora
sudo dnf install libwebsockets-devel cjson-devel openssl-devel
```

### Issue: pkg-config doesn't find libwsv5

**Solution:**
```bash
# Check if pkg-config file exists
ls -la /usr/local/lib/pkgconfig/libwsv5.pc

# Add to PKG_CONFIG_PATH if in non-standard location
export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH
echo "export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:\$PKG_CONFIG_PATH" >> ~/.bashrc

# Verify
pkg-config --modversion libwsv5
```

### Issue: "Cannot find -lwsv5" during linking

**Solution:**
```bash
# Check library exists
ls -la /usr/local/lib/libwsv5.*

# Make sure library directory is in linker path
export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH

# Or add to ldconfig
echo "/usr/local/lib" | sudo tee /etc/ld.so.conf.d/libwsv5.conf
sudo ldconfig
```

### Issue: Header file not found

**Solution:**
```bash
# Verify header installation
ls -la /usr/local/include/libwsv5/libwsv5.h

# If not found, reinstall
cd libwsv5/build
cmake -DCMAKE_INSTALL_PREFIX=/usr/local ..
sudo make install
```

### Issue: .deb installation fails with dependency errors

**Solution:**
```bash
# Install dependencies first
sudo apt-get install libwebsockets1 libcjson1 libssl3

# Then install .deb
sudo dpkg -i libwsv5_1.1.0_amd64.deb

# Or let apt handle dependencies
sudo apt-get install ./libwsv5_1.1.0_amd64.deb
```

## Platform-Specific Notes

### Ubuntu/Debian

- Use `/usr/local` for installation prefix to avoid conflicts with system packages
- pkg-config should find the library automatically if in standard paths
- Consider creating a dedicated .deb for system package management

### Fedora/RHEL

- Install `development-tools` group for build essentials
- libwebsockets may be `libwebsockets.so` instead of `libwebsockets.so.1`
- Use `/usr` as prefix if managing via package manager

### macOS

- Use Homebrew for dependency management
- May need to add `/usr/local/opt/libwsv5/lib` to `DYLD_LIBRARY_PATH`
- Xcode Command Line Tools required for compilation

## Next Steps

After installation, you can:

1. **Review API Documentation**: Check `API_REFERENCE.md`
2. **Run Tests**: `./test -h localhost -p 4455 -w obs_password`
3. **Build Examples**: `cmake -DBUILD_EXAMPLES=ON && make`
4. **Read Examples**: Check `example.c` and `tests/test.c`

## Getting Help

- Check [API Reference](API_REFERENCE.md)
- Review example code in `tests/test.c`
- Generate Doxygen docs: `make doc` (requires doxygen)
- Open an issue on GitHub for problems