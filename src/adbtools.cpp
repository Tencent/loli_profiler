#include "adbtools.h"

#include <cctype>
#include <cstdio>

namespace loli {

namespace {

// Splits on runs of whitespace (QString::split(QRegularExpression("\\s+"),
// SkipEmptyParts) parity).
std::vector<std::string> SplitWhitespace(const std::string& line) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i])))
            ++i;
        const std::size_t start = i;
        while (i < line.size() && !std::isspace(static_cast<unsigned char>(line[i])))
            ++i;
        if (i > start)
            out.emplace_back(line, start, i - start);
    }
    return out;
}

std::vector<std::string> SplitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\n') {
            std::string line = text.substr(start, i - start);
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            lines.push_back(std::move(line));
            start = i + 1;
        }
    }
    if (start < text.size())
        lines.push_back(text.substr(start));
    return lines;
}

bool Contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

std::string ToLower(std::string s) {
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

bool ParseMemInfoDump(const std::string& output, const std::string& appName,
                      MemInfo& outInfo, std::string& outAppPid) {
    outInfo.Reset();
    const std::vector<std::string> lines = SplitLines(output);
    const std::string appHeader = "[" + appName + "]";
    bool foundAppName = false;
    int errorReadCount = 0;
    int readCount = 0;
    for (const auto& line : lines) {
        if (!foundAppName) {
            if (errorReadCount < 2) {
                // Qt checked case-sensitively: "error" and "No process found"
                if (Contains(ToLower(line), "error") || Contains(line, "No process found"))
                    return false;
            }
            errorReadCount++;
            if (Contains(line, appHeader)) {
                const auto list = SplitWhitespace(line);
                if (list.size() > 5)
                    outAppPid = list[4];
                foundAppName = true;
            }
        } else {
            if (readCount > 22)
                break;
            if (line.empty())
                continue;
            const auto list = SplitWhitespace(line);
            if (list.size() < 2)
                continue;
            const std::string& name = list[0];
            // Qt: list.at(2).toUInt() / 1024 (etc.) - toUInt returns 0 on
            // non-numeric input, /1024 truncates.
            auto cell = [&list](std::size_t idx) -> uint32_t {
                if (idx >= list.size())
                    return 0;
                return static_cast<uint32_t>(strtoul(list[idx].c_str(), nullptr, 10) / 1024);
            };
            if (name == "Native") {
                outInfo.NativeHeap = cell(2);
            } else if (name == "Gfx") {
                outInfo.GfxDev = cell(2);
            } else if (name == "EGL") {
                outInfo.EGLmtrack = cell(2);
            } else if (name == "GL") {
                outInfo.GLmtrack = cell(2);
            } else if (name == "Unknown") {
                outInfo.Unknown = cell(1);
            } else if (name == "TOTAL") {
                outInfo.Total = cell(1);
            }
            readCount++;
        }
    }
    return foundAppName;
}

bool MemInfoDumper::DumpAsync(const std::string& appName,
                              const std::string& subProcessName,
                              ResultHandler onFinished) {
    if (runner_.IsRunning())
        return false;

    std::string app = appName;
    if (!subProcessName.empty())
        app += ":" + subProcessName;

    std::vector<std::string> args;
    if (!deviceSerial_.empty())
        args.push_back("-s"), args.push_back(deviceSerial_);
    args.push_back("shell");
    args.push_back("dumpsys");
    args.push_back("meminfo");
    args.push_back("--package");
    args.push_back(app);

    runner_.SetProgram(adbPath_);
    runner_.SetArguments(args);
    return runner_.StartAsync([this, app, onFinished](int, ProcessRunner::ExitStatus) {
        const std::string output = runner_.ReadAllStdout();
        MemInfo info;
        std::string appPid;
        const bool ok = ParseMemInfoDump(output, app, info, appPid);
        if (onFinished)
            onFinished(ok, info, appPid);
    });
}

bool ScreenshotCapture::CaptureAsync(ResultHandler onFinished) {
    if (runner_.IsRunning())
        return false;

    std::vector<std::string> args;
    if (!deviceSerial_.empty())
        args.push_back("-s"), args.push_back(deviceSerial_);
    args.push_back("exec-out");
    args.push_back("screencap -p");

    runner_.SetProgram(adbPath_);
    runner_.SetArguments(args);
    return runner_.StartAsync([this, onFinished](int, ProcessRunner::ExitStatus) {
        const std::string bytes = runner_.ReadAllStdout();
        if (onFinished)
            onFinished(!bytes.empty(),
                      std::vector<uint8_t>(bytes.begin(), bytes.end()));
    });
}

size_t ResolveAddresses(const std::string& addr2linePath, const std::string& symbolFile,
                        const std::vector<std::string>& addresses,
                        std::unordered_map<std::string, std::string>& outMap) {
    ProcessRunner runner;
    runner.SetProgram(addr2linePath);
    std::vector<std::string> args = {"-f", "-C", "-e", symbolFile};
    args.insert(args.end(), addresses.begin(), addresses.end());
    runner.SetArguments(args);
    if (!runner.Start())
        return 0;
    if (!runner.WaitForFinished(30000))
        runner.Kill();

    const std::string output = runner.ReadAllStdout();
    const std::vector<std::string> lines = SplitLines(output);
    size_t index = 0;
    size_t convertedCount = 0;
    for (const auto& line : lines) {
        if (index % 2 != 0) {
            // even output lines store file & line info - skipped (Qt parity)
            index++;
            continue;
        }
        index++;
        if (!line.empty() && line[0] == '?')
            continue;
        if (line.empty())
            continue;
        if (convertedCount < addresses.size()) {
            outMap[addresses[convertedCount]] = line;
            convertedCount++;
        }
    }
    return convertedCount;
}

} // namespace loli
