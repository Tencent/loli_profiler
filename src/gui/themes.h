#ifndef LOLI_PROFILER_GUI_THEMES_H
#define LOLI_PROFILER_GUI_THEMES_H

// ImGui theme registry for the LoliProfiler GUI.
// Pure C++17, no Qt. Themes are applied through Dear ImGui only.

struct ImGuiTheme
{
    const char* name;
    void (*apply)();
};

// Returns the registry array and writes its element count to outCount (if non-null).
const ImGuiTheme* GetImGuiThemes(int* outCount);

// Applies the theme with the given name. Falls back to the first theme if not found.
void ApplyImGuiThemeByName(const char* name);

// Name of the default (first) theme in the registry.
const char* GetDefaultImGuiThemeName();

#endif // LOLI_PROFILER_GUI_THEMES_H
