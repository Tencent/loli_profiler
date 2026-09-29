## ADDED Requirements

### Requirement: Explicit .loli serializer
The system SHALL read and write `.loli` record files using an explicit, endian-explicit binary reader/writer that reproduces the existing QDataStream-based byte layout exactly (magic `0xA4B3C2D1`, version `106`, and the documented field order), without Qt.

#### Scenario: Load existing capture
- **WHEN** an existing `.loli` file produced by the Qt build is opened
- **THEN** it loads with identical records, callstacks, meminfo, screenshots, and smaps data

#### Scenario: Round-trip fidelity
- **WHEN** a session is saved with the new serializer and reloaded
- **THEN** all data round-trips losslessly

#### Scenario: Backward-compatible bytes
- **WHEN** a file written by the new serializer is opened by the previous Qt-based tool
- **THEN** it loads correctly (same byte layout)

### Requirement: Format regression harness
The serializer SHALL be validated by byte-level comparison and load-verification against the known-good sample files (`2026.09.14-11.24.23.loli`, `6s_heap_0919.loli`).

#### Scenario: Sample files load identically
- **WHEN** the reference sample files are loaded by the new reader
- **THEN** record counts and key aggregates match the Qt reader's output

### Requirement: SaveRecord implemented
The ImGui GUI's record saving SHALL be fully implemented on the new serializer (replacing the current stub), so a live or loaded session can be saved to a `.loli` file.

#### Scenario: Save a session
- **WHEN** the user invokes save after capture or load
- **THEN** a valid `.loli` file is written that reloads correctly
