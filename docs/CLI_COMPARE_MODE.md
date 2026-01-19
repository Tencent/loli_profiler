# CLI Compare Mode

## Overview

LoliProfiler CLI now supports comparing two `.loli` profile files to identify memory allocation differences between profiling sessions. This is particularly useful for:

- **Regression Detection**: Compare before/after profiles to detect memory leaks
- **Performance Optimization**: Track memory usage improvements across builds
- **A/B Testing**: Compare memory behavior between different implementations
- **CI/CD Integration**: Automated memory regression testing in pipelines

## Usage

### Basic Compare

Compare two `.loli` files and output differences as text:

```bash
LoliProfilerCLI --compare baseline.loli comparison.loli --out diff.txt
```

### Output Format

The tool produces a human-readable text report with:
- Comparison statistics (allocation counts, sizes, deltas)
- Hierarchical call stack view (deep copy format)
- 4-space indentation showing call hierarchy

Example output:
```
=== LoliProfiler Comparison Report ===

Baseline allocations: 10524
Comparison allocations: 12891
Baseline total size: 45.32 MB
Comparison total size: 52.10 MB
Size delta: +6.78 MB

Changed allocations (>1KB growth): 156

=== Memory Growth (Delta: Comparison - Baseline) ===

FTextLocalizationManager::GetDisplayString(FTextKey const&, FTextKey const&, FString const*), +5.20 MB, +141872
    FMemory::Malloc(unsigned long, unsigned int), +2.59 MB, +84836
    TArray<char16_t, TSizedDefaultAllocator<32>>::ResizeTo(int), +2.35 MB, +57034
        FMemory::Realloc(void*, unsigned long, unsigned int), +2.35 MB, +57034
```

### Skip Root Call Stack Levels

When comparing profiles from different app versions, system library addresses may differ even though they represent the same code path. Use `--skip-root-levels` to ignore the top N frames of each call stack and compare from a deeper level:

```bash
# Skip 2 root levels (e.g., skip libsystem_pthread.dylib frames)
LoliProfilerCLI --compare baseline.loli comparison.loli --out diff.txt --skip-root-levels 2
```

**Example:** If your call stacks look like:
```
/usr/lib/system/libsystem_pthread.dylib!0xe878
  /usr/lib/system/libsystem_pthread.dylib!0x9c74
    FRunnableThreadPThread::_ThreadProc(void*)
      FRunnableThreadPThread::Run()
        ...
```

Using `--skip-root-levels 2` will compare starting from `FRunnableThreadPThread::_ThreadProc` instead of the system library addresses, which may differ between versions.

## Command-Line Options

### Compare Mode Options

| Option | Description |
|--------|-------------|
| `--compare` | Enable compare mode (requires 2 positional file arguments) |
| `<baseline.loli>` | First .loli file (baseline) - positional argument |
| `<comparison.loli>` | Second .loli file (comparison) - positional argument |
| `--out <path>` | Output file path (`.txt`) |
| `--skip-root-levels <N>` | Skip N root call stack frames in comparison (default: 0) |

## Comparison Statistics

The tool calculates and reports:

- **Baseline allocations**: Total number of allocations in baseline file
- **Comparison allocations**: Total number of allocations in comparison file
- **Baseline total size**: Total memory allocated in baseline
- **Comparison total size**: Total memory allocated in comparison
- **Size delta**: Net change in memory usage (positive = growth, negative = reduction)
- **Changed allocations (>1KB growth)**: Leaf allocations with size increase >1KB

## Output Format Details

### Text Output Format

The text format follows the "deep copy" style from the GUI:

```
function_name, +size, +count
    child_function, +size, +count
        grandchild_function, +size, +count
```

- Each level of indentation = 4 spaces
- Size uses human-readable units (Bytes, KB, MB, GB) with `+` prefix for growth
- Count shows increase in number of allocations with `+` prefix
- Parent nodes accumulate sizes from all children
- Only nodes with >1KB growth are included

## Use Cases

### 1. Detecting Memory Leaks

```bash
# Profile before feature implementation
LoliProfilerCLI --app com.example.game --out before.loli --duration 300

# Profile after feature implementation
LoliProfilerCLI --app com.example.game --out after.loli --duration 300

# Compare to find new leaks
LoliProfilerCLI --compare before.loli after.loli --out leaks.txt
```

### 2. CI/CD Integration

```bash
#!/bin/bash
# regression_test.sh

# Profile baseline (from git)
LoliProfilerCLI --app com.example.game --out baseline.loli --duration 60

# Profile current branch
LoliProfilerCLI --app com.example.game --out current.loli --duration 60

# Compare
LoliProfilerCLI --compare baseline.loli current.loli --out diff.txt

# Parse results and fail if memory increased > 5%
# (implement your own threshold logic)
```

### 3. Performance Optimization Validation

```bash
# Profile before optimization
LoliProfilerCLI --app com.example.game --out pre_optimization.loli --duration 120

# Apply optimization, rebuild, reinstall

# Profile after optimization
LoliProfilerCLI --app com.example.game --out post_optimization.loli --duration 120

# Compare to verify improvements
LoliProfilerCLI --compare pre_optimization.loli post_optimization.loli --out optimization_impact.txt
```

### 4. Comparing Different Implementations

```bash
# Profile implementation A
LoliProfilerCLI --app com.example.game --out impl_a.loli --duration 60

# Switch to implementation B, rebuild, reinstall

# Profile implementation B
LoliProfilerCLI --app com.example.game --out impl_b.loli --duration 60

# Compare
LoliProfilerCLI --compare impl_a.loli impl_b.loli --out impl_comparison.txt
```

## Notes

- Both `.loli` files must be valid LoliProfiler profile files (magic number: `0xA4B3C2D1`, version: `106`)
- Comparison is based on call stack hashing for efficient matching
- Symbol resolution requires that symbol maps are present in the `.loli` files
- The comparison algorithm matches allocations by call stack, not by memory address
- Only leaf nodes with >1KB growth are included in the output

## Troubleshooting

### "Failed to load baseline/comparison"

- Ensure both files are valid `.loli` files
- Check file permissions
- Verify files are not corrupted

### "Version mismatch"

- Both files must be created with the same version of LoliProfiler
- Current version: 106
- Magic number: 0xA4B3C2D1

## Implementation Details

### Comparison Algorithm

1. **Load both profiles**: Parse binary `.loli` format into memory structures
2. **Build call trees with hash maps**: Use `qHashRange()` for O(1) node lookup by call stack suffix
3. **Leaf-node diffing**: Compare only leaf nodes (allocation sites) by hash
4. **Filter growth**: Only include allocations with >1KB size increase
5. **Bottom-up propagation**: Propagate leaf deltas up to parent nodes
6. **Export results**: Generate text output with hierarchical format

### Data Structures

- **CallTreeNode**: Hierarchical tree node with function name, size delta, and count delta
- **ProfileData**: Complete profile including stack records, call stacks, symbols, and metadata
- **Hash-based matching**: Uses Qt's `qHashRange()` for efficient call stack comparison

### Performance

- Loading: O(n) where n = number of allocations
- Comparison: O(n + m) where n, m = allocations in each file
- Export: O(k) where k = allocations in output

For typical game profiling sessions (10k-100k allocations), comparison completes in <1 second.
