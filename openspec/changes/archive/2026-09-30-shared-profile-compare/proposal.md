## Why

The ImGui application cannot compare saved captures, and the existing CLI comparison discards reductions and several real growth cases. Users need one reliable comparison engine and a separate desktop window for inspecting two captures.

## What Changes

- Add a Qt-free shared comparison API that builds baseline, comparison, and signed delta trees from live allocations.
- Route the CLI comparison through that API, preserving all byte and count changes and library-qualified stack identity.
- Add a standalone `LoliProfilerCompare` executable accepting baseline/comparison arguments or opening two files interactively.
- Add Compare to the main toolbar and File menu, launching the separate executable.
- Provide three dockable panels, initially in columns, with independent search footers, expansion, selection, and sorting; compact file toolbar, bottom status, and theme settings.
- **BREAKING**: CLI comparison reports become complete signed live-allocation diffs instead of thresholded cumulative growth reports; signed diffs use text rather than lossy `.loli` export.

## Capabilities

### New Capabilities

- `shared-profile-comparison`: Accurate shared capture comparison and CLI reports.
- `comparison-window`: Standalone ImGui comparison window and main app launch controls.

### Modified Capabilities

None; existing specs remain within their original changes and have not been promoted to `openspec/specs`.

## Impact

LoliCore, CLI file processing, ImGui shell, CMake, release packaging, build driver, documentation, and comparison regression tests. No new third-party dependencies. Validate locally with real captures while excluding private capture data from published documentation.
