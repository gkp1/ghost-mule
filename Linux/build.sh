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
    echo "✓ Library build successful"
else
    echo "✗ Library build failed!"
    exit 1
fi
echo ""

# Build CLI (library must stay for linking)
echo "=== Building CLI ==="
cd "$SCRIPT_DIR/cli"
make clean 2>/dev/null || true
make

if [ -f "ProxyBridge" ]; then
    echo "✓ CLI build successful"
else
    echo "✗ CLI build failed!"
    exit 1
fi
echo ""

# Move binaries to output
echo "Moving binaries to output directory..."
mv "$SCRIPT_DIR/src/libproxybridge.so" "$OUTPUT_DIR/"
mv ProxyBridge "$OUTPUT_DIR/"
echo "✓ Binaries moved to output"
echo ""

# Cleanup build files
echo "Cleaning up build artifacts..."
cd "$SCRIPT_DIR/src"
rm -f *.o
make clean 2>/dev/null || true
cd "$SCRIPT_DIR/cli"
rm -f *.o
make clean 2>/dev/null || true
echo "✓ Cleanup complete"
echo ""

# Show results
echo "==================================="
echo "Build Complete!"
echo "==================================="
cd "$OUTPUT_DIR"
ls -lh
echo ""
echo "Output location: $OUTPUT_DIR"

