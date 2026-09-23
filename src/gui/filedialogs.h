#ifndef LOLI_PROFILER_GUI_FILEDIALOGS_H
#define LOLI_PROFILER_GUI_FILEDIALOGS_H

// Thin C++17 wrapper around nativefiledialog-extended (NFD).
// Pure C++17, no Qt. Only dependency is nfd.h and the C++ standard library.

#include <optional>
#include <string>
#include <vector>

namespace FileDialogs
{

// A single file-type filter, e.g. { "Loli Record", "loli" }.
// `spec` is a comma-separated list of extensions and/or wildcard patterns,
// without the leading dot (e.g. "png,jpg" or "*").
struct FileFilter
{
    std::string name;
    std::string spec;
};

// Initializes NFD (NFD_Init). Idempotent: multiple calls are safe.
void Init();

// De-initializes NFD (NFD_Quit). Idempotent-safe.
void Shutdown();

// Shows a single-file open dialog. Returns the chosen path, or std::nullopt
// if the user cancelled (or on error; the error is logged to stderr).
std::optional<std::string> OpenFile(const std::vector<FileFilter>& filters = {},
                                    const std::string& defaultPath = "");

// Shows a single-file save dialog. Returns the chosen path, or std::nullopt
// if the user cancelled (or on error; the error is logged to stderr).
std::optional<std::string> SaveFile(const std::vector<FileFilter>& filters = {},
                                    const std::string& defaultPath = "");

// Shows a folder-pick dialog. Returns the chosen path, or std::nullopt
// if the user cancelled (or on error; the error is logged to stderr).
std::optional<std::string> PickFolder(const std::string& defaultPath = "");

} // namespace FileDialogs

#endif // LOLI_PROFILER_GUI_FILEDIALOGS_H
