#include "mainwindow.h"
#include <QApplication>

int main(int argc, char *argv[]) {
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    
    QApplication a(argc, argv);
    // Set application name for consistent config paths
    a.setApplicationName("LoliProfiler");
    
    MainWindow w;
    w.show();
    
    return a.exec();
}
