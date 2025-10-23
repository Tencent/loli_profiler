#include "scriptsession.h"
#include "QConsoleWidget.h"
#include "qscriptcompleter.h"

#include <QApplication>
#include <QEventLoop>
#include <QScriptEngine>
#include <QScriptValue>

#include <QTimer>
#include <QElapsedTimer>
#include <QIODevice>


QScriptValue quitfunc(QScriptContext *context, QScriptEngine *engine)
{
    Q_UNUSED(context);
    ScriptSession* session = qobject_cast<ScriptSession*>(engine->parent());
    session->quit();
    qApp->quit();
    return QScriptValue(QScriptValue::UndefinedValue);
}
QScriptValue ticfunc(QScriptContext *context, QScriptEngine *engine)
{
    Q_UNUSED(context);
    ScriptSession* session = qobject_cast<ScriptSession*>(engine->parent());
    session->tic();
    return QScriptValue(QScriptValue::UndefinedValue);
}
QScriptValue tocfunc(QScriptContext *context, QScriptEngine *engine)
{
    Q_UNUSED(context);
    ScriptSession* session = qobject_cast<ScriptSession*>(engine->parent());
    return QScriptValue(session->toc());
}
QScriptValue log(QScriptContext *context, QScriptEngine *engine)
{
    if (context->argumentCount()!=1) {
        return context->throwError(QScriptContext::SyntaxError,
                            "log must be called with 1 argument\n"
                            "  Usage: log(x)");
    }
    ScriptSession* session = qobject_cast<ScriptSession*>(engine->parent());
    QString msg = context->argument(0).toString() + "\n";
    session->widget()->writeStdOut(msg.toLatin1());
    return QScriptValue(QScriptValue::UndefinedValue);
}

QScriptValue wait(QScriptContext *context, QScriptEngine *engine)
{
    if (context->argumentCount()!=1) {
        return context->throwError(QScriptContext::SyntaxError,
                            "wait must be called with 1 argument\n"
                            "  Usage: wait(ms)");
    }
    ScriptSession* session = qobject_cast<ScriptSession*>(engine->parent());
    int msecs = context->argument(0).toUInt32();

    QEventLoop loop;
    QObject::connect(session->widget(),SIGNAL(abortEvaluation()),&loop,SLOT(quit()));
    QTimer::singleShot(msecs,Qt::PreciseTimer,&loop,SLOT(quit()));

    loop.exec();

    return QScriptValue(QScriptValue::UndefinedValue);
}

ScriptSession::ScriptSession(QConsoleWidget *aw) : QObject(), w(aw)
{
    // Create script engine and add extra functions
    e = new QScriptEngine(this);

    QScriptValue v;
    v = e->newFunction(quitfunc);
    e->globalObject().setProperty("quit", v);
    e->globalObject().setProperty("exit", v);

    v = e->newFunction(log);
    e->globalObject().setProperty("log", v);

    v = e->newFunction(wait);
    e->globalObject().setProperty("wait", v);

    v = e->newFunction(ticfunc);
    e->globalObject().setProperty("tic", v);

    v = e->newFunction(tocfunc);
    e->globalObject().setProperty("toc", v);

    // arrange to quit with the application
    connect(qApp,SIGNAL(lastWindowClosed()),this,SLOT(quit()));

    // Prepare QConsoleWidget
    w->device()->open(QIODevice::ReadWrite);
    connect(w,SIGNAL(abortEvaluation()),this,SLOT(abortEvaluation()));

    // select a console font
#if defined(Q_OS_MAC)
    w->setFont(QFont("Monaco"));
#elif defined(Q_OS_UNIX)
    w->setFont(QFont("Monospace"));
#elif defined(Q_OS_WIN)
    w->setFont(QFont("Courier New"));
#endif

//    QTextCharFormat fmt = w->channelCharFormat(QConsoleWidget::StandardInput);
//    fmt.setUnderlineStyle(QTextCharFormat::SingleUnderline);
//    w->setChannelCharFormat(QConsoleWidget::StandardInput,fmt);

    // write preample
    w->writeStdOut(
                "QConsoleWidget example:"
                " interactive qscript interpreter\n\n"
                "Additional commands:\n"
                "  - quit()   : end program\n"
                "  - exit()   : same\n"
                "  - log(x)   : printout x.toString()\n"
                "  - wait(ms) : block qscript execution for given ms\n"
                "  - tic()    : start timer\n"
                "  - toc()    : return elapsed ms\n\n"
                "Ctrl-Q aborts a qscript evaluation\n\n"
                );

    // Set the completer
    QScriptCompleter* c = new QScriptCompleter;
    c->seScripttEngine(e);
    w->setCompleter(c);
    // A "." triggers the completer
    QStringList tr;
    tr << ".";
    w->setCompletionTriggers(tr);

    // init the internal timer for tic()/toc()
    tmr_ = new QElapsedTimer();

}

void ScriptSession::tic()
{
    tmr_->start();
}
qreal ScriptSession::toc()
{
    return  1.e-6*tmr_->nsecsElapsed();
}

void ScriptSession::quit()
{
    w->device()->close();
}

void ScriptSession::abortEvaluation()
{
    e->abortEvaluation();
}

void ScriptSession::REPL()
{
    QIODevice* d = w->device();
    QTextStream ws(d);
    QString multilineCode;

    while(ws.device()->isOpen()) {

        ws << (multilineCode.isEmpty() ? "qs> " : "....> ") << flush;

        ws >> inputMode >> waitForInput;

        multilineCode += ws.readAll();

        if (!multilineCode.isEmpty())
        {

            if (e->canEvaluate(multilineCode))
            {
                QScriptValue ret = e->evaluate(multilineCode);
                multilineCode = "";

                if (e->hasUncaughtException())
                {
                    ws << errChannel;
                    if (!ret.isUndefined()) ws << ret.toString();
                    ws << endl;
                    ws << e->uncaughtExceptionBacktrace().join("\n") << endl;
                    ws << outChannel;
                }
                else if (ret.isValid() && !ret.isUndefined())
                    ws << ret.toString() << endl;
            }
        }
    }

}




