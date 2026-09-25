## ADDED Requirements

### Requirement: Qt-free config persistence
Application settings (theme, last directories, SDK/NDK/python paths) SHALL persist via a Qt-free store (INI or JSON file), replacing `QSettings`. The existing `loli3.conf` capture-config text format SHALL remain readable and writable with its current `key:value` layout.

#### Scenario: Settings persist across restarts
- **WHEN** the user changes theme or SDK/NDK/python paths and restarts the app
- **THEN** the settings are restored from the Qt-free store

#### Scenario: loli3.conf compatibility
- **WHEN** an existing `loli3.conf` is present
- **THEN** the capture configuration loads with identical values

### Requirement: No registry dependence
On Windows, settings SHALL NOT be read from or written to the registry; they SHALL live in a per-user config file.

#### Scenario: Portable settings location
- **WHEN** settings are saved on Windows
- **THEN** they are written to a config file (not the Windows registry)
