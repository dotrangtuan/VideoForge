#pragma once

#include <QMainWindow>
#include <QMediaPlayer>
#include <QStringList>

#include "FFmpegExporter.h"
#include "ProjectFile.h"
#include <QTimer>
#include <QLockFile>
#include <memory>

class EffectsPanel;
class ExportQueue;
class ExportQueueDialog;
class QDoubleSpinBox;
class QComboBox;
class QVBoxLayout;
class QCloseEvent;
class QAudioOutput;
class QCheckBox;
class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QScrollArea;
class QSlider;
class RealtimePreview;
class MediaListWidget;
class TimelineWidget;
class FFmpegExporter;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;
    ProjectState projectState() const;
    bool setSequenceSettings(const SequenceSettings &settings);
    bool addBackgroundComposition(const QStringList &originals,const QStringList &backgrounds,bool backgroundAudio,QString *error=nullptr);
    bool setExportRange(bool enabled,qint64 inMs,qint64 outMs);
    bool saveProjectFile(const QString &path,QString *error=nullptr);
    bool loadProjectFile(const QString &path,QString *error=nullptr);
    bool writeAutosave(QString *error=nullptr);
    bool restoreAutosave(const QString &path,QString *error=nullptr);
    bool relinkMediaFile(const QString &oldPath,const QString &newPath,QString *error=nullptr);
    QString autosavePath() const { return m_autosavePath; }
    bool hasUnsavedChanges() const { return m_dirty; }
protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void editSequenceSettings();
    void previousFrame();
    void nextFrame();
    void newProject();
    void openProject();
    bool saveProject();
    bool saveProjectAs();
    void relinkMissingMedia();
    void recoverAutosave();
    void importVideos();
    void addSelectedMediaToTimeline();
    void togglePlayback();
    void splitAtPlayhead();
    void deleteSelectedClip();
    void undoEdit();
    void redoEdit();
    void exportVideo();
    void addExportToQueue();
    void addV2ExportsToQueue();
    void showExportQueue();

    void onSliderMoved(int value);
    void onTimelineSeek(qint64 positionMs);
    void onTimelineChanged();
    void onDroppedFiles(const QStringList &paths, int track, qint64 startMs);

    void onExportProgress(int percent,
                          qint64 processedMs,
                          qint64 totalMs,
                          double speed,
                          qint64 elapsedMs,
                          qint64 remainingMs);
    void onExportFinished(bool success, const QString &message);

private:
    QVector<FFmpegExporter::ClipSegment> segments() const;
    void buildUi();
    void buildCompositionUi(QVBoxLayout *layout);
    void buildExportRangeUi(QVBoxLayout *layout);
    void chooseCompositionFiles(bool originals);
    void updateEditingUi();
    void updateRangeUi();
    void addExportJob(bool startImmediately);
    void updateEffectsUi();
    void buildProjectUi();
    void buildSequenceUi();
    void refreshSequenceInfo();
    void applyProject(const ProjectState &state);
    void markDirty();
    void updateTitle();
    bool confirmSave();
    void updateClipAudioUi();
    void commitClipAudio();
    void checkRecovery();
    void addMediaFile(const QString &path, bool addToTimeline);
    void removeResources(bool all);
    qint64 probeDurationMs(const QString &path) const;
    void previewTimelinePosition(qint64 timelineMs, bool resumePlayback);
    void updateTransportUi(qint64 timelineMs);
    void pauseForEdit();

    QString formatTime(qint64 ms) const;
    QString formatShortDuration(qint64 ms) const;

    EffectsPanel *m_effectsPanel=nullptr;
    RealtimePreview *m_videoWidget = nullptr;
    MediaListWidget *m_mediaList = nullptr;
    TimelineWidget *m_timeline = nullptr;
    FFmpegExporter *m_exporter = nullptr;
    ExportQueue *m_queue=nullptr;
    ExportQueueDialog *m_queueDialog=nullptr;

    QPushButton *m_playButton = nullptr;
    QSlider *m_positionSlider = nullptr;
    QLabel *m_timeLabel = nullptr;
    QLabel *m_sequenceLabel = nullptr;
    QSlider *m_zoomSlider = nullptr;

    QCheckBox *m_hardwareEncode = nullptr;
    QPushButton *m_exportButton = nullptr;
    QPushButton *m_cancelExportButton = nullptr;
    QProgressBar *m_exportProgress = nullptr;
    QLabel *m_exportStatusLabel = nullptr;
    QPlainTextEdit *m_log = nullptr;

    SequenceSettings m_sequenceSettings;
    bool m_backgroundMode=false;
    ExportRange m_exportRange;
    QComboBox *m_editMode=nullptr;
    QWidget *m_backgroundPanel=nullptr;
    QCheckBox *m_backgroundAudio=nullptr;
    QLabel *m_backgroundHint=nullptr;
    QStringList m_originalFiles,m_backgroundFiles;
    QComboBox *m_exportScope=nullptr;
    QDoubleSpinBox *m_exportIn=nullptr,*m_exportOut=nullptr;
    QLabel *m_rangeSummary=nullptr;
    QPushButton *m_sequenceSettingsButton=nullptr;
    QString m_projectPath;
    QString m_autosavePath;
    std::unique_ptr<QLockFile> m_sessionLock;
    QTimer m_autosaveTimer;
    bool m_dirty=false;
    bool m_loading=false;
    QCheckBox *m_snapCheck=nullptr;
    QDoubleSpinBox *m_gainSpin=nullptr;
    QDoubleSpinBox *m_fadeInSpin=nullptr;
    QDoubleSpinBox *m_fadeOutSpin=nullptr;
    qint64 m_timelinePositionMs = 0;
};
