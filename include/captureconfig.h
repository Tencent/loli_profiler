#ifndef CAPTURECONFIG_H
#define CAPTURECONFIG_H

#include <string>
#include <vector>

// Qt-free capture configuration (task 4.4): the loli3.conf store, replacing
// ConfigDialog's NO_GUI_MODE settings surface (ParseConfigFile /
// GetCurrentSettings / SetCurrentSettings / IsNoStackMode).
//
// Format compatibility: loli3.conf keeps its existing `key:value` text
// layout exactly - same keys (threshold, whitelist, blacklist, mode, build,
// type, arch, compiler, hook), same ':' separator, same ',' list separator
// (including the trailing-comma quirk the Qt writer emits), same
// "saved:<name>" prefixed blocks for named presets, and the same default
// file content when none exists.
//
// File location: Windows %APPDATA%/LoliProfiler/loli3.conf (matching the Qt
// build's QStandardPaths::AppDataLocation with the app name set to
// "LoliProfiler"); on POSIX ~/.local/share/LoliProfiler/loli3.conf.
namespace loli {

struct CaptureConfig {
    int threshold = 128;
    std::string mode = "strict";        // strict | nostack
    std::string build = "default";      // default | framepointer
    std::string type = "white list";    // white list | black list
    std::string arch = "armeabi-v7a";   // armeabi-v7a | arm64-v8a
    std::string compiler = "gcc";       // gcc | llvm
    std::string hook = "malloc";        // malloc | ...
    std::vector<std::string> whitelist;
    std::vector<std::string> blacklist;
};

// Named presets (the "saved:" blocks in loli3.conf).
struct SavedCaptureConfigs {
    std::vector<std::pair<std::string, CaptureConfig>> entries;

    const CaptureConfig* Find(const std::string& name) const {
        for (const auto& e : entries)
            if (e.first == name)
                return &e.second;
        return nullptr;
    }
};

// Reads %APPDATA%/LoliProfiler/loli3.conf (creating it with the default
// content if missing) and returns the current settings.
CaptureConfig LoadCaptureConfig(SavedCaptureConfigs* savedOut = nullptr);

// Writes current + saved settings back to loli3.conf.
bool StoreCaptureConfig(const CaptureConfig& config,
                        const SavedCaptureConfigs* saved = nullptr);

// Convenience matching ConfigDialog::IsNoStackMode.
bool IsNoStackMode();

// The config file path (for diagnostics/tests).
std::string CaptureConfigFilePath();

} // namespace loli

#endif // CAPTURECONFIG_H
