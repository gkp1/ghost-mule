#!/bin/bash

set -e  # Exit on error

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUTPUT_DIR="$SCRIPT_DIR/output"

# Remove and recreate output directory
if [ -d "$OUTPUT_DIR" ]; then
    echo "Removing existing output directory..."
    rm -rf "$OUTPUT_DIR"
fi

echo "Creating output directory..."
mkdir -p "$OUTPUT_DIR"
echo ""

# Build library
echo "=== Building Library ==="
cd "$SCRIPT_DIR/src"
make clean 2>/dev/null || true
make

if [ -f "libproxybridge.so" ]; then
    echo "Library build successful"
else
    echo "Library build failed!"
    exit 1
fi
echo ""

# Build CLI (library must stay for linking)
echo "=== Building CLI ==="
cd "$SCRIPT_DIR/cli"
make clean 2>/dev/null || true
make

if [ -f "ProxyBridge" ]; then
    echo "CLI build successful"
else
    echo "CLI build failed!"
    exit 1
fi
echo ""

# Move binaries to output
echo "=== Building GUI ==="
cd "$SCRIPT_DIR"
rm -f ProxyBridgeGUI
if pkg-config --exists gtk+-3.0; then
    GUI_CFLAGS="-Wall -Wno-unused-parameter -O3 -Isrc -D_GNU_SOURCE -fstack-protector-strong -D_FORTIFY_SOURCE=2 -fPIE -Wformat -Wformat-security -Werror=format-security -fno-strict-overflow -fno-delete-null-pointer-checks -fwrapv $(pkg-config --cflags gtk+-3.0)"
    GUI_LDFLAGS="-Lsrc -pie -Wl,-z,relro,-z,now -Wl,-z,noexecstack -s -Wl,-rpath,'$ORIGIN/.' -lproxybridge -lpthread $(pkg-config --libs gtk+-3.0) -export-dynamic"

    gcc $GUI_CFLAGS -c gui/main.c -o gui_main.o
    gcc -o ProxyBridgeGUI gui_main.o $GUI_LDFLAGS
    
    rm -f gui_main.o
    echo "GUI build successful"
else
    echo "GTK3 not found. Skipping GUI build."
    echo "  Install with: sudo apt install libgtk-3-dev  (Debian/Ubuntu/Mint)"
    echo "                sudo dnf install gtk3-devel    (Fedora)"
fi

echo ""
echo "Moving binaries to output directory..."
mv "$SCRIPT_DIR/src/libproxybridge.so" "$OUTPUT_DIR/"
mv "$SCRIPT_DIR/cli/ProxyBridge" "$OUTPUT_DIR/"
if [ -f ProxyBridgeGUI ]; then
    mv ProxyBridgeGUI "$OUTPUT_DIR/"
fi
echo "Binaries moved to output"
echo ""

# Cleanup build files
echo "Cleaning up build artifacts..."
cd "$SCRIPT_DIR/src"
rm -f *.o
make clean 2>/dev/null || true
cd "$SCRIPT_DIR/cli"
rm -f *.o
make clean 2>/dev/null || true
echo "Cleanup complete"
echo ""

# Show results
echo "==================================="
echo "Build Complete!"
echo "==================================="
cd "$OUTPUT_DIR"
ls -lh
echo ""
echo "Output location: $OUTPUT_DIR"

