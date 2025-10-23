#include "QConsoleWidget.h"
#include "QConsoleIODevice.h"

#include <QMenu>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QApplication>
#include <QTextDocumentFragment>
#include <QTextBlock>
#include <QTextCursor>
#include <QDebug>
#include <QStringListModel>
#include <QScrollBar>
#include <QAbstractItemView>
#include <QClipboard>
#include <QMimeData>

QConsoleWidget::QConsoleWidget(QWidget* parent)
    : QPlainTextEdit(parent), mode_(Output), completer_(0)
{
    iodevice_ = new QConsoleIODevice(this,this);

    QTextCharFormat fmt = currentCharFormat();
    for(int i=0; i<nConsoleChannels; i++)
        chanFormat_[i] = fmt;

    chanFormat_[StandardOutput].setForeground(Qt::darkBlue);
    chanFormat_[StandardError].setForeground(Qt::red);

    setTextInteractionFlags(Qt::TextEditorInteraction);
    setUndoRedoEnabled(false);
}

QConsoleWidget::~QConsoleWidget()
{
}

void QConsoleWidget::setMode(ConsoleMode m)
{
    if (m==mode_) return;

    if (m==Input) {
        QTextCursor cursor = textCursor();
        cursor.movePosition(QTextCursor::End);
        setTextCursor(cursor);
        setCurrentCharFormat(chanFormat_[StandardInput]);
        inpos_ = cursor.position();
        mode_ = Input;
    }

    if (m==Output) {
        mode_ = Output;
    }
}

QString QConsoleWidget::getCommandLine()
{
    if (mode_==Output) return QString();
    // select text in edit zone (from the input pos to the end)
    QTextCursor textCursor = this->textCursor();
    textCursor.movePosition(QTextCursor::End);
    textCursor.setPosition(inpos_,QTextCursor::KeepAnchor);
    QString code = textCursor.selectedText();
    code.replace(QChar::ParagraphSeparator,QChar::LineFeed);
    return code;
}

void QConsoleWidget::handleReturnKey()
{
    QString code = getCommandLine();

    // start new block
    appendPlainText(QString());
    setMode(Output);

    QTextCursor textCursor = this->textCursor();
    textCursor.movePosition(QTextCursor::End);
    setTextCursor(textCursor);

    // Update the history
    if (!code.isEmpty()) history_.add(code);

    // append the newline char and
    // send signal / update iodevice
    code += "\n";
    if (iodevice_->isOpen())
        iodevice_->consoleWidgetInput(code);
    else {
        emit consoleCommand(code);
    }
}

void QConsoleWidget::handleTabKey()
{
    QTextCursor tc = this->textCursor();
    int anchor = tc.anchor();
    int position = tc.position();
    tc.setPosition(inpos_);
    tc.setPosition(position, QTextCursor::KeepAnchor);
    QString text = tc.selectedText().trimmed();
    tc.setPosition(anchor, QTextCursor::MoveAnchor);
    tc.setPosition(position, QTextCursor::KeepAnchor);
    if (text.isEmpty())
    {
        tc.insertText("    ");
    }
    else
    {
        updateCompleter();
        if (completer_ && completer_->completionCount() == 1)
        {
            insertCompletion(completer_->currentCompletion());
            completer_->popup()->hide();
        }
    }
}

void QConsoleWidget::updateCompleter()
{
    if (!completer_) return;

    // if the completer is first shown, mark
    // the text position
    QTextCursor textCursor = this->textCursor();
    if (!completer_->popup()->isVisible()) {
        completion_pos_ = textCursor.position();
        // qDebug() << "show completer, pos " << completion_pos_;
    }

    // Get the text between the current cursor position
    // and the start of the input
    textCursor.setPosition(inpos_, QTextCursor::KeepAnchor);
    QString commandText = textCursor.selectedText();
    // qDebug() << "code to complete: " << commandText;

    // Call the completer to update the completion model
    // Place and show the completer if there are available completions
    int count;
    if ((count = completer_->updateCompletionModel(commandText)))
    {
        // qDebug() << "found " << count << " completions";

        // Get a QRect for the cursor at the start of the
        // current word and then translate it down 8 pixels.
        textCursor = this->textCursor();
        textCursor.movePosition(QTextCursor::StartOfWord);
        QRect cr = this->cursorRect(textCursor);
        cr.translate(0, 8);
        cr.setWidth(completer_->popup()->sizeHintForColumn(0) +
                    completer_->popup()->verticalScrollBar()->sizeHint().width());
        completer_->complete(cr);
    }
    else
    {
        // qDebug() << "no completions - hiding";
        completer_->popup()->hide();
    }
}

void QConsoleWidget::checkCompletionTriggers(const QString &txt)
{
    if (!completer_ || completion_triggers_.isEmpty() || txt.isEmpty()) return;

    foreach(const QString& tr, completion_triggers_)
    {
        if (tr.endsWith(txt))
        {
            QTextCursor tc = this->textCursor();
            tc.movePosition(QTextCursor::Left,QTextCursor::KeepAnchor,tr.length());
            if (tc.selectedText()==tr) {
                updateCompleter();
                return;
            }
        }
    }
}

void QConsoleWidget::insertCompletion(const QString& completion)
{    
    QTextCursor tc = textCursor();
    tc.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor,
                    tc.position() - inpos_ - completer_->insertPos());
    tc.insertText(completion,chanFormat_[StandardInput]);
    setTextCursor(tc);
}

void QConsoleWidget::setCompleter(QConsoleWidgetCompleter *c)
{
    if (completer_)
    {
        completer_->setWidget(0);
        QObject::disconnect(completer_, SIGNAL(activated(const QString&)), this,
                            SLOT(insertCompletion(const QString&)));
    }
    completer_ = c;
    if (completer_)
    {
        completer_->setWidget(this);
        QObject::connect(completer_, SIGNAL(activated(const QString&)), this,
                         SLOT(insertCompletion(const QString&)));
    }
}

void QConsoleWidget::keyPressEvent(QKeyEvent* e)
{
    if (completer_ && completer_->popup()->isVisible())
    {
        // The following keys are forwarded by the completer to the widget
        switch (e->key())
        {
        case Qt::Key_Tab:
        case Qt::Key_Enter:
        case Qt::Key_Return:
        case Qt::Key_Escape:
        case Qt::Key_Backtab:
            e->ignore();
            return; // let the completer do default behavior
        default:
            break;
        }
    }

    QTextCursor textCursor   = this->textCursor();
    bool selectionInEditZone = isSelectionInEditZone();

    // check for user abort request
    if (e->modifiers() & Qt::ControlModifier)
    {
        if (e->key()==Qt::Key_Q) // Ctrl-Q aborts
        {
            emit abortEvaluation();
            e->accept();
            return;
        }
    }

    // Allow copying anywhere in the console ...
    if (e->key() == Qt::Key_C && e->modifiers() == Qt::ControlModifier)
    {
        if (textCursor.hasSelection()) copy();
        e->accept();
        return;
    }

    // the rest of key events are ignored during output mode
    if (mode()!=Input) {
        e->ignore();
        return;
    }

    // Allow cut only if the selection is limited to the interactive area ...
    if (e->key() == Qt::Key_X && e->modifiers() == Qt::ControlModifier)
    {
        if (selectionInEditZone) cut();
        e->accept();
        return;
    }

    // Allow paste only if the selection is in the interactive area ...
    if (e->key() == Qt::Key_V && e->modifiers() == Qt::ControlModifier)
    {
        if (selectionInEditZone || isCursorInEditZone())
        {
            const QMimeData* const clipboard = QApplication::clipboard()->mimeData();
            const QString text = clipboard->text();
            if (!text.isNull())
            {
                textCursor.insertText(text,channelCharFormat(StandardInput));
            }
        }

        e->accept();
        return;
    }


    int key = e->key();
    int shiftMod = e->modifiers() == Qt::ShiftModifier;

    if (history_.isActive() && key!=Qt::Key_Up && key!=Qt::Key_Down)
        history_.deactivate();

    // Force the cursor back to the interactive area
    // for all keys except modifiers
    if (!isCursorInEditZone() &&
            key != Qt::Key_Control &&
            key != Qt::Key_Shift &&
            key != Qt::Key_Alt)
    {
        textCursor.movePosition(QTextCursor::End);
        setTextCursor(textCursor);
    }

    switch (key)
    {
    case Qt::Key_Up:
        // Activate the history and move to the 1st matching history item
        if (!history_.isActive()) history_.activate(getCommandLine());
        if (history_.move(true))
            replaceCommandLine(history_.currentValue());
        else QApplication::beep();
        e->accept();
        break;

    case Qt::Key_Down:
        if (history_.move(false))
            replaceCommandLine(history_.currentValue());
        else QApplication::beep();
        e->accept();

    case Qt::Key_Left:
        if (textCursor.position() > inpos_)
            QPlainTextEdit::keyPressEvent(e);
        else
        {
            QApplication::beep();
            e->accept();
        }
        break;

    case Qt::Key_Delete:
        e->accept();
        if (selectionInEditZone) cut();
        else
        {
            // cursor must be in edit zone
            if (textCursor.position() < inpos_) QApplication::beep();
            else QPlainTextEdit::keyPressEvent(e);
        }
        break;

    case Qt::Key_Backspace:
        e->accept();
        if (selectionInEditZone) cut();
        else
        {
            // cursor must be in edit zone
            if (textCursor.position() <= inpos_) QApplication::beep();
            else QPlainTextEdit::keyPressEvent(e);
        }
        break;

    case Qt::Key_Tab:
        e->accept();
        handleTabKey();
        return;

    case Qt::Key_Home:
        e->accept();
        textCursor.setPosition(inpos_,
                               shiftMod ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor);
        setTextCursor(textCursor);
        break;

    case Qt::Key_Enter:
    case Qt::Key_Return:
        e->accept();
        handleReturnKey();
        break;


    case Qt::Key_Escape:
        e->accept();
        replaceCommandLine(QString());
        break;


    default:
        e->accept();
        setCurrentCharFormat(chanFormat_[StandardInput]);
        QPlainTextEdit::keyPressEvent(e);
        // check if the last key triggers a completion
        checkCompletionTriggers(e->text());
        break;
    }

    if (completer_ && completer_->popup()->isVisible())
    {
        // if the completer is visible check if it should be updated
        if (this->textCursor().position()<completion_pos_)
            completer_->popup()->hide();
        else
            updateCompleter();
    }
}

void QConsoleWidget::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu *menu = createStandardContextMenu();

    QAction* a;
    if ((a = menu->findChild<QAction*>("edit-cut")))
        a->setEnabled(canCut());
    if ((a = menu->findChild<QAction*>("edit-delete")))
        a->setEnabled(canCut());
    if ((a = menu->findChild<QAction*>("edit-paste")))
        a->setEnabled(canPaste());

    menu->exec(event->globalPos());
    delete menu;
}

bool QConsoleWidget::isSelectionInEditZone() const
{
    QTextCursor textCursor = this->textCursor();
    if (!textCursor.hasSelection()) return false;

    int selectionStart    = textCursor.selectionStart();
    int selectionEnd      = textCursor.selectionEnd();

    return selectionStart >= inpos_ && selectionEnd >= inpos_;
}

bool QConsoleWidget::isCursorInEditZone() const
{
    return textCursor().position()>=inpos_;
}

bool QConsoleWidget::canPaste() const
{
    QTextCursor textCursor = this->textCursor();
    if (textCursor.position()<inpos_) return false;
    if (textCursor.anchor()<inpos_) return false;
    return true;
}

void QConsoleWidget::replaceCommandLine(const QString& str) {

    // Select the text after the last command prompt ...
    QTextCursor textCursor = this->textCursor();
    textCursor.movePosition(QTextCursor::End);
    textCursor.setPosition(inpos_, QTextCursor::KeepAnchor);

    // ... and replace it with new string.
    textCursor.insertText(str,chanFormat_[StandardInput]);

    // move to the end of the document
    textCursor.movePosition(QTextCursor::End);
    setTextCursor(textCursor);
}

void QConsoleWidget::write(const QString & message, const QTextCharFormat& fmt)
{
    QTextCharFormat currfmt = currentCharFormat();
    QTextCursor tc = textCursor();

    if (mode()==Input)
    {
        // in Input mode output messages are inserted
        // before the edit block

        // get offset of current pos from the end
        int editpos = tc.position();
        tc.movePosition(QTextCursor::End);
        editpos = tc.position() - editpos;

        // convert the input pos as relative from the end
        inpos_ = tc.position() - inpos_;

        // insert block
        tc.movePosition(QTextCursor::StartOfBlock);
        tc.insertBlock();
        tc.movePosition(QTextCursor::PreviousBlock);

        tc.insertText(message,fmt);
        tc.movePosition(QTextCursor::End);
        // restore input pos
        inpos_ = tc.position() - inpos_;
        // restore the edit pos
        tc.movePosition(QTextCursor::Left,QTextCursor::MoveAnchor,editpos);
        setTextCursor(tc);
        setCurrentCharFormat(currfmt);
    }
    else
    {
        // in output mode messages are appended
        QTextCursor tc1 = tc;
        tc1.movePosition(QTextCursor::End);

        // check is cursor was not at the end
        // (e.g. had been moved per mouse action)
        bool needsRestore = tc1.position()!=tc.position();

        // insert text
        setTextCursor(tc1);
        textCursor().insertText(message,fmt);
        ensureCursorVisible();

        // restore cursor if needed
        if (needsRestore) setTextCursor(tc);
    }


}

void QConsoleWidget::writeStdOut(const QString& s)
{
    write(s,chanFormat_[StandardOutput]);
}

void QConsoleWidget::writeStdErr(const QString& s)
{
    write(s,chanFormat_[StandardError]);
}

/////////////////// QConsoleWidget::History /////////////////////

#define HISTORY_FILE ".command_history.lst"

QConsoleWidget::History QConsoleWidget::history_;

QConsoleWidget::History::History(void) : pos_(0), active_(false), maxsize_(10000)
{
    QFile f(HISTORY_FILE);
    if (f.open(QFile::ReadOnly)) {
        QTextStream is(&f);
        while(!is.atEnd()) add(is.readLine());
    }
}
QConsoleWidget::History::~History(void)
{
    QFile f(HISTORY_FILE);
    if (f.open(QFile::WriteOnly | QFile::Truncate)) {
        QTextStream os(&f);
        int n = strings_.size();
        while(n>0) os << strings_.at(--n) << endl;
    }
}

void QConsoleWidget::History::add(const QString& str)
{
    active_ = false;
    //if (strings_.contains(str)) return;
    if (strings_.size() == maxsize_) strings_.pop_back();
    strings_.push_front(str);
}

void QConsoleWidget::History::activate(const QString& tk)
{
    active_ = true;
    token_ = tk;
    pos_ = -1;
}

bool QConsoleWidget::History::move(bool dir)
{
    if (active_)
    {
        int next = indexOf ( dir, pos_ );
        if (pos_!=next)
        {
            pos_=next;
            return true;
        }
        else return false;
    }
    else return false;
}

int QConsoleWidget::History::indexOf(bool dir, int from) const
{
    int i = from, to = from;
    if (dir)
    {
        while(i < strings_.size()-1)
        {
            const QString& si = strings_.at(++i);
            if (si.startsWith(token_)) { return i; }
        }
    }
    else
    {
        while(i > 0)
        {
            const QString& si = strings_.at(--i);
            if (si.startsWith(token_)) { return i; }
        }
        return -1;
    }
    return to;
}

/////////////////// Stream manipulators /////////////////////

QTextStream &waitForInput(QTextStream &s)
{
    QConsoleIODevice* d = qobject_cast<QConsoleIODevice*>(s.device());
    if (d) d->waitForReadyRead(-1);
    return s;
}

QTextStream &inputMode(QTextStream &s)
{
    QConsoleIODevice* d = qobject_cast<QConsoleIODevice*>(s.device());
    if (d && d->widget()) d->widget()->setMode(QConsoleWidget::Input);
    return s;
}
QTextStream &outChannel(QTextStream &s)
{
    QConsoleIODevice* d = qobject_cast<QConsoleIODevice*>(s.device());
    if (d) d->setCurrentWriteChannel(QConsoleWidget::StandardOutput);
    return s;
}
QTextStream &errChannel(QTextStream &s)
{
    QConsoleIODevice* d = qobject_cast<QConsoleIODevice*>(s.device());
    if (d) d->setCurrentWriteChannel(QConsoleWidget::StandardError);
    return s;
}

