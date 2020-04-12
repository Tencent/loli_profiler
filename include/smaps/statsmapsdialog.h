#ifndef STATSMAPSDIALOG_H
#define STATSMAPSDIALOG_H

#include <QDialog>
#include "smapssection.h"

class StatSmapsDialog : public QDialog {
    Q_OBJECT
public:
    explicit StatSmapsDialog(QWidget *parent = nullptr);
    ~StatSmapsDialog();
    void ShowSmap(const QHash<QString, SMapsSection>& smap);
};

#endif // STATSMAPSDIALOG_H
