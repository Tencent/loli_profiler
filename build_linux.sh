#!/bin/bash
set -e

echo "======================================"
echo "LoliProfiler Linux Build Script (CLI Only)"
echo "======================================"

# Check if QT5Path is set
if [ -z "$QT5Path" ]; then
    echo "QT5Path environment variable is not set, trying to auto-detect..."
    # Try to find Qt5 installation
    if [ -d "/usr/lib64/qt5" ]; then
        export QT5Path="/usr/lib64/qt5"
    elif [ -d "/usr/lib/x86_64-linux-gnu/qt5" ]; then
        export QT5Path="/usr/lib/x86_64-linux-gnu/qt5"
    else
        # Try to use qmake to find Qt5
        if command -v qmake-qt5 &> /dev/null; then
            export QT5Path=$(qmake-qt5 -query QT_INSTALL_PREFIX)
        elif command -v qmake &> /dev/null; then
            export QT5Path=$(qmake -query QT_INSTALL_PREFIX)
        else
            echo "ERROR: Cannot find Qt5 installation. Please set QT5Path environment variable."
            exit 1
        fi
    fi
fi

echo "QT5Path: $QT5Path"

# Set build paths
export ReleasePath="./build/cmake/bin/release"
export DeployPath="./dist"

# Clean previous build
echo "Cleaning previous build..."
rm -rf ./build
rm -rf ./dist

# Create build directory
mkdir -p build/cmake

# Configure with CMake (CLI only, no GUI)
echo "Configuring CMake (CLI only)..."
cd build/cmake
cmake ../.. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH=$QT5Path \
    -DBUILD_GUI=OFF

# Build
echo "Building LoliProfilerCLI..."
make -j$(nproc) LoliProfilerCLI

# Install to release directory
echo "Installing to release directory..."
make install

cd ../..

# Create deployment directory
echo "Creating deployment package..."
mkdir -p $DeployPath/LoliProfiler

# Copy CLI binary
echo "Copying LoliProfilerCLI binary..."
cp -v $ReleasePath/LoliProfilerCLI $DeployPath/LoliProfiler/

# Deploy Qt dependencies
echo "Deploying Qt dependencies..."
bash scripts/Deployqt_linux.sh

# Copy Python analysis scripts
echo "Copying Python analysis scripts..."
cp -v analyze_memory_diff.py $DeployPath/LoliProfiler/
cp -v preprocess_memory_diff.py $DeployPath/LoliProfiler/
cp -v markdown_to_html.py $DeployPath/LoliProfiler/

# Copy config files if they exist
if [ -d "res" ]; then
    echo "Copying resource files..."
    mkdir -p $DeployPath/LoliProfiler/res
    cp -rv res/* $DeployPath/LoliProfiler/res/ 2>/dev/null || true
fi

# Create archive
echo "Creating distribution archive..."
cd $DeployPath
zip -r LoliProfiler-linux-cli.zip LoliProfiler
cd ..

echo "======================================"
echo "Build completed successfully!"
echo "Distribution package: $DeployPath/LoliProfiler-linux-cli.zip"
echo "======================================"
