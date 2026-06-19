#include "SourceListWidget.h"

#include <QDataStream>
#include <QFileInfo>
#include <QMimeData>

namespace {
constexpr const char *SourceMimeType = "application/x-clipthing-source";
}

SourceListWidget::SourceListWidget(QWidget *parent)
    : QListWidget(parent)
{
    setDragEnabled(true);
    setDragDropMode(QAbstractItemView::DragOnly);
    setDefaultDropAction(Qt::CopyAction);
    setSelectionMode(QAbstractItemView::SingleSelection);
}

QMimeData *SourceListWidget::mimeData(const QList<QListWidgetItem *> &items) const
{
    auto *mimeData = new QMimeData();
    if (items.isEmpty()) {
        return mimeData;
    }

    const QListWidgetItem *item = items.first();
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream << item->data(Qt::UserRole).toString();
    stream << item->data(Qt::UserRole + 1).toInt();
    stream << item->data(Qt::UserRole + 2).toDouble();
    mimeData->setData(QString::fromLatin1(SourceMimeType), payload);
    mimeData->setText(QFileInfo(item->data(Qt::UserRole).toString()).fileName());
    return mimeData;
}

QStringList SourceListWidget::mimeTypes() const
{
    return { QString::fromLatin1(SourceMimeType) };
}
