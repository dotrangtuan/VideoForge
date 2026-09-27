#include "MainWindow.h"
#include "BackgroundComposition.h"
#include "TimelineWidget.h"
#include "RealtimePreview.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QComboBox>
#include <QGroupBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QFileDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QShortcut>

void MainWindow::buildCompositionUi(QVBoxLayout *layout) {
    auto *parent=layout->parentWidget();
    m_editMode=new QComboBox(parent);m_editMode->setObjectName("editMode");
    m_editMode->addItems({"Standard edit","Background composition"});layout->addWidget(m_editMode);
    m_backgroundPanel=new QGroupBox("Background setup",parent);
    m_backgroundPanel->setMinimumWidth(220);
    auto *box=new QVBoxLayout(m_backgroundPanel);
    auto *original=new QPushButton("Original videos... (V2)",m_backgroundPanel);original->setObjectName("chooseOriginalVideos");
    auto *background=new QPushButton("Background files... (V1)",m_backgroundPanel);background->setObjectName("chooseBackgroundFiles");
    box->addWidget(original);box->addWidget(background);
    m_backgroundAudio=new QCheckBox("Background audio",m_backgroundPanel);m_backgroundAudio->setToolTip("Enable audio for all background clips. Track mute still applies.");m_backgroundAudio->setObjectName("backgroundAudio");box->addWidget(m_backgroundAudio);
    m_backgroundHint=new QLabel("Select original videos and background files. Each list is appended to its own track: originals on V2, backgrounds on V1. Videos keep their full length; images start at 10 seconds. Adjust original size with Video FX.",m_backgroundPanel);
    m_backgroundHint->setWordWrap(true);box->addWidget(m_backgroundHint);layout->addWidget(m_backgroundPanel);m_backgroundPanel->hide();
    connect(m_editMode,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int index){m_backgroundMode=index==1;updateEditingUi();markDirty();});
    connect(original,&QPushButton::clicked,this,[this]{chooseCompositionFiles(true);});
    connect(background,&QPushButton::clicked,this,[this]{chooseCompositionFiles(false);});
    connect(m_backgroundAudio,&QCheckBox::toggled,this,[this](bool enabled){if(m_loading)return;pauseForEdit();m_timeline->setBackgroundAudio(enabled);});
}

void MainWindow::chooseCompositionFiles(bool originals) {
    const QString video="*.mp4 *.mov *.m4v *.mkv *.avi *.webm";
    const auto paths=QFileDialog::getOpenFileNames(this,originals?"Choose original videos (multiple selection)":"Choose background videos or images (multiple selection)",{},
        originals ? "Videos ("+video+")" : "Backgrounds ("+video+" *.png *.jpg *.jpeg *.bmp *.webp)");
    if(paths.isEmpty())return;
    (originals?m_originalFiles:m_backgroundFiles)=paths;
    m_backgroundHint->setText(QString("Selected: %1 original(s), %2 background(s). Choose the other source to append a group.").arg(m_originalFiles.size()).arg(m_backgroundFiles.size()));
    m_backgroundHint->setToolTip((m_originalFiles+m_backgroundFiles).join('\n'));
    if(m_originalFiles.isEmpty() || m_backgroundFiles.isEmpty())return;
    QString error;
    if(!addBackgroundComposition(m_originalFiles,m_backgroundFiles,m_backgroundAudio->isChecked(),&error)) {
        m_backgroundHint->setText(error);return;
    }
    m_backgroundHint->setText("Added in order to each track: originals on V2, backgrounds on V1. Each track keeps its own length. Choose both sources again to append more, or use Undo to remove this addition.");
    m_originalFiles.clear();m_backgroundFiles.clear();
}

bool MainWindow::addBackgroundComposition(const QStringList &originals,const QStringList &backgrounds,bool audio,QString *error) {
    QVector<CompositionSource> sourceVideos,sourceBackgrounds;
    auto read=[&](const QStringList &paths,bool original,QVector<CompositionSource> &result){
        for(const auto &path:paths) {
            const QString full=QFileInfo(path).absoluteFilePath();const bool image=QImageReader(full).canRead();
            if(!QFileInfo(full).isFile() || (original && image)){if(error)*error="Choose readable video files for originals and video/image files for backgrounds.";return false;}
            if(!image) {
                QProcess probe;probe.start(m_exporter->ffprobeExecutable(),{"-v","error","-select_streams","v:0","-show_entries","stream=index","-of","csv=p=0",full});
                if(!probe.waitForFinished(6000)){probe.kill();probe.waitForFinished();if(error)*error="Timed out reading: "+full;return false;}
                if(probe.exitCode()!=0 || probe.readAllStandardOutput().trimmed().isEmpty()){if(error)*error="No video stream in: "+full;return false;}
            }
            const qint64 duration=probeDurationMs(full);
            if(duration<120 || duration>86400000){if(error)*error="Could not read duration: "+full;return false;}
            result.append({full,duration,image});
        }
        return true;
    };
    if(!read(originals,true,sourceVideos) || !read(backgrounds,false,sourceBackgrounds))return false;
    QVector<TimelineClip> clips;
    qint64 originalEnd=0,backgroundEnd=0;
    for(const auto &clip:m_timeline->clips()) {
        if(clip.track==1)originalEnd=qMax(originalEnd,clip.startMs+clip.durationMs());
        if(clip.track==0)backgroundEnd=qMax(backgroundEnd,clip.startMs+clip.durationMs());
    }
    if(!BackgroundComposition::build(sourceVideos,sourceBackgrounds,originalEnd,backgroundEnd,audio,clips,error))return false;
    pauseForEdit();
    if(!m_timeline->appendComposition(clips)){if(error)*error="Cannot place composition, or the project would exceed 10000 clips.";return false;}
    for(const auto &source:sourceVideos+sourceBackgrounds)addMediaFile(source.path,false);
    m_backgroundMode=true;updateEditingUi();markDirty();return true;
}

void MainWindow::updateEditingUi() {
    if(!m_editMode)return;
    const QSignalBlocker mode(m_editMode),audio(m_backgroundAudio);
    m_editMode->setCurrentIndex(m_backgroundMode?1:0);m_backgroundPanel->setVisible(m_backgroundMode);
    m_backgroundPanel->parentWidget()->setMinimumWidth(m_backgroundMode?250:180);
    bool found=false,enabled=false;
    for(const auto &clip:m_timeline->clips())if(clip.role==TimelineClip::Background){found=true;enabled|=!clip.muted;}
    if(found)m_backgroundAudio->setChecked(enabled);
}

void MainWindow::buildExportRangeUi(QVBoxLayout *layout) {
    auto *parent=layout->parentWidget();
    m_exportScope=new QComboBox(parent);m_exportScope->setObjectName("exportScope");
    m_exportScope->addItems({"Entire timeline","In / Out range"});layout->addWidget(m_exportScope);
    auto *form=new QFormLayout;
    auto row=[&](const QString &label,const char *name,QDoubleSpinBox *&spin,bool isIn){
        spin=new QDoubleSpinBox(parent);spin->setObjectName(name);spin->setRange(0,86400);spin->setDecimals(3);spin->setSuffix(" s");spin->setKeyboardTracking(false);
        auto *button=new QPushButton(isIn?"Set In":"Set Out",parent);button->setObjectName(isIn?"setExportIn":"setExportOut");
        button->setToolTip(isIn?"Use the playhead as In (I while timeline is focused)":"Use the playhead as exclusive Out (O while timeline is focused)");
        auto *line=new QHBoxLayout;line->addWidget(spin,1);line->addWidget(button);form->addRow(label,line);
        connect(button,&QPushButton::clicked,this,[this,isIn]{
            const qint64 duration=m_timeline->sequenceDurationMs();
            const qint64 in=isIn?m_timelinePositionMs:m_exportRange.inMs;
            const qint64 out=isIn?(m_exportRange.outMs>in?m_exportRange.outMs:duration):m_timelinePositionMs;
            if(!setExportRange(true,in,out))statusBar()->showMessage("Choose In before Out, within the sequence.",5000);
        });
    };
    row("In","exportIn",m_exportIn,true);row("Out","exportOut",m_exportOut,false);layout->addLayout(form);
    m_rangeSummary=new QLabel(parent);m_rangeSummary->setObjectName("exportRangeSummary");m_rangeSummary->setWordWrap(true);layout->addWidget(m_rangeSummary);
    connect(m_exportScope,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int index){
        if(!setExportRange(index==1,m_exportRange.inMs,m_exportRange.outMs))updateRangeUi();
    });
    auto edit=[this]{
        m_exportIn->interpretText();m_exportOut->interpretText();
        if(!setExportRange(true,qRound64(m_exportIn->value()*1000),qRound64(m_exportOut->value()*1000))) {
            updateRangeUi();statusBar()->showMessage("Out must be after In by at least one frame.",5000);
        }
    };
    connect(m_exportIn,&QDoubleSpinBox::editingFinished,this,edit);connect(m_exportOut,&QDoubleSpinBox::editingFinished,this,edit);
}

bool MainWindow::setExportRange(bool enabled,qint64 in,qint64 out) {
    const qint64 duration=m_timeline->sequenceDurationMs();
    ExportRange range{enabled,in,out};if(!range.valid(duration))return false;
    if(enabled && duration>0) {
        range.inMs=qMin(duration,m_sequenceSettings.frameToMs(m_sequenceSettings.nearestFrame(in)));
        range.outMs=qMin(duration,m_sequenceSettings.frameToMs(m_sequenceSettings.nearestFrame(out)));
        if(!range.valid(duration) || m_sequenceSettings.nearestFrame(range.outMs)<=m_sequenceSettings.nearestFrame(range.inMs))return false;
    }
    m_exportRange=range;updateRangeUi();markDirty();return true;
}

void MainWindow::updateRangeUi() {
    if(!m_exportScope)return;
    const qint64 duration=m_timeline->sequenceDurationMs();
    if(m_exportRange.outMs==0 && duration>0)m_exportRange.outMs=duration;
    m_exportRange.outMs=qMin(m_exportRange.outMs,duration);
    const qint64 lastIn=m_sequenceSettings.frameToMs(qMax<qint64>(0,m_sequenceSettings.nearestFrame(m_exportRange.outMs)-1));
    m_exportRange.inMs=qMin(m_exportRange.inMs,lastIn);
    const QSignalBlocker scope(m_exportScope),in(m_exportIn),out(m_exportOut);
    m_exportScope->setCurrentIndex(m_exportRange.enabled?1:0);
    for(auto *spin:{m_exportIn,m_exportOut}){spin->setMaximum(duration/1000.0);spin->setSingleStep(1.0*m_sequenceSettings.fpsDen/m_sequenceSettings.fpsNum);spin->setEnabled(duration>0);}
    m_exportIn->setValue(m_exportRange.inMs/1000.0);m_exportOut->setValue(m_exportRange.outMs/1000.0);
    const qint64 length=m_exportRange.enabled?m_exportRange.outMs-m_exportRange.inMs:duration;
    m_rangeSummary->setText(QString("Export duration: %1\nOut is exclusive; points align to sequence frames.").arg(formatTime(length)));
    m_timeline->setExportRange(m_exportRange);
}
