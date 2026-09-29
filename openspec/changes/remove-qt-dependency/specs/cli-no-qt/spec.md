## ADDED Requirements

### Requirement: Qt-free CLI
The `LoliProfilerCLI` executable (capture, `--dump`, `--compare`) SHALL build and run with no Qt dependency, on the Qt-free core.

#### Scenario: CLI captures without Qt
- **WHEN** `LoliProfilerCLI --app <pkg> --out profile.loli --duration 60` runs
- **THEN** it captures and writes a valid `.loli` with no Qt runtime present

#### Scenario: CLI dump/compare parity
- **WHEN** `LoliProfilerCLI --dump` or `--compare` runs on the reference sample files
- **THEN** the text output matches the prior Qt-based CLI output (golden-output check)

### Requirement: Remove legacy Qt GUI
The legacy Qt Widgets/Charts GUI target and its files (`mainwindow`, `.ui` files, `qdarkstyle`, vendored qconsolewidget, Qt model classes) SHALL be removed once the CLI is Qt-free and the ImGui GUI covers the workflows.

#### Scenario: No Qt GUI target
- **WHEN** the project is configured
- **THEN** there is no Qt-Widgets GUI target and no Qt dependency in any build target

### Requirement: Build/docs without Qt
The Python build driver (`scripts/build.py`), Dockerfile, and current docs SHALL NOT require `QT5Path` or any Qt installation.

#### Scenario: Build on a machine without Qt
- **WHEN** the project is built per the documented steps on a machine with no Qt installed
- **THEN** all targets (GUI + CLI) build successfully
