#pragma once

#include "ClipItem.h"

#include <QListWidget>

class SourceListWidget : public QListWidget {
    Q_OBJECT

public:
    explicit SourceListWidget(QWidget *parent = nullptr);

protected:
    QMimeData *mimeData(const QList<QListWidgetItem *> &items) const override;
    QStringList mimeTypes() const override;
};
