## Context

The leoin2012 fork at commit `7144e2e301d922d20a76416ee3d4bde27c327f1c` provides a Qt comparison dialog, signed red/green trees, base/comparison tooltips, bidirectional CLI comparison, optional heuristic pseudo-symbol alignment, and a version-107 comparison tail. This repository has ported an older growth-only CLI comparator to Qt-free C++, but has no comparison window. The fork's newer implementation still thresholds at 1 KiB and computes from leaves only. The local older comparator also matches hashes without structural equality and uses cumulative statistics. These behaviors cannot serve as a complete memory diff.

## Goals / Non-Goals

Goals: one independently tested LoliCore API, signed live-allocation deltas, responsive standalone ImGui inspection, independent search state for three tabs, and working build/release integration.

Non-goals: changing capture or timeline leak analysis, reproducing incorrect legacy diff behavior, and encoding signed counts in the unsigned capture format.

## Decisions

- Build a flat union call tree with exact `(parent, library, function)` equality. Resolve symbols per session; named symbols match across address changes, unresolved frames match library plus address. Hashes accelerate lookup but never decide equality. UUIDs and allocation addresses do not identify stacks across captures.
- Store baseline/comparison inclusive and self byte/count metrics on each node. Delta is comparison minus baseline. Keep internal-node self allocations and baseline-only branches. Show zero-net ancestors when their descendants change. Include changes smaller than 1 KiB and count-only changes.
- Use saved free sequence filtering (`record.seq < latestFree[record.addr]`) for both sides. Missing/empty call stacks get an explicit fallback frame; root skipping retains an explicit omitted-stack bucket to conserve totals.
- Expose the union tree and per-view metrics through `profilecomparison.h`. The CLI report and GUI read this same result. Retain the old dump implementation, but route its comparator facade through the new API. Reject `.loli` delta export with a clear explanation because that format cannot preserve signed counts, internal self deltas, or large signed byte totals.
- Add `LoliProfilerCompare` as a separate SFML/ImGui executable. File selection happens on the UI thread; loading/comparison happens asynchronously. Each tab owns expansion, selection, search matches, navigation, visible rows, and sort state. Clip visible rows and reveal ancestors on search.
- Use native ImGui docking for three independent tree windows, defaulting to Base/Comparer/Diff columns. Save layouts to `loli_compare_imgui.ini` beside the settings file, separately from main-app layout. Keep filenames in fixed-width toolbar slots, full paths in the native title/tooltips, status in a bottom viewport bar, and per-panel searches beneath the clipped tree. Remove expand/collapse-all controls. Settings holds theme selection and infrequent root skipping; rebuild style from defaults before applying DPI scale on a theme change.
- Launch the executable using a detached native process with correctly quoted arguments, so closing the main app does not close the comparison window. Resolve it relative to the actual executable (including macOS bundle layout).

## Risks / Trade-offs

- Different symbol quality between files can produce separate branches: display resolved identity and document symbolizing both captures consistently.
- The fork heuristically aligns pseudo-symbol subtrees and writes version-107 compare captures: omit heuristic pairing to avoid silently conflating different stacks; consume standard version-106 captures and export exact signed text without extending the capture format.
- Large captures require significant parser memory: aggregate each file sequentially and release its session before loading the next.
- Existing automation expects growth-only output: document complete signed live diff semantics and the `.loli` export limitation.
- Desktop behavior requires runtime validation: add GUI smoke diagnostics plus exercise file dialogs/search/tab independence with the supplied captures.

## Migration Plan

Ship all three executables together and update build/package scripts. Existing CLI text invocation remains valid. Roll back by reverting this change; capture files remain untouched.

## Open Questions

None blocking. Private captures were used for local verification; their identifying paths, contents, and statistics are excluded from published artifacts. Public screenshots show only the empty comparison window.
