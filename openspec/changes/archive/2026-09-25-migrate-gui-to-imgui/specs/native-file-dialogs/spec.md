## ADDED Requirements

### Requirement: Native file dialogs via NFD
The system SHALL use nativefiledialog-extended (NFD), vendored under thirdparty/, for all file open, file save, and folder pick operations in the GUI, replacing Qt's QFileDialog.

#### Scenario: Open record file
- **WHEN** the user selects File → Open Record
- **THEN** a native OS open-file dialog appears filtered to `.loli` files, and the chosen path is returned to the application

#### Scenario: Save dialog
- **WHEN** the user triggers an export/save action
- **THEN** a native OS save-file dialog appears with appropriate extension filters

#### Scenario: Cancel dialog
- **WHEN** the user cancels a native file dialog
- **THEN** the application continues unchanged and no file operation is attempted

### Requirement: C++ wrapper
The system SHALL wrap the NFD C API in a small C++ helper returning `std::optional<std::string>` (or equivalent), so no UI panel calls the NFD C API directly.

#### Scenario: Wrapper usage
- **WHEN** any panel needs a file path from the user
- **THEN** it calls the C++ helper rather than NFD functions directly
