## ADDED Requirements

### Requirement: Application window and frame loop
The system SHALL create a desktop window using SFML (via the imgui-sfml binding) as the default windowing backend, run an immediate-mode ImGui frame loop at display refresh rate, and render with the OpenGL3 backend. An SDL-based backend SHALL be available as a compile-time switchable alternative behind a platform abstraction interface, so that panel code contains no SFML- or SDL-specific calls.

#### Scenario: Launch the GUI
- **WHEN** the user starts the ImGui GUI executable
- **THEN** a window opens with an ImGui dockspace workspace and a menubar, and the frame loop runs without Qt Widgets

#### Scenario: Backend fallback
- **WHEN** the build is configured with the SDL backend option
- **THEN** the application compiles and runs with SDL windowing with no changes to panel source files

### Requirement: Dockable workspace, no dashboard
The system SHALL build ImGui from the docking branch with docking enabled, and SHALL provide a root dockspace hosting the main panels (stacktrace, timeline charts, treemap, smaps, screenshot, console, capture status). Nearly all panels SHALL be dockable, rearrangeable, and floatable. The system SHALL NOT include a persistent dashboard page; the docking workspace itself is the main window, and launch-time settings live in the Run/Launch dialog (see profiling-control-ui).

#### Scenario: Rearrange panels
- **WHEN** the user drags a panel tab to another dock region or outside the window
- **THEN** the panel re-docks or floats at the target and the layout persists for the remainder of the session

#### Scenario: No dashboard on startup
- **WHEN** the application starts
- **THEN** no dashboard/settings page is shown; the workspace with docked panels is displayed

### Requirement: Menubar
The system SHALL display a menubar containing at minimum a File menu with: Run/Launch…, Open Record, Themes submenu, and Exit.

#### Scenario: Menubar visible
- **WHEN** the application window is shown
- **THEN** the menubar displays File → Run/Launch…, File → Open Record, File → Themes, and File → Exit entries

### Requirement: ImGui version
The system SHALL vendor Dear ImGui (docking branch) pinned to the stable release tag v1.92.9b under thirdparty/.

#### Scenario: Version pin
- **WHEN** the project is configured and built
- **THEN** the compiled ImGui version reports v1.92.9b (IMGUI_VERSION matching that tag) with docking support enabled
