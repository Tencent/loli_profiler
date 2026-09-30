## ADDED Requirements

### Requirement: Collision-safe CLI call trees
CLI snapshot dump SHALL identify nodes by their exact parent call path, library, and resolved function name. Colliding string/hash values SHALL NOT merge unrelated paths or create cycles. Comparison SHALL delegate to the upstream shared signed comparison API, retaining all live allocation changes and rejecting lossy capture-format diff export.

#### Scenario: Hash collisions in supplied capture
- **WHEN** a supplied capture or a fixture containing colliding legacy function hashes is exported or compared
- **THEN** construction terminates with an acyclic tree retaining distinct paths, consistent allocation totals, and zero self-differences

### Requirement: Read-only archived sample validation
The offline validator SHALL accept an external directory containing archived `.loli` samples, safely extract into a separate workspace, process every supplied capture serially, and emit a machine-readable report. It SHALL leave source archives unchanged.

#### Scenario: Six supplied sample archives
- **WHEN** the validator runs against the supplied HeapMemoryProfiler output directory
- **THEN** all six captures export to integrity-checked SQLite databases with consistent metadata, root byte totals, and allocation totals

### Requirement: Meaningful CLI and agent regression checks
Validation SHALL check text export, zero self-comparison differences, representative cross-capture comparison, and Python agent queries against generated databases. Failed commands or inconsistent results SHALL make validation fail.

#### Scenario: Self comparison and agent queries
- **WHEN** a supplied capture is compared with itself and queried through the Python agent
- **THEN** changed/new allocation counts are zero and summary, hotspot, and call-path queries return data matching the exported database

### Requirement: GUI saved-record acceptance
macOS acceptance SHALL include native GUI initialization, saved-record load with nonempty callstack trees, and time-range analysis on a representative capture, with evidence retained.

#### Scenario: Saved capture GUI load
- **WHEN** the GUI opens a supplied capture on this macOS host
- **THEN** it publishes nonempty snapshot and tree data and completes a supported time-range analysis without crashing

#### Scenario: Single-series imported capture
- **WHEN** a supplied HeapMemoryProfiler capture contains only its Total memory timeline
- **THEN** the GUI displays that timeline in MB, supports time selection, and omits unavailable Android memory categories from its plot and tooltip

### Requirement: Final real-device capture gate
The change SHALL retain a pending real-device CLI acceptance task until an actual Android phone capture succeeds. The user SHALL be asked to connect the phone only after native build, offline checks, and release preparation are complete.

#### Scenario: Offline work finished
- **WHEN** native and offline acceptance completes
- **THEN** the final user-facing step asks for a connected unlocked Android phone with USB debugging and the target application package, and records device testing as pending

#### Scenario: Connected phone capture acceptance
- **WHEN** the requested phone is available in a subsequent step
- **THEN** CLI launch/injection, allocation capture, orderly stop, save, and reopening/export of nonempty captured data are verified before the task is marked complete

#### Scenario: UE4 scene capture on the supplied phone
- **WHEN** the supplied UE4 phone app and matching symbols are available
- **THEN** capture uses LLVM, malloc, strict mode, frame pointers, arm64-v8a, threshold zero, and the requested library whitelist; after launch reaches login, the supplied game automation CLI opens the requested map, and capture stops after the game scene is visible
- **AND** saved records, symbols, free-event accounting, memory timelines, screenshots, and library mappings are validated before acceptance
