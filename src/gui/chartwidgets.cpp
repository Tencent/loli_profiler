// ImDrawList-based visualization widgets for the ImGui GUI.

#include "chartwidgets.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace gui {

namespace {

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

// Formats bytes into a human-readable string (up to "1023.9 KB").
// Duplicated here (no Qt, no cross-file dependency on main_imgui.cpp).
std::string FormatBytes(uint64_t bytes) {
    static const char* units[] = {"B", "KB", "MB", "GB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 3) {
        value /= 1024.0;
        ++unit;
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), unit == 0 ? "%.0f %s" : "%.1f %s", value, units[unit]);
    return buf;
}

// ---------------------------------------------------------------------------
// Timeline chart
// ---------------------------------------------------------------------------

struct TimelineSeries {
    const char* name;
    ImU32 color;
    uint32_t MemInfoSample::*member;
};

constexpr TimelineSeries kTimelineSeries[] = {
    {"Total",       IM_COL32(245, 245, 245, 255), &MemInfoSample::total},
    {"Native Heap", IM_COL32( 80, 200, 120, 255), &MemInfoSample::nativeHeap},
    {"Gfx Dev",     IM_COL32( 90, 150, 240, 255), &MemInfoSample::gfxDev},
    {"EGL mtrack",  IM_COL32(240, 160,  60, 255), &MemInfoSample::eglMtrack},
    {"GL mtrack",   IM_COL32(170, 110, 220, 255), &MemInfoSample::glMtrack},
    {"Unknown",     IM_COL32(150, 150, 150, 255), &MemInfoSample::unknown},
};
constexpr int kTimelineSeriesCount = IM_ARRAYSIZE(kTimelineSeries);

double SampleValue(const MemInfoSample& s, int seriesIdx) {
    return static_cast<double>(s.*kTimelineSeries[seriesIdx].member);
}

double NiceStep(double rawStep) {
    const double mag = std::pow(10.0, std::floor(std::log10(rawStep)));
    const double norm = rawStep / mag;
    double nice = 1.0;
    if (norm <= 1.0)      nice = 1.0;
    else if (norm <= 2.0) nice = 2.0;
    else if (norm <= 5.0) nice = 5.0;
    else                  nice = 10.0;
    return nice * mag;
}

// ---------------------------------------------------------------------------
// Smaps stacked bar
// ---------------------------------------------------------------------------

struct SmapsBarEntry {
    const SMapsSectionSnapshot* section;
    uint64_t pss = 0;
};

void DrawSmapsStackedBar(const std::vector<SMapsSectionSnapshot>& sections) {
    std::vector<SmapsBarEntry> entries;
    entries.reserve(sections.size());
    uint64_t totalPss = 0;
    for (const auto& s : sections) {
        entries.push_back({&s, s.pss});
        totalPss += s.pss;
    }
    std::sort(entries.begin(), entries.end(),
              [](const SmapsBarEntry& a, const SmapsBarEntry& b) {
                  return a.pss > b.pss;
              });

    constexpr int kTopN = 10;
    if (static_cast<int>(entries.size()) > kTopN) {
        uint64_t otherPss = 0;
        for (size_t i = kTopN; i < entries.size(); ++i)
            otherPss += entries[i].pss;
        entries.resize(kTopN);
        entries.push_back({nullptr, otherPss});
    }
    if (entries.empty() || totalPss == 0)
        return;

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float height = 26.0f;
    const float width = std::max(avail.x, 60.0f);
    const ImVec2 bbMin = pos;
    const ImVec2 bbMax(pos.x + width, pos.y + height);
    ImGui::Dummy(ImVec2(width, height));

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRect(bbMin, bbMax, IM_COL32(120, 120, 120, 255));

    float x = bbMin.x;
    for (size_t i = 0; i < entries.size(); ++i) {
        const float frac = static_cast<float>(static_cast<double>(entries[i].pss) /
                                              static_cast<double>(totalPss));
        const float w = std::max(frac * width, 1.0f);
        const ImU32 col = IM_COL32(static_cast<int>(60 + (i * 37) % 180),
                                   static_cast<int>(60 + (i * 67) % 180),
                                   static_cast<int>(60 + (i * 97) % 180), 255);
        if (x < bbMax.x)
            dl->AddRectFilled(ImVec2(x, bbMin.y), ImVec2(std::min(x + w, bbMax.x), bbMax.y), col);
        if (ImGui::IsMouseHoveringRect(ImVec2(x, bbMin.y),
                                       ImVec2(std::min(x + w, bbMax.x), bbMax.y))) {
            const char* name = entries[i].section ? entries[i].section->name.c_str() : "Other";
            ImGui::SetTooltip("%s\nPSS: %s (%.1f%%)", name,
                              FormatBytes(entries[i].pss).c_str(), frac * 100.0f);
        }
        x += w;
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Timeline chart
// ---------------------------------------------------------------------------

void DrawMemoryTimelineChart(const GuiSnapshot& snapshot, TimelineView& view) {
    if (snapshot.memTimeline.empty()) {
        ImGui::TextUnformatted("No memory timeline data.");
        return;
    }

    const auto& samples = snapshot.memTimeline;
    const int n = static_cast<int>(samples.size());
    const double t0 = samples.front().timeMs;
    const double t1 = samples.back().timeMs;
    if (n < 2 || t1 <= t0) {
        ImGui::TextUnformatted("Not enough timeline samples.");
        return;
    }

    // Compute the data range and max value once per frame.
    double maxVal = 0.0;
    for (const auto& s : samples) {
        for (int k = 0; k < kTimelineSeriesCount; ++k)
            maxVal = std::max(maxVal, SampleValue(s, k));
    }
    if (maxVal <= 0.0)
        maxVal = 1.0;

    // Resolve view range: auto-fit until the user zooms/pans.
    if (!view.userZoomed) {
        view.zoomMin = t0;
        view.zoomMax = t1;
    }
    view.zoomMin = std::max(view.zoomMin, t0);
    view.zoomMax = std::min(view.zoomMax, t1);
    if (view.zoomMax - view.zoomMin < 1.0) {
        view.zoomMax = std::min(view.zoomMin + 1.0, t1);
        view.zoomMin = std::max(view.zoomMax - 1.0, t0);
    }
    const double vmin = view.zoomMin;
    const double vmax = view.zoomMax;
    const double vspan = vmax - vmin;

    // Header line: sample count + view info + reset button.
    ImGui::Text("Samples: %zu", samples.size());
    ImGui::SameLine();
    ImGui::TextDisabled("View: %.1fs - %.1fs", vmin / 1000.0, vmax / 1000.0);
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset View")) {
        view.userZoomed = false;
        view.zoomMin = t0;
        view.zoomMax = t1;
    }

    // Legend with colored markers.
    for (int k = 0; k < kTimelineSeriesCount; ++k) {
        if (k > 0)
            ImGui::SameLine();
        ImGui::TextColored(ImColor(kTimelineSeries[k].color), "%s", "\xE2\x96\xA0"); // small square
        ImGui::SameLine(0.0f, 2.0f);
        ImGui::TextUnformatted(kTimelineSeries[k].name);
    }

    // Plot geometry.
    const float labelW = 76.0f;  // left gutter for Y labels
    const float labelH = 20.0f;  // bottom gutter for X labels
    ImVec2 avail = ImGui::GetContentRegionAvail();
    avail.x = std::max(avail.x, labelW + 40.0f);
    avail.y = std::max(avail.y, labelH + 40.0f);

    ImGui::InvisibleButton("##timeline_plot", avail);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const ImVec2 pMin = ImGui::GetItemRectMin();
    const ImVec2 pMax = ImGui::GetItemRectMax();
    const float plotW = pMax.x - pMin.x - labelW;
    const float plotH = pMax.y - pMin.y - labelH;
    const ImVec2 plotMin(pMin.x + labelW, pMin.y);
    const ImVec2 plotMax(pMin.x + labelW + plotW, pMin.y + plotH);
    if (plotW < 10.0f || plotH < 10.0f)
        return;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(plotMin, plotMax, true);
    dl->AddRectFilled(plotMin, plotMax, IM_COL32(25, 28, 34, 255));

    const ImU32 gridCol = IM_COL32(255, 255, 255, 28);
    const ImU32 axisCol = IM_COL32(255, 255, 255, 70);

    // Background gridlines.
    for (int i = 1; i < 5; ++i) {
        const float y = plotMin.y + plotH * static_cast<float>(i) / 5.0f;
        dl->AddLine(ImVec2(plotMin.x, y), ImVec2(plotMax.x, y), gridCol);
    }
    for (int i = 1; i < 8; ++i) {
        const float x = plotMin.x + plotW * static_cast<float>(i) / 8.0f;
        dl->AddLine(ImVec2(x, plotMin.y), ImVec2(x, plotMax.y), gridCol);
    }
    dl->AddRect(plotMin, plotMax, axisCol);

    const auto valueToY = [&](double v) {
        return plotMax.y - static_cast<float>(v / maxVal) * plotH;
    };
    const auto timeToX = [&](double t) {
        return plotMin.x + static_cast<float>((t - vmin) / vspan) * plotW;
    };

    // Y axis ticks with byte-formatted labels.
    {
        const double rawStep = maxVal / 4.0;
        const double step = NiceStep(rawStep);
        char buf[64];
        for (double v = step; v <= maxVal; v += step) {
            const float y = valueToY(v);
            dl->AddLine(ImVec2(plotMin.x, y), ImVec2(plotMax.x, y), gridCol);
            std::snprintf(buf, sizeof(buf), "%s", FormatBytes(static_cast<uint64_t>(v)).c_str());
            dl->AddText(ImVec2(pMin.x + 2.0f, y - 6.0f), IM_COL32(200, 200, 200, 255), buf);
        }
        dl->AddText(ImVec2(pMin.x + 2.0f, plotMin.y), IM_COL32(200, 200, 200, 255),
                    FormatBytes(static_cast<uint64_t>(maxVal)).c_str());
    }

    // X axis ticks with second-formatted labels.
    {
        const double rawStep = vspan / 6.0;
        const double step = NiceStep(rawStep);
        char buf[64];
        const double start = std::ceil(vmin / step) * step;
        for (double t = start; t <= vmax; t += step) {
            const float x = timeToX(t);
            dl->AddLine(ImVec2(x, plotMin.y), ImVec2(x, plotMax.y), gridCol);
            std::snprintf(buf, sizeof(buf), "%.0fs", t / 1000.0);
            dl->AddText(ImVec2(x + 2.0f, plotMax.y + 3.0f), IM_COL32(200, 200, 200, 255), buf);
        }
    }

    // One polyline per series (NaN gaps between outside-range segments).
    for (int k = 0; k < kTimelineSeriesCount; ++k) {
        const ImU32 col = kTimelineSeries[k].color;
        bool pen = false;
        ImVec2 prev(0.0f, 0.0f);
        for (int i = 0; i < n; ++i) {
            const double t = samples[i].timeMs;
            const float x = static_cast<float>(timeToX(t));
            const float y = static_cast<float>(valueToY(SampleValue(samples[i], k)));
            const bool inside = t >= vmin && t <= vmax;
            if (inside) {
                if (pen && prev.x <= plotMax.x + 1.0f)
                    dl->AddLine(prev, ImVec2(x, y), col, 1.5f);
                pen = true;
                prev = ImVec2(x, y);
            } else {
                pen = false;
            }
        }
    }

    // Hover: nearest sample tooltip.
    if (hovered) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const double tAt = vmin + static_cast<double>(mouse.x - plotMin.x) / plotW * vspan;
        int best = 0;
        double bestDist = 1e30;
        for (int i = 0; i < n; ++i) {
            const double d = std::fabs(samples[i].timeMs - tAt);
            if (d < bestDist) {
                bestDist = d;
                best = i;
            }
        }
        const float cx = timeToX(samples[best].timeMs);
        dl->AddLine(ImVec2(cx, plotMin.y), ImVec2(cx, plotMax.y), IM_COL32(255, 255, 255, 90));

        ImGui::BeginTooltip();
        ImGui::Text("Time: %.2fs", samples[best].timeMs / 1000.0);
        for (int k = 0; k < kTimelineSeriesCount; ++k) {
            ImGui::TextColored(ImColor(kTimelineSeries[k].color), "%s:", kTimelineSeries[k].name);
            ImGui::SameLine();
            ImGui::Text("%s", FormatBytes(samples[best].*kTimelineSeries[k].member).c_str());
        }
        ImGui::EndTooltip();
    }

    dl->PopClipRect();

    // Zoom: mouse wheel around cursor.
    if (hovered) {
        const float wheel = ImGui::GetIO().MouseWheel;
        if (std::fabs(wheel) > 0.001f) {
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            const double anchor = vmin + static_cast<double>(mouse.x - plotMin.x) / plotW * vspan;
            const double scale = std::pow(0.85, static_cast<double>(wheel));
            const double minSpan = std::max((t1 - t0) / 1000.0, 1.0);
            double newMin = anchor - (anchor - vmin) * scale;
            double newMax = anchor + (vmax - anchor) * scale;
            if (newMax - newMin < minSpan) {
                const double mid = (newMin + newMax) * 0.5;
                newMin = mid - minSpan * 0.5;
                newMax = mid + minSpan * 0.5;
            }
            view.zoomMin = std::max(newMin, t0);
            view.zoomMax = std::min(newMax, t1);
            view.userZoomed = true;
        }
    }

    // Pan: right or middle mouse drag.
    if (hovered) {
        const bool rmb = ImGui::IsMouseDragging(ImGuiMouseButton_Right);
        const bool mmb = ImGui::IsMouseDragging(ImGuiMouseButton_Middle);
        if (rmb || mmb) {
            const float dx = ImGui::GetIO().MouseDelta.x;
            if (std::fabs(dx) > 0.0f) {
                const double dt = -static_cast<double>(dx) / plotW * vspan;
                double newMin = vmin + dt;
                double newMax = vmax + dt;
                if (newMin < t0) {
                    newMax += t0 - newMin;
                    newMin = t0;
                }
                if (newMax > t1) {
                    newMin -= newMax - t1;
                    newMax = t1;
                }
                newMin = std::max(newMin, t0);
                newMax = std::min(newMax, t1);
                view.zoomMin = newMin;
                view.zoomMax = newMax;
                view.userZoomed = true;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Smaps panel
// ---------------------------------------------------------------------------

void DrawSmapsPanel(const GuiSnapshot& snapshot) {
    if (snapshot.smaps.empty()) {
        ImGui::TextUnformatted("No smaps data.");
        return;
    }

    // Stacked bar of top-N sections by PSS.
    DrawSmapsStackedBar(snapshot.smaps);
    ImGui::Spacing();

    const ImGuiTableFlags flags =
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable | ImGuiTableFlags_SizingFixedFit;
    if (!ImGui::BeginTable("smaps_table", 8, flags)) {
        return;
    }
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Virtual", ImGuiTableColumnFlags_DefaultSort);
    ImGui::TableSetupColumn("RSS");
    ImGui::TableSetupColumn("PSS");
    ImGui::TableSetupColumn("SharedClean");
    ImGui::TableSetupColumn("SharedDirty");
    ImGui::TableSetupColumn("PrivateClean");
    ImGui::TableSetupColumn("PrivateDirty");
    ImGui::TableHeadersRow();

    // Build a sorted view of row indices according to the active sort specs.
    const int count = static_cast<int>(snapshot.smaps.size());
    std::vector<int> order(count);
    for (int i = 0; i < count; ++i)
        order[i] = i;

    if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs()) {
        if (specs->SpecsDirty) {
            auto getCol = [](const SMapsSectionSnapshot& s, int col) -> uint64_t {
                switch (col) {
                    case 1:  return s.virtualSize;
                    case 2:  return s.rss;
                    case 3:  return s.pss;
                    case 4:  return s.sharedClean;
                    case 5:  return s.sharedDirty;
                    case 6:  return s.privateClean;
                    case 7:  return s.privateDirty;
                    default: return 0;
                }
            };
            const ImGuiTableColumnSortSpecs* spec = &specs->Specs[0];
            std::stable_sort(order.begin(), order.end(),
                             [&](int a, int b) {
                                 const auto& sa = snapshot.smaps[a];
                                 const auto& sb = snapshot.smaps[b];
                                 bool less;
                                 if (spec->ColumnIndex == 0)
                                     less = sa.name < sb.name;
                                 else
                                     less = getCol(sa, spec->ColumnIndex) < getCol(sb, spec->ColumnIndex);
                                 return spec->SortDirection == ImGuiSortDirection_Ascending ? less : !less;
                             });
            specs->SpecsDirty = false;
        }
    }

    ImGuiListClipper clipper;
    clipper.Begin(count);
    while (clipper.Step()) {
        for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r) {
            const SMapsSectionSnapshot& s = snapshot.smaps[order[r]];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(s.name.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%s", FormatBytes(s.virtualSize).c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%s", FormatBytes(s.rss).c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%s", FormatBytes(s.pss).c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%s", FormatBytes(s.sharedClean).c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%s", FormatBytes(s.sharedDirty).c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%s", FormatBytes(s.privateClean).c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%s", FormatBytes(s.privateDirty).c_str());
        }
    }
    ImGui::EndTable();
}

} // namespace gui
