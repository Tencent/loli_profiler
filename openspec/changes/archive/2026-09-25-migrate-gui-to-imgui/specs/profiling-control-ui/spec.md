## ADDED Requirements

### Requirement: Run/Launch dialog
The system SHALL provide a modal Run/Launch dialog that consolidates all launch-time settings — target device selection, target app/package selection, and capture configuration (the options previously spread across the Qt config dialog and dashboard) — in one place. The dialog SHALL be opened from the menubar File → Run/Launch… and from a toolbar Run button. There SHALL be no persistent dashboard page for these settings.

#### Scenario: Open dialog from menubar
- **WHEN** the user selects File → Run/Launch…
- **THEN** a modal dialog appears with device selection, app selection, and capture configuration controls populated from the connected ADB state

#### Scenario: Open dialog from toolbar
- **WHEN** the user clicks the Run/Launch toolbar button
- **THEN** the same modal dialog appears

### Requirement: Capture start/stop control
The system SHALL allow starting a capture from the Run/Launch dialog (confirm action) and SHALL provide a prominent stop control while a capture is running, with capture status (elapsed time, sample counts) visible in a dockable status panel.

#### Scenario: Start capture
- **WHEN** the user confirms the Run/Launch dialog with a valid device and app selected
- **THEN** profiling starts, the dialog closes, and the capture status panel shows the running session

#### Scenario: Stop capture
- **WHEN** the user clicks Stop during a running capture
- **THEN** profiling stops and captured data becomes available in the visualization panels

### Requirement: Record file loading
The system SHALL load `.loli` record files chosen via the native open-file dialog and populate all visualization panels from the record without a connected device.

#### Scenario: Load record file
- **WHEN** the user opens a `.loli` file via File → Open Record
- **THEN** the stacktrace, charts, treemap, and smaps panels populate from the record data
