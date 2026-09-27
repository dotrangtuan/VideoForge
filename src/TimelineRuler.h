#pragma once
#include "SequenceSettings.h"
#include <QVector>
#include <cmath>

namespace TimelineRuler {
struct Tick {qint64 frame;bool major;bool middle;};
inline QVector<Tick> ticks(const SequenceSettings &settings,double pixelsPerSecond,
                          qint64 firstMs,qint64 lastMs,int labelWidth) {
    QVector<Tick> result;
    if(!settings.valid() || !std::isfinite(pixelsPerSecond) || pixelsPerSecond<=0 || lastMs<firstMs)return result;
    const double pixelsPerFrame=pixelsPerSecond*settings.fpsDen/settings.fpsNum;
    const double required=(labelWidth+16)/pixelsPerFrame;
    QVector<qint64> candidates{1,2,5,10,15,20};
    for(qint64 seconds:{1LL,2LL,5LL,10LL,15LL,30LL,60LL,120LL,300LL,600LL,900LL,1800LL,
                        3600LL,7200LL,14400LL,21600LL,43200LL,86400LL})
        candidates.append(seconds*settings.nominalFps());
    std::sort(candidates.begin(),candidates.end());
    qint64 major=candidates.last();for(qint64 candidate:candidates)if(candidate>=required){major=candidate;break;}
    qint64 minor=major;
    for(int divisor:{20,10,5,4,3,2})if(major%divisor==0 && major/divisor*pixelsPerFrame>=6){minor=major/divisor;break;}
    const qint64 first=settings.frameAt(qMax<qint64>(0,firstMs))/minor*minor;
    const qint64 last=settings.nearestFrame(qMin<qint64>(86400000,lastMs));
    for(qint64 frame=first;frame<=last;frame+=minor)
        result.append({frame,frame%major==0,major%2==0 && frame%major==major/2});
    return result;
}
}
