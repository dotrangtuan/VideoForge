#include "MainWindow.h"
#include "ExportQueue.h"
#include "ExportQueueDialog.h"
#include "EffectsPanel.h"
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QMessageBox>
#include <QRadioButton>
#include <QTableWidget>
#include <QHeaderView>

void MainWindow::showExportQueue(){m_queueDialog->show();m_queueDialog->raise();m_queueDialog->activateWindow();}
void MainWindow::addExportToQueue(){addExportJob(false);}
void MainWindow::addV2ExportsToQueue() {
    if(!m_queue->available()){QMessageBox::warning(this,"Export queue",m_queue->error());return;}
    commitClipAudio();m_effectsPanel->commitPendingEdits();
    const auto snapshot=projectState();const auto clips=ExportQueue::v2Videos(snapshot);
    if(clips.isEmpty()){QMessageBox::information(this,"V2 batch export","Add video clips to V2 first. Images do not count as videos.");return;}
    QDialog dialog(this);dialog.setObjectName("v2BatchDialog");dialog.setWindowTitle("Add V2 videos to export queue");
    auto *layout=new QVBoxLayout(&dialog);
    auto *first=new QRadioButton("First videos, from left to right",&dialog);first->setObjectName("v2FirstVideos");first->setChecked(true);
    auto *all=new QRadioButton(QString("All videos on V2 (%1)").arg(clips.size()),&dialog);all->setObjectName("v2AllVideos");
    auto *count=new QSpinBox(&dialog);count->setObjectName("v2VideoCount");count->setRange(1,int(clips.size()));
    auto *choice=new QHBoxLayout;choice->addWidget(first);choice->addWidget(count);choice->addStretch();layout->addLayout(choice);layout->addWidget(all);
    auto *folder=new QLineEdit(QFileInfo(clips.first().filePath).absolutePath(),&dialog);folder->setObjectName("v2OutputFolder");
    auto *browse=new QPushButton("Output folder...",&dialog);auto *row=new QHBoxLayout;row->addWidget(folder,1);row->addWidget(browse);layout->addLayout(row);
    connect(browse,&QPushButton::clicked,&dialog,[&]{const auto path=QFileDialog::getExistingDirectory(&dialog,"Choose output folder",folder->text());if(!path.isEmpty())folder->setText(path);});
    auto *settings=new QLabel("Output: "+snapshot.sequence.description(),&dialog);layout->addWidget(settings);
    auto *hardware=new QCheckBox("Prefer hardware encoder",&dialog);hardware->setObjectName("v2Hardware");hardware->setChecked(m_hardwareEncode->isChecked());layout->addWidget(hardware);
    auto *table=new QTableWidget(&dialog);table->setObjectName("v2BatchPreview");table->setColumnCount(3);table->setHorizontalHeaderLabels({"V2 video / output name","Start","Duration"});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);table->setSelectionBehavior(QAbstractItemView::SelectRows);table->setSortingEnabled(false);
    table->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);for(int column=1;column<3;++column)table->horizontalHeader()->setSectionResizeMode(column,QHeaderView::ResizeToContents);
    layout->addWidget(table,1);
    auto *summary=new QLabel(&dialog);summary->setObjectName("v2BatchSummary");summary->setWordWrap(true);layout->addWidget(summary);
    auto *hint=new QLabel("Each job uses its V2 clip's time span, including V1 background and the other visible layers/audio, with current FX and mute settings. Editor In/Out is ignored. Names follow V2 clips; duplicate names receive a numeric suffix. Files are never overwritten.",&dialog);hint->setWordWrap(true);layout->addWidget(hint);
    auto *errorLabel=new QLabel(&dialog);errorLabel->setObjectName("v2BatchError");errorLabel->setWordWrap(true);errorLabel->setStyleSheet("color: #c33;");layout->addWidget(errorLabel);
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);buttons->button(QDialogButtonBox::Ok)->setText("Add to Queue");layout->addWidget(buttons);
    auto refresh=[&]{
        count->setEnabled(!all->isChecked());const int selected=all->isChecked()?int(clips.size()):count->value();
        if(table->rowCount()!=selected) {
            table->setRowCount(selected);
            for(int i=0;i<selected;++i) {
                const auto &clip=clips[i];const auto &s=snapshot.sequence;
                const QStringList values{clip.name.isEmpty()?QFileInfo(clip.filePath).fileName():clip.name,s.timecode(clip.startMs),s.timecodeForFrame(s.nearestFrame(clip.startMs+clip.durationMs())-s.nearestFrame(clip.startMs))};
                for(int c=0;c<3;++c)table->setItem(i,c,new QTableWidgetItem(values[c]));
            }
        }
        const int room=100-int(m_queue->jobs().size());
        summary->setText(QString("%1 job(s) selected | Queue has room for %2 more. Duration uses the sequence frame rate.").arg(selected).arg(room));
        buttons->button(QDialogButtonBox::Ok)->setEnabled(selected<=room);
    };
    connect(count,qOverload<int>(&QSpinBox::valueChanged),&dialog,refresh);connect(all,&QRadioButton::toggled,&dialog,refresh);connect(m_queue,&ExportQueue::changed,&dialog,refresh);refresh();
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{
        count->interpretText();QString error;
        if(!m_queue->addV2Clips(snapshot,all->isChecked()?-1:count->value(),folder->text().trimmed(),hardware->isChecked(),&error)){errorLabel->setText(error);return;}
        dialog.accept();
    });
    dialog.resize(660,520);if(dialog.exec()==QDialog::Accepted)showExportQueue();
}
void MainWindow::addExportJob(bool startImmediately) {
    if(!m_queue->available()){QMessageBox::warning(this,"Export queue",m_queue->error());return;}
    commitClipAudio();m_effectsPanel->commitPendingEdits();
    auto snapshot=projectState();if(snapshot.clips.isEmpty()){QMessageBox::information(this,"Export queue","Add clips to the timeline first.");return;}
    QDialog dialog(this);dialog.setObjectName("addExportJobDialog");dialog.setWindowTitle("Add sequence to export queue");
    auto layout=new QVBoxLayout(&dialog);auto form=new QFormLayout;
    auto name=new QLineEdit(m_projectPath.isEmpty()?"Sequence":QFileInfo(m_projectPath).completeBaseName(),&dialog);name->setObjectName("queueJobName");name->setMaxLength(256);form->addRow("Job name",name);
    qint64 duration=0;for(const auto &clip:snapshot.clips)duration=qMax(duration,clip.startMs+clip.durationMs());
    auto *scope=new QComboBox(&dialog);scope->setObjectName("queueExportScope");scope->addItems({"Entire timeline","In / Out range"});scope->setCurrentIndex(snapshot.exportRange.enabled?1:0);form->addRow("Export",scope);
    auto *in=new QDoubleSpinBox(&dialog),*out=new QDoubleSpinBox(&dialog);in->setObjectName("queueExportIn");out->setObjectName("queueExportOut");
    for(auto *box:{in,out}){box->setRange(0,duration/1000.0);box->setDecimals(3);box->setSuffix(" s");box->setKeyboardTracking(false);box->setEnabled(scope->currentIndex()==1);}
    in->setValue(snapshot.exportRange.inMs/1000.0);out->setValue((snapshot.exportRange.outMs?snapshot.exportRange.outMs:duration)/1000.0);
    form->addRow("In",in);form->addRow("Out (exclusive)",out);
    connect(scope,qOverload<int>(&QComboBox::currentIndexChanged),&dialog,[&](int index){in->setEnabled(index==1);out->setEnabled(index==1);});
    auto path=new QLineEdit(&dialog);path->setObjectName("queueOutputPath");
    const auto directory=QFileInfo(snapshot.clips.first().filePath).absoluteDir();int suffix=1;QString suggested;
    do {suggested=directory.absoluteFilePath(QString("VideoForge_export_%1.mp4").arg(suffix++,3,10,QLatin1Char('0')));bool queued=false;for(const auto &job:m_queue->jobs())queued|=job.outputPath.compare(suggested,Qt::CaseInsensitive)==0;if(!queued && !QFileInfo::exists(suggested))break;}while(suffix<10000);
    path->setText(suggested);auto browse=new QPushButton("Browse...",&dialog);auto outputRow=new QHBoxLayout;outputRow->addWidget(path,1);outputRow->addWidget(browse);form->addRow("Output MP4",outputRow);
    connect(browse,&QPushButton::clicked,&dialog,[&]{auto chosen=QFileDialog::getSaveFileName(&dialog,"New output filename",path->text(),"MP4 (*.mp4)");if(!chosen.isEmpty())path->setText(chosen);});
    auto preset=new QComboBox(&dialog);preset->setObjectName("queueSizePreset");preset->addItems({"Current / Custom","HD 1280 x 720","Full HD 1920 x 1080","Portrait 1080 x 1920","Square 1080 x 1080","UHD 3840 x 2160"});form->addRow("Frame size",preset);
    auto width=new QSpinBox(&dialog);auto height=new QSpinBox(&dialog);width->setObjectName("queueWidth");height->setObjectName("queueHeight");
    for(auto box:{width,height}){box->setRange(64,4096);box->setSingleStep(2);}width->setValue(snapshot.sequence.width);height->setValue(snapshot.sequence.height);
    form->addRow("Width",width);form->addRow("Height",height);
    connect(preset,qOverload<int>(&QComboBox::activated),&dialog,[&](int index){const std::array<QSize,5> sizes{{{1280,720},{1920,1080},{1080,1920},{1080,1080},{3840,2160}}};if(index>0){width->setValue(sizes[index-1].width());height->setValue(sizes[index-1].height());}});
    auto rate=new QComboBox(&dialog);rate->setObjectName("queueTimebase");for(const auto &fps:SequenceSettings::rates()){rate->addItem(QString::fromLatin1(fps.label)+" fps");if(fps.numerator==snapshot.sequence.fpsNum && fps.denominator==snapshot.sequence.fpsDen)rate->setCurrentIndex(rate->count()-1);}form->addRow("Timebase",rate);
    auto hardware=new QCheckBox("Prefer hardware encoder",&dialog);hardware->setObjectName("queueHardware");hardware->setChecked(m_hardwareEncode->isChecked());form->addRow(hardware);layout->addLayout(form);
    auto hint=new QLabel("This job stores the current edits and FX. Output settings here do not change the editor sequence. Media files are referenced, not copied. Existing output files are never overwritten.",&dialog);hint->setWordWrap(true);layout->addWidget(hint);
    auto errorLabel=new QLabel(&dialog);errorLabel->setWordWrap(true);errorLabel->setStyleSheet("color: #c33;");layout->addWidget(errorLabel);
    auto buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);buttons->button(QDialogButtonBox::Ok)->setText(startImmediately?"Add & Start Queue":"Add to Queue");layout->addWidget(buttons);
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{
        width->interpretText();height->interpretText();const auto fps=SequenceSettings::rates()[rate->currentIndex()];snapshot.sequence={width->value(),height->value(),fps.numerator,fps.denominator};
        if(!snapshot.sequence.valid()){errorLabel->setText("Width and height must be even numbers.");return;}
        in->interpretText();out->interpretText();snapshot.exportRange={scope->currentIndex()==1,qRound64(in->value()*1000),qRound64(out->value()*1000)};
        if(!snapshot.exportRange.valid(duration) || (snapshot.exportRange.enabled && snapshot.sequence.nearestFrame(snapshot.exportRange.outMs)<=snapshot.sequence.nearestFrame(snapshot.exportRange.inMs))) {errorLabel->setText("Choose In before Out by at least one output frame, within the sequence.");return;}
        QString output=path->text().trimmed();if(output.isEmpty()){errorLabel->setText("Choose an output filename.");return;}if(!output.endsWith(".mp4",Qt::CaseInsensitive))output+=".mp4";
        QString error;if(!m_queue->add(snapshot,name->text(),output,hardware->isChecked(),&error)){errorLabel->setText(error);return;}dialog.accept();
    });
    dialog.resize(560,420);if(dialog.exec()!=QDialog::Accepted)return;
    showExportQueue();if(startImmediately && !m_queue->busy()){QString error;if(!m_queue->start(&error))QMessageBox::warning(this,"Export queue",error);}
}
