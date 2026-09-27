#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QProcess>
#include <QSize>
#include <QString>
#include <QTemporaryFile>
#include <memory>
#include <QVector>
#include <QHash>
#include "SequenceSettings.h"
#include "VideoEffects.h"
#include "ExportRange.h"

class FFmpegExporter : public QObject
{
    Q_OBJECT

public:
    struct ClipSegment {
        QString inputPath;
        QVector<VideoEffect> effects;
        double gain = 1.0;
        qint64 fadeInMs = 0;
        qint64 fadeOutMs = 0;
        int track = 0;
        qint64 startMs = 0;
        bool image = false;
        bool visible = true;
        bool muted = false;
        qint64 inMs = 0;
        qint64 outMs = 0;
    };

    explicit FFmpegExporter(QObject *parent = nullptr);

    ~FFmpegExporter() override;
    bool isRunning() const;

    // Cross-platform FFmpeg discovery. The app first looks beside the
    // executable (portable Windows package), then falls back to PATH.
    QString ffmpegExecutable() const;
    QString ffprobeExecutable() const;

    // Returns a friendly name such as "NVIDIA NVENC", "Intel Quick Sync",
    // "AMD AMF" or "Apple VideoToolbox". Empty means no usable hardware
    // H.264 encoder was detected and CPU/libx264 will be used.
    QString hardwareEncoderDisplayName() const;
    QString hardwareEncoderDiagnostics() const;

    void exportSequence(const QVector<ClipSegment> &clips,
                        const QString &outputPath,
                        bool useHardwareEncoder, bool preview = false,
                        const SequenceSettings &settings = SequenceSettings{}, bool overwrite = true,
                        const ExportRange &range = ExportRange{});
    void cancel();

signals:
    void started();
    void progressChanged(int percent,
                         qint64 processedMs,
                         qint64 totalMs,
                         double speed,
                         qint64 elapsedMs,
                         qint64 remainingMs);
    void logMessage(const QString &message);
    void finished(bool success, const QString &message);

private slots:
    void readProgress();
    void readStderr();
    void processFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    struct EncoderChoice {
        QString codec;
        QString displayName;
        bool hardware = false;
    };

    void handleProgressLine(const QString &line);
    qint64 parseFfmpegTime(const QString &value) const;
    struct SourceInfo {
        bool hasAudio = true; // Preserve the existing fallback if ffprobe fails.
        QSize frameSize;
        bool opaque = false;
        qint64 fileSize = -1;
        qint64 modifiedMs = -1;
    };
    SourceInfo probeSource(const QString &path) const;
    mutable QHash<QString, SourceInfo> m_sourceInfo;

    QString findBundledOrPathTool(const QString &baseName) const;
    EncoderChoice detectHardwareEncoder() const;
    EncoderChoice chooseEncoder(bool preferHardware) const;
    bool encoderListed(const QString &codec) const;
    bool encoderSmokeTest(const QString &codec, QString *reason = nullptr) const;

    std::unique_ptr<QTemporaryFile> m_filterFile;
    QString m_workPath;
    QProcess m_process;
    QByteArray m_progressBuffer;
    qint64 m_totalMs = 0;
    qint64 m_processedMs = 0;
    double m_speed = 0.0;
    QString m_outputPath;
    bool m_cancelled = false;
    bool m_overwrite = true;
    QElapsedTimer m_elapsedTimer;

    mutable bool m_encoderListLoaded = false;
    mutable QString m_encoderList;
    mutable bool m_hardwareDetectionDone = false;
    mutable EncoderChoice m_cachedHardwareEncoder;
    mutable QString m_hardwareEncoderDiagnostics;
};
