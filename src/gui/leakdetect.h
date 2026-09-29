#ifndef LOLI_PROFILER_GUI_LEAKDETECT_H
#define LOLI_PROFILER_GUI_LEAKDETECT_H

#include <cstdint>
#include <memory>

namespace loli { struct Session; }
namespace gui {
class StacktraceTree;

// Build the old Qt "Show Possible Memory Leaks" merged callstack result.
// Times are capture-relative milliseconds; start <= end is required.
// With persistentOnly, records freed at any time are excluded, matching the
// old GUI's Persistent allocation filter.
std::shared_ptr<StacktraceTree> BuildPossibleLeaksTree(
    const loli::Session& session, int32_t startMs, int32_t endMs,
    bool persistentOnly = false);

} // namespace gui
#endif
