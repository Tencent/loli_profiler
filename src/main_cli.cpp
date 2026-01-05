#include "cliprofiler.h"
#include "clilogger.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDir>
#include <iostream>

void printUsage() {
    std::cout << "LoliProfiler CLI - Android Memory Profiling Tool\n\n";
    std::cout << "Usage:\n";
    std::cout << "  LoliProfilerCLI --app <package_name> --out <output.loli> [options]\n\n";
    std::cout << "Required Options:\n";
    std::cout << "  --app <name>           Target application package name\n";
    std::cout << "  --out <path>           Output .loli file path\n\n";
    std::cout << "Optional Options:\n";
    std::cout << "  --symbol <path>        Symbol file (.so/.sym) for address translation\n";
    std::cout << "  --subprocess <name>    Target subprocess name\n";
    std::cout << "  --device <serial>      Device serial number (required if multiple devices)\n";
    std::cout << "  --duration <seconds>   Profiling duration in seconds (0 = until exit)\n";
    std::cout << "  --attach               Attach to running app instead of launching\n";
    std::cout << "  --verbose, -v          Verbose output\n";
    std::cout << "  --help, -h             Show this help message\n\n";
    std::cout << "Examples:\n";
    std::cout << "  # Profile for 60 seconds\n";
    std::cout << "  LoliProfilerCLI --app com.example.game --out profile.loli --duration 60\n\n";
    std::cout << "  # Profile until process exits with symbol translation\n";
    std::cout << "  LoliProfilerCLI --app com.example.game --out profile.loli \\\n";
    std::cout << "    --symbol /path/to/libgame.so --duration 0\n\n";
    std::cout << "  # Profile with specific device (multiple devices connected)\n";
    std::cout << "  LoliProfilerCLI --app com.example.game --out profile.loli \\\n";
    std::cout << "    --device ABC123456 --duration 60\n\n";
    std::cout << "  # Attach to running app\n";
    std::cout << "  LoliProfilerCLI --app com.example.game --out profile.loli --attach\n";
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
        "Profiling duration in seconds (0 = until process exits)", "seconds");
    parser.addOption(durationOption);
    
    QCommandLineOption attachOption(QStringList() << "attach", 
        "Attach to running app instead of launching");
    parser.addOption(attachOption);
    
    QCommandLineOption verboseOption(QStringList() << "v" << "verbose", 
        "Verbose output");
    parser.addOption(verboseOption);
    
    // Parse arguments
    CLI_LOG("Parsing command line arguments...");
    parser.process(app);
    
    CLI_LOG("Arguments parsed successfully");
    
    // Validate required options
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
    
    // Connect finished signal to quit application
    QObject::connect(&profiler, &CliProfiler::Finished, [&app](int exitCode) {
        CLI_LOG(QString("Profiler finished with exit code: %1").arg(exitCode));
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
