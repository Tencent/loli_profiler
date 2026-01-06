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
  - Manual stop with Ctrl+C (profile until you're ready to stop)
- **Graceful Shutdown**: Ctrl+C triggers proper data collection and file save
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
- `--duration <seconds>` - Profiling duration in seconds (omit for manual stop with Ctrl+C)
- `--attach` - Attach to running app instead of launching new instance
- `--verbose` - Enable verbose output for debugging
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

### Manual Stop with Ctrl+C

Profile until you manually stop (press Ctrl+C when ready):

```bash
LoliProfilerCLI.exe --app com.example.game --out profile.loli --verbose
```

When you press Ctrl+C:
- The profiler sends a SMAPS_DUMP command to the Android agent
- Memory mapping data is collected
- All data is saved to the `.loli` file
- The profiler exits cleanly with proper cleanup

This is useful for:
- Interactive profiling sessions where you control when to stop
- Capturing specific gameplay scenarios
- Ensuring complete memory mapping data is collected before shutdown

### Attach to Running App

Attach to an already-running application (use Ctrl+C to stop when done):

```bash
LoliProfilerCLI.exe --app com.example.game --out profile.loli --attach
```

Or with a fixed duration:

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

Use `--verbose` flag to see detailed progress information:

```bash
LoliProfilerCLI.exe --app com.example.game --out profile.loli --verbose
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
| Duration | Manual stop button | Timed or Ctrl+C |
| Progress | Visual progress bar | Console messages |
| Symbol Loading | File dialog | `-symbol` flag |
| Data Optimization | User prompt | Always enabled |
| Launch Mode | User prompt | `-attach` flag |

## Stopping Profiling

### With Duration (Automatic Stop)

When you specify `--duration <seconds>`, the profiler will automatically:
1. Profile for exactly the specified duration
2. Send SMAPS_DUMP command to collect memory mapping data
3. Save all captured data to the output file
4. Exit with code 0 on success

### Without Duration (Manual Stop with Ctrl+C)

When you omit `--duration`, the profiler runs indefinitely until you press **Ctrl+C**:

```bash
LoliProfilerCLI.exe --app com.example.game --out profile.loli
# Output: "Profiling... Press Ctrl+C to stop."
# ... profile as long as you want ...
# Press Ctrl+C when ready
# Output: "Received stop signal, stopping profiling gracefully..."
```

**What happens on Ctrl+C:**
1. Signal handler catches SIGINT (Ctrl+C) or SIGTERM
2. Queues stop request to main thread (thread-safe)
3. Sends SMAPS_DUMP command to Android agent
4. Collects memory mapping information
5. Processes and saves all data to `.loli` file
6. Exits cleanly with code 0

**Important Notes:**
- Always use Ctrl+C to stop gracefully - this ensures complete data collection
- Killing the CLI process forcefully (Task Manager, `kill -9`) will result in incomplete data
- If the app crashes or connection is lost unexpectedly, you'll see an error and data may be incomplete
- The profiler is independent of the app lifecycle - the app can crash/restart and profiling continues

### Process Exit vs. Profiler Exit

**Important:** The CLI profiler does NOT automatically stop when the target app exits. This is intentional and matches behavior of professional profiling tools like `perf` and Android Studio Profiler.

**Why?**
- Allows profiling across app restarts
- Lets you capture multiple runs in a single session
- Gives you control over exactly when to stop
- Ensures proper data collection with SMAPS_DUMP before shutdown

If the app exits or crashes:
- The profiler will detect connection loss and show an error
- Data captured up to that point is preserved (in cache files)
- You can restart the app and continue profiling
- Use Ctrl+C when you're done to save all data properly

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
