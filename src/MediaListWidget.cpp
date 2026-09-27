#include "MediaListWidget.h"

#include <QDrag>
#include <QListWidgetItem>
#include <QMimeData>
#include <QUrl>

MediaListWidget::MediaListWidget(QWidget *parent)
    : QListWidget(parent)
{
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setDragEnabled(true);
}

void MediaListWidget::startDrag(Qt::DropActions)
{
    const auto items = selectedItems();
    if (items.isEmpty())
        return;

    QList<QUrl> urls;
    for (QListWidgetItem *item : items) {
        const QString path = item->data(Qt::UserRole).toString();
        if (!path.isEmpty())
            urls.push_back(QUrl::fromLocalFile(path));
    }

    if (urls.isEmpty())
        return;

    auto *mime = new QMimeData;
    mime->setUrls(urls);

    auto *drag = new QDrag(this);
    drag->setMimeData(mime);
    drag->exec(Qt::CopyAction);
}
