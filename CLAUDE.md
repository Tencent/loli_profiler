# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

LoliProfiler is a C/C++ memory profiling tool for Android games and applications built with Qt. It connects to Android devices via ADB to capture and analyze memory allocation patterns, stack traces, and system memory information.

The project builds two executables:
- **LoliProfiler** - Full GUI application with interactive profiling and visualization
- **LoliProfilerCLI** - Console application for automated profiling and CI/CD integration

Both executables share core profiling logic and configuration files, with the CLI version excluding GUI dependencies (Widgets, Charts, OpenGL) for a lighter footprint.

## Build Commands

### Prerequisites
Set these environment variables before building:
- `QT5Path` - Path to Qt 5.12/5.14/5.15 installation
- `MSBUILD_EXE` - Path to MSBuild (Windows only)
- `Ndk_R16_CMD` - Path to Android NDK r16b ndk-build
- `Ndk_R20_CMD` - Path to Android NDK r20/r25 ndk-build

### Build All
**Windows:**
```bash
build.bat
```

**macOS:**
```bash
sh build.sh
```

**Linux:**
```bash
./build_linux_with_docker.sh
```

### Build Process
The build system executes these steps:
1. `BuildProject.bat/.sh` - Builds both LoliProfiler and LoliProfilerCLI executables via CMake
2. `BuildAndroidLibs.bat/.sh` - Compiles Android native libraries (libhook) for multiple architectures
3. `CopyConfig.bat/.sh` - Copies configuration files to output directory
4. `Deployqt.bat/.sh` - Deploys Qt dependencies

### Build Outputs
- GUI: `./build/cmake/bin/release/LoliProfiler.exe` (Windows) or `LoliProfiler.app` (macOS)
- CLI: `./build/cmake/bin/release/LoliProfilerCLI.exe` (Windows) or `LoliProfilerCLI` (macOS/Linux)
- Final package: `./dist/`

### CLI Usage Examples
```bash
# Profile for 60 seconds with symbol translation
LoliProfilerCLI --app com.example.game --out profile.loli --symbol /path/to/lib.so --duration 60

# Profile until Ctrl+C (manual stop)
LoliProfilerCLI --app com.example.game --out profile.loli --verbose

# Compare two profiles to detect memory regressions
LoliProfilerCLI --compare baseline.loli current.loli --out diff.txt

# Compare with skipped root levels (for system libs without symbols)
LoliProfilerCLI --compare baseline.loli current.loli --out diff.txt --skip-root-levels 2
```

## Architecture

### Core Data Flow

**GUI Mode:**
1. Android agent hooks malloc/free and sends `RawStackInfo` records via TCP
2. MainWindow receives records in real-time, builds `StackTraceModel`
3. User interacts with visualizations (timeline, tree map, call tree, fragmentation)
4. Data can be saved to `.loli` files for later analysis

**CLI Mode:**
1. Android agent hooks malloc/free and sends `RawStackInfo` records via TCP
2. CliProfiler receives records and caches to disk (`cache/` directory)
3. After capture completes (duration timeout or Ctrl+C), processes cached records
4. Saves complete `.loli` file for GUI analysis

### Key Components

**Entry Points:**
- `src/main.cpp` - GUI mode entry point (Qt Widgets application)
- `src/main_cli.cpp` - CLI mode entry point (QCoreApplication, no GUI)

**Controllers:**
- `MainWindow` (`src/mainwindow.cpp`) - GUI controller, manages all UI and profiling operations
- `CliProfiler` (`src/cliprofiler.cpp`) - CLI controller, manages headless profiling workflow
- `ProfileComparator` (`src/profilecomparator.cpp`) - Comparison engine for detecting memory regressions between two `.loli` files

**Process Management (Base: `AdbProcess`):**
- `StartAppProcess` - Launches target application via ADB
- `StackTraceProcess` - TCP socket connection to Android profiling agent, receives allocation/deallocation records
- `MemInfoProcess` - Captures system memory info (`/proc/meminfo`) via ADB
- `ScreenshotProcess` - Takes device screenshots for correlation
- `AddressProcess` - Resolves memory addresses to function symbols

**Data Models:**
- `StackTraceModel` - Table model containing `StackRecord` entries (UUID, time, size, address, library)
- `StackTraceProxyModel` - Filtering/sorting proxy for stack traces
- `RawStackInfo` - Raw allocation/deallocation record format from Android agent
- `SMapsSection` - Memory mapping information from `/proc/pid/smaps`

**Android Native Libraries (`plugins/Android/`):**
- Built with NDK for multiple architectures (armeabi, armeabi-v7a, arm64-v8a)
- Hooks malloc/calloc/realloc/free using custom interception techniques
- Communicates with desktop client via TCP socket on port 44515
- Sends stack traces with allocation/deallocation metadata

### Conditional Compilation

The codebase uses `NO_GUI_MODE` preprocessor flag to enable CLI-only builds:
- When defined, excludes Qt Widgets/Charts/OpenGL dependencies
- Allows shared components like `ConfigDialog` to function as data containers without UI
- Process classes (`StartAppProcess`, `ScreenshotProcess`) work in both modes
- Build system automatically defines this flag when compiling LoliProfilerCLI

### Configuration Sharing

Both GUI and CLI modes share the same configuration file:
- Windows: `%LOCALAPPDATA%\MoreFun\LoliProfiler\loli3.conf`
- macOS/Linux: `~/.local/share/MoreFun/LoliProfiler/loli3.conf`

Both executables set organization name ("MoreFun") and application name ("LoliProfiler") to ensure config compatibility. This allows CLI to use settings configured in GUI (compiler type, architecture, whitelist, blacklist).

## Key Data Structures

**`StackRecord`:**
```cpp
struct StackRecord {
    QUuid uuid_;        // Call stack UUID
    quint32 seq_;       // Sequence number
    qint32 time_;       // Timestamp
    qint32 size_;       // Allocation size
    quint64 addr_;      // Memory address
    quint64 funcAddr_;  // Function address
    HashString library_; // Library name (optimized string storage)
};
```

**`RawStackInfo`** (from Android agent):
- Contains allocation/deallocation flag, timestamp, size, address, call stack frames

**Call Stack Maps:**
- `callStackMap_` - Maps UUIDs to call stack sequences (library + function address pairs)
- `symbloMap_` - Address-to-symbol resolution cache
- `freeAddrMap_` - Tracks deallocated addresses to filter out freed memory

## Symbol Resolution

The profiler supports loading symbol files (`.so` or `.sym`) to resolve memory addresses to function names:
- GUI: File dialog for manual symbol loading
- CLI: `--symbol <path>` flag for automatic loading
- Symbol files should match Android library structure
- Symbols are cached in `symbloMap_` for fast lookups

## CLI Compare Mode

The CLI supports comparing two `.loli` files to detect memory regressions:

**Key Features:**
- Calculates allocation count and size deltas
- Generates hierarchical call stack diff (deep copy format)
- Outputs as text report or `.loli` file (viewable in GUI)
- Supports skipping root call stack frames (`--skip-root-levels`) for system libs without symbols

**Comparison Algorithm:**
1. Load both profiles and build allocation maps
2. Group allocations by `(library_name, function_address)` key
3. Calculate statistics (new, removed, changed allocations)
4. Build call tree with size/count deltas
5. Export as text or `.loli` format

**Use Cases:**
- CI/CD regression detection
- Before/after performance optimization validation
- A/B testing different implementations
- Memory leak detection across builds

## Development Notes

### Adding New Features to Shared Components

When adding features to components used by both GUI and CLI:
1. Guard GUI-specific code with `#ifndef NO_GUI_MODE`
2. Ensure core functionality works without Qt Widgets dependencies
3. Test both executables after changes
4. Update `CMakeLists.txt` if adding new files

### Android Agent Protocol

The Android agent communicates via TCP socket with binary protocol:
- Port: 44515 (forwarded via ADB)
- Commands: `START_CAPTURE`, `STOP_CAPTURE`, `SMAPS_DUMP`
- Data format: LZ4-compressed `RawStackInfo` records

### Multi-threading

- GUI operations run on main Qt thread
- ADB processes run asynchronously (QProcess)
- TCP socket communication is event-driven
- Data processing may use `Qt::Concurrent` for heavy operations
- CLI mode uses `QCoreApplication` (no GUI event loop)

### Signal Handling (CLI Mode)

CLI implements graceful shutdown via SIGINT/SIGTERM handlers:
- Signal handler is async-signal-safe (uses `write()` instead of stdio)
- Invokes `CliProfiler::RequestStop()` via `Qt::QueuedConnection` (thread-safe)
- Ensures proper SMAPS dump and file save before exit
- Allows profiling across app restarts (doesn't auto-exit when app exits)

### File Format

`.loli` files use binary format with:
- Magic number: `0xA4B3C2D1`
- Version: `106`
- Contains: stack records, call stacks, symbols, memory info series, screenshots, SMAPS sections

## Common Pitfalls

1. **Path spaces on Windows**: Always quote paths with spaces when passing to ADB commands
2. **JDWP injection**: Requires debuggable apps or rooted devices
3. **Symbol file structure**: Must match Android library directory layout
4. **Memory optimization**: Use streaming mode for large datasets (CLI enables by default)
5. **Comparison version mismatch**: Both `.loli` files must have same version/magic number
