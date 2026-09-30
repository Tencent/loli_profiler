## Context

The application uses a Qt-free C++17 core, SFML 3, Dear ImGui, nativefiledialog, and a Python build/release driver. macOS branches exist but have not run on this Apple Silicon host. Six external ZIP archives contain large Qt-era captures. No SDK is installed at the conventional user location at initial inspection.

## Goals / Non-Goals

**Goals:** Build and run GUI and CLI natively; make local and extracted runtimes usable; validate all supplied captures with reproducible assertions; prepare Android hook/tool paths before requesting a phone.

**Non-Goals:** Universal/Intel binary certification, notarization/signing for public distribution, Linux acceptance, changing capture format, and completing unrelated migration tasks.

## Decisions

1. Keep the pinned submodule versions and fix integration in the parent project. Inspect dependency platform constraints before choosing the deployment target; respect an explicit supported user target. A dependency upgrade would broaden the scope unnecessarily.
2. Share runtime staging between host build output locations: executable-relative injector/config/icon and available hook libraries. Full builds stage again after hooks compile. The release archive must retain executable modes and any required frameworks. Check `otool` output and run an extracted copy to detect accidental build-tree dependencies.
3. Use explicit CLI/source/output arguments in a Python validation driver. Extract only `.loli` members under an ignored workspace and never write beside the original archives. Export each capture to SQLite and check integrity, metadata, tree/root counts, and allocation totals. Compare each capture with itself for zero differences and compare a representative pair. Exercise the Python agent directly against generated databases.
4. Test GUI loading on the active macOS desktop, retaining logs and screenshots where possible. Add a bounded diagnostic smoke mode only if necessary to make load/range analysis and clean exit repeatable.
5. Discover installed SDK/NDK and use existing tools before considering installation. Fix macOS-specific live-symbolizer discovery and modern NDK tool fallback. No device operations are required for offline acceptance.
6. Leave real-device acceptance unchecked until an actual phone capture succeeds. The final user prompt requests a USB-connected, unlocked Android phone with debugging enabled and the target package; subsequent testing must inspect capture counts, stop behavior, and saved-file reopen/export.
7. Replace comparator suffix-hash identity with exact `(parent path ID, library, resolved function)` keys shared across baseline/comparison builds. A sampled native supplied-capture export spent every CPU sample following a cyclic parent chain created by 32-bit hash merging. Build root-first, assign each parent before its children, and aggregate records by UUID to avoid repeatedly resolving identical callstacks. Retain the SQLite snapshot schema; previously collision-merged node IDs necessarily change. Add synthetic colliding-name and shared-prefix comparisons to verify distinct paths and zero self-diffs.
8. The supplied captures originate from the Python iOS importer, which writes one Total-memory series in MB. Load available series without requiring all six Android categories, and track which series exist so absent categories are omitted from rendering/tooltips. Keep timestamp normalization and on-disk serialization unchanged.
9. Android tools were initially unavailable on this host. Provide an explicit `--native-only` offline archive with a distinct name and verified extracted binaries. Full packaging retains its strict hook requirements; do not silently substitute old hook binaries from the installed legacy app. Omit generated runtime state from bundled app trees. In the authorized phone follow-up, checksum-verified official platform-tools and NDK r27 archives now enable all-ABI hooks and full packaging.

10. The first real-device launch exposed the Python-2-only bundled JDWP injector under the discovered Python 3 host. Port wire buffers to bytes and decoded names to text, replace Python 2 dictionary APIs, read exact socket lengths, and queue asynchronous events/replies by packet ID. Preserve legacy-compatible syntax and verify protocol fixtures under host Python 3.9/3.13 before retrying.
11. On the Xiaomi ARM64 phone, signed null return PCs passed the frame-pointer walker's minimum-address check and produced bogus root frames. Strip return-address authentication using XPACLRI's baseline-compatible HINT encoding before validation and recording. This instruction is a no-op on older cores; avoid assuming a fixed virtual-address width. Verify observed signed-null fixtures on the actual phone and repeat capture with freshly built hooks.
12. ImGui's enabled macOS behavior swaps physical Command/Super into logical Control before application code reads input. Use logical `ImGuiMod_Ctrl` chords consistently for every application shortcut instead of selecting `KeySuper` on Apple hosts. Preserve native text editing, menu labels, action availability, and dialog key cleanup. Exercise all five action keys under both macOS and non-macOS modifier normalization, including wrong modifiers and held keys.

## Risks / Trade-offs

- [Apple-only compile/runtime behavior] → Configure and build on this host and inspect the emitted binaries/frameworks.
- [Large captures consume memory/disk] → Process captures serially; retain machine-readable results and logs under `build/`.
- [Old captures have unknown totals] → Independently read serialization header records/totals when practical and assert SQLite root accounting, rather than trusting exit status alone.
- [SDK/NDK unavailable] → Search existing installations and clearly record any missing prerequisite; offline acceptance remains independent of the phone.
- [GUI launch alone misses data errors] → Verify published snapshot/tree data after loading, and exercise time filtering when the sample has a usable allocation span.

## Migration Plan

Create specs and tasks, initialize dependencies, fix native build failures, prepare hooks/runtime, run offline validation, package and extract, document evidence, then request the phone. Keep source samples unchanged and rollback by reverting scoped code changes; generated build/dist artifacts are ignored.

## Open Questions

- Resolved in the phone follow-up: the supplied debuggable UE4 app, matching symbols, LLVM/malloc/strict/framepointer/arm64-v8a/threshold zero/library whitelist. Use the supplied game automation CLI to enter the requested scene after login and stop once visible. Live-allocation retention bounds host memory.
- Actual supported deployment target and runtime framework layout will be settled using the pinned dependencies and native build evidence.

## Upstream integration

Merge `origin/remove-qt` comparison feature commit `3d81a4e` before committing the macOS work. The new signed comparison API replaces growth-only comparison and rejects lossy diff `.loli` export; retain the exact snapshot-dump identity fix separately. Update tests and validator report assertions for signed changed/new/removed stacks. Include `LoliProfilerCompare` in build/staging/package targets and conditionally link `sfml-main` for both GUIs. Check focused comparison fixtures, actual GUI layouts/report parity, a representative saved capture, and extracted launch behavior. Detailed private capture evidence remains in ignored local build directories.
