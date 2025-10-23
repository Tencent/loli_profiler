# CODEBUDDY.md This file provides guidance to CodeBuddy Code when working with code in this repository.

## Project Overview

LoliProfiler is a C/C++ memory profiling tool for Android games and applications. It's a Qt-based desktop application that connects to Android devices via ADB to capture and analyze memory allocation patterns, stack traces, and system memory information.

## Build System & Commands

### Prerequisites
- Qt 5.12/5.14/5.15 with QtCharts plugin
- CMake
- Android NDK r16b or r20
- Visual Studio 2017 (Windows) / XCode (macOS) / GCC (Linux)

### Build Commands

**Windows:**
```bash
# Set environment variables in build.bat or manually:
set QT5Path="G:/SDK/QT/5.14.1/msvc2017_64"
set MSBUILD_EXE="%ProgramFiles(x86)%\Microsoft Visual Studio\2017\Community\MSBuild\15.0\Bin\MSBuild.exe"
set Ndk_R16_CMD="G:/SDK/AndroidNDKForUnity/android-ndk-r16b/ndk-build.cmd"
set Ndk_R20_CMD="G:/SDK/AndroidNDKForUnity/android-ndk-r20/ndk-build.cmd"
build.bat
```

**macOS:**
```bash
export QT5Path=/Users/yourname/Qt5.14.1
export Ndk_R16_CMD=/android-ndk-r16b/ndk-build
export Ndk_R20_CMD=/android-ndk-r20b/ndk-build
sh build.sh
```

**Linux:**
```bash
# Using Docker (recommended)
./build_linux_with_docker.sh

# Or native build
export QT5Path=/Users/yourname/Qt5.14.1
export Ndk_R16_CMD=/android-ndk-r16b/ndk-build
export Ndk_R20_CMD=/android-ndk-r20b/ndk-build
bash build_linux.sh
```

### Build Process
The build system uses a multi-step process:
1. `BuildProject.bat/.sh` - Builds the Qt desktop application using CMake
2. `BuildAndroidLibs.bat/.sh` - Compiles Android native libraries using NDK
3. `CopyConfig.bat/.sh` - Copies configuration files
4. `Deployqt.bat/.sh` - Deploys Qt dependencies

### Output Locations
- Desktop application: `./build/cmake/bin/release/LoliProfiler.exe` (Windows) or `LoliProfiler.app` (macOS)
- Deployed package: `./dist/`

## Architecture Overview

### Core Components

**MainWindow (`src/mainwindow.cpp`, `include/mainwindow.h`)**
- Central UI controller managing all profiling operations
- Coordinates between different process types and data visualization
- Handles Qt Charts for memory timeline visualization
- Manages stack trace models and filtering

**Process Management**
- `AdbProcess` - Base class for all ADB-based operations
- `StackTraceProcess` - TCP socket connection to Android profiling agent
- `MemInfoProcess` - Captures system memory information via ADB
- `ScreenshotProcess` - Takes device screenshots for correlation
- `StartAppProcess` - Launches target applications
- `AddressProcess` - Resolves memory addresses to symbols

**Data Models**
- `StackTraceModel` - Core data model for stack trace information
- `StackTraceProxyModel` - Filtering and sorting proxy for stack traces
- `RawStackInfo` struct - Raw memory allocation/deallocation records
- `SMapsSection` - Memory mapping information from /proc/pid/smaps

**Visualization Components**
- `InteractiveChartView` - Custom Qt Charts view with zoom/pan
- `TreeMapGraphicsView` - Tree map visualization for memory usage
- `MemGraphicsView` - Memory fragmentation visualization
- Custom graphics views for different data presentation modes

### Android Integration

**Native Libraries (`plugins/Android/`)**
- Built with Android NDK for both GCC and LLVM toolchains
- Hooks into malloc/free functions to capture allocation data
- Communicates with desktop client via TCP socket
- Supports multiple Android architectures (armeabi, armeabi-v7a, arm64-v8a)

**Connection Flow**
1. ADB port forwarding setup
2. TCP socket connection to Android agent
3. Real-time streaming of allocation/deallocation events
4. Symbol resolution for stack traces

### Data Processing Pipeline

1. **Capture**: Android agent hooks memory operations and sends raw data
2. **Transport**: TCP socket streams `RawStackInfo` records to desktop
3. **Processing**: MainWindow interprets records and builds stack trace models
4. **Filtering**: Time-based and criteria-based filtering of data
5. **Visualization**: Multiple view modes (timeline, tree map, call tree, fragmentation)

### Key Data Structures

- `loliFlags` enum - Memory operation types (malloc, free, calloc, etc.)
- `HashString` - Optimized string storage for library names
- `callStackMap_` - Maps UUIDs to stack trace sequences
- `symbloMap_` - Address-to-symbol resolution cache
- `recordsCache_` - Buffered stack trace records

## Development Notes

### Symbol Resolution
The profiler supports loading symbol files to resolve memory addresses to function names. Symbol files should be placed in a directory structure matching the Android library layout.

### Memory Leak Detection
The tool can detect C++ memory leaks by tracking unmatched malloc/free pairs. This feature has been tested specifically with Unreal Engine 4.26.

### SMaps Integration
The profiler can parse Android /proc/pid/smaps files to show detailed memory mapping information alongside allocation data.

### Multi-threading Considerations
- UI operations run on main Qt thread
- ADB processes run asynchronously
- TCP socket communication is event-driven
- Data processing may use Qt::Concurrent for heavy operations