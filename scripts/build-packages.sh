#!/bin/bash
# Build script for creating libwsv5 packages
# Creates .deb, source archives, and prepares for GitHub release

set -e

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${PROJECT_DIR}/build-release"
ARTIFACTS_DIR="${PROJECT_DIR}/release-artifacts"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${YELLOW}libwsv5 Package Builder${NC}\n"

# Clean up old build
if [ -d "$BUILD_DIR" ]; then
    echo "Cleaning old build directory..."
    rm -rf "$BUILD_DIR"
fi

mkdir -p "$BUILD_DIR" "$ARTIFACTS_DIR"

# Configure build
echo -e "${GREEN}Configuring build...${NC}"
cd "$BUILD_DIR"
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON ..

# Build
echo -e "${GREEN}Building libwsv5...${NC}"
make -j$(nproc)

# Run tests if OBS is available
if command -v obs &> /dev/null; then
    echo -e "${GREEN}Running tests...${NC}"
    ./test -h localhost -p 4455 || echo -e "${YELLOW}Tests require OBS running on localhost:4455${NC}"
else
    echo -e "${YELLOW}OBS not found, skipping tests${NC}"
fi

# Create packages
echo -e "${GREEN}Creating .deb package...${NC}"
cpack -G DEB

echo -e "${GREEN}Creating source archives...${NC}"
cpack --config CPackSourceConfig.cmake -G TGZ
cpack --config CPackSourceConfig.cmake -G ZIP

# Move artifacts
echo -e "${GREEN}Collecting artifacts...${NC}"
cp libwsv5_*.deb "$ARTIFACTS_DIR/" 2>/dev/null || true
cp libwsv5_*.tar.gz "$ARTIFACTS_DIR/" 2>/dev/null || true
cp libwsv5_*.zip "$ARTIFACTS_DIR/" 2>/dev/null || true

# Create checksums
echo -e "${GREEN}Creating checksums...${NC}"
cd "$ARTIFACTS_DIR"
sha256sum * > SHA256SUMS
cat SHA256SUMS

# Summary
echo -e "\n${GREEN}Build complete!${NC}"
echo -e "${YELLOW}Artifacts location:${NC} $ARTIFACTS_DIR"
echo ""
echo "To install locally:"
echo "  sudo dpkg -i $ARTIFACTS_DIR/libwsv5_*.deb"
echo ""
echo "To create GitHub release:"
echo "  git tag -a v1.1.0 -m 'Release 1.1.0'"
echo "  git push origin v1.1.0"
echo "  gh release create v1.1.0 $ARTIFACTS_DIR/*"