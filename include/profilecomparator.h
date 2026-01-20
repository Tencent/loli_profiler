#ifndef PROFILECOMPARATOR_H
#define PROFILECOMPARATOR_H

#include <QString>
#include <QHash>
#include <QVector>
#include <QPair>
#include <QUuid>
#include "hashstring.h"
#include "stacktracemodel.h"
#include "smaps/smapssection.h"

// Forward declarations
class QTextStream;

/**
 * ProfileComparator - Compares two .loli profiling files and generates diff reports
 * 
 * Supports:
 * - Loading and comparing two profiling sessions
 * - Generating text diff reports with hierarchical call stack format
 * - Exporting diff as .loli file for GUI visualization
 */
class ProfileComparator {
public:
    ProfileComparator();
    ~ProfileComparator();
    
    /**
     * Load a .loli profile file
     * @param filePath Path to .loli file
     * @param isBaseline true for baseline/file1, false for comparison/file2
     * @return true if successful
     */
    bool LoadProfile(const QString& filePath, bool isBaseline);
    
    /**
     * Compare the two loaded profiles
     * Must call LoadProfile for both baseline and comparison first
     * @param skipRootLevels Number of root stack frames to skip (default: 0)
     *                       Useful to skip system library frames without symbols
     * @return true if comparison successful
     */
    bool Compare(int skipRootLevels = 0);
    
    /**
     * Export comparison result to text format (deep copy style)
     * Format: function_name, size, count (with tab indentation for hierarchy)
     * @param outputPath Path to output text file
     * @return true if export successful
     */
    bool ExportToText(const QString& outputPath);

    /**
     * Export comparison result to .loli format for GUI visualization
     * Creates a .loli file containing delta call stacks (positive = growth, negative = reduction)
     * @param outputPath Path to output .loli file
     * @return true if export successful
     */
    bool ExportToLoli(const QString& outputPath);
    
    /**
     * Get comparison summary statistics
     */
    struct ComparisonStats {
        quint64 baselineTotalSize;
        quint64 comparisonTotalSize;
        qint64 sizeDelta;              // positive = growth, negative = reduction
        int baselineAllocCount;
        int comparisonAllocCount;
        int changedAllocations;        // allocations with size growth >1KB
    };
    
    ComparisonStats GetStats() const { return stats_; }
    
    QString GetErrorMessage() const { return errorMessage_; }
    
private:
    struct ProfileData {
        int maxMemInfoValue;
        QVector<QPair<int, QVector<QPair<int, int>>>> memInfoSeries;  // series[time, value]
        QHash<quint32, QString> stringHashMap;
        QVector<StackRecord> stackRecords;
        QHash<QUuid, QVector<QPair<HashString, quint64>>> callStackMap;
        QHash<QString, QHash<quint64, QString>> symbolMap;
        QHash<quint64, quint32> freeAddrMap;
        QVector<QPair<int, QByteArray>> screenshots;
        QHash<QString, SMapsSection> smapsSections;
    };
    
    struct CallTreeNode {
        QString functionName;      // Display name (resolved symbol or "library!0xaddress")
        QString libraryName;       // Original library name
        quint64 functionAddress;   // Original function address
        qint64 size;               // Changed to qint64 to support negative deltas
        qint64 count;              // Changed to qint64 to support negative deltas
        QVector<CallTreeNode*> children;
        CallTreeNode* parent;      // For easier parent chain traversal

        CallTreeNode() : functionAddress(0), size(0), count(0), parent(nullptr) {}
        ~CallTreeNode() {
            qDeleteAll(children);
        }
    };
    
    bool LoadFromFile(const QString& filePath, ProfileData& data);
    
    // Efficient hash-based call tree building (matches MainWindow::GetMergedCallstacks logic)
    QHash<uint, CallTreeNode*> BuildCallTreeWithHashMap(const ProfileData& data, QVector<CallTreeNode*>& roots);

    // Write call tree to text output
    void WriteCallTreeToText(QTextStream& stream, CallTreeNode* node, int depth);

    // Convert delta tree to stack records and callstack map for .loli export
    void ConvertDeltaTreeToRecords(
        QVector<StackRecord>& stackRecords,
        QHash<QUuid, QVector<QPair<HashString, quint64>>>& callStackMap);

    ProfileData baselineData_;
    ProfileData comparisonData_;
    ComparisonStats stats_;
    QString errorMessage_;
    bool baselineLoaded_;
    bool comparisonLoaded_;
    bool compared_;
    int skipRootLevels_;  // Number of root levels to skip

    // Store delta tree roots for export
    QVector<CallTreeNode*> deltaRoots_;
};

#endif // PROFILECOMPARATOR_H
