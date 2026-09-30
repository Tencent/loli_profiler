// Comparison UX informed by leoin2012/loli_profiler's Loli Compare module
// (credited upstream to shuchangliu), reimplemented for the Qt-free shared API.
#include "comparetreestate.h"
#include "filedialogs.h"
#include "themes.h"
#include "appsettings.h"
#include "lolilogger.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui-SFML.h"
#include <SFML/Graphics.hpp>
#include <array>
#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <future>
#include <string>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <shellapi.h>
#endif

namespace {
struct Options {
    std::string base, comparison, output, screenshot;
    int skip = 0;
    bool smoke = false, help = false, smokeStacked = false, smokeEmpty = false;
};
bool Parse(const std::vector<std::string>& args, Options& options, std::string& error) {
    std::vector<std::string> positional;
    for (size_t i = 0; i < args.size(); ++i) {
        std::string key = args[i], value;
        const auto equal = key.find('=');
        if (equal != std::string::npos && key.rfind("--", 0) == 0) {
            value = key.substr(equal + 1); key.resize(equal);
        }
        if (key == "--help" || key == "-h") { options.help = true; continue; }
        if (key == "--smoke-test") { options.smoke = true; continue; }
        if (key == "--smoke-empty") { options.smoke = options.smokeEmpty = true; continue; }
        if (key == "--smoke-stacked-layout") { options.smokeStacked = true; continue; }
        if (key == "--base" || key == "--compare" || key == "--comparison" ||
            key == "--skip-root-levels" || key == "--out" || key == "--screenshot") {
            if (equal == std::string::npos) {
                if (++i == args.size() || args[i].rfind("--", 0) == 0) { error = "Missing value for " + key; return false; }
                value = args[i];
            }
            if (value.empty()) { error = "Missing value for " + key; return false; }
            if (key == "--base") options.base = value;
            else if (key == "--compare" || key == "--comparison") options.comparison = value;
            else if (key == "--out") options.output = value;
            else if (key == "--screenshot") options.screenshot = value;
            else {
                auto parsed = std::from_chars(value.data(), value.data() + value.size(), options.skip);
                if (parsed.ec != std::errc() || parsed.ptr != value.data() + value.size() || options.skip < 0) {
                    error = "--skip-root-levels must be a non-negative integer"; return false;
                }
            }
        } else if (!key.empty() && key[0] == '-') { error = "Unknown option: " + key; return false; }
        else positional.push_back(key);
    }
    if (!positional.empty()) {
        if (positional.size() > 2 || !options.base.empty() || !options.comparison.empty()) {
            error = "Use up to two positional files, or --base and --compare"; return false;
        }
        options.base = positional[0];
        if (positional.size() == 2) options.comparison = positional[1];
    }
    if (options.smokeEmpty && (!options.base.empty() || !options.comparison.empty())) {
        error = "Empty-window preview must not open capture files"; return false;
    }
    if (options.smoke && !options.smokeEmpty && (options.base.empty() || options.comparison.empty())) {
        error = "Smoke test requires both input files"; return false;
    }
    return true;
}
void ShowMessage(const std::string& message) {
#ifdef _WIN32
    const int size = MultiByteToWideChar(CP_UTF8, 0, message.c_str(), -1, nullptr, 0);
    std::wstring wide(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, message.c_str(), -1, &wide[0], size);
    MessageBoxW(nullptr, wide.c_str(), L"LoliProfiler Compare", MB_OK);
#else
    std::fprintf(stderr, "%s\n", message.c_str());
#endif
}
struct Loaded {
    loli::ComparisonResult result;
    std::string error;
    bool ok = false;
};

const char* kPanelNames[] = {"Base", "Comparer", "Diff"};

void BuildComparisonLayout(ImGuiID dockspace, bool stacked) {
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::DockBuilderRemoveNode(dockspace);
    ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodePos(dockspace, viewport->WorkPos);
    ImGui::DockBuilderSetNodeSize(dockspace, viewport->WorkSize);
    ImGuiID base, comparison, diff, remainder;
    if (stacked) {
        ImGui::DockBuilderSplitNode(dockspace, ImGuiDir_Left, 0.5f, &remainder, &diff);
        ImGui::DockBuilderSplitNode(remainder, ImGuiDir_Up, 0.5f, &base, &comparison);
    } else {
        ImGui::DockBuilderSplitNode(dockspace, ImGuiDir_Left, 1.0f / 3.0f, &base, &remainder);
        ImGui::DockBuilderSplitNode(remainder, ImGuiDir_Left, 0.5f, &comparison, &diff);
    }
    ImGui::DockBuilderDockWindow(kPanelNames[0], base);
    ImGui::DockBuilderDockWindow(kPanelNames[1], comparison);
    ImGui::DockBuilderDockWindow(kPanelNames[2], diff);
    ImGui::DockBuilderFinish(dockspace);
}

void DrawFileName(const char* id, const std::string& path, float width) {
    ImGui::PushID(id);
    const auto& style = ImGui::GetStyle();
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 size(width, ImGui::GetFrameHeight());
    const ImVec2 max(min.x + size.x, min.y + size.y);
    ImGui::InvisibleButton("##file", size);
    ImGui::GetWindowDrawList()->AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_FrameBg), style.FrameRounding);
    const std::string name = path.empty() ? "No file selected" : std::filesystem::u8path(path).filename().u8string();
    const ImVec2 textMin(min.x + style.FramePadding.x, min.y + style.FramePadding.y);
    const ImVec2 textMax(max.x - style.FramePadding.x, max.y - style.FramePadding.y);
    ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), textMin, textMax, textMax.x, name.c_str(), nullptr, nullptr);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", path.empty() ? "No file selected" : path.c_str());
    ImGui::PopID();
}

void UpdateTitle(sf::RenderWindow& window, const Options& options) {
    const std::string title = "LoliProfiler Compare | Base: " +
        (options.base.empty() ? "(none)" : options.base) + " | Comparer: " +
        (options.comparison.empty() ? "(none)" : options.comparison);
    window.setTitle(sf::String::fromUtf8(title.begin(), title.end()));
}
}

int main(int argc, char** argv) {
    std::vector<std::string> args;
#ifdef _WIN32
    int count = 0;
    auto wideArgs = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!wideArgs) return 1;
    for (int i = 1; i < count; ++i) {
        const int size = WideCharToMultiByte(CP_UTF8, 0, wideArgs[i], -1, nullptr, 0, nullptr, nullptr);
        std::string text(size, '\0');
        WideCharToMultiByte(CP_UTF8, 0, wideArgs[i], -1, &text[0], size, nullptr, nullptr);
        text.pop_back(); args.push_back(std::move(text));
    }
    LocalFree(wideArgs);
#else
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
#endif
    Options options;
    std::string error;
    if (!Parse(args, options, error)) {
        if (options.smoke || std::find(args.begin(), args.end(), "--smoke-test") != args.end())
            std::fprintf(stderr, "%s\n", error.c_str());
        else ShowMessage(error);
        return 1;
    }
    if (options.help) {
        ShowMessage("LoliProfilerCompare [base.loli comparison.loli]\n"
                    "LoliProfilerCompare --base base.loli --compare comparison.loli [--skip-root-levels N]\n"
                    "Without files, select Base and Comparer in the window.\n"
                    "--out diff.txt also saves the shared signed comparison report.");
        return 0;
    }
    auto& logger = loli::LoliLogger::Instance();
    logger.SetConsoleEnabled(false);
    sf::RenderWindow window(sf::VideoMode({1440, 900}), "LoliProfiler Compare");
    window.setVerticalSyncEnabled(true);
    if (!ImGui::SFML::Init(window)) return 1;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
    AppSettings settings;
    std::string currentTheme = settings.Get("theme", GetDefaultImGuiThemeName());
    const std::string layoutPath = (std::filesystem::u8path(settings.GetFilePath()).parent_path() / "loli_compare_imgui.ini").u8string();
    ImGui::GetIO().IniFilename = options.smoke ? nullptr : layoutPath.c_str();
    float dpi = 1.0f;
#ifdef _WIN32
    dpi = float(GetDpiForSystem()) / 96.0f;
    ImGui::GetIO().Fonts->Clear();
    ImFont* font = ImGui::GetIO().Fonts->AddFontFromFileTTF("C:/Windows/Fonts/segoeui.ttf", 14.0f * dpi);
    if (!font) font = ImGui::GetIO().Fonts->AddFontDefault();
    ImGui::GetIO().FontDefault = font;
    (void)ImGui::SFML::UpdateFontTexture();
#endif
    auto applyTheme = [&](const std::string& theme) {
        ImGui::GetStyle() = ImGuiStyle();
        ApplyImGuiThemeByName(theme.c_str());
        ImGui::GetStyle().ScaleAllSizes(dpi);
    };
    applyTheme(currentTheme);
    UpdateTitle(window, options);
    FileDialogs::Init();
    loli::ComparisonResult result;
    std::array<gui::CompareTreeState, 3> tabs;
    const std::array<loli::ComparisonView, 3> views = {
        loli::ComparisonView::Base, loli::ComparisonView::Comparison, loli::ComparisonView::Diff};
    for (int i = 0; i < 3; ++i) tabs[i].Reset(result, views[i]);
    std::atomic<int> progress{0};
    // Future destruction waits for the worker before its progress sink dies.
    std::future<Loaded> loading;
    bool ready = false, requestLoad = !options.base.empty() && !options.comparison.empty();
    bool firstFrame = true, resetLayout = false, stackedLayout = options.smokeStacked;
    bool showSettings = false;
    int smokeFrames = 0, exitCode = 0;
    sf::Clock frameClock, totalClock;
    auto choose = [&](bool base) {
        if (auto path = FileDialogs::OpenFile({{"Loli capture", "loli"}})) {
            (base ? options.base : options.comparison) = *path;
            // Invalidate immediately so an old result cannot be exported under new input labels.
            ready = false; result = {};
            for (int i = 0; i < 3; ++i) tabs[i].Reset(result, views[i]);
            error.clear();
            requestLoad = !options.base.empty() && !options.comparison.empty();
            UpdateTitle(window, options);
        }
        ImGui::GetIO().ClearInputKeys();
    };
    while (window.isOpen()) {
        while (auto event = window.pollEvent()) {
            ImGui::SFML::ProcessEvent(window, *event);
            if (event->is<sf::Event::Closed>()) window.close();
        }
        if (!window.isOpen()) break;
        ImGui::SFML::Update(window, frameClock.restart());
        bool busy = loading.valid();
        if (busy && loading.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            auto loaded = loading.get();
            busy = false;
            ready = loaded.ok;
            error = std::move(loaded.error);
            if (ready) {
                result = std::move(loaded.result);
                for (int i = 0; i < 3; ++i) tabs[i].Reset(result, views[i]);
                if (!options.output.empty() && !loli::WriteComparisonReport(result, options.output, error) && options.smoke)
                    loaded.ok = false;
                LOLI_INFO("compare") << "base_bytes=" << result.base.bytes << " comparison_bytes=" << result.comparison.bytes
                                     << " nodes=" << result.nodes.size();
            }
            if (!loaded.ok && options.smoke) { exitCode = 1; break; }
        }
        if (requestLoad && !busy) {
            requestLoad = false; ready = false; error.clear(); progress = 0;
            const auto base = options.base, comparison = options.comparison;
            const int skip = options.skip;
            loading = std::async(std::launch::async, [base, comparison, skip, &progress]() {
                Loaded loaded;
                loaded.ok = loli::CompareFiles(base, comparison, loaded.result, loaded.error, skip,
                    [&progress](const std::string&) { ++progress; });
                return loaded;
            });
            busy = true;
        }
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Open Base...", nullptr, false, !busy)) choose(true);
                if (ImGui::MenuItem("Open Comparer...", nullptr, false, !busy)) choose(false);
                if (ImGui::MenuItem("Export Diff...", nullptr, false, ready && !busy)) {
                    if (auto path = FileDialogs::SaveFile({{"Comparison report", "txt"}})) {
                        if (std::filesystem::u8path(*path).extension().empty()) *path += ".txt";
                        loli::WriteComparisonReport(result, *path, error);
                    }
                    ImGui::GetIO().ClearInputKeys();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Settings...")) showSettings = true;
                ImGui::Separator();
                if (ImGui::MenuItem("Exit")) window.close();
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Window")) {
                if (ImGui::MenuItem("Three columns")) { resetLayout = true; stackedLayout = false; }
                if (ImGui::MenuItem("Base above Comparer, Diff right")) { resetLayout = true; stackedLayout = true; }
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }
        const float barHeight = ImGui::GetFrameHeight() + 2.0f * ImGui::GetStyle().WindowPadding.y;
        if (ImGui::BeginViewportSideBar("##CompareToolbar", ImGui::GetMainViewport(), ImGuiDir_Up, barHeight,
                                       ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings)) {
            ImGui::BeginDisabled(busy);
            if (ImGui::Button("Open Base...")) choose(true);
            ImGui::SameLine();
            DrawFileName("basefile", options.base, 200.0f * dpi);
            ImGui::SameLine();
            if (ImGui::Button("Open Comparer...")) choose(false);
            ImGui::SameLine();
            DrawFileName("comparisonfile", options.comparison, 200.0f * dpi);
            ImGui::SameLine();
            if (ImGui::Button("Swap")) {
                std::swap(options.base, options.comparison); ready = false;
                requestLoad = !options.base.empty() && !options.comparison.empty();
                UpdateTitle(window, options);
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Settings")) showSettings = true;
        }
        ImGui::End();

        if (ImGui::BeginViewportSideBar("##CompareStatus", ImGui::GetMainViewport(), ImGuiDir_Down,
                                       ImGui::GetFrameHeightWithSpacing(), ImGuiWindowFlags_NoScrollbar |
                                       ImGuiWindowFlags_NoSavedSettings)) {
            if (!error.empty()) {
                ImGui::TextUnformatted("Comparison error (hover for details)");
                ImGui::SetItemTooltip("%s", error.c_str());
            } else if (busy) {
                const char* stages[] = {"Preparing...", "Opening base...", "Building base tree...", "Opening comparer...",
                                        "Building comparer tree...", "Computing signed differences..."};
                ImGui::TextUnformatted(stages[std::min(5, progress.load())]);
            } else if (ready) {
                ImGui::Text("Base: %s (%lld allocs)  |  Comparer: %s (%lld allocs)  |  Diff: %s (%+lld allocs)",
                    loli::FormatComparisonBytes(result.base.bytes).c_str(), (long long)result.base.count,
                    loli::FormatComparisonBytes(result.comparison.bytes).c_str(), (long long)result.comparison.count,
                    loli::FormatComparisonBytes(result.comparison.bytes - result.base.bytes, true).c_str(),
                    (long long)(result.comparison.count - result.base.count));
            } else {
                ImGui::Text("Base: %s  |  Comparer: %s",
                    options.base.empty() ? "not opened" : "selected",
                    options.comparison.empty() ? "not opened" : "selected");
            }
        }
        ImGui::End();

        const ImGuiID dockspace = ImHashStr("CompareDockSpace");
        const bool hasSavedDockspace = ImGui::DockBuilderGetNode(dockspace) != nullptr;
        ImGui::DockSpaceOverViewport(dockspace, nullptr, ImGuiDockNodeFlags_None);
        if (resetLayout || (firstFrame && (options.smoke || !hasSavedDockspace))) {
            BuildComparisonLayout(dockspace, stackedLayout);
            resetLayout = false;
        }
        firstFrame = false;
        for (int i = 0; i < 3; ++i) {
            if (ready && options.smoke && smokeFrames == i * 12) {
                // Search/reveal in each panel without sharing its selection state.
                for (const auto& node : result.nodes) if (node.Present(views[i]) && node.children.empty()) {
                    tabs[i].Search(node.function); tabs[i].NextMatch(1); break;
                }
            }
            if (ImGui::Begin(kPanelNames[i], nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
                if (busy) ImGui::TextUnformatted("Loading capture trees...");
                else {
                    ImGui::BeginDisabled(!ready);
                    gui::DrawComparisonTree(result, views[i], tabs[i], ready ? nullptr : "Open both captures to compare.");
                    ImGui::EndDisabled();
                }
            }
            ImGui::End();
        }
        if (options.smoke && (ready || options.smokeEmpty)) ++smokeFrames;

        if (showSettings) {
            ImGui::OpenPopup("Comparison Settings");
            showSettings = false;
        }
        if (ImGui::BeginPopupModal("Comparison Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::SeparatorText("Theme");
            int themeCount = 0;
            const auto* themes = GetImGuiThemes(&themeCount);
            ImGui::SetNextItemWidth(250.0f * dpi);
            if (ImGui::BeginCombo("##theme", currentTheme.c_str())) {
                for (int i = 0; i < themeCount; ++i) {
                    const bool selected = currentTheme == themes[i].name;
                    if (ImGui::Selectable(themes[i].name, selected)) {
                        currentTheme = themes[i].name;
                        applyTheme(currentTheme);
                        // Read fresh settings to retain preferences changed by the main app.
                        AppSettings updatedSettings;
                        updatedSettings.Set("theme", currentTheme);
                        updatedSettings.Sync();
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::SeparatorText("Comparison");
            ImGui::BeginDisabled(busy);
            ImGui::SetNextItemWidth(140.0f * dpi);
            if (ImGui::InputInt("Skip root levels", &options.skip)) {
                options.skip = std::max(0, options.skip);
                ready = false;
                requestLoad = !options.base.empty() && !options.comparison.empty();
            }
            ImGui::SetItemTooltip("Skip this many outer stack frames in both captures. Rebuilds the comparison.");
            ImGui::EndDisabled();
            ImGui::Separator();
            if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        window.clear(sf::Color(25, 25, 25));
        ImGui::SFML::Render(window);
        window.display();
        if (options.smoke && smokeFrames >= 36) {
            for (const auto& tab : tabs) if (!tab.Matches().empty() && tab.selected < 0) exitCode = 1;
            const auto* basePanel = ImGui::FindWindowByName(kPanelNames[0]);
            const auto* comparerPanel = ImGui::FindWindowByName(kPanelNames[1]);
            const auto* diffPanel = ImGui::FindWindowByName(kPanelNames[2]);
            if (!basePanel || !comparerPanel || !diffPanel ||
                !basePanel->DockId || !comparerPanel->DockId || !diffPanel->DockId) exitCode = 1;
            else if (options.smokeStacked) {
                if (basePanel->Pos.x != comparerPanel->Pos.x || basePanel->Pos.y >= comparerPanel->Pos.y ||
                    diffPanel->Pos.x <= basePanel->Pos.x || diffPanel->Size.y <= basePanel->Size.y) exitCode = 1;
            } else if (basePanel->Pos.x >= comparerPanel->Pos.x || comparerPanel->Pos.x >= diffPanel->Pos.x ||
                       basePanel->Pos.y != comparerPanel->Pos.y || comparerPanel->Pos.y != diffPanel->Pos.y) exitCode = 1;
            if (!options.screenshot.empty()) {
                sf::Texture texture;
                if (!texture.resize(window.getSize())) exitCode = 1;
                else {
                    texture.update(window);
                    if (!texture.copyToImage().saveToFile(std::filesystem::u8path(options.screenshot))) exitCode = 1;
                }
            }
            window.close();
        }
        if (options.smoke && totalClock.getElapsedTime().asSeconds() > 180) { exitCode = 1; window.close(); }
    }
    FileDialogs::Shutdown();
    ImGui::SFML::Shutdown();
    return exitCode;
}
