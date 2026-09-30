# shared-profile-comparison Specification

## Purpose
Define the shared Qt-free capture comparison API used by CLI and GUI, preserving exact signed live-allocation metrics and structural stack identity.

## Requirements
### Requirement: Shared signed comparison
LoliCore SHALL expose one API used by the CLI and comparison GUI to compare live allocations and provide baseline, comparison, and signed comparison-minus-baseline inclusive and self metrics with 64-bit bytes and counts.

#### Scenario: Complete changes
- **WHEN** stacks grow, shrink, disappear, change size without count growth, or change only count
- **THEN** the diff retains all changes including changes below 1 KiB and zero-net parents with changed descendants

#### Scenario: Internal allocations
- **WHEN** a stack ends at a node that also has children
- **THEN** its own allocations remain represented independently of its descendants

### Requirement: Exact stack identity and live filtering
The API SHALL match library-qualified resolved stack paths with exact key equality, filter saved frees by allocation sequence, and represent records with unavailable or omitted stacks explicitly.

#### Scenario: Identity across captures
- **WHEN** intern hashes, UUIDs, allocation addresses, or symbol addresses differ between captures but library-qualified symbol paths match
- **THEN** those paths match, while the same function in different libraries remains separate

#### Scenario: Reused address and missing stacks
- **WHEN** one address is freed and reused, or a live record has no call stack
- **THEN** the old allocation is excluded, the newer allocation is retained, and totals reconcile with root sums

### Requirement: Reports and errors
CLI comparison SHALL use the shared API, report exact signed totals and hierarchical changes, reject invalid options and unreadable/malformed inputs, and reject capture-format diff export that cannot preserve signed metrics.

#### Scenario: Existing invocation
- **WHEN** `LoliProfilerCLI --compare baseline.loli comparison.loli --out diff.txt` succeeds
- **THEN** its totals and tree metrics equal the GUI result for those files

#### Scenario: Unsupported signed capture export
- **WHEN** comparison output ends in `.loli`
- **THEN** the CLI explains the limitation and does not create or overwrite that output
