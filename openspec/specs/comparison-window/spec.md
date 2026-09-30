# comparison-window Specification

## Purpose
Provide a standalone dockable comparison workspace for saved captures, with independent tree searches, compact file controls, status, and theme/root-skip settings.

## Requirements
### Requirement: Separate comparison executable
The desktop application SHALL offer Compare in the top toolbar and File menu, opening an independently running `LoliProfilerCompare` window. The executable SHALL accept two positional files or `--base` and `--compare` paths, and allow interactive selection of both files without arguments.

#### Scenario: Launch and independent lifetime
- **WHEN** Compare is clicked and the main app is subsequently closed
- **THEN** the comparison process remains open and prompts for any missing input files

#### Scenario: Standalone files
- **WHEN** the executable starts with baseline and comparison paths
- **THEN** it asynchronously loads those captures and displays the shared comparison result

### Requirement: Three independently searchable trees
The window SHALL provide Base, Comparer, and Diff dockable tree panels, each with its own search text, matches, next/previous navigation, expansion, selection, and sorting. Search SHALL reveal matching nodes and scroll to them from a footer below the tree. Tables SHALL clip rows for large captures and show signed delta bytes/counts with libraries. The window SHALL omit expand/collapse-all controls from the toolbar and context menus.

#### Scenario: Search isolation
- **WHEN** a user searches and expands a node in one tab and switches tabs
- **THEN** the other tabs retain their own state and returning restores the original search and selection

#### Scenario: Loading errors and replacing inputs
- **WHEN** an input fails to load or the user selects another file
- **THEN** the window remains responsive, reports the error, and permits selecting inputs again

### Requirement: Dockable workspace and compact chrome
The panels SHALL default to three equally sized columns ordered Base, Comparer, Diff. Users SHALL be able to dock, split, stack, or group the panels and restore their saved arrangement across launches. Layout persistence SHALL be separate from the profiling GUI. A one-row toolbar SHALL hold file-open actions, fixed-width basename labels, Swap, and Settings. Root skipping SHALL appear only in Settings and command-line arguments. Full file paths SHALL appear in the native window title and file-label tooltips. Loading and allocation status SHALL appear in a bottom bar.

#### Scenario: Alternative arrangement
- **WHEN** Base is docked above Comparer on the left and Diff occupies the right
- **THEN** each panel remains usable with its independent search footer and the arrangement is restored on reopening

#### Scenario: Long file paths
- **WHEN** captures are opened from long paths
- **THEN** the toolbar remains one row with clipped basename labels and full paths remain accessible from the title and hover tooltips

### Requirement: Comparison settings
The comparison window SHALL expose Settings from its toolbar and File menu, with a theme selector using the existing theme registry and a non-negative Skip root levels field. Theme selection SHALL update immediately and persist without accumulating DPI scaling. Changing root skipping SHALL rebuild both loaded captures through the shared comparison API.

#### Scenario: Change theme
- **WHEN** a user chooses a theme in comparison Settings
- **THEN** the workspace uses that theme immediately and restores it on the next launch

#### Scenario: Change root skipping
- **WHEN** a user changes Skip root levels in Settings with both captures selected
- **THEN** the trees rebuild with the requested number of outer frames omitted and live allocation totals remain conserved
