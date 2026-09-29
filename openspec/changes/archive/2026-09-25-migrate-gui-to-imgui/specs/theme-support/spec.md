## ADDED Requirements

### Requirement: Theme registry
The system SHALL provide a compile-time registry of named themes, each implemented as a self-contained `Setup*Style()` function that configures `ImGuiStyle` (colors, sizing, rounding), ported from the Fury editor theme set (including at minimum a Dark theme and a Forest Green theme).

#### Scenario: Registry contains themes
- **WHEN** the application starts
- **THEN** at least two named themes (Dark, Forest Green) are registered and available

### Requirement: Runtime theme switching via menubar
The system SHALL expose all registered themes under the menubar File → Themes submenu and SHALL apply the selected theme immediately to all visible UI without restart.

#### Scenario: Switch theme at runtime
- **WHEN** the user selects File → Themes → Forest Green
- **THEN** the entire UI immediately renders with the Forest Green style

### Requirement: Theme persistence
The system SHALL persist the selected theme name in the application settings store and SHALL restore it on next launch.

#### Scenario: Theme restored on launch
- **WHEN** the user previously selected a non-default theme and relaunches the application
- **THEN** the application starts with the previously selected theme applied
