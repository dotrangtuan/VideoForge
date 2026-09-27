#pragma once
#include <QDialog>
#include "ExportQueue.h"
class QTableWidget;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class ExportQueueDialog : public QDialog {
    Q_OBJECT
public:
    explicit ExportQueueDialog(ExportQueue *queue,QWidget *parent=nullptr);
signals:
    void addRequested();
private:
    void refresh();
    void details();
    void report(bool success,const QString &error);
    ExportQueue *m_queue;
    QTableWidget *m_table;
    QLabel *m_status;
    QPlainTextEdit *m_details;
    QPushButton *m_start,*m_stop,*m_cancel,*m_retry,*m_remove,*m_up,*m_down,*m_output,*m_add;
};
