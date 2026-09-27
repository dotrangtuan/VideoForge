#include "ExportQueueDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QFileDialog>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QFileInfo>

ExportQueueDialog::ExportQueueDialog(ExportQueue *queue,QWidget *parent):QDialog(parent),m_queue(queue) {
    setWindowTitle("VideoForge Export Queue");setObjectName("exportQueueDialog");resize(1020,620);
    auto layout=new QVBoxLayout(this);auto hint=new QLabel("Each job keeps its own timeline, effects and output settings. Jobs run one at a time. Closing this window does not stop the queue.",this);hint->setWordWrap(true);layout->addWidget(hint);
    auto actions=new QHBoxLayout;
    auto button=[&](const QString &text,const char *name){auto b=new QPushButton(text,this);b->setObjectName(name);actions->addWidget(b);return b;};
    m_add=button("Add current sequence...","queueAdd");m_start=button("Start / Resume","queueStart");m_cancel=button("Cancel current","queueCancel");m_stop=button("Stop queue","queueStop");layout->addLayout(actions);
    m_table=new QTableWidget(0,5,this);m_table->setObjectName("queueTable");m_table->setHorizontalHeaderLabels({"Job / output","Frame size / fps","Encoder","Status","Progress"});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);m_table->setSelectionMode(QAbstractItemView::SingleSelection);m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);m_table->setSortingEnabled(false);
    m_table->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);for(int c=1;c<5;++c)m_table->horizontalHeader()->setSectionResizeMode(c,QHeaderView::ResizeToContents);
    layout->addWidget(m_table,1);actions=new QHBoxLayout;
    m_up=button("Up","queueUp");m_down=button("Down","queueDown");m_retry=button("Retry using CPU","queueRetry");m_output=button("Change output...","queueOutput");m_remove=button("Remove job","queueRemove");layout->addLayout(actions);
    m_status=new QLabel(this);m_status->setWordWrap(true);layout->addWidget(m_status);
    m_details=new QPlainTextEdit(this);m_details->setReadOnly(true);m_details->setMaximumHeight(170);m_details->setObjectName("queueDetails");layout->addWidget(m_details);
    connect(m_add,&QPushButton::clicked,this,&ExportQueueDialog::addRequested);
    connect(m_start,&QPushButton::clicked,this,[this]{QString error;report(m_queue->start(&error),error);});
    connect(m_stop,&QPushButton::clicked,m_queue,&ExportQueue::stop);connect(m_cancel,&QPushButton::clicked,m_queue,&ExportQueue::cancelCurrent);
    connect(m_retry,&QPushButton::clicked,this,[this]{QString error;report(m_queue->retry(m_table->currentRow(),true,&error),error);});
    connect(m_remove,&QPushButton::clicked,this,[this]{QString error;report(m_queue->remove(m_table->currentRow(),&error),error);});
    connect(m_up,&QPushButton::clicked,this,[this]{int row=m_table->currentRow();QString error;if(m_queue->move(row,-1,&error))m_table->selectRow(row-1);else report(false,error);});
    connect(m_down,&QPushButton::clicked,this,[this]{int row=m_table->currentRow();QString error;if(m_queue->move(row,1,&error))m_table->selectRow(row+1);else report(false,error);});
    connect(m_output,&QPushButton::clicked,this,[this]{
        const int row=m_table->currentRow();if(row<0)return;const QString id=m_queue->jobs()[row].id;
        auto path=QFileDialog::getSaveFileName(this,"Choose a new MP4 filename",m_queue->jobs()[row].outputPath,"MP4 (*.mp4)");if(path.isEmpty())return;if(!path.endsWith(".mp4",Qt::CaseInsensitive))path+=".mp4";
        // Rows can change while a native file dialog pumps events.
        for(int i=0;i<m_queue->jobs().size();++i)if(m_queue->jobs()[i].id==id){QString error;report(m_queue->changeOutput(i,path,&error),error);break;}
    });
    connect(m_table,&QTableWidget::itemSelectionChanged,this,&ExportQueueDialog::details);
    connect(m_queue,&ExportQueue::changed,this,&ExportQueueDialog::refresh);refresh();
}
void ExportQueueDialog::report(bool success,const QString &error){if(!success)QMessageBox::warning(this,"Export queue",error);}
void ExportQueueDialog::refresh() {
    const QSignalBlocker blocker(m_table);QString selected;
    if(m_table->currentRow()>=0 && m_table->item(m_table->currentRow(),0))selected=m_table->item(m_table->currentRow(),0)->data(Qt::UserRole).toString();
    const auto &jobs=m_queue->jobs();m_table->setRowCount(jobs.size());int selectedRow=-1,done=0,pending=0,failed=0;
    for(int row=0;row<jobs.size();++row) {
        const auto &job=jobs[row];QStringList values{job.name+" — "+QFileInfo(job.outputPath).fileName(),job.snapshot.sequence.description(),job.hardware?"Hardware preferred":"CPU",ExportQueue::statusName(job.status),QString::number(job.percent)+"%"};
        for(int col=0;col<5;++col){auto item=m_table->item(row,col);if(!item){item=new QTableWidgetItem;m_table->setItem(row,col,item);}item->setText(values[col]);item->setToolTip(job.outputPath);}
        m_table->item(row,0)->setData(Qt::UserRole,job.id);if(job.id==selected)selectedRow=row;
        done+=job.status==ExportJob::Completed;pending+=job.status==ExportJob::Pending;failed+=job.status==ExportJob::Failed;
    }
    if(selectedRow<0 && !jobs.isEmpty())selectedRow=qMax(0,m_queue->activeIndex());if(selectedRow>=0)m_table->selectRow(selectedRow);
    m_add->setEnabled(m_queue->available());m_start->setEnabled(m_queue->available() && !m_queue->busy() && pending>0);m_stop->setEnabled(m_queue->busy());m_cancel->setEnabled(m_queue->activeIndex()>=0);
    m_status->setText(m_queue->error().isEmpty()?QString("%1 | %2 completed, %3 pending, %4 failed | Queue saved automatically").arg(m_queue->busy()?"Working":"Stopped").arg(done).arg(pending).arg(failed):m_queue->error());details();
}
void ExportQueueDialog::details() {
    const auto &jobs=m_queue->jobs();const int row=m_table->currentRow();const bool valid=row>=0 && row<jobs.size();const bool writable=m_queue->available();
    for(auto b:{m_retry,m_remove,m_up,m_down,m_output})b->setEnabled(false);
    if(!valid){m_details->setPlainText("Queue file: "+m_queue->storePath()+"\nStop queue cancels the current job and keeps pending jobs. Closing the editor interrupts export; reopen and retry explicitly.");return;}
    const auto &job=jobs[row];const bool active=row==m_queue->activeIndex();
    m_remove->setEnabled(writable && !active);m_output->setEnabled(writable && !active && job.status!=ExportJob::Completed);
    m_retry->setEnabled(writable && (job.status==ExportJob::Failed || job.status==ExportJob::Cancelled || job.status==ExportJob::Interrupted));
    m_up->setEnabled(writable && job.status==ExportJob::Pending && row>0 && jobs[row-1].status==ExportJob::Pending);
    m_down->setEnabled(writable && job.status==ExportJob::Pending && row+1<jobs.size() && jobs[row+1].status==ExportJob::Pending);
    QString text=job.outputPath+"\n"+job.snapshot.sequence.description()+QString(" | %1 clips | %2\n").arg(job.snapshot.clips.size()).arg(ExportQueue::statusName(job.status))+job.message;
    const auto range=job.snapshot.exportRange;
    text+=range.enabled ? QString("\nIn-Out: %1 s - %2 s | Duration: %3 s").arg(range.inMs/1000.0,0,'f',3).arg(range.outMs/1000.0,0,'f',3).arg((range.outMs-range.inMs)/1000.0,0,'f',3) : "\nRange: Entire timeline";
    if(active)text+=QString("\nElapsed: %1 s | Remaining: %2 | Speed: %3x").arg(job.elapsedMs/1000).arg(job.remainingMs<0?"--":QString::number(job.remainingMs/1000)+" s").arg(job.speed,0,'f',2);
    text+="\n"+job.log;if(m_details->toPlainText()!=text)m_details->setPlainText(text);
}
