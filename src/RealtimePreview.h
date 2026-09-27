#pragma once
#include <QWidget>
#include <QImage>
#include <QTimer>
#include <QElapsedTimer>
#include <QMediaPlayer>
#include <array>
#include "FFmpegExporter.h"

class QAudioOutput;
class QVideoSink;

// Direct source playback. The monotonic sequence clock owns transport time;
// each track decodes only its active source and is periodically resynchronized.
class RealtimePreview : public QWidget {
    Q_OBJECT
public:
    explicit RealtimePreview(QWidget *parent = nullptr);
    ~RealtimePreview() override;
    void setSequence(const QVector<FFmpegExporter::ClipSegment> &clips);
    void setSettings(const SequenceSettings &settings);
    void seek(qint64 position);
    void play();
    void pause();
    bool isPlaying() const { return m_playing; }
    qint64 position() const { return m_position; }
    QImage compositeFrame(QSize size = QSize()) const;
signals:
    void positionChanged(qint64 position);
    void playingChanged(bool playing);
    void frameChanged();
    void playbackError(const QString &message);
protected:
    void paintEvent(QPaintEvent *) override;
private:
    struct Track {
        QMediaPlayer *player = nullptr;
        QAudioOutput *audio = nullptr;
        QVideoSink *sink = nullptr;
        QImage frame;
        mutable QImage rendered;
        mutable qint64 renderedSourceKey=0;
        mutable QVector<VideoEffect> renderedEffects;
        int index = -1;
        bool loaded = false;
        bool awaitingFrame = false;
        qint64 requestedSourceMs = 0;
        qint64 lastCorrectionMs = -1000;
    };
    void clearTrack(int track);
    void sync(bool forceSeek);
    void activate(int track, int clipIndex);
    void updatePlayer(int track, bool forceSeek);
    void tick();
    void prepareNext();
    int trackForPlayer(QMediaPlayer *player) const;
    QVector<FFmpegExporter::ClipSegment> m_clips;
    std::array<Track,10> m_tracks;
    SequenceSettings m_settings;
    QTimer m_timer;
    QTimer m_seekTimer;
    QElapsedTimer m_clock;
    qint64 m_anchor = 0;
    qint64 m_position = 0;
    qint64 m_duration = 0;
    bool m_playing = false;
};
