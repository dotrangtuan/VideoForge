#pragma once

#include <QListWidget>

class MediaListWidget : public QListWidget
{
    Q_OBJECT

public:
    explicit MediaListWidget(QWidget *parent = nullptr);

protected:
    void startDrag(Qt::DropActions supportedActions) override;
};
