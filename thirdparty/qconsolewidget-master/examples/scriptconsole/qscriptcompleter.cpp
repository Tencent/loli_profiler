#include "qscriptcompleter.h"

#include <QDebug>
#include <QStringListModel>
#include <QScriptEngine>
#include <QScriptValue>
#include <QScriptValueIterator>

QScriptCompleter::QScriptCompleter()
{
    // https://developer.mozilla.org/en/JavaScript/Reference/Reserved_Words
    js_keywords_ << "break";
    js_keywords_ << "case";
    js_keywords_ << "catch";
    js_keywords_ << "continue";
    js_keywords_ << "default";
    js_keywords_ << "delete";
    js_keywords_ << "do";
    js_keywords_ << "else";
    js_keywords_ << "finally";
    js_keywords_ << "for";
    js_keywords_ << "function";
    js_keywords_ << "if";
    js_keywords_ << "in";
    js_keywords_ << "instanceof";
    js_keywords_ << "new";
    js_keywords_ << "return";
    js_keywords_ << "switch";
    js_keywords_ << "this";
    js_keywords_ << "throw";
    js_keywords_ << "try";
    js_keywords_ << "typeof";
    js_keywords_ << "var";
    js_keywords_ << "void";
    js_keywords_ << "while";
    js_keywords_ << "with";

    js_keywords_ << "true";
    js_keywords_ << "false";
    js_keywords_ << "null";
}

int QScriptCompleter::updateCompletionModel(const QString& code)
{
    // Start by clearing the model
    this->setModel(0);

    // Don't try to complete the empty string
    if (code.isEmpty())
    {
      return 0;
    }

    // Search backward through the string for usable characters
    QString textToComplete;
    for(int i=code.length()-1; i>=0; i--)
    {
        QChar c = code.at(i);
        if (c.isLetterOrNumber() || c == '.' || c == '_')
        {
            textToComplete.prepend(c);
            insert_pos_ = i;
        }
        else
        {
            break;
        }
    }

    // Split the string at the last dot, if one exists
    QString lookup;
    QString compareText = textToComplete;
    int dot = compareText.lastIndexOf('.');
    if (dot != -1)
    {
      lookup = compareText.mid(0, dot);
      compareText = compareText.mid(dot + 1);
      insert_pos_ += (dot+1);
    }

    // Lookup QtScript names
    QStringList found;
    if (!lookup.isEmpty() || !compareText.isEmpty())
    {
      compareText = compareText.toLower();
      QStringList l = introspection(lookup);
      foreach (QString n, l)
        if (n.toLower().startsWith(compareText)) found << n;
    }

    /*
    qDebug() << "lookup : " << lookup;
    qDebug() << "compareText : " << compareText;
    qDebug() << "insert pos : " << insert_pos_;
    qDebug() << "found : " << found.size();
    */

    // Initialize the completion model
    if (!found.isEmpty())
    {
      setCompletionMode(QCompleter::PopupCompletion);
      setModel(new QStringListModel(found, this));
      setCaseSensitivity(Qt::CaseInsensitive);
      setCompletionPrefix(compareText.toLower());
      //if (popup())
        //popup()->setCurrentIndex(completionModel()->index(0, 0));
    }

    return found.size();
}

QStringList QScriptCompleter::introspection(const QString &lookup)
{
    // list of found tokens
    QStringList properties, children, functions;

    if (!eng) return properties;

    QScriptValue scriptObj;
    if (lookup.isEmpty()) {
        properties = js_keywords_;
        scriptObj = eng->globalObject();
    }
    else {
        scriptObj = eng->evaluate(lookup);
        // if the engine cannot recognize the variable return
        if (eng->hasUncaughtException()) return properties;
    }

     // if a QObject add the named children
    if (scriptObj.isQObject())
    {
        QObject* obj = scriptObj.toQObject();

        foreach(QObject* ch, obj->children())
        {
            QString name = ch->objectName();
            if (!name.isEmpty())
                children << name;
        }

    }

    // add the script properties
    {
        QScriptValue obj(scriptObj); // the object to iterate over
        while (obj.isObject()) {
            QScriptValueIterator it(obj);
            while (it.hasNext()) {
                it.next();

                // avoid array indices
                bool isIdx;
                it.scriptName().toArrayIndex(&isIdx);
                if (isIdx) continue;

                // avoid "hidden" properties starting with "__"
                if (it.name().startsWith("__")) continue;

                // include in list
                if (it.value().isQObject()) children << it.name();
                else if (it.value().isFunction()) functions << it.name();
                else properties << it.name();
            }
            obj = obj.prototype();
        }
    }

    children.removeDuplicates();
    children.sort(Qt::CaseInsensitive);
    functions.removeDuplicates();
    functions.sort(Qt::CaseInsensitive);
    properties.removeDuplicates();
    properties.sort(Qt::CaseInsensitive);

    children.append(properties);
    children.append(functions);

    return children;

}
