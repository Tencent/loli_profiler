# Validation

Validated on Windows x64 / VS2022 Release. The reference was [leoin2012/loli_profiler](https://github.com/leoin2012/loli_profiler) at commit `7144e2e301d922d20a76416ee3d4bde27c327f1c`; its Loli Compare module credits shuchangliu.

## Reproducible checks

```powershell
cmake -S . -B build/cmake -DBUILD_COMPARISON_TESTS=ON
cmake --build build/cmake --config Release --target LoliProfilerImGui LoliProfilerCLI LoliComparisonTests --parallel 8
ctest --test-dir build/cmake -C Release --output-on-failure
python tests/check_comparison_cli.py --cli build/cmake/Release/LoliProfilerCLI.exe --gui build/cmake/Release/LoliProfilerCompare.exe
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/check_compare_launch.ps1
python tests/verify_comparison_samples.py base.loli comparison.loli --cli build/cmake/Release/LoliProfilerCLI.exe --out-dir build/comparison-validation
python scripts/build.py --mode native --non-interactive
```

C++ tests include independently aggregated randomized dictionary references and explicit signed/count-only/sub-1-KiB changes, zero-net ancestors, internal-node allocations, same symbols in different libraries, changed addresses/intern hashes/UUIDs, saved frees/address reuse, missing stacks, root skipping, totals above 32-bit range, malformed inputs, facade state transitions, and independent panel search/sorting/selection. End-to-end tests generate synthetic capture wire data and exercise Unicode/spaced paths, invalid arguments, input protection, GUI reports, missing inputs, and identical-file rendering. All pass.

The independent mmap/struct reader parses complete version-106 captures without LoliCore and verifies inclusive/self byte/count deltas for every changed path. Forward/reverse/identical real-capture comparisons and GUI/CLI report parity passed. Private filenames, paths, statistics, and report hashes are intentionally omitted.

## Desktop behavior

- Default Base/Comparer/Diff columns and the stacked-left/Diff-right arrangement render correctly. Smoke checks verify actual dock IDs and panel geometry.
- The separate comparison process outlives its launcher. Closing during a large load also exits normally after the worker finishes safely.
- An isolated executable copy retained identical docking-node data across close/restart using its separate `loli_compare_imgui.ini`.
- Search footers are independent per panel; expand/collapse-all controls are removed.
- Toolbar file actions use fixed-width basenames. Full paths are in the native title/tooltips; loading/allocation status is at the bottom.
- Settings contains theme selection and root skipping. Theme application resets style before DPI scaling to prevent cumulative scaling.
- Native file dialogs use the existing FileDialogs API. Actual file-picker interaction, dragging dock tabs, and selecting theme/root-skip controls were not automated.
- Windows native build/staging and release packaging include all three executables. macOS/Linux code paths were not built or run here.

## Safe documentation images and cleanup

`docs/images/imgui-compare-empty.png` and `imgui-compare-empty-stacked.png` were captured from the application with no files opened, then visually inspected. They contain no capture paths, symbols, or allocation data. Empty preview mode refuses input capture paths.

Generated real-data reports/screenshots, disposable executable copies, and stale test outputs are removed before publication. Reusable tests contain only synthetic data or accept local input paths from arguments; original private captures remain outside the repository.

## Compatibility

Diff uses live allocation state and retains all signed changes, replacing the old cumulative growth-only report. Exact signed text export is supported; capture-format diff export is rejected because `.loli` cannot faithfully encode signed counts and arbitrary signed self/64-bit totals. The fork's heuristic pseudo-symbol alignment and version-107 comparison tail are not imported. Snapshot dump and timeline Leaks keep their original algorithms.
