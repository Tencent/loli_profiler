#ifndef LOLI_RUNTIMEPATHS_H
#define LOLI_RUNTIMEPATHS_H

#include <cstdlib>
#include <filesystem>

namespace loli {

// Immutable bundle resources and writable state must have separate locations.
inline bool IsMacBundleExecutableDirectory(const std::filesystem::path& directory) {
#ifdef __APPLE__
    return directory.filename() == "MacOS" &&
           directory.parent_path().filename() == "Contents" &&
           directory.parent_path().parent_path().extension() == ".app";
#else
    (void)directory;
    return false;
#endif
}

inline std::filesystem::path RuntimeDirectory(const std::filesystem::path& executableDirectory) {
    return IsMacBundleExecutableDirectory(executableDirectory)
        ? executableDirectory.parent_path() / "Resources" : executableDirectory;
}

inline std::filesystem::path StateDirectory(const std::filesystem::path& executableDirectory) {
    if (IsMacBundleExecutableDirectory(executableDirectory)) {
        if (const char* userHome = std::getenv("HOME")) {
            if (*userHome)
                return std::filesystem::path(userHome) / "Library" /
                       "Application Support" / "LoliProfiler";
        }
        return executableDirectory.parent_path().parent_path().parent_path();
    }
    return executableDirectory;
}

} // namespace loli
#endif
