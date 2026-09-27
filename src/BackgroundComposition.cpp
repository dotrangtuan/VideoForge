#include "BackgroundComposition.h"
#include <QFileInfo>

bool BackgroundComposition::build(const QVector<CompositionSource> &originals, const QVector<CompositionSource> &backgrounds,
                                 qint64 originalStartMs, qint64 backgroundStartMs, bool backgroundAudio,
                                 QVector<TimelineClip> &result, QString *error) {
    auto fail=[&](const QString &message){if(error)*error=message;return false;};
    if(originals.isEmpty() || backgrounds.isEmpty())return fail("Choose original videos and background files first.");
    if(originalStartMs<0 || originalStartMs>=86400000 || backgroundStartMs<0 || backgroundStartMs>=86400000)
        return fail("Sequence exceeds 24 hours.");
    if(originals.size()+backgrounds.size()>10000)return fail("Too many clips. The project limit is 10000 clips.");
    for(const auto &sources:{originals,backgrounds}) for(const auto &source:sources)
        if(source.path.isEmpty() || source.durationMs<120 || source.durationMs>86400000)return fail("Invalid source duration.");
    QVector<TimelineClip> clips;
    auto make=[](const CompositionSource &source,int role,qint64 start,qint64 duration){
        TimelineClip c;c.role=role;c.track=role==TimelineClip::Original?1:0;
        c.filePath=source.path;c.name=QFileInfo(source.path).fileName();c.image=source.image;c.startMs=start;
        c.sourceDurationMs=source.image?qMax<qint64>(120,duration):source.durationMs;c.sourceOutMs=duration;
        return c;
    };
    qint64 end=originalStartMs;
    for(const auto &source:originals) {
        if(source.image)return fail("Original sources must be videos.");
        if(source.durationMs>86400000-end)return fail("Sequence exceeds 24 hours.");
        clips.append(make(source,TimelineClip::Original,end,source.durationMs));end+=source.durationMs;
    }
    qint64 cursor=backgroundStartMs;
    for(const auto &source:backgrounds) {
        if(source.durationMs>86400000-cursor)return fail("Sequence exceeds 24 hours.");
        auto c=make(source,TimelineClip::Background,cursor,source.durationMs);c.muted=!backgroundAudio;
        clips.append(c);cursor+=source.durationMs;
    }
    result=clips;return true;
}
