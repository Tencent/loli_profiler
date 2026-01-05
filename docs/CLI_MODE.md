# LoliProfiler CLI Mode

## Overview

LoliProfiler now includes a dedicated CLI executable (`LoliProfilerCLI.exe`) designed for CI/CD integration and automated testing workflows. This console application allows you to profile Android applications without the GUI, automatically capture memory data, and save results to `.loli` files for later analysis.

## Executables

- **LoliProfiler.exe** - GUI application
- **LoliProfilerCLI.exe** - Dedicated console application (CLI only, smaller size, better console integration)

## Features

- **Headless Operation**: No GUI required, perfect for CI/CD pipelines
- **Automatic Launch & Injection**: Automatically starts and injects profiling hooks
- **Flexible Capture Modes**: 
  - Timed profiling (fixed duration)
  - Process-exit detection (profile until app terminates)
- **Symbol Translation**: Automatic address-to-symbol translation with symbol files
- **Data Optimization**: Streaming mode enabled by default for large datasets
- **Device Selection**: Support for multiple connected Android devices

## Command-Line Options

All options use Qt's standard format: `--option value` (double dash with space-separated value).

### Required Options

- `--app <package_name>` - Target application package name (e.g., `com.example.game`)
- `--out <output_path>` - Output `.loli` file path

### Optional Options

- `--symbol <symbol_path>` - Path to symbol file (`.so` or `.sym`) for address translation
- `--subprocess <name>` - Target subprocess name (if app uses multiple processes)
- `--device <serial>` - Device serial number (required when multiple devices connected)
- `--duration <seconds>` - Profiling duration in seconds (default: 0 = until process exits)
- `--attach` - Attach to running app instead of launching new instance
- `--verbose` or `-v` - Enable verbose output for debugging
- `--help` or `-h` - Display help message

> All other options will use what you set in gui mode.

## Usage Examples

### Basic Usage

Profile an app for 60 seconds:

```bash
# Using dedicated CLI executable (recommended)
LoliProfilerCLI.exe --app com.example.game --out profile.loli --duration 60
```

### With Symbol Translation

Profile with automatic symbol resolution:

```bash
LoliProfilerCLI.exe --app com.example.game --out profile.loli \
  --symbol /path/to/libgame.so --duration 120
```

### Until Process Exits

Profile until the app terminates naturally:

```bash
LoliProfilerCLI.exe --app com.example.game --out profile.loli \
  --duration 0 --verbose
```

### Attach to Running App

Attach to an already-running application:

```bash
LoliProfilerCLI.exe --app com.example.game --out profile.loli \
  --attach --duration 30
```

### Multiple Devices

When multiple Android devices are connected:

```bash
LoliProfilerCLI.exe --app com.example.game --out profile.loli \
  --device emulator-5554 --duration 60
```

### Subprocess Profiling

Profile a specific subprocess (e.g., for Unity games):

```bash
LoliProfilerCLI.exe com.unity.game --subprocess UnityMain \
  --out profile.loli --duration 120
```

## Output Format

The CLI mode produces standard `.loli` files identical to those created by the GUI. These files contain:

- Memory allocation/deallocation records
- Stack traces with symbol information (if provided)
- Memory info timeline (Total, NativeHeap, GfxDev, etc.)
- Screenshots captured during profiling
- SMaps (memory mapping) information

## Verbose Mode

Use `-v` flag to see detailed progress information:

```bash
loliprofiler.exe -cli -app=com.example.game -out=profile.loli -v
```

This will show:
- Application launch status
- Connection attempts
- Data capture progress
- Symbol translation details
- Save operations

## Differences from GUI Mode

| Feature | GUI Mode | CLI Mode |
|---------|----------|----------|
| User Interface | Full GUI | Console only |
| Device Selection | Interactive dialog | `-device` flag |
| Duration | Manual stop | Timed or process-exit |
| Progress | Visual progress bar | Console messages |
| Symbol Loading | File dialog | `-symbol` flag |
| Data Optimization | User prompt | Always enabled |
| Launch Mode | User prompt | `-attach` flag |

## Limitations

- No real-time visualization (analyze with GUI later)
- Cannot interactively select time ranges (use full capture)
- No manual screenshot triggering (automatic every 5 seconds)
- Requires configured Android SDK/NDK paths
- Windows: Requires properly quoted paths for spaces

## See Also

- [Quick Start Guide](QUICK_START.md)
- [Configuration Guide](../README.md)
- [Symbol File Preparation](../README.md#symbol-resolution)
