#ifndef MEMINFOPROCESS_H
#define MEMINFOPROCESS_H

#include "adbprocess.h"
#include <algorithm>

struct MemInfo {
    unsigned int Total = 0;
    unsigned int NativeHeap = 0;
    unsigned int GfxDev = 0;
    unsigned int EGLmtrack = 0;
    unsigned int GLmtrack = 0;
    unsigned int Unknown = 0;

    void Max(const MemInfo& info) {
        Total = std::max(info.Total, Total);
        NativeHeap = std::max(info.NativeHeap, NativeHeap);
        GfxDev = std::max(info.GfxDev, GfxDev);
        EGLmtrack = std::max(info.EGLmtrack, EGLmtrack);
        GLmtrack = std::max(info.GLmtrack, GLmtrack);
        Unknown = std::max(info.Unknown, Unknown);
    }

    void Reset() {
        Total = 0;
        NativeHeap = 0;
        GfxDev = 0;
        EGLmtrack = 0;
        GLmtrack = 0;
        Unknown = 0;
    }
};

class MemInfoProcess : public AdbProcess {
public:
    MemInfoProcess(QObject* parent = nullptr);

    const MemInfo& GetMemInfo() const {
        return meminfo_;
    }

    const QString& GetAppPid() const {
        return appPid_;
    }

    void DumpMemInfoAsync(const QString& appName);

protected:
    void OnProcessFinihed() override;

private:
    QString appName_;
    QString appPid_;
    MemInfo meminfo_;
};

#endif // MEMINFOPROCESS_H
