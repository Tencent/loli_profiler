#ifndef QSCRIPTCOMPLETER_H
#define QSCRIPTCOMPLETER_H

#include "QConsoleWidget.h"

class QScriptEngine;

class QScriptCompleter : public QConsoleWidgetCompleter
{
public:
    QScriptCompleter();
    int updateCompletionModel(const QString& code) override;
    int insertPos() override
    { return insert_pos_; }

    void seScripttEngine(QScriptEngine* e)
    { eng = e; }

private:
    QStringList introspection(const QString& lookup);
    QScriptEngine* eng;
    QStringList js_keywords_;
    int insert_pos_;
};

#endif // QSCRIPTCOMPLETER_H
