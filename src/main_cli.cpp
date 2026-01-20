#include "cliprofiler.h"
#include "clilogger.h"
#include "profilecomparator.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QMetaObject>
#include <csignal>
#include <cstring>
#include <iostream>

#ifdef _WIN32
    #include <io.h>
    #define write _write
    #define STDOUT_FILENO 1
#else
    #include <unistd.h>
#endif

// Global pointer to profiler for signal handler
static CliProfiler* g_profiler = nullptr;

void signalHandler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        // Signal handlers must be async-signal-safe
        // We can only call async-signal-safe functions here
        // Use write() to output message (async-signal-safe)
        const char* msg = "\nReceived stop signal, stopping profiling gracefully...\n";
        write(STDOUT_FILENO, msg, static_cast<unsigned int>(strlen(msg)));
        
        if (g_profiler) {
            // Use Qt's thread-safe mechanism to invoke method in main thread
            QMetaObject::invokeMethod(g_profiler, "RequestStop", Qt::QueuedConnection);
        }
    }
}

void printUsage() {
    std::cout << "LoliProfiler CLI - Android Memory Profiling Tool\n\n";
    std::cout << "Usage:\n";
    std::cout << "  LoliProfilerCLI --app <package_name> --out <output.loli> [options]\n";
    std::cout << "  LoliProfilerCLI --compare <baseline.loli> <comparison.loli> --out <output> [options]\n\n";
    std::cout << "Profiling Mode - Required Options:\n";
    std::cout << "  --app <name>           Target application package name\n";
    std::cout << "  --out <path>           Output .loli file path\n\n";
    std::cout << "Profiling Mode - Optional Options:\n";
    std::cout << "  --symbol <path>        Symbol file (.so/.sym) for address translation\n";
    std::cout << "  --subprocess <name>    Target subprocess name\n";
    std::cout << "  --device <serial>      Device serial number (required if multiple devices)\n";
    std::cout << "  --duration <seconds>   Profiling duration in seconds (omit for manual stop with Ctrl+C)\n";
    std::cout << "  --attach               Attach to running app instead of launching\n";
    std::cout << "  --verbose              Verbose output\n\n";
    std::cout << "Compare Mode - Usage:\n";
    std::cout << "  --compare              Enable compare mode (requires 2 positional file arguments)\n";
    std::cout << "  <baseline.loli>        First .loli file (baseline)\n";
    std::cout << "  <comparison.loli>      Second .loli file (comparison)\n";
    std::cout << "  --out <path>           Output file path (.txt for text report, .loli for GUI visualization)\n\n";
    std::cout << "Compare Mode - Optional Options:\n";
    std::cout << "  --skip-root-levels <N> Skip N root call stack frames (useful for system libs without symbols)\n\n";
    std::cout << "General Options:\n";
    std::cout << "  --help, -h             Show this help message\n";
    std::cout << "  --version, -v          Show version information\n\n";
    std::cout << "Examples:\n";
    std::cout << "  # Profile for 60 seconds\n";
    std::cout << "  LoliProfilerCLI --app com.example.game --out profile.loli --duration 60\n\n";
    std::cout << "  # Profile until manually stopped (Ctrl+C) with symbol translation\n";
    std::cout << "  LoliProfilerCLI --app com.example.game --out profile.loli \\\n";
    std::cout << "    --symbol /path/to/libgame.so\n\n";
    std::cout << "  # Compare two profiles and export diff as text\n";
    std::cout << "  LoliProfilerCLI --compare baseline.loli comparison.loli --out diff.txt\n\n";
    std::cout << "  # Compare and export as .loli for GUI visualization\n";
    std::cout << "  LoliProfilerCLI --compare baseline.loli comparison.loli --out diff.loli\n\n";
    std::cout << "  # Compare and skip 2 root call stack levels (e.g., system library frames)\n";
    std::cout << "  LoliProfilerCLI --compare baseline.loli comparison.loli --out diff.txt --skip-root-levels 2\n";
}

int main(int argc, char *argv[]) {
    // CLI Mode
    QCoreApplication app(argc, argv);
    // Use the same app name as GUI to share config files
    app.setApplicationName("LoliProfiler");
    
    // Initialize file logging immediately
    QString logPath = QCoreApplication::applicationDirPath() + "/cli_profiler.log";
    CliLogger::Instance().Init(logPath);
    
    CLI_LOG("=== LoliProfiler CLI Mode ===");
    CLI_LOG("Initializing...");
    CLI_LOG(QString("Log file: %1").arg(logPath));
    
    CLI_LOG("Creating command line parser...");
    
    QCommandLineParser parser;
    parser.setApplicationDescription("LoliProfiler CLI - Android Memory Profiling Tool");
    parser.addHelpOption();
    parser.addVersionOption();
    
    // Required options
    QCommandLineOption appOption(QStringList() << "app", 
        "Target application package name", "package");
    parser.addOption(appOption);
    
    QCommandLineOption outOption(QStringList() << "out", 
        "Output .loli file path", "file");
    parser.addOption(outOption);
    
    // Optional options
    QCommandLineOption symbolOption(QStringList() << "symbol", 
        "Symbol file (.so/.sym) for address translation", "file");
    parser.addOption(symbolOption);
    
    QCommandLineOption subprocessOption(QStringList() << "subprocess", 
        "Target subprocess name", "name");
    parser.addOption(subprocessOption);
    
    QCommandLineOption deviceOption(QStringList() << "device", 
        "Device serial number (required if multiple devices connected)", "serial");
    parser.addOption(deviceOption);
    
    QCommandLineOption durationOption(QStringList() << "duration", 
        "Profiling duration in seconds (omit for manual stop with Ctrl+C)", "seconds");
    parser.addOption(durationOption);
    
    QCommandLineOption attachOption(QStringList() << "attach", 
        "Attach to running app instead of launching");
    parser.addOption(attachOption);
    
    QCommandLineOption verboseOption(QStringList() << "verbose", 
        "Verbose output");
    parser.addOption(verboseOption);
    
    // Compare mode options
    QCommandLineOption compareOption(QStringList() << "compare",
        "Compare two .loli files (baseline vs comparison)");
    parser.addOption(compareOption);
    
    parser.addPositionalArgument("files", "Input .loli files for comparison (baseline comparison)", "[file1] [file2]");
    
    
    QCommandLineOption skipRootLevelsOption(QStringList() << "skip-root-levels",
        "Number of root call stack frames to skip in comparison (default: 0)", "levels");
    parser.addOption(skipRootLevelsOption);
    
    // Parse arguments
    CLI_LOG("Parsing command line arguments...");
    parser.process(app);
    
    CLI_LOG("Arguments parsed successfully");
    
    // Check if this is compare mode
    if (parser.isSet(compareOption)) {
        // Compare mode
        CLI_LOG("Running in COMPARE mode");
        
        QStringList compareFiles = parser.positionalArguments();
        if (compareFiles.size() != 2) {
            CLI_ERROR("--compare requires exactly two file arguments");
            std::cerr << "Error: --compare requires exactly two .loli files\n";
            std::cerr << "Usage: LoliProfilerCLI --compare <baseline.loli> <comparison.loli> --out <output>\n";
            std::cerr << "Example: LoliProfilerCLI --compare file1.loli file2.loli --out diff.txt\n";
            printUsage();
            CliLogger::Instance().Close();
            return 1;
        }
        
        if (!parser.isSet(outOption)) {
            CLI_ERROR("--out is required in compare mode");
            std::cerr << "Error: --out is required to specify output file\n";
            printUsage();
            CliLogger::Instance().Close();
            return 1;
        }
        
        QString baselineFile = compareFiles[0];
        QString comparisonFile = compareFiles[1];
        QString outputFile = parser.value(outOption);
        int skipRootLevels = parser.value(skipRootLevelsOption).toInt();
        
        CLI_LOG(QString("Baseline file: %1").arg(baselineFile));
        CLI_LOG(QString("Comparison file: %1").arg(comparisonFile));
        CLI_LOG(QString("Output file: %1").arg(outputFile));
        CLI_LOG(QString("Skip root levels: %1").arg(skipRootLevels));
        
        std::cout << "Loading baseline profile: " << baselineFile.toStdString() << "...\n";
        
        ProfileComparator comparator;
        
        // Load baseline
        if (!comparator.LoadProfile(baselineFile, true)) {
            CLI_ERROR(QString("Failed to load baseline: %1").arg(comparator.GetErrorMessage()));
            std::cerr << "Error: " << comparator.GetErrorMessage().toStdString() << "\n";
            CliLogger::Instance().Close();
            return 1;
        }
        
        std::cout << "Loading comparison profile: " << comparisonFile.toStdString() << "...\n";
        
        // Load comparison
        if (!comparator.LoadProfile(comparisonFile, false)) {
            CLI_ERROR(QString("Failed to load comparison: %1").arg(comparator.GetErrorMessage()));
            std::cerr << "Error: " << comparator.GetErrorMessage().toStdString() << "\n";
            CliLogger::Instance().Close();
            return 1;
        }
        
        std::cout << "Comparing profiles";
        if (skipRootLevels > 0) {
            std::cout << " (skipping " << skipRootLevels << " root levels)";
        }
        std::cout << "...\n";
        
        // Perform comparison
        if (!comparator.Compare(skipRootLevels)) {
            CLI_ERROR(QString("Failed to compare: %1").arg(comparator.GetErrorMessage()));
            std::cerr << "Error: " << comparator.GetErrorMessage().toStdString() << "\n";
            CliLogger::Instance().Close();
            return 1;
        }
        
        // Get stats
        auto stats = comparator.GetStats();
        std::cout << "\n=== Comparison Results ===\n";
        std::cout << "Baseline allocations: " << stats.baselineAllocCount << "\n";
        std::cout << "Comparison allocations: " << stats.comparisonAllocCount << "\n";
        std::cout << "Baseline total size: " << sizeToString(stats.baselineTotalSize).toStdString() << "\n";
        std::cout << "Comparison total size: " << sizeToString(stats.comparisonTotalSize).toStdString() << "\n";
        std::cout << "Size delta: ";
        if (stats.sizeDelta >= 0) {
            std::cout << "+" << sizeToString(static_cast<quint64>(stats.sizeDelta)).toStdString();
        } else {
            std::cout << "-" << sizeToString(static_cast<quint64>(-stats.sizeDelta)).toStdString();
        }
        std::cout << "\n";
        std::cout << "Changed allocations (>1KB growth): " << stats.changedAllocations << "\n\n";

        // Detect output format based on file extension
        bool exportAsLoli = outputFile.toLower().endsWith(".loli");

        if (exportAsLoli) {
            // Export as .loli file for GUI visualization
            std::cout << "Exporting diff as .loli file: " << outputFile.toStdString() << "...\n";

            if (!comparator.ExportToLoli(outputFile)) {
                CLI_ERROR(QString("Failed to export as .loli: %1").arg(comparator.GetErrorMessage()));
                std::cerr << "Error: " << comparator.GetErrorMessage().toStdString() << "\n";
                CliLogger::Instance().Close();
                return 1;
            }

            std::cout << "Comparison complete! .loli file saved to: " << outputFile.toStdString() << "\n";
            std::cout << "You can open this file in LoliProfiler GUI to visualize the memory growth.\n";
        } else {
            // Export as text file
            std::cout << "Exporting diff as text file: " << outputFile.toStdString() << "...\n";

            if (!comparator.ExportToText(outputFile)) {
                CLI_ERROR(QString("Failed to export as text: %1").arg(comparator.GetErrorMessage()));
                std::cerr << "Error: " << comparator.GetErrorMessage().toStdString() << "\n";
                CliLogger::Instance().Close();
                return 1;
            }

            std::cout << "Comparison complete! Output saved to: " << outputFile.toStdString() << "\n";
        }

        CLI_LOG("Comparison completed successfully");
        CliLogger::Instance().Close();
        return 0;
    }
    
    // Validate required options for profiling mode
    if (!parser.isSet(appOption) || !parser.isSet(outOption)) {
        CLI_ERROR("--app and --out are required");
        CLI_LOG(QString("Parsed values:"));
        CLI_LOG(QString("  --app: %1").arg(parser.isSet(appOption) ? "SET" : "NOT SET"));
        CLI_LOG(QString("  --out: %1").arg(parser.isSet(outOption) ? "SET" : "NOT SET"));
        printUsage();
        CliLogger::Instance().Close();
        return 1;
    }
    
    // Build CLI options
    CLI_LOG("Building CLI options...");
    CliProfiler::CliOptions options;
    options.appName = parser.value(appOption);
    options.outputFile = parser.value(outOption);
    options.symbolPath = parser.value(symbolOption);
    options.subProcessName = parser.value(subprocessOption);
    options.deviceSerial = parser.value(deviceOption);
    options.duration = parser.value(durationOption).toInt();
    options.attachMode = parser.isSet(attachOption);
    options.verbose = parser.isSet(verboseOption);
    
    CLI_LOG("Configuration:");
    CLI_LOG(QString("  App: %1").arg(options.appName));
    CLI_LOG(QString("  Output: %1").arg(options.outputFile));
    CLI_LOG(QString("  Symbol: %1").arg(options.symbolPath.isEmpty() ? "(none)" : options.symbolPath));
    CLI_LOG(QString("  Device: %1").arg(options.deviceSerial.isEmpty() ? "(default)" : options.deviceSerial));
    CLI_LOG(QString("  Duration: %1 seconds").arg(options.duration));
    CLI_LOG(QString("  Attach: %1").arg(options.attachMode ? "yes" : "no"));
    CLI_LOG(QString("  Verbose: %1").arg(options.verbose ? "yes" : "no"));
    
    // Create CLI profiler
    CLI_LOG("Creating CLI profiler...");
    CliProfiler profiler;
    
    // Set global pointer for signal handler
    g_profiler = &profiler;
    
    // Register signal handlers for graceful shutdown
    std::signal(SIGINT, signalHandler);   // Ctrl+C
    std::signal(SIGTERM, signalHandler);  // Termination request
    CLI_LOG("Signal handlers registered (SIGINT, SIGTERM)");
    
    // Connect finished signal to quit application
    QObject::connect(&profiler, &CliProfiler::Finished, [&app](int exitCode) {
        CLI_LOG(QString("Profiler finished with exit code: %1").arg(exitCode));
        g_profiler = nullptr;  // Clear global pointer
        CliLogger::Instance().Close();
        QCoreApplication::exit(exitCode);
    });
    
    // Initialize
    CLI_LOG("Initializing profiler...");
    if (!profiler.Initialize(options)) {
        CLI_ERROR("Failed to initialize profiler");
        CliLogger::Instance().Close();
        return 1;
    }
    
    // Start profiling
    CLI_LOG("Starting profiling...");
    profiler.Start();
    
    CLI_LOG("Entering event loop...");
    int result = app.exec();
    CLI_LOG(QString("Event loop exited with code: %1").arg(result));
    CliLogger::Instance().Close();
    
    return result;
}
