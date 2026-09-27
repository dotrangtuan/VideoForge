#pragma once
#include "VideoEffects.h"

struct TimelineClip
{
    enum Role { Regular = 0, Original = 1, Background = 2 };
    int role = Regular;
    bool muted = false;
    QVector<VideoEffect> effects;
    double gain = 1.0;
    qint64 fadeInMs = 0;
    qint64 fadeOutMs = 0;
    int id = 0;
    int track = 0; // 0=V1, 1=V2, 2=V3, 3=A1, 4=A2
    qint64 startMs = 0;
    bool image = false;
    bool audioOnly = false;
    QString name;
    QString filePath;
    qint64 sourceDurationMs = 0;
    qint64 sourceInMs = 0;
    qint64 sourceOutMs = 0;

    qint64 durationMs() const
    {
        return qMax<qint64>(0, sourceOutMs - sourceInMs);
    }
};

