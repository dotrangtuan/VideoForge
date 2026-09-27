#pragma once
#include <QWidget>
#include "VideoEffects.h"
class QLabel;
class QListWidget;
class QComboBox;
class QPushButton;
class QDoubleSpinBox;

class EffectsPanel : public QWidget {
    Q_OBJECT
public:
    explicit EffectsPanel(QWidget *parent=nullptr);
    void setClip(int id,const QString &name,const QVector<VideoEffect> &effects);
    void commitPendingEdits();
signals:
    void editRequested(int clipId,const QVector<VideoEffect> &effects);
private:
    void rebuild(int row);
    void showParameters();
    void publish(int row);
    int m_clipId=-1;
    bool m_updating=false;
    QVector<VideoEffect> m_effects;
    QLabel *m_title,*m_hint;
    QListWidget *m_list;
    QComboBox *m_type;
    QPushButton *m_add,*m_up,*m_down,*m_remove;
    std::array<QLabel*,4> m_labels{};
    std::array<QDoubleSpinBox*,4> m_values{};
};
