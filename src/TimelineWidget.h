#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>
#include <QPointer>
#include <QSet>
#include <array>
#include "TimelineClip.h"
#include "ExportRange.h"
#include "SequenceSettings.h"

class QScrollArea;

class TimelineWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TimelineWidget(QWidget *parent = nullptr);

    void addClip(const QString &name, const QString &filePath, qint64 sourceDurationMs, int track = -1, qint64 startMs = -1);
    bool appendComposition(const QVector<TimelineClip> &clips);
    void setBackgroundAudio(bool enabled);
    void setExportRange(const ExportRange &range);
    void setSequenceSettings(const SequenceSettings &settings);
    bool moveClip(int index, int track, qint64 startMs);
    bool trimClip(int index, qint64 inMs, qint64 outMs);
    bool trackVisible(int track) const { return m_visible[track]; }
    bool trackMuted(int track) const { return m_muted[track]; }
    void toggleTrack(int track, bool visibility);
    void clearTimeline();
    void setContents(const QVector<TimelineClip> &clips, const std::array<bool,5> &visible, const std::array<bool,5> &muted);
    bool setClipEffects(int index,const QVector<VideoEffect> &effects);
    bool setClipAudio(int index, double gain, qint64 fadeInMs, qint64 fadeOutMs);
    void setSnapping(bool enabled) { m_snapping=enabled; }
    bool snapping() const { return m_snapping; }
    int zoom() const { return m_zoomLevel; }
    double pixelsPerSecond() const { return m_pixelsPerSecond; }
    void setScrollArea(QScrollArea *area);
    qint64 snapTime(qint64 time, int except=-1, qint64 duration=0) const;

    QVector<TimelineClip> clips() const;
    qint64 sequenceDurationMs() const;
    qint64 clipStartMs(int index) const;
    int clipIndexAtTime(qint64 timelineMs) const;
    int selectedClipIndex() const;

    void setPosition(qint64 positionMs);
    qint64 position() const;
    void setZoomLevel(int level);

    bool splitSelectedAt(qint64 timelineMs);
    bool deleteSelected();
    bool removeClips(const QSet<int> &ids);
    bool undo();
    bool redo();

signals:
    void seekRequested(qint64 positionMs);
    void selectionChanged(int clipIndex);
    void clipsChanged();
    void filesDropped(const QStringList &paths, int track, qint64 startMs);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    enum class DragMode {
        None,
        TrimLeft,
        TrimRight,
        MoveClip
    };

    qint64 xToTime(int x) const;
    int timeToX(qint64 ms) const;
    QRect clipRect(int index) const;
    void updateGeometryForDuration();
    void recordUndo();
    void emitTimelineChanged();
    bool isVideoPath(const QString &path) const;

    bool canPlace(const TimelineClip &clip, int except = -1) const;
    int trackAtY(int y) const;
    bool m_visible[5] = {true,true,true,true,true};
    bool m_muted[5] = {false,false,false,false,false};
    QVector<TimelineClip> m_clips;
    QVector<QVector<TimelineClip>> m_undoStack;
    QVector<QVector<TimelineClip>> m_redoStack;

    int m_nextClipId = 1;
    ExportRange m_exportRange;
    SequenceSettings m_sequenceSettings;
    int m_selectedIndex = -1;
    qint64 m_positionMs = 0;
    int m_zoomLevel = 70;
    double m_pixelsPerSecond = 70;
    QPointer<QScrollArea> m_scrollArea;

    bool m_snapping = true;
    bool m_scrubbing = false;
    DragMode m_dragMode = DragMode::None;
    int m_dragClipIndex = -1;
    int m_dragStartX = 0;
    qint64 m_dragInitialInMs = 0;
    qint64 m_dragInitialOutMs = 0;
    QVector<TimelineClip> m_interactionBefore;
    bool m_interactionHistoryRecorded = false;
};
