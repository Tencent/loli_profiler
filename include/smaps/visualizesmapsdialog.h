#ifndef VISUALIZESMAPSDIALOG_H
#define VISUALIZESMAPSDIALOG_H

#include <QDialog>
#include <QAbstractItemModel>
#include "smaps/smapssection.h"

class VisualizeSmapsDialog : public QDialog {
    Q_OBJECT
public:
    explicit VisualizeSmapsDialog(QWidget *parent = nullptr);
    ~VisualizeSmapsDialog();
    void VisualizeSmap(const QHash<QString, SMapsSection>& smap, QAbstractItemModel *curModel);
};

#endif // VISUALIZESMAPSDIALOG_H
