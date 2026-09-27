#include "MainWindow.h"
#include "ExportQueue.h"
#include "ExportQueueDialog.h"
#include "EffectsPanel.h"
#include <QTabWidget>
#include "RealtimePreview.h"
#include "FFmpegExporter.h"
#include "MediaListWidget.h"
#include "TimelineWidget.h"

#include <QAudioOutput>
#include <QCheckBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidgetItem>
#include <QMediaPlayer>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QSlider>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QUrl>
#include <QVBoxLayout>
#include <QVideoWidget>
#include <QWidget>
#include <QImageReader>
#include <QTimer>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <algorithm>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    m_queue=new ExportQueue(ExportQueue::defaultStorePath(),this);
    m_exporter=m_queue->exporter();
    buildUi();
    buildProjectUi();
    buildSequenceUi();
    updateEditingUi();updateRangeUi();
    m_queueDialog=new ExportQueueDialog(m_queue,this);
    connect(m_queueDialog,&ExportQueueDialog::addRequested,this,&MainWindow::addExportToQueue);
    connect(m_queue,&ExportQueue::changed,this,[this]{m_exportButton->setEnabled(m_queue->available() && !m_queue->busy());});
    m_exportButton->setEnabled(m_queue->available());
    connect(m_videoWidget,&RealtimePreview::positionChanged,this,[this](qint64 position) {
        m_timelinePositionMs=position;
        m_timeline->setPosition(position);
        updateTransportUi(position);
    });
    connect(m_videoWidget,&RealtimePreview::playingChanged,this,[this](bool playing) {
        m_playButton->setText(playing ? "Pause" : "Play");
    });
    connect(m_videoWidget,&RealtimePreview::playbackError,this,[this](const QString &message) {
        statusBar()->showMessage("Preview: "+message,8000);
    });

    connect(m_exporter, &FFmpegExporter::started, this, [this]() {
        m_exportProgress->setRange(0, 100);
        m_exportProgress->setValue(0);
        m_exportProgress->setVisible(true);
        m_exportStatusLabel->setVisible(true);
        m_exportStatusLabel->setText("Rendering... 0%");
        m_exportButton->setEnabled(false);
        m_cancelExportButton->setEnabled(true);
        statusBar()->showMessage("Encoding sequence...");
    });
    connect(m_exporter, &FFmpegExporter::progressChanged,
            this, &MainWindow::onExportProgress);
    connect(m_exporter, &FFmpegExporter::logMessage, this,
            [this](const QString &message) {
                m_log->appendPlainText(message);
            });
    connect(m_exporter, &FFmpegExporter::finished,
            this, &MainWindow::onExportFinished);

    const QString hwEncoder = m_exporter->hardwareEncoderDisplayName();
    const QString hwDiagnostics = m_exporter->hardwareEncoderDiagnostics();
    if (!hwEncoder.isEmpty()) {
        m_hardwareEncode->setText("Hardware H.264 — " + hwEncoder);
        if (!hwDiagnostics.isEmpty())
            m_hardwareEncode->setToolTip(hwDiagnostics);
        m_hardwareEncode->setChecked(true);
        m_hardwareEncode->setEnabled(true);
        QString message = "Detected hardware encoder: " + hwEncoder;
        if (hwDiagnostics.contains("NVIDIA NVENC:") && hwEncoder != "NVIDIA NVENC")
            message += " (NVIDIA NVENC unavailable; see checkbox tooltip)";
        statusBar()->showMessage(message, 8000);
    } else {
        m_hardwareEncode->setText("Hardware H.264 unavailable — CPU/libx264 fallback");
        if (!hwDiagnostics.isEmpty())
            m_hardwareEncode->setToolTip(hwDiagnostics);
        m_hardwareEncode->setChecked(false);
        m_hardwareEncode->setEnabled(false);
    }

    resize(1450, 900);
    updateTitle();
}

MainWindow::~MainWindow() {
    delete m_queue;
}

void MainWindow::buildUi()
{
    auto *central = new QWidget(this);
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(8, 8, 8, 8);

    auto *mainSplitter = new QSplitter(Qt::Horizontal, central);
    mainSplitter->setObjectName("workspaceColumns");
    mainSplitter->setChildrenCollapsible(false);

    // Import and composition controls stay separate from the media bin.
    auto *mediaBox = new QGroupBox("Media", mainSplitter);
    mediaBox->setObjectName("mediaControls");
    auto *mediaLayout = new QVBoxLayout(mediaBox);
    auto *importButton = new QPushButton("Import Media", mediaBox);
    buildCompositionUi(mediaLayout);
    mediaLayout->addWidget(importButton);
    mediaLayout->addStretch(1);

    auto *resourcesBox = new QGroupBox("Resources", mainSplitter);
    resourcesBox->setObjectName("resourcesPanel");
    resourcesBox->setMinimumWidth(180);
    auto *resourcesLayout = new QVBoxLayout(resourcesBox);
    auto *resourcesHint = new QLabel("Drag to the timeline or double-click to add.", resourcesBox);
    resourcesHint->setWordWrap(true);
    resourcesLayout->addWidget(resourcesHint);
    m_mediaList = new MediaListWidget(resourcesBox);
    m_mediaList->setObjectName("importedResources");
    resourcesLayout->addWidget(m_mediaList, 1);
    auto *addButton = new QPushButton("Add to Timeline", resourcesBox);
    resourcesLayout->addWidget(addButton);
    auto *removeAllButton = new QPushButton("Remove All Resources", resourcesBox);
    removeAllButton->setObjectName("removeAllResources");
    removeAllButton->setEnabled(false);
    resourcesLayout->addWidget(removeAllButton);
    auto refreshRemove = [this,removeAllButton]{removeAllButton->setEnabled(m_mediaList->count()>0);};
    connect(m_mediaList->model(), &QAbstractItemModel::rowsInserted, this, refreshRemove);
    connect(m_mediaList->model(), &QAbstractItemModel::rowsRemoved, this, refreshRemove);
    connect(m_mediaList->model(), &QAbstractItemModel::modelReset, this, refreshRemove);
    connect(removeAllButton, &QPushButton::clicked, this, [this]{removeResources(true);});
    auto *removeResourceKey = new QShortcut(QKeySequence(Qt::Key_Delete), m_mediaList);
    removeResourceKey->setContext(Qt::WidgetWithChildrenShortcut);
    removeResourceKey->setAutoRepeat(false);
    connect(removeResourceKey, &QShortcut::activated, this, [this]{removeResources(false);});
    m_mediaList->setToolTip("Select resources and press Delete to remove them from Resources.");

    connect(importButton, &QPushButton::clicked,
            this, &MainWindow::importVideos);
    connect(addButton, &QPushButton::clicked,
            this, &MainWindow::addSelectedMediaToTimeline);
    connect(m_mediaList, &QListWidget::itemDoubleClicked,
            this, [this](QListWidgetItem *) {
                addSelectedMediaToTimeline();
            });

    // Program monitor.
    auto *previewBox = new QGroupBox("Program Monitor", mainSplitter);
    auto *previewLayout = new QVBoxLayout(previewBox);
    m_videoWidget = new RealtimePreview(previewBox);
    m_videoWidget->setMinimumSize(640, 360);

    auto *transport = new QHBoxLayout;
    m_playButton = new QPushButton("Play", previewBox);
    auto *splitButton = new QPushButton("Split", previewBox);
    auto *deleteButton = new QPushButton("Delete Clip", previewBox);
    auto *undoButton = new QPushButton("Undo", previewBox);
    auto *redoButton = new QPushButton("Redo", previewBox);
    m_positionSlider = new QSlider(Qt::Horizontal, previewBox);
    m_positionSlider->setRange(0, 1000);
    m_timeLabel = new QLabel("00:00:00:00 / 00:00:00:00", previewBox);
    m_timeLabel->setObjectName("previewTimecode");
    m_timeLabel->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

    auto *previous=new QPushButton("< Frame",previewBox);
    auto *next=new QPushButton("Frame >",previewBox);
    previous->setObjectName("previousFrame");next->setObjectName("nextFrame");
    connect(previous,&QPushButton::clicked,this,&MainWindow::previousFrame);
    connect(next,&QPushButton::clicked,this,&MainWindow::nextFrame);
    transport->addWidget(previous);
    transport->addWidget(m_playButton);
    transport->addWidget(next);
    transport->addWidget(splitButton);
    transport->addWidget(deleteButton);
    transport->addWidget(undoButton);
    transport->addWidget(redoButton);
    transport->addStretch(1);
    auto *scrubRow = new QHBoxLayout;
    scrubRow->addWidget(m_positionSlider, 1);
    scrubRow->addWidget(m_timeLabel);

    auto *previewHint=new QLabel("Live preview — drag the ruler to scrub; Space to play/pause",previewBox);
    previewLayout->addWidget(previewHint);
    previewLayout->addWidget(m_videoWidget, 1);
    previewLayout->addLayout(scrubRow);
    previewLayout->addLayout(transport);

    connect(m_playButton, &QPushButton::clicked,
            this, &MainWindow::togglePlayback);
    connect(splitButton, &QPushButton::clicked,
            this, &MainWindow::splitAtPlayhead);
    connect(deleteButton, &QPushButton::clicked,
            this, &MainWindow::deleteSelectedClip);
    connect(undoButton, &QPushButton::clicked,
            this, &MainWindow::undoEdit);
    connect(redoButton, &QPushButton::clicked,
            this, &MainWindow::redoEdit);
    connect(m_positionSlider, &QSlider::sliderMoved,
            this, &MainWindow::onSliderMoved);

    auto *rightTabs=new QTabWidget(mainSplitter);rightTabs->setObjectName("inspectorTabs");
    m_effectsPanel=new EffectsPanel(rightTabs);rightTabs->addTab(m_effectsPanel,"Video FX");
    auto *exportBox = new QGroupBox("Export / Encode", rightTabs);rightTabs->addTab(exportBox,"Export");
    auto *exportLayout = new QVBoxLayout(exportBox);
    m_sequenceLabel = new QLabel("Sequence: 0 clips  •  00:00.000", exportBox);
    m_hardwareEncode = new QCheckBox("Detecting hardware H.264 encoder...", exportBox);
    m_hardwareEncode->setChecked(true);
    m_exportButton = new QPushButton("Export / Start Queue...", exportBox);
    auto addQueue=new QPushButton("Add to Export Queue...",exportBox);addQueue->setObjectName("addToExportQueue");
    auto showQueue=new QPushButton("Open Export Queue...",exportBox);showQueue->setObjectName("openExportQueue");
    auto batchV2=new QPushButton("Add V2 Videos to Queue...",exportBox);batchV2->setObjectName("addV2ToExportQueue");
    connect(batchV2,&QPushButton::clicked,this,&MainWindow::addV2ExportsToQueue);
    connect(addQueue,&QPushButton::clicked,this,&MainWindow::addExportToQueue);
    connect(showQueue,&QPushButton::clicked,this,&MainWindow::showExportQueue);

    m_exportProgress = new QProgressBar(exportBox);
    m_exportProgress->setRange(0, 100);
    m_exportProgress->setValue(0);
    m_exportProgress->setFormat("%p%");
    m_exportProgress->setTextVisible(true);
    m_exportProgress->setVisible(false);

    m_exportStatusLabel = new QLabel("Ready", exportBox);
    m_exportStatusLabel->setWordWrap(true);
    m_exportStatusLabel->setVisible(false);

    m_cancelExportButton = new QPushButton("Cancel Export", exportBox);
    m_cancelExportButton->setEnabled(false);

    m_log = new QPlainTextEdit(exportBox);
    m_log->setReadOnly(true);
    m_log->setPlaceholderText("FFmpeg log...");

    exportLayout->addWidget(m_sequenceLabel);
    buildExportRangeUi(exportLayout);
    exportLayout->addWidget(m_hardwareEncode);
    exportLayout->addWidget(m_exportButton);
    exportLayout->addWidget(addQueue);
    exportLayout->addWidget(batchV2);
    exportLayout->addWidget(showQueue);
    exportLayout->addWidget(m_exportProgress);
    exportLayout->addWidget(m_exportStatusLabel);
    exportLayout->addWidget(m_cancelExportButton);
    exportLayout->addWidget(m_log, 1);

    connect(m_exportButton, &QPushButton::clicked,
            this, &MainWindow::exportVideo);
    connect(m_cancelExportButton, &QPushButton::clicked,
            m_queue, &ExportQueue::cancelCurrent);

    mainSplitter->addWidget(mediaBox);
    mainSplitter->addWidget(resourcesBox);
    mainSplitter->addWidget(previewBox);
    mainSplitter->addWidget(rightTabs);
    mainSplitter->setStretchFactor(0, 0);
    mainSplitter->setStretchFactor(1, 0);
    mainSplitter->setStretchFactor(2, 1);
    mainSplitter->setStretchFactor(3, 0);
    mainSplitter->setSizes({250, 210, 700, 270});

    // Timeline toolbar + horizontally scrollable timeline.
    auto *timelineToolbar = new QHBoxLayout;
    auto *timelineTitle = new QLabel("Timeline V3 / V2 / V1 / A1 / A2 — Eye: visibility, On/Mute: audio", central);
    auto *zoomLabel = new QLabel("Zoom", central);
    m_zoomSlider = new QSlider(Qt::Horizontal, central);
    m_zoomSlider->setRange(25, 220);
    m_zoomSlider->setValue(25);
    m_zoomSlider->setObjectName("timelineZoom");
    m_zoomSlider->setToolTip("Drag left to show the whole sequence; drag right for detail.");
    m_zoomSlider->setMaximumWidth(240);

    timelineToolbar->addWidget(timelineTitle);
    timelineToolbar->addStretch(1);
    auto *fitTimeline = new QPushButton("Fit", central);
    fitTimeline->setObjectName("fitTimeline");
    fitTimeline->setToolTip("Show the whole sequence from start to end (Ctrl+Shift+F)");
    timelineToolbar->addWidget(fitTimeline);
    timelineToolbar->addWidget(zoomLabel);
    timelineToolbar->addWidget(m_zoomSlider);

    m_timeline = new TimelineWidget(central);
    auto *timelineScroll = new QScrollArea(central);
    timelineScroll->setWidget(m_timeline);
    timelineScroll->setWidgetResizable(false);
    timelineScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    timelineScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    timelineScroll->setMinimumHeight(355);
    timelineScroll->setMaximumHeight(365);
    timelineScroll->setObjectName("timelineScroll");
    m_timeline->setScrollArea(timelineScroll);
    m_timeline->setZoomLevel(m_zoomSlider->value());
    for(const auto &binding : {qMakePair(Qt::Key_I,"setExportIn"),qMakePair(Qt::Key_O,"setExportOut")}) {
        auto *shortcut=new QShortcut(QKeySequence(binding.first),m_timeline);
        shortcut->setContext(Qt::WidgetShortcut);
        connect(shortcut,&QShortcut::activated,findChild<QPushButton*>(binding.second),&QPushButton::click);
    }
    connect(fitTimeline, &QPushButton::clicked, this, [this] { m_zoomSlider->setValue(25); });
    auto *fitShortcut = new QShortcut(QKeySequence("Ctrl+Shift+F"), this);
    connect(fitShortcut, &QShortcut::activated, fitTimeline, &QPushButton::click);

    connect(m_timeline, &TimelineWidget::seekRequested,
            this, &MainWindow::onTimelineSeek);
    connect(m_timeline, &TimelineWidget::clipsChanged,
            this, &MainWindow::onTimelineChanged);
    connect(m_timeline, &TimelineWidget::filesDropped,
            this, &MainWindow::onDroppedFiles);
    connect(m_zoomSlider, &QSlider::valueChanged,
            m_timeline, &TimelineWidget::setZoomLevel);

    connect(m_timeline,&TimelineWidget::selectionChanged,this,[this]{updateEffectsUi();});
    connect(m_effectsPanel,&EffectsPanel::editRequested,this,[this](int clipId,const QVector<VideoEffect> &effects){
        const auto clips=m_timeline->clips();
        for(int i=0;i<clips.size();++i)if(clips[i].id==clipId){pauseForEdit();m_timeline->setClipEffects(i,effects);break;}
    });
    root->addWidget(mainSplitter, 1);
    root->addLayout(timelineToolbar);
    root->addWidget(timelineScroll);

    setCentralWidget(central);
    statusBar()->showMessage(
        "Import video, then double-click it or drag it onto the timeline.");

    // Editing shortcuts.
    auto *space = new QShortcut(QKeySequence(Qt::Key_Space), this);
    connect(space, &QShortcut::activated, this, &MainWindow::togglePlayback);

    auto *split = new QShortcut(QKeySequence("Ctrl+B"), this);
    connect(split, &QShortcut::activated, this, &MainWindow::splitAtPlayhead);
#ifdef Q_OS_MACOS
    auto *splitMac = new QShortcut(
        QKeySequence("Meta+B"), this);
    connect(splitMac, &QShortcut::activated,
            this, &MainWindow::splitAtPlayhead);
#endif

    auto *deleteShortcut = new QShortcut(QKeySequence(Qt::Key_Backspace), m_timeline);
    deleteShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    auto *deleteKey = new QShortcut(QKeySequence(Qt::Key_Delete), m_timeline);
    deleteKey->setContext(Qt::WidgetWithChildrenShortcut);
    connect(deleteKey, &QShortcut::activated, this, &MainWindow::deleteSelectedClip);
    connect(deleteShortcut, &QShortcut::activated,
            this, &MainWindow::deleteSelectedClip);

    auto *undoShortcut = new QShortcut(QKeySequence(QKeySequence::Undo), this);
    connect(undoShortcut, &QShortcut::activated,
            this, &MainWindow::undoEdit);

    auto *redoShortcut = new QShortcut(QKeySequence(QKeySequence::Redo), this);
    connect(redoShortcut, &QShortcut::activated,
            this, &MainWindow::redoEdit);
}

void MainWindow::importVideos()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this,
        "Import videos",
        QString(),
        "Media (*.mp4 *.mov *.m4v *.mkv *.avi *.webm *.png *.jpg *.jpeg *.bmp *.webp *.wav *.mp3 *.aac *.m4a *.flac *.ogg *.opus);;All Files (*)");

    for (const QString &file : files)
        addMediaFile(file, false);
}

void MainWindow::addMediaFile(const QString &path, bool addToTimeline)
{
    if (path.isEmpty() || !QFileInfo::exists(path))
        return;

    qint64 durationMs = probeDurationMs(path);
    if (durationMs < 120 || durationMs > 86400000) {
        statusBar()->showMessage("Could not read duration: " + path, 6000);
        return;
    }

    QListWidgetItem *existing = nullptr;
    for (int i = 0; i < m_mediaList->count(); ++i) {
        QListWidgetItem *item = m_mediaList->item(i);
        if (item->data(Qt::UserRole).toString() == path) {
            existing = item;
        markDirty();
            break;
        }
    }

    if (!existing) {
        auto *item = new QListWidgetItem(QFileInfo(path).fileName());
        item->setToolTip(path);
        item->setData(Qt::UserRole, path);
        item->setData(Qt::UserRole + 1, durationMs);
        m_mediaList->addItem(item);
        existing = item;
        markDirty();
    }

    if (addToTimeline) {
        m_timeline->addClip(QFileInfo(path).fileName(), path, durationMs);
    }

    statusBar()->showMessage("Imported: " + QFileInfo(path).fileName(), 3000);
}

void MainWindow::removeResources(bool all)
{
    auto items=m_mediaList->selectedItems();
    if(all) {
        items.clear();
        for(int i=0;i<m_mediaList->count();++i)items.append(m_mediaList->item(i));
    }
    if(items.isEmpty())return;
    auto pathKey=[](const QString &path) {
        const QFileInfo info(path);
        QString key=info.canonicalFilePath();
        if(key.isEmpty())key=QDir::cleanPath(info.absoluteFilePath());
#ifdef Q_OS_WIN
        key=key.toCaseFolded();
#endif
        return key;
    };
    QSet<QString> paths;QStringList names;
    for(auto *item:items) {paths.insert(pathKey(item->data(Qt::UserRole).toString()));names.append(item->text());}
    QSet<int> clipIds;
    for(const auto &clip:m_timeline->clips())if(paths.contains(pathKey(clip.filePath)))clipIds.insert(clip.id);
    if(!clipIds.isEmpty()) {
        QMessageBox confirm(QMessageBox::Question,"Remove resources",
            QString("Remove %1 resource(s) and %2 clip(s) using them on the timeline?").arg(items.size()).arg(clipIds.size()),
            QMessageBox::Yes|QMessageBox::No,this);
        confirm.setObjectName("confirmRemoveResources");
        confirm.setInformativeText("This removes every matching clip from all tracks. Source files on disk and existing export queue jobs are kept.");
        confirm.setDetailedText(names.join('\n'));
        confirm.setDefaultButton(QMessageBox::No);confirm.setEscapeButton(QMessageBox::No);
        if(confirm.exec()!=QMessageBox::Yes)return;
    }
    if(!clipIds.isEmpty()) {
        commitClipAudio();m_effectsPanel->commitPendingEdits();pauseForEdit();
        m_timeline->removeClips(clipIds);
    }
    for(auto *item:items)delete m_mediaList->takeItem(m_mediaList->row(item));
    markDirty();
    statusBar()->showMessage(QString("Removed %1 resource(s) and %2 timeline clip(s). Source files kept.").arg(items.size()).arg(clipIds.size()),5000);
}

qint64 MainWindow::probeDurationMs(const QString &path) const
{
    if (QImageReader(path).canRead()) return 10000; // stills start with a ten-second source range
    const QString ffprobe = m_exporter->ffprobeExecutable();
    if (ffprobe.isEmpty())
        return 0;

    QProcess probe;
    probe.start(ffprobe, {
        "-v", "error",
        "-show_entries", "format=duration",
        "-of", "default=noprint_wrappers=1:nokey=1",
        path
    });

    if (!probe.waitForFinished(6000)) {
        probe.kill();
        return 0;
    }

    bool ok = false;
    const double seconds = QString::fromUtf8(
                               probe.readAllStandardOutput())
                               .trimmed()
                               .toDouble(&ok);
    if (!ok || seconds <= 0.0)
        return 0;

    return static_cast<qint64>(seconds * 1000.0);
}

void MainWindow::addSelectedMediaToTimeline()
{
    const auto items = m_mediaList->selectedItems();
    if (items.isEmpty()) {
        QMessageBox::information(this, "Media",
                                 "Select one or more videos in the Media panel first.");
        return;
    }

    pauseForEdit();
    for (QListWidgetItem *item : items) {
        const QString path = item->data(Qt::UserRole).toString();
        const qint64 durationMs = item->data(Qt::UserRole + 1).toLongLong();
        m_timeline->addClip(QFileInfo(path).fileName(), path, durationMs);
    }
}

void MainWindow::onDroppedFiles(const QStringList &paths, int track, qint64 startMs) {
    pauseForEdit();
    for(const auto &path:paths) {
        addMediaFile(path,false);
        qint64 duration=probeDurationMs(path);
        int before=m_timeline->clips().size();
        m_timeline->addClip(QFileInfo(path).fileName(),path,duration,track,startMs);
        if(m_timeline->clips().size()>before) startMs+=duration;
        else statusBar()->showMessage("Drop rejected: overlap, incompatible track, or unreadable media.",6000);
    }
}
QVector<FFmpegExporter::ClipSegment> MainWindow::segments() const {return ExportQueue::segmentsFor(projectState());}
void MainWindow::togglePlayback() {
    if(m_videoWidget->isPlaying()) m_videoWidget->pause();
    else m_videoWidget->play();
}

void MainWindow::splitAtPlayhead()
{
    pauseForEdit();
    if (!m_timeline->splitSelectedAt(m_timelinePositionMs)) {
        statusBar()->showMessage(
            "Select a clip and place the playhead inside it before splitting.",
            5000);
        return;
    }
    previewTimelinePosition(m_timelinePositionMs, false);
}

void MainWindow::deleteSelectedClip()
{
    pauseForEdit();
    if (!m_timeline->deleteSelected()) {
        statusBar()->showMessage("Select a clip first.", 3000);
        return;
    }

    if (!m_timeline->clips().isEmpty())
        previewTimelinePosition(
            std::min(m_timelinePositionMs,
                     m_timeline->sequenceDurationMs() - 1),
            false);
    else {
        m_videoWidget->pause();
        m_timelinePositionMs = 0;
        updateTransportUi(0);
    }
}

void MainWindow::undoEdit()
{
    pauseForEdit();
    if (m_timeline->undo() && !m_timeline->clips().isEmpty()) {
        previewTimelinePosition(
            std::min(m_timelinePositionMs,
                     m_timeline->sequenceDurationMs() - 1),
            false);
    }
}

void MainWindow::redoEdit()
{
    pauseForEdit();
    if (m_timeline->redo() && !m_timeline->clips().isEmpty()) {
        previewTimelinePosition(
            std::min(m_timelinePositionMs,
                     m_timeline->sequenceDurationMs() - 1),
            false);
    }
}

void MainWindow::pauseForEdit() {
    m_videoWidget->pause();
}

void MainWindow::onTimelineSeek(qint64 positionMs)
{
    previewTimelinePosition(positionMs, false);
}

void MainWindow::onSliderMoved(int value)
{
    const qint64 duration = m_timeline->sequenceDurationMs();
    if (duration <= 0)
        return;

    const qint64 position = (duration * value) / 1000;
    previewTimelinePosition(position, false);
}

void MainWindow::previewTimelinePosition(qint64 timelineMs, bool resumePlayback) {
    m_videoWidget->seek(timelineMs);
    if(resumePlayback) m_videoWidget->play();
}

void MainWindow::updateEffectsUi() {
    if(!m_effectsPanel)return;
    const auto clips=m_timeline->clips();const int index=m_timeline->selectedClipIndex();
    if(index>=0 && index<clips.size() && clips[index].track<3 && !clips[index].audioOnly)
        m_effectsPanel->setClip(clips[index].id,clips[index].name,clips[index].effects);
    else m_effectsPanel->setClip(-1,{},{});
}
void MainWindow::onTimelineChanged()
{
    updateEditingUi();updateRangeUi();
    markDirty();
    updateClipAudioUi();
    updateEffectsUi();
    m_videoWidget->setSequence(segments());
    const QVector<TimelineClip> clips = m_timeline->clips();
    const qint64 duration = m_timeline->sequenceDurationMs();

    refreshSequenceInfo();

    if (duration <= 0) {
        m_positionSlider->setValue(0);
        updateTransportUi(0);
    } else {
        m_timelinePositionMs = std::min(m_timelinePositionMs, duration);
        updateTransportUi(m_timelinePositionMs);
    }
}

void MainWindow::updateTransportUi(qint64 timelineMs)
{
    const qint64 duration = m_timeline->sequenceDurationMs();
    timelineMs = std::clamp<qint64>(timelineMs, 0, duration);

    if (duration > 0 && !m_positionSlider->isSliderDown()) {
        m_positionSlider->setValue(
            static_cast<int>((timelineMs * 1000) / duration));
    }

    m_timeLabel->setText(
        m_sequenceSettings.timecode(timelineMs) + " / " + m_sequenceSettings.timecode(duration));
    m_timeLabel->setToolTip(QString("Hours:Minutes:Seconds:Frames | %1 fps | Non-drop-frame").arg(m_sequenceSettings.rateLabel()));
}

void MainWindow::exportVideo(){addExportJob(true);}

void MainWindow::onExportProgress(int percent,
                                  qint64 processedMs,
                                  qint64 totalMs,
                                  double speed,
                                  qint64 elapsedMs,
                                  qint64 remainingMs)
{
    m_exportProgress->setValue(percent);

    QString status = QString("Rendering... %1%   %2 / %3")
                         .arg(percent)
                         .arg(formatTime(processedMs))
                         .arg(formatTime(totalMs));

    if (speed > 0.0)
        status += QString("   Speed: %1x").arg(speed, 0, 'f', 2);

    status += "   Elapsed: " + formatShortDuration(elapsedMs);
    if (remainingMs >= 0)
        status += "   Remaining: " + formatShortDuration(remainingMs);

    m_exportStatusLabel->setText(status);
}

void MainWindow::onExportFinished(bool success, const QString &message)
{
    m_exportButton->setEnabled(!m_queue->busy());
    m_cancelExportButton->setEnabled(false);

    if (success) {
        m_exportProgress->setValue(100);
        m_exportStatusLabel->setText("✓ 100% — Export completed");
    } else {
        m_exportStatusLabel->setText(message);
    }

    statusBar()->showMessage(message, 8000);


}

QString MainWindow::formatTime(qint64 ms) const
{
    ms = std::max<qint64>(0, ms);
    const qint64 totalSeconds = ms / 1000;
    const qint64 hours = totalSeconds / 3600;
    const qint64 minutes = (totalSeconds % 3600) / 60;
    const qint64 seconds = totalSeconds % 60;
    const qint64 millis = ms % 1000;

    if (hours > 0) {
        return QString("%1:%2:%3.%4")
            .arg(hours, 2, 10, QLatin1Char('0'))
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(seconds, 2, 10, QLatin1Char('0'))
            .arg(millis, 3, 10, QLatin1Char('0'));
    }

    return QString("%1:%2.%3")
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'))
        .arg(millis, 3, 10, QLatin1Char('0'));
}

QString MainWindow::formatShortDuration(qint64 ms) const
{
    if (ms < 0)
        return "--";

    const double seconds = static_cast<double>(ms) / 1000.0;
    if (seconds < 60.0)
        return QString("%1s").arg(seconds, 0, 'f', 1);

    const qint64 wholeSeconds = ms / 1000;
    const qint64 minutes = wholeSeconds / 60;
    const qint64 remainingSeconds = wholeSeconds % 60;
    return QString("%1m %2s").arg(minutes).arg(remainingSeconds);
}
