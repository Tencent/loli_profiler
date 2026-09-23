#include "filedialogs.h"

#include <cstdio>
#include <mutex>
#include <vector>

#include <nfd.h>

namespace FileDialogs
{

namespace
{

// Manages the NFD session lifetime (NFD_Init / NFD_Quit).
// - Every dialog function acquires a Handle, so Init/Quit are paired
//   automatically and multiple dialogs in sequence are fine.
// - Explicit Init()/Shutdown() calls hold a separate "manual" reference so
//   that Init() keeps NFD alive across dialog calls until Shutdown().
// All calls are expected from a single thread (the UI thread); the mutex
// only guards the shared state.
std::mutex g_mutex;
int g_dialogRefCount = 0; // active dialog calls
bool g_manualInit = false; // explicit Init() without matching Shutdown()
bool g_initialized = false; // NFD_Init succeeded

bool AcquireNFD()
{
    if (g_initialized)
    {
        return true;
    }

    if (NFD_Init() != NFD_OKAY)
    {
        std::fprintf(stderr, "FileDialogs: NFD_Init failed: %s\n", NFD_GetError());
        return false;
    }

    g_initialized = true;
    return true;
}

void ReleaseNFD()
{
    if (g_initialized)
    {
        NFD_Quit();
        g_initialized = false;
    }
}

struct Handle
{
    Handle()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_dialogRefCount++;
    }

    ~Handle()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_dialogRefCount--;
        if (g_dialogRefCount == 0 && !g_manualInit)
        {
            ReleaseNFD();
        }
    }

    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;

    bool valid()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        return AcquireNFD();
    }
};

// Builds the NFD filter array. The returned nfdfilteritem_t elements point
// into `filters`, so `filters` must outlive the array's use.
std::vector<nfdfilteritem_t> BuildFilterList(const std::vector<FileFilter>& filters)
{
    std::vector<nfdfilteritem_t> items;
    items.reserve(filters.size());
    for (const FileFilter& filter : filters)
    {
        items.push_back({ filter.name.c_str(), filter.spec.c_str() });
    }
    return items;
}

std::optional<std::string> WrapResult(nfdresult_t result, nfdchar_t* outPath)
{
    if (result == NFD_OKAY)
    {
        std::string path(outPath);
        NFD_FreePath(outPath);
        return path;
    }

    if (result == NFD_ERROR)
    {
        const char* error = NFD_GetError();
        std::fprintf(stderr, "FileDialogs: dialog failed: %s\n", error ? error : "unknown error");
    }

    // NFD_CANCEL (and NFD_ERROR): no path to free.
    return std::nullopt;
}

} // namespace

void Init()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_manualInit)
    {
        return; // idempotent
    }

    if (AcquireNFD())
    {
        g_manualInit = true;
    }
}

void Shutdown()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_manualInit)
    {
        return; // idempotent-safe
    }

    g_manualInit = false;
    if (g_dialogRefCount == 0)
    {
        ReleaseNFD();
    }
    // If dialogs are active, NFD is released when the last Handle goes away.
}

std::optional<std::string> OpenFile(const std::vector<FileFilter>& filters,
                                    const std::string& defaultPath)
{
    Handle handle;
    if (!handle.valid())
    {
        return std::nullopt;
    }

    const std::vector<nfdfilteritem_t> items = BuildFilterList(filters);
    nfdchar_t* outPath = nullptr;
    const nfdresult_t result = NFD_OpenDialog(&outPath,
                                              items.empty() ? nullptr : items.data(),
                                              static_cast<nfdfiltersize_t>(items.size()),
                                              defaultPath.empty() ? nullptr : defaultPath.c_str());
    return WrapResult(result, outPath);
}

std::optional<std::string> SaveFile(const std::vector<FileFilter>& filters,
                                    const std::string& defaultPath)
{
    Handle handle;
    if (!handle.valid())
    {
        return std::nullopt;
    }

    const std::vector<nfdfilteritem_t> items = BuildFilterList(filters);
    nfdchar_t* outPath = nullptr;
    const nfdresult_t result = NFD_SaveDialog(&outPath,
                                              items.empty() ? nullptr : items.data(),
                                              static_cast<nfdfiltersize_t>(items.size()),
                                              defaultPath.empty() ? nullptr : defaultPath.c_str(),
                                              nullptr);
    return WrapResult(result, outPath);
}

std::optional<std::string> PickFolder(const std::string& defaultPath)
{
    Handle handle;
    if (!handle.valid())
    {
        return std::nullopt;
    }

    nfdchar_t* outPath = nullptr;
    const nfdresult_t result = NFD_PickFolder(&outPath,
                                              defaultPath.empty() ? nullptr : defaultPath.c_str());
    return WrapResult(result, outPath);
}

} // namespace FileDialogs
