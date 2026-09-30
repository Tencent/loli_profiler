#include "appsettings.h"
#include "runtimepaths.h"

#include <cstdio>
#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <filesystem>
#else
#include <unistd.h>
#endif
#include <fstream>
#include <sstream>

#include <rapidjson/document.h>
#include <rapidjson/ostreamwrapper.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/istreamwrapper.h>

namespace {

// Portable binaries keep settings beside the executable; app bundles use user state.
constexpr const char* kSettingsFileName = "loli_settings.json";

std::string GetExecutableDir() {
#ifdef _WIN32
    char path[MAX_PATH * 2] = {};
    const DWORD len = GetModuleFileNameA(nullptr, path, sizeof(path) - 1);
    if (len == 0 || len >= sizeof(path) - 1)
        return ".";
    std::string full(path, len);
    const std::size_t slash = full.find_last_of("\\/");
    return slash == std::string::npos ? "." : full.substr(0, slash);
#elif defined(__APPLE__)
    uint32_t length = 0;
    _NSGetExecutablePath(nullptr, &length);
    std::string path(length, '\0');
    if (_NSGetExecutablePath(path.data(), &length) != 0)
        return ".";
    return std::filesystem::weakly_canonical(path.c_str()).parent_path().string();
#else
    char path[4096] = {};
    const ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (len <= 0)
        return ".";
    std::string full(path, static_cast<std::size_t>(len));
    const std::size_t slash = full.find_last_of('/');
    return slash == std::string::npos ? "." : full.substr(0, slash);
#endif
}

} // namespace

AppSettings::AppSettings()
    : AppSettings((loli::StateDirectory(GetExecutableDir()) / kSettingsFileName).string()) {
    // Preserve settings from earlier macOS builds that wrote inside the bundle.
    const auto legacy = std::filesystem::path(GetExecutableDir()) / kSettingsFileName;
    if (legacy != filePath_ && !std::filesystem::exists(filePath_)) {
        AppSettings previous(legacy.string());
        values_ = previous.values_;
        auto logPath = values_.find("DiagnosticLogPath");
        if (logPath != values_.end() &&
            logPath->second == (legacy.parent_path() / "loli_gui.log").string())
            logPath->second = (std::filesystem::path(filePath_).parent_path() / "loli_gui.log").string();
        dirty_ = !values_.empty();
    }
}

AppSettings::AppSettings(const std::string& filePath)
    : filePath_(filePath) {
    Load();
}

AppSettings::~AppSettings() {
    Sync();
}

void AppSettings::Load() {
    values_.clear();
    std::ifstream in(filePath_.c_str());
    if (!in.good())
        return; // no settings yet - defaults apply

    rapidjson::IStreamWrapper inWrapper(in);
    rapidjson::Document doc;
    doc.ParseStream(inWrapper);
    if (doc.HasParseError() || !doc.IsObject())
        return; // unreadable settings fall back to defaults, not a crash

    for (auto it = doc.MemberBegin(); it != doc.MemberEnd(); ++it) {
        if (it->value.IsString())
            values_[it->name.GetString()] = it->value.GetString();
    }
}

std::string AppSettings::Get(const std::string& key, const std::string& fallback) const {
    const auto it = values_.find(key);
    if (it == values_.end() || it->second.empty())
        return fallback;
    return it->second;
}

void AppSettings::Set(const std::string& key, const std::string& value) {
    if (values_[key] == value)
        return;
    values_[key] = value;
    dirty_ = true;
}

bool AppSettings::Has(const std::string& key) const {
    return values_.find(key) != values_.end();
}

void AppSettings::Remove(const std::string& key) {
    if (values_.erase(key) > 0)
        dirty_ = true;
}

void AppSettings::Sync() {
    if (!dirty_)
        return;

    rapidjson::Document doc;
    doc.SetObject();
    rapidjson::Document::AllocatorType& allocator = doc.GetAllocator();
    for (const auto& kv : values_) {
        doc.AddMember(rapidjson::Value(kv.first.c_str(), static_cast<rapidjson::SizeType>(kv.first.size()), allocator),
                      rapidjson::Value(kv.second.c_str(), static_cast<rapidjson::SizeType>(kv.second.size()), allocator),
                      allocator);
    }

    std::error_code error;
    const auto directory = std::filesystem::path(filePath_).parent_path();
    if (!directory.empty())
        std::filesystem::create_directories(directory, error);
    std::ofstream out(filePath_.c_str());
    if (!out.good())
        return; // best-effort persistence (QSettings parity: silent failure)
    rapidjson::OStreamWrapper outWrapper(out);
    rapidjson::PrettyWriter<rapidjson::OStreamWrapper> writer(outWrapper);
    doc.Accept(writer);
    dirty_ = false;
}
