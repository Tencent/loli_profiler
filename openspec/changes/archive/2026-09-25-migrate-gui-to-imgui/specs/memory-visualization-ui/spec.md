## ADDED Requirements

### Requirement: Virtualized stacktrace tree
The system SHALL display the profiling call tree as an expandable tree/table using a flat-index representation with `ImGuiListClipper` (or equivalent) so that only visible rows are rendered per frame. The implementation SHALL NOT instantiate an ImGui widget hierarchy for the full tree per frame. Expansion state SHALL survive data updates, and filtering SHALL operate on the flat representation.

#### Scenario: Large profile stays interactive
- **WHEN** the user loads the reference sample `2026.09.14-11.24.23.loli` (~395 MB) and scrolls/expands the stacktrace tree
- **THEN** the UI remains interactive without multi-second frame stalls

#### Scenario: Expansion preserved on update
- **WHEN** the user has expanded nodes and new data arrives (live capture) or a filter is applied/cleared
- **THEN** previously expanded nodes remain expanded where still present

### Requirement: Timeline and fragmentation charts
The system SHALL render the allocation timeline and fragmentation charts as custom ImGui widgets (ImDrawList-based) with hover tooltips and zoom/pan interaction equivalent to the Qt versions.

#### Scenario: Chart hover tooltip
- **WHEN** the user hovers over the timeline chart
- **THEN** a tooltip shows the values at the hovered time point

#### Scenario: Chart zoom
- **WHEN** the user scrolls or drags on the chart
- **THEN** the chart zooms/pans the visible time range

### Requirement: Treemap view
The system SHALL render the memory treemap as a custom ImDrawList-based widget with hover highlighting and a tooltip showing the allocation path and size, equivalent to the Qt treemap graphics view.

#### Scenario: Treemap hover
- **WHEN** the user hovers a treemap cell
- **THEN** the cell highlights and a tooltip shows its path and size

### Requirement: Simplified screenshot preview
The system SHALL provide a screenshot panel that previews the captured device screenshot as an image (fit-to-window and 1:1 display), and SHALL NOT port the Qt screenshot view's extra annotation/scrubbing features.

#### Scenario: Preview screenshot
- **WHEN** a screenshot is captured or loaded from a record
- **THEN** the screenshot panel displays the image scaled to fit, with an option for 1:1 pixel display

### Requirement: Smaps overview
The system SHALL render smaps statistics and visualization panels equivalent in information content to the Qt smaps dialogs.

#### Scenario: View smaps stats
- **WHEN** smaps data is available
- **THEN** the smaps panel displays the per-section statistics table and summary visualization
