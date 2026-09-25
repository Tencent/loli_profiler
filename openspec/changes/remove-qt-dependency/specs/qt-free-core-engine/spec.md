## ADDED Requirements

### Requirement: Qt-free core engine
The core profiling data pipeline (adb process control, device stacktrace channel, record model, symbol resolution, smaps parsing, screenshots, meminfo) SHALL be implemented in portable C++ (STL + vendored libraries) with no Qt includes, Qt types, or Qt linkage.

#### Scenario: Core builds without Qt
- **WHEN** the project is configured and built
- **THEN** no core source or header includes a Qt header and no target links Qt5/Qt6 libraries

#### Scenario: Identical data pipeline output
- **WHEN** a capture or record load runs on the Qt-free core
- **THEN** the produced records, stacktraces, meminfo samples, smaps sections, and screenshots match the Qt implementation's output for the same input

### Requirement: Qt-free string/container model
All core data structures SHALL use STL types (`std::string`, `std::vector`, `std::unordered_map`, `std::unordered_set`, `std::pair`) instead of Qt containers, while preserving the public behavior of the existing model classes.

#### Scenario: Record and callstack model
- **WHEN** records and callstacks are stored and queried
- **THEN** they use STL containers and expose the same fields (seq, time, size, addr, library, frames) as before
