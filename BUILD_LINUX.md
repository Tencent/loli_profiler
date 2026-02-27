# LoliProfiler Linux Build Guide

## Overview
This branch contains fixes for building LoliProfiler CLI on Linux platforms. The build system has been enhanced to support CLI-only builds without requiring GUI dependencies.

## Prerequisites

### Required Packages
For CentOS/RHEL:
```bash
yum install -y qt5-qtbase-devel cmake gcc-c++ make zip
```

For Ubuntu/Debian:
```bash
apt-get install -y qtbase5-dev qt5-qmake cmake g++ make zip
```

## Building

### One-Click Build
Simply run the build script:
```bash
./build_linux.sh
```

This script will:
1. Configure CMake with CLI-only build option (`-DBUILD_GUI=OFF`)
2. Build the LoliProfilerCLI binary
3. Deploy Qt dependencies automatically
4. Copy Python analysis scripts
5. Create a distribution package: `dist/LoliProfiler-linux-cli.zip`

### Manual Build
If you prefer to build manually:

```bash
# Create build directory
mkdir -p build/cmake
cd build/cmake

# Configure (CLI only)
cmake ../.. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH=/path/to/qt5 \
    -DBUILD_GUI=OFF

# Build
make -j$(nproc) LoliProfilerCLI

# Install
make install
```

## Running

After building, the distribution package will be in `dist/` directory. Extract and run:

```bash
cd dist/LoliProfiler
./LoliProfilerCLI.sh --help
```

The launcher script (`LoliProfilerCLI.sh`) automatically sets the correct library paths.

## Changes Made

### CMakeLists.txt
- Added `BUILD_GUI` option (default: ON for backward compatibility)
- GUI application build is now conditional based on `BUILD_GUI` flag
- Linux builds can use `-DBUILD_GUI=OFF` to build CLI only

### build_linux.sh
- Complete rewrite based on `build.bat` (Windows build script)
- Auto-detects Qt5 installation
- Builds CLI-only by default
- Calls deployment script to copy dependencies
- Creates final ZIP distribution package

### scripts/Deployqt_linux.sh
- New script equivalent to Windows `windeployqt`
- Automatically detects and copies Qt dependencies
- Creates launcher script with proper `LD_LIBRARY_PATH`
- Copies Qt plugins (platforms, xcbglintegrations)

### Code Fixes
Fixed Qt 5.15+ compatibility issues:
- Replaced deprecated `QString::SkipEmptyParts` with `Qt::SkipEmptyParts`
- Replaced deprecated `endl` with `Qt::endl`
- Added missing `#include <QPointF>` in profilecomparator.cpp
- Fixed unused parameter warnings in NO_GUI_MODE

## Distribution Package Contents

```
LoliProfiler/
├── LoliProfilerCLI          # Main CLI binary
├── LoliProfilerCLI.sh       # Launcher script (use this to run)
├── qt.conf                  # Qt configuration
├── lib/                     # Qt and other dependencies
├── plugins/                 # Qt plugins
├── analyze_memory_diff.py   # Python analysis script
├── preprocess_memory_diff.py
└── markdown_to_html.py
```

## Platform Compatibility

The changes are designed to:
- ✅ Not affect Windows builds (BUILD_GUI defaults to ON)
- ✅ Not affect macOS builds (BUILD_GUI defaults to ON)
- ✅ Enable Linux CLI-only builds with minimal CMake changes
- ✅ Use platform-specific `#ifdef` blocks where needed

## Usage Examples

### Profile an Android app
```bash
./LoliProfilerCLI.sh --app com.example.app --out profile.loli --duration 60
```

### Compare two profiles
```bash
./LoliProfilerCLI.sh --compare baseline.loli comparison.loli
```

### Attach to running app
```bash
./LoliProfilerCLI.sh --app com.example.app --attach --out profile.loli
```

## Troubleshooting

### Qt not found
If CMake cannot find Qt5, set the `QT5Path` environment variable:
```bash
export QT5Path=/usr/lib64/qt5  # or your Qt5 installation path
./build_linux.sh
```

### Missing libraries at runtime
Use the provided launcher script `LoliProfilerCLI.sh` instead of running the binary directly.

## Notes

- GUI modules are NOT built on Linux by default (saves compilation time and dependencies)
- All platform-specific code is wrapped in `#ifdef` blocks to ensure cross-platform compatibility
- The deployment script mimics Windows `windeployqt` behavior for consistency
