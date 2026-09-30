#include "captureconfig.h"

#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#else
#include <sys/stat.h>
#endif

namespace loli {

namespace {

std::string JoinList(const std::vector<std::string>& list) {
    // The Qt writer emits a trailing ',' after every entry (its loop writes
    // the separator when i != numLibs, which is always true) - reproduce.
    std::string out;
    for (const auto& item : list) {
        out += item;
        out += ',';
    }
    return out;
}

std::vector<std::string> SplitList(const std::string& csv) {
    std::vector<std::string> out;
    std::string item;
    for (char c : csv) {
        if (c == ',') {
            if (!item.empty())
                out.push_back(item);
            item.clear();
        } else {
            item += c;
        }
    }
    if (!item.empty())
        out.push_back(item);
    return out;
}

void WriteSettingsBlock(std::ostream& stream, const CaptureConfig& s) {
    stream << "threshold:" << s.threshold << "\n";
    stream << "whitelist:" << JoinList(s.whitelist) << "\n";
    stream << "blacklist:" << JoinList(s.blacklist) << "\n";
    stream << "mode:" << s.mode << "\n";
    stream << "build:" << s.build << "\n";
    stream << "type:" << s.type << "\n";
    stream << "arch:" << s.arch << "\n";
    stream << "compiler:" << s.compiler << "\n";
    stream << "hook:" << s.hook << "\n";
}

std::string DefaultConfigContent() {
    // CreateIfNoConfigFile parity (verbatim field values).
    std::ostringstream os;
    os << "threshold:256\n";
    os << "whitelist:libunity,libil2cpp\n";
    os << "blacklist:libloli,libart,libc++,libc,libcutils\n";
    os << "mode:strict\n";
    os << "build:default\n";
    os << "type:white list\n";
    os << "arch:armeabi-v7a\n";
    os << "compiler:gcc\n";
    os << "hook:malloc\n";
    return os.str();
}

} // namespace

std::string CaptureConfigFilePath() {
#ifdef _WIN32
    char appData[MAX_PATH] = {};
    if (SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, appData) != S_OK)
        return "loli3.conf";
    return std::string(appData) + "\\LoliProfiler\\loli3.conf";
#else
    const char* home = getenv("HOME");
    if (!home)
        return "loli3.conf";
    return std::string(home) + "/.local/share/LoliProfiler/loli3.conf";
#endif
}

CaptureConfig LoadCaptureConfig(SavedCaptureConfigs* savedOut) {
    if (savedOut)
        savedOut->entries.clear();

    const std::string path = CaptureConfigFilePath();
    {
        // Create with defaults when missing (CreateIfNoConfigFile parity).
        std::ifstream probe(path.c_str());
        if (!probe.good()) {
            const std::string dir = path.substr(0, path.find_last_of("\\/"));
            std::error_code error;
            std::filesystem::create_directories(dir, error); // best-effort
            std::ofstream create(path.c_str());
            if (create.good())
                create << DefaultConfigContent();
        }
    }

    std::ifstream in(path.c_str());
    if (!in.good())
        return CaptureConfig();

    CaptureConfig current;
    CaptureConfig* settings = &current;
    std::ostringstream buffer;
    buffer << in.rdbuf();
    std::istringstream stream(buffer.str());
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos)
            continue;
        const std::string key = line.substr(0, colon);
        const std::string value = line.substr(colon + 1);
        if (key == "threshold") {
            settings->threshold = atoi(value.c_str());
        } else if (key == "whitelist") {
            settings->whitelist = SplitList(value);
        } else if (key == "blacklist") {
            settings->blacklist = SplitList(value);
        } else if (key == "mode") {
            settings->mode = value;
        } else if (key == "build") {
            settings->build = value;
        } else if (key == "type") {
            settings->type = value;
        } else if (key == "arch") {
            settings->arch = value;
        } else if (key == "compiler") {
            settings->compiler = value;
        } else if (key == "hook") {
            settings->hook = value;
        } else if (key == "saved") {
            if (savedOut)
                savedOut->entries.emplace_back(value, CaptureConfig());
            settings = savedOut ? &savedOut->entries.back().second : nullptr;
        }
    }
    return current;
}

bool StoreCaptureConfig(const CaptureConfig& config, const SavedCaptureConfigs* saved) {
    const std::string path = CaptureConfigFilePath();
    const std::string dir = path.substr(0, path.find_last_of("\\/"));
    std::error_code error;
    std::filesystem::create_directories(dir, error);

    std::ofstream out(path.c_str());
    if (!out.good())
        return false;
    WriteSettingsBlock(out, config);
    if (saved) {
        for (const auto& entry : saved->entries) {
            out << "saved:" << entry.first << "\n";
            WriteSettingsBlock(out, entry.second);
        }
    }
    out.flush();
    return out.good();
}

bool IsNoStackMode() {
    return LoadCaptureConfig().mode == "nostack";
}

} // namespace loli
