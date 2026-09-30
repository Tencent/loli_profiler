## 1. Native build

- [x] 1.1 Initialize pinned dependencies, inspect platform constraints, and fix macOS CMake/compiler/link failures.
- [x] 1.2 Build native arm64 GUI and CLI through the Python driver and verify CLI help and bundle metadata.

## 2. Capture-ready runtime

- [x] 2.1 Fix SDK/modern NDK tool discovery and macOS live symbolizer selection; verify with installed tools or focused fixtures.
- [x] 2.2 Stage executable-relative runtime files for macOS local builds and refresh after hook builds.
- [x] 2.3 Build all four LLVM Android hook ABIs with an installed NDK and verify staged copies.
- [x] 2.4 After Android tools are available, package and verify the full capture-ready macOS archive containing all four freshly built LLVM hooks.

## 3. Offline acceptance

- [x] 3.0 Fix the observed snapshot comparator hash-collision cycle using exact path identities, and verify synthetic collision/delta cases before rerunning samples.
- [x] 3.1 Implement a repeatable archive-sample validator with SQLite accounting, CLI comparison/text, and agent query assertions.
- [x] 3.2 Run the validator on all six supplied archives and retain report/logs without modifying originals.
- [x] 3.3 Exercise native GUI sample load, tree publication, and selected-time-range analysis; retain evidence.

## 4. Release and documentation

- [x] 4.1 Package the explicit native-only macOS release, extract separately with executable modes retained, inspect runtime dependencies, and exercise extracted GUI/CLI/agent. Capture-ready packaging remains in 2.4.
- [x] 4.2 Document verified macOS scope, build and validation commands, findings, artifacts, and pending device acceptance; validate OpenSpec and review the final diff.

- [x] 4.3 Port the bundled JDWP injector to Python 3, verify binary packets/fragmented socket reads/events, and refresh both release runtimes after the observed device-launch failure.
- [x] 4.4 Strip ARM64 pointer authentication from frame-pointer return PCs, verify signed-null rejection on the phone, rebuild all hook ABIs, and repeat the scene capture with clean root frames.
- [x] 4.5 Correct all GUI application shortcuts for ImGui's normalized macOS modifiers, verify Command/Control behavior and dialog key cleanup, and refresh both release archives.

## 5. Final phone acceptance

- [x] 5.1 After all offline work completes, request a connected unlocked Android phone with USB debugging and the target package; verify real CLI launch/injection, capture, stop/save, and saved-data reopening/export in the follow-up session.

## 6. Upstream integration

- [x] 6.1 Merge the upstream shared signed comparison/viewer feature, retain macOS snapshot and runtime fixes, build all three desktop targets and four hook ABIs, verify comparison/packaged launch checks, and prepare tested changes for commit/push.
