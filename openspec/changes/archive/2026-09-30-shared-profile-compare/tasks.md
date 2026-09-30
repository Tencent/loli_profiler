## 1. Shared engine and CLI

- [x] 1.1 Inspect fork comparison behavior and document deliberate corrections.
- [x] 1.2 Implement exact structural comparison, live filtering, signed inclusive/self metrics, and reports in LoliCore.
- [x] 1.3 Route CLI and comparator facade through the API; validate options and export errors.
- [x] 1.4 Add regression tests covering signed changes, internal self allocations, identity, frees, malformed input, and large totals.

## 2. Desktop comparison

- [x] 2.1 Implement standalone argument handling, asynchronous loading, input selection, and three tab views.
- [x] 2.2 Implement independent searchable, sortable, clipped tree views with navigation and copy/tooltips.
- [x] 2.3 Add detached launch from main File menu and top toolbar.
- [x] 2.4 Integrate CMake, build driver, and release packaging on supported platforms.

## 3. Validation and documentation

- [x] 3.1 Build all executables and run focused automated regression tests.
- [x] 3.2 Independently reconcile supplied captures; verify forward, reverse, and identical comparisons and GUI/CLI parity.
- [x] 3.3 Exercise GUI tabs/search and separate process launch/lifetime; document results and any limitations.
- [x] 3.4 Update usage/architecture documentation and validate OpenSpec artifacts.

## 4. Dockable comparison workspace refinement

- [x] 4.1 Replace the tab bar with three dockable panels, default to equal horizontal columns, and persist custom layouts separately.
- [x] 4.2 Consolidate tools into one toolbar with fixed-width filenames, full-path title/tooltips, and bottom loading/allocation status.
- [x] 4.3 Move each node search to its panel footer, remove expand/collapse UI, and add persistent theme settings.
- [x] 4.4 Verify default and stacked-left/diff-right layouts on the supplied captures; update usage and validation documentation.

## 5. Release preparation

- [x] 5.1 Move root skipping into Settings and generate documentation screenshots with no loaded capture data.
- [x] 5.2 Credit the source fork and compare contributor; remove private sample details from publication artifacts.
- [x] 5.3 Run final checks and remove disposable/generated test artifacts while retaining reusable regression tests.
- [x] 5.4 Review publication-safe code/docs and finalize the handoff for archive, commit, and push.
