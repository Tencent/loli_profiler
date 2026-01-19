#include "profilecomparator.h"
#include "stacktracemodel.h"
#include "smaps/smapssection.h"
#include <QFile>
#include <QDataStream>
#include <QTextStream>
#include <QDebug>

#define APP_MAGIC 0xA4B3C2D1
#define APP_VERSION 106

ProfileComparator::ProfileComparator()
    : baselineLoaded_(false)
    , comparisonLoaded_(false)
    , compared_(false)
    , skipRootLevels_(0)
{
}

ProfileComparator::~ProfileComparator()
{
    qDeleteAll(deltaRoots_);
}

bool ProfileComparator::LoadProfile(const QString& filePath, bool isBaseline)
{
    ProfileData& data = isBaseline ? baselineData_ : comparisonData_;
    
    if (!LoadFromFile(filePath, data)) {
        errorMessage_ = QString("Failed to load %1: %2")
            .arg(isBaseline ? "baseline" : "comparison")
            .arg(filePath);
        return false;
    }
    
    if (isBaseline) {
        baselineLoaded_ = true;
    } else {
        comparisonLoaded_ = true;
    }
    
    compared_ = false;  // Reset comparison state
    return true;
}

bool ProfileComparator::LoadFromFile(const QString& filePath, ProfileData& data)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        errorMessage_ = QString("Cannot open file: %1").arg(filePath);
        return false;
    }
    
    QDataStream stream(&file);
    stream.setVersion(QDataStream::Qt_5_12);
    
    // Read magic and version
    quint32 magic;
    qint32 version;
    stream >> magic;
    if (magic != APP_MAGIC) {
        errorMessage_ = QString("Invalid magic number in file: %1").arg(filePath);
        return false;
    }
    
    stream >> version;
    if (version != APP_VERSION) {
        errorMessage_ = QString("Version mismatch in file: %1 (expected %2, got %3)")
            .arg(filePath).arg(APP_VERSION).arg(version);
        return false;
    }
    
    // Read meminfo charts
    stream >> data.maxMemInfoValue;
    qint32 seriesCount;
    stream >> seriesCount;
    data.memInfoSeries.resize(seriesCount);
    for (int i = 0; i < seriesCount; i++) {
        qint32 pointsCount;
        stream >> pointsCount;
        QVector<QPair<int, int>>& points = data.memInfoSeries[i].second;
        points.reserve(pointsCount);
        for (int j = 0; j < pointsCount; j++) {
            QPointF point;
            stream >> point;
            points.append(qMakePair(static_cast<int>(point.x()), static_cast<int>(point.y())));
        }
    }
    
    // Read string hashes
    data.stringHashMap.clear();
    stream >> data.stringHashMap;
    
    // Update global HashString map (needed for HashString::Get() to work)
    for (auto it = data.stringHashMap.begin(); it != data.stringHashMap.end(); ++it) {
        HashString::hashmap_[it.key()] = it.value();
    }
    
    // Read stack trace records
    qint32 recordCount;
    stream >> recordCount;
    data.stackRecords.clear();
    data.stackRecords.reserve(recordCount);
    
    for (int i = 0; i < recordCount; i++) {
        StackRecord record;
        QString uuidStr;
        stream >> uuidStr;
        record.uuid_ = QUuid::fromString(uuidStr);
        stream >> record.seq_;
        stream >> record.time_;
        stream >> record.size_;
        stream >> record.addr_;
        stream >> record.funcAddr_;
        stream >> record.library_.hashcode_;
        data.stackRecords.append(record);
    }
    
    // Read call stack map
    data.callStackMap.clear();
    qint32 callStackMapSize;
    stream >> callStackMapSize;
    for (int i = 0; i < callStackMapSize; i++) {
        QString uuidStr;
        qint32 stackSize;
        stream >> uuidStr >> stackSize;
        
        QVector<QPair<HashString, quint64>> callstack;
        callstack.reserve(stackSize);
        for (int j = 0; j < stackSize; j++) {
            QPair<HashString, quint64> pair;
            stream >> pair.first.hashcode_ >> pair.second;
            callstack.append(pair);
        }
        data.callStackMap.insert(QUuid::fromString(uuidStr), callstack);
    }
    
    // Read symbol map
    data.symbolMap.clear();
    qint32 symbolMapSize;
    stream >> symbolMapSize;
    for (int i = 0; i < symbolMapSize; i++) {
        QString libraryName;
        stream >> libraryName;
        qint32 symbolCount;
        stream >> symbolCount;
        
        QHash<quint64, QString>& symbols = data.symbolMap[libraryName];
        for (int j = 0; j < symbolCount; j++) {
            quint64 addr;
            QString funcName;
            stream >> addr >> funcName;
            symbols[addr] = funcName;
        }
    }
    
    // Read free address map
    data.freeAddrMap.clear();
    qint32 freeAddrMapSize;
    stream >> freeAddrMapSize;
    for (int i = 0; i < freeAddrMapSize; i++) {
        quint64 addr;
        quint32 seq;
        stream >> addr >> seq;
        data.freeAddrMap.insert(addr, seq);
    }
    
    // Read screenshots
    data.screenshots.clear();
    qint32 screenshotCount;
    stream >> screenshotCount;
    data.screenshots.reserve(screenshotCount);
    for (int i = 0; i < screenshotCount; i++) {
        qint32 time;
        QByteArray imageData;
        stream >> time >> imageData;
        data.screenshots.append(qMakePair(time, imageData));
    }
    
    // Read smaps sections
    data.smapsSections.clear();
    qint32 smapsCount;
    stream >> smapsCount;
    for (int i = 0; i < smapsCount; i++) {
        QString name;
        stream >> name;
        SMapsSection section;
        
        qint32 addrCount;
        stream >> addrCount;
        for (int j = 0; j < addrCount; j++) {
            quint64 start, end, offset;
            stream >> start >> end >> offset;
            section.addrs_.push_back(SMapsSectionAddr(start, end, offset));
        }
        
        stream >> section.virtual_;
        stream >> section.rss_;
        stream >> section.pss_;
        stream >> section.privateClean_;
        stream >> section.privateDirty_;
        stream >> section.sharedClean_;
        stream >> section.sharedDirty_;
        
        data.smapsSections.insert(name, section);
    }
    
    return true;
}

QHash<uint, ProfileComparator::CallTreeNode*> ProfileComparator::BuildCallTreeWithHashMap(
    const ProfileData& data, QVector<CallTreeNode*>& roots)
{
    // Build a tree structure matching MainWindow::GetMergedCallstacks logic
    // This uses hash of callstack suffix for O(1) node lookup
    QHash<uint, CallTreeNode*> nodeMap;
    
    for (const auto& record : data.stackRecords) {
        // Get the call stack for this record
        auto stackIt = data.callStackMap.find(record.uuid_);
        if (stackIt == data.callStackMap.end()) {
            continue;
        }
        
        const auto& callStack = stackIt.value();
        
        // Skip if call stack is too short after skipRootLevels_
        if (callStack.size() <= skipRootLevels_) {
            continue;
        }
        
        // Build list of function names, skipping root levels
        // Call stacks are stored leaf-first (index 0 = allocation site, last index = root)
        // We iterate from 0 to (size - skipRootLevels_) to match MainWindow::GetMergedCallstacks
        QStringList callstackNames;
        int endIndex = callStack.size() - skipRootLevels_;
        for (int i = 0; i < endIndex; ++i) {
            const auto& frame = callStack[i];
            QString libraryName = frame.first.Get();
            quint64 funcAddr = frame.second;
            
            // Resolve function name from symbol map
            QString funcName;
            auto libIt = data.symbolMap.find(libraryName);
            if (libIt != data.symbolMap.end()) {
                auto symIt = libIt.value().find(funcAddr);
                if (symIt != libIt.value().end()) {
                    funcName = symIt.value();
                } else {
                    funcName = QString("%1!0x%2").arg(libraryName).arg(funcAddr, 0, 16);
                }
            } else {
                funcName = QString("%1!0x%2").arg(libraryName).arg(funcAddr, 0, 16);
            }
            
            callstackNames << funcName;
        }
        
        // Now build the tree using hash-based merging (same as MainWindow::GetMergedCallstacks)
        CallTreeNode* child = nullptr;
        for (auto it = callstackNames.begin(); it != callstackNames.end(); ++it) {
            // Hash of the suffix from current position to end - this uniquely identifies a path
            auto curHash = qHashRange(it, callstackNames.end());
            auto itemIt = nodeMap.find(curHash);
            CallTreeNode* node = nullptr;
            
            if (itemIt != nodeMap.end()) {
                // Node exists - update size/count for entire parent chain
                node = itemIt.value();
                CallTreeNode* parent = node;
                while (parent != nullptr) {
                    parent->size += record.size_;
                    parent->count += 1;
                    parent = parent->parent;
                }
                
                // Attach child if we created one
                if (child != nullptr) {
                    node->children.append(child);
                    child->parent = node;
                }
                break;  // Stop - rest of the chain already exists
            }
            
            // Create new node
            node = new CallTreeNode();
            node->functionName = *it;
            node->size = record.size_;
            node->count = 1;
            node->parent = nullptr;
            nodeMap.insert(curHash, node);
            
            // Attach child
            if (child != nullptr) {
                node->children.append(child);
                child->parent = node;
            }
            
            child = node;
        }
        
        // Add to roots if this is a top-level node
        if (child != nullptr && child->parent == nullptr) {
            bool alreadyInRoots = false;
            for (CallTreeNode* root : roots) {
                if (root == child) {
                    alreadyInRoots = true;
                    break;
                }
            }
            if (!alreadyInRoots) {
                roots.append(child);
            }
        }
    }
    
    return nodeMap;
}

bool ProfileComparator::Compare(int skipRootLevels)
{
    if (!baselineLoaded_ || !comparisonLoaded_) {
        errorMessage_ = "Both baseline and comparison profiles must be loaded first";
        return false;
    }
    
    // Save skipRootLevels for use in BuildCallTreeWithHashMap
    skipRootLevels_ = skipRootLevels;
    
    // Clean up previous delta tree
    qDeleteAll(deltaRoots_);
    deltaRoots_.clear();
    
    // Calculate basic statistics
    stats_ = ComparisonStats();
    stats_.baselineAllocCount = baselineData_.stackRecords.size();
    stats_.comparisonAllocCount = comparisonData_.stackRecords.size();
    
    for (const auto& record : baselineData_.stackRecords) {
        stats_.baselineTotalSize += record.size_;
    }
    
    for (const auto& record : comparisonData_.stackRecords) {
        stats_.comparisonTotalSize += record.size_;
    }
    
    stats_.sizeDelta = static_cast<qint64>(stats_.comparisonTotalSize) - 
                       static_cast<qint64>(stats_.baselineTotalSize);
    
    // Build call trees with hash maps (efficient O(1) lookup by hash)
    QVector<CallTreeNode*> baselineRoots;
    QVector<CallTreeNode*> comparisonRoots;
    
    auto baselineHashmap = BuildCallTreeWithHashMap(baselineData_, baselineRoots);
    auto comparisonHashmap = BuildCallTreeWithHashMap(comparisonData_, comparisonRoots);
    
    // Size limiter - ignore small differences (1 KiB)
    quint64 sizeLimiter = 1024;
    
    // Collect leaf nodes for delta calculation
    QList<CallTreeNode*> leafItems;
    
    // Diff leaf nodes only (matching MainWindow::on_actionShow_Leaks_triggered logic)
    for (auto it = comparisonHashmap.begin(); it != comparisonHashmap.end(); ++it) {
        auto key = it.key();
        auto compNode = it.value();
        auto baselineIt = baselineHashmap.find(key);
        
        // Only process leaf nodes that exist in both trees
        if (compNode->children.size() != 0 || baselineIt == baselineHashmap.end()) {
            compNode->size = 0;
            compNode->count = 0;
            continue;
        }
        
        leafItems.append(compNode);
        
        auto baseNode = baselineIt.value();
        qint64 newSize = compNode->size - baseNode->size;
        qint64 newCount = compNode->count - baseNode->count;
        
        // Filter small differences
        if (newSize < static_cast<qint64>(sizeLimiter) || newCount <= 0) {
            compNode->size = 0;
            compNode->count = 0;
        } else {
            compNode->size = newSize;
            compNode->count = newCount;
        }
    }
    
    // Release baseline tree - no longer needed
    qDeleteAll(baselineRoots);
    baselineRoots.clear();
    baselineHashmap.clear();
    
    // Recalculate parent size & count from leaf nodes (bottom-up propagation)
    // First, reset all non-leaf nodes to zero
    for (auto it = comparisonHashmap.begin(); it != comparisonHashmap.end(); ++it) {
        auto node = it.value();
        if (node->children.size() > 0) {
            node->size = 0;
            node->count = 0;
        }
    }
    
    // Then propagate leaf values up to parents
    for (auto leaf : leafItems) {
        auto parent = leaf->parent;
        while (parent != nullptr) {
            parent->size += leaf->size;
            parent->count += leaf->count;
            parent = parent->parent;
        }
    }
    leafItems.clear();
    
    // Count statistics and remove zero-count nodes
    stats_.changedAllocations = 0;

    for (auto node : comparisonHashmap) {
        if (node->count > 0) {
            stats_.changedAllocations++;
        } else {
            // Remove from parent's children list
            if (node->parent) {
                node->parent->children.removeOne(node);
            } else {
                comparisonRoots.removeOne(node);
            }
        }
    }
    comparisonHashmap.clear();

    // Store delta roots for export (transfer ownership)
    deltaRoots_ = comparisonRoots;

    compared_ = true;
    return true;
}

bool ProfileComparator::ExportToText(const QString& outputPath)
{
    if (!compared_) {
        errorMessage_ = "Must call Compare() before exporting";
        return false;
    }
    
    QFile file(outputPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        errorMessage_ = QString("Cannot create output file: %1").arg(outputPath);
        return false;
    }
    
    QTextStream stream(&file);
    stream.setCodec("UTF-8");
    
    // Write header
    stream << "=== LoliProfiler Comparison Report ===" << "\n\n";
    stream << "Baseline allocations: " << stats_.baselineAllocCount << "\n";
    stream << "Comparison allocations: " << stats_.comparisonAllocCount << "\n";
    stream << "Baseline total size: " << sizeToString(stats_.baselineTotalSize) << "\n";
    stream << "Comparison total size: " << sizeToString(stats_.comparisonTotalSize) << "\n";
    stream << "Size delta: ";
    if (stats_.sizeDelta >= 0) {
        stream << "+" << sizeToString(static_cast<quint64>(stats_.sizeDelta));
    } else {
        stream << "-" << sizeToString(static_cast<quint64>(-stats_.sizeDelta));
    }
    stream << "\n\n";
    
    stream << "Changed allocations (>1KB growth): " << stats_.changedAllocations << "\n\n";
    
    stream << "=== Memory Growth (Delta: Comparison - Baseline) ===" << "\n\n";
    
    // Write delta tree
    for (CallTreeNode* root : deltaRoots_) {
        WriteCallTreeToText(stream, root, 0);
    }
    
    file.close();
    return true;
}

void ProfileComparator::WriteCallTreeToText(QTextStream& stream, CallTreeNode* node, int depth)
{
    if (!node) return;
    
    // Write indentation
    for (int i = 0; i < depth; ++i) {
        stream << "    ";  // 4 spaces for indentation
    }
    
    // Write function name
    stream << node->functionName << ", ";
    
    // Write size with +/- prefix for deltas
    if (node->size > 0) {
        stream << "+" << sizeToString(static_cast<quint64>(node->size));
    } else if (node->size < 0) {
        stream << "-" << sizeToString(static_cast<quint64>(-node->size));
    } else {
        stream << sizeToString(0);
    }
    
    stream << ", ";
    
    // Write count with +/- prefix for deltas
    if (node->count > 0) {
        stream << "+" << node->count;
    } else if (node->count < 0) {
        stream << node->count;  // Already has negative sign
    } else {
        stream << "0";
    }
    
    stream << "\n";
    
    // Recursively write children
    for (CallTreeNode* child : node->children) {
        WriteCallTreeToText(stream, child, depth + 1);
    }
}
