#pragma once
#include "TimelineClip.h"

struct CompositionSource {
    QString path;
    qint64 durationMs = 0;
    bool image = false;
};

namespace BackgroundComposition {
bool build(const QVector<CompositionSource> &originals, const QVector<CompositionSource> &backgrounds,
           qint64 originalStartMs, qint64 backgroundStartMs, bool backgroundAudio,
           QVector<TimelineClip> &result, QString *error = nullptr);
}
