#include "QConsoleWidget.h"
#include "scriptsession.h"
#include <QApplication>
#include <QTimer>
#include <QVBoxLayout>

// make a wrapper for QConsoleWidget
class MainWidget : public QWidget
{
public:
    MainWidget()
    {
        QVBoxLayout* vl = new QVBoxLayout;
        w_ = new QConsoleWidget(this);
        vl->addWidget(w_);
        vl->setMargin(1);
        setLayout(vl);
    }
    QConsoleWidget* widget() const
    { return w_; }
private:
    QConsoleWidget* w_;
};


int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    MainWidget W;
    W.setWindowTitle("qscript console");
    ScriptSession qs(W.widget());
    W.show();

    QTimer::singleShot(200, &qs, SLOT(REPL()));

    return a.exec();
}
