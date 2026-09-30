## Why

The Qt-free macOS build and package have only been statically checked. Validate the actual Apple Silicon GUI and CLI against existing captures before relying on this host for Android profiling.

## What Changes

- Build the pinned native dependencies, GUI app bundle, CLI, and Android hooks on macOS; fix platform failures discovered by that build.
- Stage capture runtime files for both local binaries and release packages, and verify the extracted package runs.
- Add repeatable offline validation using the six archived `.loli` captures supplied outside the repository, without modifying those originals.
- Exercise GUI load/range analysis and CLI dump, SQLite export, compare, and Python agent queries.
- Prevent hash collisions from merging unrelated CLI call paths or creating cyclic trees, as exposed by a supplied capture.
- Document verified host/toolchain scope, reproducible commands, and actual outcomes.
- Keep real-device CLI capture as a final pending acceptance step, requested only after offline work is complete.
- Complete the subsequently authorized login-to-game-scene phone capture and fix the observed Python 3 injector and ARM64 authenticated-return-address failures.
- Correct all five macOS GUI application shortcuts to honor physical Command with ImGui's modifier normalization, and document platform shortcut mappings.

## Capabilities

### New Capabilities

- `macos-native-release`: Native macOS build, capture-runtime staging, and runnable release archive.
- `macos-capture-validation`: Offline sample validation and an explicit final real-device acceptance gate.

### Modified Capabilities

None.

## Impact

CMake configuration, Python build/package scripts, platform tool discovery, the JDWP injector and Android frame-pointer walker, macOS documentation, and a sample-driven validation script. Pinned dependencies and `.loli` serialization remain compatible. External sample archives are read-only inputs; generated outputs stay under ignored build directories. Linux/Windows device acceptance and unrelated incomplete migration tasks are outside this change.

The final integration also merges the upstream shared signed comparison engine and dockable viewer, retaining macOS fixes while adopting its comparison semantics and focused regressions.
